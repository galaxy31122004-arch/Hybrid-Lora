// Host tests for firmware/v0.4.0/protocol (packet v2 + gateway state).
#include "gateway_state.h"
#include "packet.h"

#include <cassert>
#include <cstdint>

using namespace hybrid_lora;
using namespace hybrid_lora::packet;

static void test_sizes() {
  assert(encode(make_command(1, 0, 1, 0)).size() == 5);
  assert(encode(make_ack(1, 0, 0, 0)).size() == 5);
  assert(encode(make_status(1, 0, 0xFFFF, 1, 1, 3)).size() == 6);
  assert(encode(make_beacon(7)).size() == 3);
}

static void test_round_trip_status() {
  const auto bytes = encode(make_status(12, 9, 420, 1, 0, 0x2A));
  const auto decoded = decode(bytes.data(), bytes.size());
  assert(decoded.ok && decoded.frame.type == Type::Status);
  assert(decoded.frame.node_id == 12 && decoded.frame.seq == 9);
  StatusPayload status{};
  assert(read_status(decoded.frame, status));
  assert(status.lux == 420 && status.light_state == 1 && status.mode == 0);
  assert(status.health == 0x2A);
}

static void test_beacon_is_broadcast_only() {
  const auto bytes = encode(make_beacon(1));
  assert(bytes[1] == kBroadcastId);
  assert(decode(bytes.data(), bytes.size()).ok);
  assert(encode(Frame{Type::Beacon, 3, 0, {}}).empty());       // unicast beacon
  assert(encode(make_command(kBroadcastId, 0, 1, 0)).empty()); // broadcast command
}

static void test_rejects_malformed() {
  const uint8_t heartbeat_v1[] = {0x04, 0x01, 0x00, 0x00};
  assert(!decode(heartbeat_v1, sizeof(heartbeat_v1)).ok);  // type removed in v2
  const uint8_t short_status[] = {0x03, 0x01, 0x00, 0x10, 0x00};
  assert(!decode(short_status, sizeof(short_status)).ok);
  const uint8_t node_zero[] = {0x01, 0x00, 0x00, 0x01, 0x00};
  assert(!decode(node_zero, sizeof(node_zero)).ok);
  assert(!decode(nullptr, 3).ok);
}

static void test_gateway_status_and_ack_mark_seen() {
  GatewayState gateway;
  gateway.on_frame(make_status(3, 0, 100, 1, 1, 5), 1000);
  assert(gateway.node(3).seen && gateway.node(3).has_status);
  assert(gateway.node(3).health_flags == 5);

  gateway.on_frame(make_ack(4, 0, 0, 0), 2000);
  assert(gateway.node(4).seen && !gateway.node(4).has_status);
  assert(gateway.node(4).last_seen_ms == 2000);
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

int main() {
  test_sizes();
  test_round_trip_status();
  test_beacon_is_broadcast_only();
  test_rejects_malformed();
  test_gateway_status_and_ack_mark_seen();
  test_gateway_retry_interval();
}
