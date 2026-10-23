#include "gateway_state.h"

#include <cassert>

using namespace hybrid_lora;
using namespace hybrid_lora::packet;

static void test_node_updates_only_from_telemetry() {
  GatewayState gateway;
  gateway.on_frame(make_ack(3, 1, 0, 0), 100);
  assert(!gateway.node(3).seen);

  gateway.on_frame(make_heartbeat(3, 2, 0x0D), 200);
  assert(gateway.node(3).seen);
  assert(!gateway.node(3).has_status);
  assert(gateway.node(3).last_seen_ms == 200);

  gateway.on_frame(make_status(3, 3, 0, 420, 1, 1), 300);
  assert(gateway.node(3).has_status);
  assert(gateway.node(3).status.lux == 420);
  assert(gateway.node(3).status.light_state == 1);
}

static void test_command_retry_and_matching_ack() {
  GatewayState gateway;
  assert(gateway.queue_command(3, 1, 0, 42));
  assert(!gateway.queue_command(3, 2, 0, 43));
  auto frame = gateway.next_due_command(100);
  assert(frame.has_value());
  assert(frame->node_id == 3 && frame->payload[0] == 1);
  gateway.mark_sent(3, 100);
  assert(!gateway.next_due_command(1200).has_value());
  frame = gateway.next_due_command(1600);
  assert(frame.has_value() && frame->seq == 0);
  gateway.mark_sent(3, 1600);

  gateway.on_frame(make_ack(3, 0, 1, 0), 1700);
  assert(!gateway.take_result().has_value());
  gateway.on_frame(make_ack(3, 0, 0, 0), 1800);
  auto result = gateway.take_result();
  assert(result.has_value());
  assert(result->node_id == 3 && result->rpc_id == 42 && result->success);
  assert(!gateway.next_due_command(4000).has_value());
}

static void test_command_timeout_and_invalid_node() {
  GatewayState gateway;
  assert(!gateway.queue_command(0, 1, 0, 1));
  assert(!gateway.queue_command(51, 1, 0, 1));
  assert(gateway.queue_command(1, 2, 0, 7));
  for (uint32_t time : {100u, 1700u, 3300u, 4900u}) {
    assert(gateway.next_due_command(time).has_value());
    gateway.mark_sent(1, time);
  }
  assert(!gateway.next_due_command(6000).has_value());
  assert(!gateway.take_result().has_value());
  assert(!gateway.next_due_command(6500).has_value());
  auto result = gateway.take_result();
  assert(result.has_value() && !result->success && result->rpc_id == 7);
}

int main() {
  test_node_updates_only_from_telemetry();
  test_command_retry_and_matching_ack();
  test_command_timeout_and_invalid_node();
}
