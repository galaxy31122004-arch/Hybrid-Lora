// Host tests for firmware/v0.5.0/protocol (packet v3 + gateway state).
#include "gateway_state.h"
#include "packet.h"

#include <cassert>
#include <cstdint>

using namespace hybrid_lora;
using namespace hybrid_lora::packet;

static void test_sizes() {
  assert(encode(make_command(1, 0, 1, 0)).size() == 5);
  assert(encode(make_ack(1, 0, 0, 0)).size() == 5);
  assert(encode(make_status(1, 0xFFFF, 1, 1, 3)).size() == 5);  // no SEQ
  assert(encode(make_beacon(7)).size() == 3);
}

static void test_status_layout_and_round_trip() {
  const auto bytes = encode(make_status(12, 420, 1, 0, 0x0A));
  assert(bytes.size() == 5);
  assert(bytes[0] == 0x03 && bytes[1] == 12);
  assert(bytes[2] == (420 & 0xFF) && bytes[3] == (420 >> 8));
  const auto decoded = decode(bytes.data(), bytes.size());
  assert(decoded.ok && decoded.frame.type == Type::Status);
  assert(decoded.frame.node_id == 12 && decoded.frame.seq == 0);
  StatusPayload status{};
  assert(read_status(decoded.frame, status));
  assert(status.lux == 420 && status.light_state == 1 && status.mode == 0);
  assert(status.health == 0x0A && status.test_mode == 0);
}

static void test_command_keeps_seq() {
  const auto bytes = encode(make_command(7, 0x42, 2, 0));
  const auto decoded = decode(bytes.data(), bytes.size());
  assert(decoded.ok && decoded.frame.seq == 0x42);
}

static void test_beacon_is_broadcast_only() {
  const auto bytes = encode(make_beacon(1));
  assert(bytes[1] == kBroadcastId);
  assert(decode(bytes.data(), bytes.size()).ok);
  assert(encode(Frame{Type::Beacon, 3, 0, {}}).empty());
  assert(encode(make_command(kBroadcastId, 0, 1, 0)).empty());
}

static void test_rejects_malformed() {
  const uint8_t v040_status[] = {0x03, 0x01, 0x00, 0x10, 0x00, 0x01};  // 6 B v0.4.0
  assert(!decode(v040_status, sizeof(v040_status)).ok);
  const uint8_t heartbeat_v1[] = {0x04, 0x01, 0x00, 0x00};
  assert(!decode(heartbeat_v1, sizeof(heartbeat_v1)).ok);
  const uint8_t short_command[] = {0x01, 0x01, 0x00, 0x01};
  assert(!decode(short_command, sizeof(short_command)).ok);
  const uint8_t one_byte[] = {0x03};
  assert(!decode(one_byte, sizeof(one_byte)).ok);
  assert(!decode(nullptr, 5).ok);
}

static void test_gateway_signal_and_status() {
  GatewayState gateway;
  gateway.on_frame(make_status(3, 100, 1, 1, 5), 1000);
  gateway.set_signal(3, -118, -7.5f);
  assert(gateway.node(3).seen && gateway.node(3).has_status);
  assert(gateway.node(3).has_signal);
  assert(gateway.node(3).rssi_dbm == -118 && gateway.node(3).snr_db == -7.5f);
  gateway.set_signal(0, -1, 0);  // ignored
  gateway.set_signal(51, -1, 0);
}

static void test_ack_updates_known_state() {
  GatewayState gateway;
  gateway.on_frame(make_status(4, 0xFFFF, 0, 0, 0), 0);
  assert(gateway.queue_command(4, 0x01, 0, 9));  // light on
  auto frame = gateway.next_due_command(10);
  assert(frame.has_value());
  gateway.mark_sent(4, 10);
  gateway.on_frame(make_ack(4, 0, frame->seq, 0), 900);
  assert(gateway.node(4).status.light_state == 1);

  assert(gateway.take_result().has_value());
  assert(gateway.queue_command(4, 0x03, 1, 10));  // remote mode
  frame = gateway.next_due_command(1000);
  gateway.mark_sent(4, 1000);
  gateway.on_frame(make_ack(4, 0, frame->seq, 0), 1900);
  assert(gateway.node(4).status.mode == 1);

  assert(gateway.take_result().has_value());
  assert(gateway.queue_command(4, 0x02, 0, 11));  // light off, but node rejects
  frame = gateway.next_due_command(2000);
  gateway.mark_sent(4, 2000);
  gateway.on_frame(make_ack(4, 0, frame->seq, 1), 2900);
  assert(gateway.node(4).status.light_state == 1);  // unchanged on failure
}

