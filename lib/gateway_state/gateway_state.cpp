#include "gateway_state.h"

namespace hybrid_lora {

void GatewayState::on_frame(const packet::Frame& frame, uint32_t now_ms) {
  if (frame.node_id == 0 || frame.node_id > kMaxNodes) return;
  Node& node = nodes_[frame.node_id];
  if (frame.type == packet::Type::Status) {
    packet::StatusPayload status{};
    if (!packet::read_status(frame, status)) return;
    node.status = status;
    node.has_status = true;
    node.seen = true;
    node.last_seen_ms = now_ms;
  } else if (frame.type == packet::Type::Heartbeat) {
    packet::HeartbeatPayload health{};
    if (!packet::read_heartbeat(frame, health)) return;
    node.health_flags = health.flags;
    node.seen = true;
    node.last_seen_ms = now_ms;
  } else if (frame.type == packet::Type::Ack) {
    packet::AckPayload ack{};
    if (!packet::read_ack(frame, ack)) return;
    Pending& command = pending_[frame.node_id];
    if (command.active && command.attempts > 0 &&
        command.seq == ack.command_seq) {
      finish(frame.node_id, ack.result == 0, ack.result);
    }
  }
}

bool GatewayState::queue_command(uint8_t node_id, uint8_t command,
                                 uint8_t param, uint32_t rpc_id) {
  if (node_id == 0 || node_id > kMaxNodes || command < 1 || command > 4 ||
      pending_[node_id].active || results_[node_id].has_value()) {
    return false;
  }
  Pending& pending = pending_[node_id];
  pending = {true, next_seq_[node_id]++, command, param, 0, 0, rpc_id};
  return true;
}

std::optional<packet::Frame> GatewayState::next_due_command(uint32_t now_ms) {
  for (uint8_t id = 1; id <= kMaxNodes; ++id) {
    Pending& pending = pending_[id];
    if (!pending.active) continue;
    if (pending.attempts > 0 && now_ms - pending.sent_ms < kRetryMs) continue;
    if (pending.attempts == kMaxAttempts) {
      finish(id, false, 0xFF);
      continue;
    }
    return packet::make_command(id, pending.seq, pending.command, pending.param);
  }
  return std::nullopt;
}

void GatewayState::mark_sent(uint8_t node_id, uint32_t now_ms) {
  if (node_id == 0 || node_id > kMaxNodes) return;
  Pending& pending = pending_[node_id];
  if (!pending.active || pending.attempts >= kMaxAttempts) return;
  ++pending.attempts;
  pending.sent_ms = now_ms;
}

std::optional<GatewayState::CommandResult> GatewayState::take_result() {
  for (uint8_t id = 1; id <= kMaxNodes; ++id) {
    if (!results_[id].has_value()) continue;
    auto result = results_[id];
    results_[id].reset();
    return result;
  }
  return std::nullopt;
}

void GatewayState::finish(uint8_t node_id, bool success,
                          uint8_t result_code) {
  Pending& pending = pending_[node_id];
  results_[node_id] = CommandResult{node_id, pending.rpc_id, success,
                                     result_code};
  pending.active = false;
}

}  // namespace hybrid_lora
