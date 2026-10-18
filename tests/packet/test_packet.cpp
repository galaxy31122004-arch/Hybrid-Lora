#include "packet.h"

#include <cassert>
#include <cstdint>
#include <vector>

using hybrid_lora::packet::Frame;
using hybrid_lora::packet::Type;

static void test_crc_vector() {
  const uint8_t input[] = {'1','2','3','4','5','6','7','8','9'};
  assert(hybrid_lora::packet::crc16_ccitt(input, sizeof(input)) == 0x29B1);
}

static void test_command_round_trip() {
  Frame frame{};
  frame.type = Type::Command;
  frame.node_id = 3;
  frame.seq = 25;
  frame.payload = {0x01, 0x00};

  const auto encoded = hybrid_lora::packet::encode(frame);
  assert(encoded.size() == 8);
  const auto decoded = hybrid_lora::packet::decode(encoded.data(), encoded.size());
  assert(decoded.ok);
  assert(decoded.frame.type == Type::Command);
  assert(decoded.frame.node_id == 3);
  assert(decoded.frame.seq == 25);
  assert(decoded.frame.payload == frame.payload);
}

static void test_rejects_bad_lengths_and_crc() {
  const uint8_t malformed[] = {0x01, 0x03, 0x19, 0x01, 0x01, 0x00, 0x00};
  assert(!hybrid_lora::packet::decode(malformed, sizeof(malformed)).ok);

  auto valid = hybrid_lora::packet::encode(Frame{Type::Beacon, 3, 1, {}});
  valid.back() ^= 0x01;
  assert(!hybrid_lora::packet::decode(valid.data(), valid.size()).ok);
}

static void test_beacon_and_status_lengths() {
  auto beacon = hybrid_lora::packet::encode(Frame{Type::Beacon, 3, 1, {}});
  assert(beacon.size() == 6);
  auto status = hybrid_lora::packet::encode(Frame{Type::Status, 3, 2,
                                                  {0,0,0,0, 0x10,0, 1,1}});
  assert(status.size() == 14);
  assert(hybrid_lora::packet::decode(status.data(), status.size()).ok);
}

int main() {
  test_crc_vector();
  test_command_round_trip();
  test_rejects_bad_lengths_and_crc();
  test_beacon_and_status_lengths();
  return 0;
}