static void test_test_mode_flag_and_command() {
  const auto bytes = encode(make_status(5, 10, 0, 1, 0x1F, 1));
  StatusPayload status{};
  assert(read_status(decode(bytes.data(), bytes.size()).frame, status));
  assert(status.test_mode == 1 && status.health == 0x1F && status.mode == 1);
  // health is masked to 5 bits so it cannot set the test-mode bit
  const auto masked = encode(make_status(5, 10, 0, 0, 0xFF, 0));
  assert(read_status(decode(masked.data(), masked.size()).frame, status));
  assert(status.test_mode == 0 && status.health == 0x1F);

  GatewayState gateway;
  gateway.on_frame(make_status(6, 0xFFFF, 0, 0, 0), 0);
  assert(!gateway.queue_command(6, kCmdTestMode, kMaxTestMinutes + 1, 1));
  assert(!gateway.queue_command(6, 0x06, 0, 1));  // unknown command
  assert(gateway.queue_command(6, kCmdTestMode, 30, 2));
  auto frame = gateway.next_due_command(0);
  gateway.mark_sent(6, 0);
  gateway.on_frame(make_ack(6, 0, frame->seq, 0), 900);
  assert(gateway.node(6).status.test_mode == 1);
}

static void test_gateway_retry_interval() {
  GatewayState gateway;
  assert(gateway.queue_command(3, 1, 0, 42));
  assert(gateway.next_due_command(0).has_value());
  gateway.mark_sent(3, 0);
  assert(!gateway.next_due_command(GatewayState::kRetryMs - 1).has_value());
  assert(gateway.next_due_command(GatewayState::kRetryMs).has_value());
  gateway.mark_sent(3, GatewayState::kRetryMs);
  gateway.on_frame(make_ack(3, 0, 0, 0), GatewayState::kRetryMs + 100);
  const auto result = gateway.take_result();
  assert(result.has_value() && result->success && result->rpc_id == 42);
}

// v0.5.1: the test flag must be known before the first STATUS, because the
// gateway's RSSI/SNR compensation reads it; other fields still wait.
static void test_test_mode_known_before_first_status() {
  GatewayState gateway;
  assert(gateway.queue_command(7, kCmdTestMode, 30, 5));
  auto frame = gateway.next_due_command(0);
  gateway.mark_sent(7, 0);
  gateway.on_frame(make_ack(7, 0, frame->seq, 0), 900);
  assert(gateway.node(7).status.test_mode == 1);
  assert(!gateway.node(7).has_status);
  (void)gateway.take_result();
  assert(gateway.queue_command(7, kCmdLightOn, 0, 6));
  frame = gateway.next_due_command(1000);
  gateway.mark_sent(7, 1000);
  gateway.on_frame(make_ack(7, 0, frame->seq, 0), 1900);
  assert(gateway.node(7).status.light_state == 0);  // no STATUS yet
}

// v0.5.1: commands to two nodes back to back. The second must wait until
// the first ACK has arrived (or its window has passed), never overlap it.
static void test_ack_window_serializes_commands() {
  GatewayState gateway;
  assert(gateway.queue_command(1, kCmdLightOn, 0, 10));
  assert(gateway.queue_command(2, kCmdLightOn, 0, 11));
  auto first = gateway.next_due_command(0);
  assert(first.has_value() && first->node_id == 1);
  gateway.mark_sent(1, 0);
  assert(gateway.awaiting_ack(0));
  // node 2 is held back while node 1's ACK may be on the air
  assert(!gateway.next_due_command(900).has_value());
  assert(!gateway.next_due_command(GatewayState::kAckWindowMs - 1).has_value());
  // ACK arrives early: the gateway is free at once
  gateway.on_frame(make_ack(1, 0, first->seq, 0), 1700);
  assert(!gateway.awaiting_ack(1700));
  auto second = gateway.next_due_command(1700);
  assert(second.has_value() && second->node_id == 2);
  gateway.mark_sent(2, 1700);
  // no ACK: free again after the window, retry after kRetryMs
  assert(gateway.awaiting_ack(1700 + GatewayState::kAckWindowMs - 1));
  assert(!gateway.awaiting_ack(1700 + GatewayState::kAckWindowMs));
  assert(!gateway.next_due_command(1700 + GatewayState::kAckWindowMs).has_value());
  assert(gateway.next_due_command(1700 + GatewayState::kRetryMs).has_value());
}

int main() {
  test_ack_window_serializes_commands();
  test_test_mode_known_before_first_status();
  test_sizes();
  test_status_layout_and_round_trip();
  test_command_keeps_seq();
  test_beacon_is_broadcast_only();
  test_rejects_malformed();
  test_gateway_signal_and_status();
  test_ack_updates_known_state();
  test_test_mode_flag_and_command();
  test_gateway_retry_interval();
}
