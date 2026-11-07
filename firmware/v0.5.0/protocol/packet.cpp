#include "packet.h"

namespace hybrid_lora::packet {
namespace {

bool valid_node_id(Type type, uint8_t node_id) {
  if (type == Type::Beacon) return node_id == kBroadcastId;
  return node_id >= 0x01 && node_id <= 0xFE;
}

bool valid_frame(const Frame& frame) {
  return is_known_type(static_cast<uint8_t>(frame.type)) &&
         valid_node_id(frame.type, frame.node_id) &&
         frame.payload.size() == expected_payload_size(frame.type);
}

}  // namespace

Frame make_command(uint8_t node_id, uint8_t seq, uint8_t command, uint8_t param) {
  return Frame{Type::Command, node_id, seq, {command, param}};
}

Frame make_ack(uint8_t node_id, uint8_t seq, uint8_t command_seq, uint8_t result) {
  return Frame{Type::Ack, node_id, seq, {command_seq, result}};
}

Frame make_status(uint8_t node_id, uint16_t lux, uint8_t light_state,
                  uint8_t mode, uint8_t health, uint8_t test_mode) {
  const uint8_t flags = static_cast<uint8_t>(
      (light_state ? kFlagLightOn : 0) | (mode ? kFlagRemoteMode : 0) |
      ((health & kHealthMask) << kFlagHealthShift) |
      (test_mode ? kFlagTestMode : 0));
  return Frame{Type::Status, node_id, 0,
               {static_cast<uint8_t>(lux), static_cast<uint8_t>(lux >> 8), flags}};
}

Frame make_beacon(uint8_t seq) {
  return Frame{Type::Beacon, kBroadcastId, seq, {}};
}

bool read_command(const Frame& frame, CommandPayload& payload) {
  if (frame.type != Type::Command || frame.payload.size() != 2) return false;
  payload = {frame.payload[0], frame.payload[1]};
  return true;
}

bool read_ack(const Frame& frame, AckPayload& payload) {
  if (frame.type != Type::Ack || frame.payload.size() != 2) return false;
  payload = {frame.payload[0], frame.payload[1]};
  return true;
}

bool read_status(const Frame& frame, StatusPayload& payload) {
  if (frame.type != Type::Status || frame.payload.size() != 3) return false;
  payload.lux = static_cast<uint16_t>(frame.payload[0] | frame.payload[1] << 8);
  const uint8_t flags = frame.payload[2];
  payload.light_state = (flags & kFlagLightOn) ? 1 : 0;
  payload.mode = (flags & kFlagRemoteMode) ? 1 : 0;
  payload.health = (flags >> kFlagHealthShift) & kHealthMask;
  payload.test_mode = (flags & kFlagTestMode) ? 1 : 0;
  return true;
}

CommandDisposition CommandReplayCache::classify(const Frame& frame) const {
  if (!has_value_ || frame.type != Type::Command || frame.node_id != node_id_ ||
      frame.payload.size() != 2 || frame.seq != seq_) {
    return CommandDisposition::New;
  }
  if (frame.payload[0] == command_ && frame.payload[1] == param_) {
    return CommandDisposition::Duplicate;
  }
  return CommandDisposition::Conflict;
}

void CommandReplayCache::remember(const Frame& frame, uint8_t result) {
  if (frame.type != Type::Command || frame.payload.size() != 2) return;
  has_value_ = true;
  node_id_ = frame.node_id;
  seq_ = frame.seq;
  command_ = frame.payload[0];
  param_ = frame.payload[1];
  result_ = result;
}

bool is_known_type(uint8_t value) {
  return value == static_cast<uint8_t>(Type::Command) ||
         value == static_cast<uint8_t>(Type::Ack) ||
         value == static_cast<uint8_t>(Type::Status) ||
         value == static_cast<uint8_t>(Type::Beacon);
}

std::size_t expected_payload_size(Type type) {
  switch (type) {
    case Type::Command:
    case Type::Ack:
      return 2;
    case Type::Status:
      return 3;
    case Type::Beacon:
      return 0;
  }
  return 0;
}

std::size_t header_size(Type type) {
  return type == Type::Status ? kStatusHeaderSize : kHeaderSize;
}

std::vector<uint8_t> encode(const Frame& frame) {
  if (!valid_frame(frame)) return {};
  std::vector<uint8_t> encoded;
  encoded.reserve(header_size(frame.type) + frame.payload.size());
  encoded.push_back(static_cast<uint8_t>(frame.type));
  encoded.push_back(frame.node_id);
  if (frame.type != Type::Status) encoded.push_back(frame.seq);
  encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
  return encoded;
}

DecodeResult decode(const uint8_t* data, std::size_t size) {
  DecodeResult result{};
  if (data == nullptr || size < kStatusHeaderSize || !is_known_type(data[0])) {
    return result;
  }
  const Type type = static_cast<Type>(data[0]);
  const std::size_t header = header_size(type);
  if (!valid_node_id(type, data[1]) ||
      size != header + expected_payload_size(type)) {
    return result;
  }
  result.ok = true;
  result.frame.type = type;
  result.frame.node_id = data[1];
  result.frame.seq = type == Type::Status ? 0 : data[2];
  result.frame.payload.assign(data + header, data + size);
  return result;
}

}  // namespace hybrid_lora::packet
