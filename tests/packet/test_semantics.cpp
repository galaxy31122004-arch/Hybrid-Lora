#include "packet.h"

#include <cassert>

using namespace hybrid_lora::packet;

static void test_typed_command_and_ack() {
  const auto command = make_command(3, 25, 0x01, 0x00);
  assert(command.payload.size() == 2);
  CommandPayload parsed{};
  assert(read_command(command, parsed));
  assert(parsed.command == 0x01 && parsed.param == 0x00);

  const auto ack = make_ack(3, 10, 25, 0x00);
  AckPayload ack_payload{};
  assert(read_ack(ack, ack_payload));
  assert(ack_payload.command_seq == 25 && ack_payload.result == 0x00);
}

static void test_typed_status_and_health() {
  const auto status = make_status(3, 2, 0x12345678, 420, 1, 1);
  StatusPayload status_payload{};
  assert(read_status(status, status_payload));
  assert(status_payload.timestamp == 0x12345678);
  assert(status_payload.lux == 420);
  assert(status_payload.light_state == 1 && status_payload.mode == 1);

  const auto heartbeat = make_heartbeat(3, 3, 0x0F);
  HeartbeatPayload health{};
  assert(read_heartbeat(heartbeat, health));
  assert(health.flags == 0x0F);
}

static void test_duplicate_cache_distinguishes_conflict() {
  CommandReplayCache cache{};
  const auto first = make_command(3, 7, 0x01, 0x00);
  assert(cache.classify(first) == CommandDisposition::New);
  cache.remember(first, 0x00);
  assert(cache.classify(first) == CommandDisposition::Duplicate);
  const auto conflict = make_command(3, 7, 0x02, 0x00);
  assert(cache.classify(conflict) == CommandDisposition::Conflict);
  assert(cache.last_result() == 0x00);
}

int main() {
  test_typed_command_and_ack();
  test_typed_status_and_health();
  test_duplicate_cache_distinguishes_conflict();
  return 0;
}
