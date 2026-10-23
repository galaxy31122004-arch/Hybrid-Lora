#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "packet.h"

namespace hybrid_lora {

class GatewayState {
 public:
  static constexpr uint8_t kMaxNodes = 50;
  static constexpr uint32_t kRetryMs = 1500;
  static constexpr uint8_t kMaxAttempts = 4;

  struct Node {
    bool seen{false};
    bool has_status{false};
    uint32_t last_seen_ms{0};
    uint8_t health_flags{0};
    packet::StatusPayload status{};
  };

  struct CommandResult {
    uint8_t node_id;
    uint32_t rpc_id;
    bool success;
    uint8_t result_code;
  };

  const Node& node(uint8_t node_id) const { return nodes_[node_id]; }
  void on_frame(const packet::Frame& frame, uint32_t now_ms);
  bool queue_command(uint8_t node_id, uint8_t command, uint8_t param,
                     uint32_t rpc_id);
  std::optional<packet::Frame> next_due_command(uint32_t now_ms);
  void mark_sent(uint8_t node_id, uint32_t now_ms);
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
