#include "packet.h"

namespace hybrid_lora::packet {
namespace {

bool valid_node_id(uint8_t node_id) {
  return node_id >= 0x01 && node_id <= 0xFE;
}

bool valid_frame(const Frame& frame) {
  return valid_node_id(frame.node_id) &&
         frame.payload.size() == expected_payload_size(frame.type);
}

}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, std::size_t size) {
  uint16_t crc = 0xFFFF;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) != 0
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool is_known_type(uint8_t value) {
  return value >= static_cast<uint8_t>(Type::Command) &&
         value <= static_cast<uint8_t>(Type::Beacon);
}

std::size_t expected_payload_size(Type type) {
  switch (type) {
    case Type::Command:
    case Type::Ack:
      return 2;
    case Type::Status:
      return 8;
    case Type::Heartbeat:
      return 1;
    case Type::Beacon:
      return 0;
  }
  return 0;
}

std::vector<uint8_t> encode(const Frame& frame) {
  if (!valid_frame(frame)) {
    return {};
  }

  std::vector<uint8_t> encoded;
  encoded.reserve(6 + frame.payload.size());
  encoded.push_back(static_cast<uint8_t>(frame.type));
  encoded.push_back(frame.node_id);
  encoded.push_back(frame.seq);
  encoded.push_back(static_cast<uint8_t>(frame.payload.size()));
  encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
  const uint16_t crc = crc16_ccitt(encoded.data(), encoded.size());
  encoded.push_back(static_cast<uint8_t>(crc & 0xFF));
  encoded.push_back(static_cast<uint8_t>(crc >> 8));
  return encoded;
}

DecodeResult decode(const uint8_t* data, std::size_t size) {
  DecodeResult result{};
  if (data == nullptr || size < 6) {
    return result;
  }

  const uint8_t type_value = data[0];
  const uint8_t node_id = data[1];
  const uint8_t payload_size = data[3];
  if (!is_known_type(type_value) || !valid_node_id(node_id) ||
      size != static_cast<std::size_t>(6 + payload_size)) {
    return result;
  }

  const Type type = static_cast<Type>(type_value);
  if (payload_size != expected_payload_size(type)) {
    return result;
  }

  const uint16_t received_crc = static_cast<uint16_t>(data[size - 2]) |
                                 static_cast<uint16_t>(data[size - 1]) << 8;
  if (crc16_ccitt(data, size - 2) != received_crc) {
    return result;
  }

  result.ok = true;
  result.frame.type = type;
  result.frame.node_id = node_id;
  result.frame.seq = data[2];
  result.frame.payload.assign(data + 4, data + 4 + payload_size);
  return result;
}

}  // namespace hybrid_lora::packet
