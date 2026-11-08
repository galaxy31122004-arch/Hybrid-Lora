#pragma once

// Packet format v3 (firmware v0.5.0).
//
//   COMMAND, ACK, BEACON:  TYPE (1) | NODE_ID (1) | SEQ (1) | PAYLOAD
//   STATUS:                TYPE (1) | NODE_ID (1) | PAYLOAD (3)   -> 5 bytes
//
// STATUS carries no SEQ: nothing matches on it, and dropping it keeps the
// frame at 5 bytes, the largest size that fits one SF12 payload block
// (6 bytes costs ~0.99 s on air, 5 bytes ~0.83 s).
// No LEN byte: the LoRa header already carries the length and every type has
// a fixed payload size. No CRC-16: the SX1278 hardware CRC is enabled and
// packets that fail or lack it are dropped by the radio driver.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hybrid_lora::packet {

constexpr std::size_t kHeaderSize = 3;        // with SEQ
constexpr std::size_t kStatusHeaderSize = 2;  // STATUS: no SEQ
constexpr uint8_t kBroadcastId = 0xFF;  // BEACON only

enum class Type : uint8_t {
  Command = 0x01,
  Ack = 0x02,
  Status = 0x03,
  // 0x04 was HEARTBEAT in v1; STATUS now carries the health bits.
  Beacon = 0x05,
};

// STATUS flag bits.
constexpr uint8_t kFlagLightOn = 0x01;
constexpr uint8_t kFlagRemoteMode = 0x02;
constexpr uint8_t kFlagHealthShift = 2;  // bits 2..6: health flags
constexpr uint8_t kHealthMask = 0x1F;
constexpr uint8_t kFlagTestMode = 0x80;  // bit 7: node is in test mode

// COMMAND codes.
constexpr uint8_t kCmdLightOn = 0x01;
constexpr uint8_t kCmdLightOff = 0x02;
constexpr uint8_t kCmdSetMode = 0x03;      // param 0 local, 1 remote
constexpr uint8_t kCmdRequestStatus = 0x04;
constexpr uint8_t kCmdTestMode = 0x05;     // param = minutes, 0 = off
constexpr uint8_t kMaxTestMinutes = 120;

struct Frame {
  Type type{};
  uint8_t node_id{};
  uint8_t seq{};  // not transmitted for STATUS (always 0 after decode)
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
  uint16_t lux{};          // 0xFFFF = sensor unavailable
  uint8_t light_state{};   // 0/1
  uint8_t mode{};          // 0 local, 1 remote
  uint8_t health{};        // 5 bits
  uint8_t test_mode{};     // 0/1
};

Frame make_command(uint8_t node_id, uint8_t seq, uint8_t command, uint8_t param);
Frame make_ack(uint8_t node_id, uint8_t seq, uint8_t command_seq, uint8_t result);
Frame make_status(uint8_t node_id, uint16_t lux, uint8_t light_state,
                  uint8_t mode, uint8_t health, uint8_t test_mode = 0);
Frame make_beacon(uint8_t seq);
bool read_command(const Frame& frame, CommandPayload& payload);
bool read_ack(const Frame& frame, AckPayload& payload);
bool read_status(const Frame& frame, StatusPayload& payload);

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

std::vector<uint8_t> encode(const Frame& frame);
DecodeResult decode(const uint8_t* data, std::size_t size);

bool is_known_type(uint8_t value);
std::size_t expected_payload_size(Type type);
std::size_t header_size(Type type);

}  // namespace hybrid_lora::packet
