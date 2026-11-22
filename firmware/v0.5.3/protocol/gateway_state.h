#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "packet.h"

namespace hybrid_lora {

class GatewayState {
 public:
  static constexpr uint8_t kMaxNodes = 50;
  // SF12: COMMAND (~0.83 s) + node processing + ACK (~0.83 s) ~= 1.7 s.
  // While that ACK may be on the air the gateway does not transmit at all:
  // radio2 on the same board would deafen radio1 and the ACK would be lost.
  static constexpr uint32_t kAckWindowMs = 2200;
  static constexpr uint32_t kRetryMs = 2500;  // >= kAckWindowMs
  static constexpr uint8_t kMaxAttempts = 4;

  struct Node {
    bool seen{false};
    bool has_status{false};
    bool has_signal{false};
    uint32_t last_seen_ms{0};
    uint8_t health_flags{0};
    int16_t rssi_dbm{0};  // last packet received from this node
    float snr_db{0};
    packet::StatusPayload status{};
    // Test STATUS statistics (packet loss by sequence number).
    struct Test {
      bool active{false};
      uint16_t last_seq{0};
      uint8_t tx_dbm{0};      // node's TX power from the last test STATUS
      uint32_t window{0};     // bit i: packet sent i packets ago arrived
      uint8_t window_len{0};  // valid bits in window (max 32)
      uint32_t received{0};
      uint32_t lost{0};
      uint8_t last_gap_lost{0};  // packets missing just before the last one
    } test{};
    // Share of the last kPdrWindow test packets that arrived, 0..100.
    uint8_t pdr_percent() const;
  };
  static constexpr uint8_t kPdrWindow = 20;

  struct CommandResult {
    uint8_t node_id;
    uint32_t rpc_id;
    bool success;
    uint8_t result_code;
  };

  const Node& node(uint8_t node_id) const { return nodes_[node_id]; }
  void on_frame(const packet::Frame& frame, uint32_t now_ms);
  void set_signal(uint8_t node_id, int16_t rssi_dbm, float snr_db);
  bool queue_command(uint8_t node_id, uint8_t command, uint8_t param,
                     uint32_t rpc_id);
  std::optional<packet::Frame> next_due_command(uint32_t now_ms);
  void mark_sent(uint8_t node_id, uint32_t now_ms);
  // True while an ACK for the last COMMAND may still be arriving. The
  // gateway sends nothing (no COMMAND, no BEACON) during this window.
  bool awaiting_ack(uint32_t now_ms) const;
  std::optional<CommandResult> take_result();

 private:
  struct Pending {
    bool active{false};
    uint8_t seq{0};
    uint8_t command{0};
    uint8_t param{0};
    uint8_t attempts{0};
    uint32_t sent_ms{0};
    uint32_t rpc_id{0};
  };

  void finish(uint8_t node_id, bool success, uint8_t result_code);

  std::array<Node, kMaxNodes + 1> nodes_{};
  std::array<Pending, kMaxNodes + 1> pending_{};
  std::array<uint8_t, kMaxNodes + 1> next_seq_{};
  std::array<std::optional<CommandResult>, kMaxNodes + 1> results_{};
};

}  // namespace hybrid_lora
