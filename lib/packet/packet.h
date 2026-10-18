#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hybrid_lora::packet {

enum class Type : uint8_t {
  Command = 0x01,
  Ack = 0x02,
  Status = 0x03,
  Heartbeat = 0x04,
  Beacon = 0x05,
};

struct Frame {
  Type type{};
  uint8_t node_id{};
  uint8_t seq{};
  std::vector<uint8_t> payload;
};

struct DecodeResult {
  bool ok{false};
  Frame frame{};
};

uint16_t crc16_ccitt(const uint8_t* data, std::size_t size);
std::vector<uint8_t> encode(const Frame& frame);
DecodeResult decode(const uint8_t* data, std::size_t size);

bool is_known_type(uint8_t value);
std::size_t expected_payload_size(Type type);

}  // namespace hybrid_lora::packet
