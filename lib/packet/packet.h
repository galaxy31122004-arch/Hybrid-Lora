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

struct CommandPayload {
  uint8_t command{};
  uint8_t param{};
};

struct AckPayload {
  uint8_t command_seq{};
  uint8_t result{};
};

struct StatusPayload {
  uint32_t timestamp{};
  uint16_t lux{};
  uint8_t light_state{};
  uint8_t mode{};
};

struct HeartbeatPayload {
  uint8_t flags{};
};

Frame make_command(uint8_t node_id, uint8_t seq, uint8_t command, uint8_t param);
Frame make_ack(uint8_t node_id, uint8_t seq, uint8_t command_seq, uint8_t result);
Frame make_status(uint8_t node_id, uint8_t seq, uint32_t timestamp,
                  uint16_t lux, uint8_t light_state, uint8_t mode);
Frame make_heartbeat(uint8_t node_id, uint8_t seq, uint8_t flags);
bool read_command(const Frame& frame, CommandPayload& payload);
bool read_ack(const Frame& frame, AckPayload& payload);
bool read_status(const Frame& frame, StatusPayload& payload);
bool read_heartbeat(const Frame& frame, HeartbeatPayload& payload);

enum class CommandDisposition : uint8_t { New, Duplicate, Conflict };

class CommandReplayCache {
 public:
  CommandDisposition classify(const Frame& frame) const;
  void remember(const Frame& frame, uint8_t result);
  uint8_t last_result() const { return result_; }

 private:
  bool has_value_{false};
  uint8_t node_id_{};
  uint8_t seq_{};
  uint8_t command_{};
  uint8_t param_{};
  uint8_t result_{};
};

uint16_t crc16_ccitt(const uint8_t* data, std::size_t size);
std::vector<uint8_t> encode(const Frame& frame);
DecodeResult decode(const uint8_t* data, std::size_t size);

bool is_known_type(uint8_t value);
std::size_t expected_payload_size(Type type);

}  // namespace hybrid_lora::packet
