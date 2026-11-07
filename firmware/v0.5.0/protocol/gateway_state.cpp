#include "gateway_state.h"

namespace hybrid_lora {

void GatewayState::on_frame(const packet::Frame& frame, uint32_t now_ms) {
  if (frame.node_id == 0 || frame.node_id > kMaxNodes) return;
  Node& node = nodes_[frame.node_id];
  if (frame.type == packet::Type::Status) {
    packet::StatusPayload status{};
    if (!packet::read_status(frame, status)) return;
    node.status = status;
    node.health_flags = status.health;
    node.has_status = true;
    node.seen = true;
    node.last_seen_ms = now_ms;
  } else if (frame.type == packet::Type::Ack) {
    packet::AckPayload ack{};
    if (!packet::read_ack(frame, ack)) return;
    node.seen = true;  // an ACK also proves the node is alive
    node.last_seen_ms = now_ms;
    Pending& command = pending_[frame.node_id];
    if (command.active && command.attempts > 0 &&
        command.seq == ack.command_seq) {
      finish(frame.node_id, ack.result == 0, ack.result);
    }
  }
}

void GatewayState::set_signal(uint8_t node_id, int16_t rssi_dbm, float snr_db) {
  if (node_id == 0 || node_id > kMaxNodes) return;
  Node& node = nodes_[node_id];
  node.has_signal = true;
  node.rssi_dbm = rssi_dbm;
  node.snr_db = snr_db;
}

bool GatewayState::queue_command(uint8_t node_id, uint8_t command,
                                 uint8_t param, uint32_t rpc_id) {
  if (node_id == 0 || node_id > kMaxNodes || command < packet::kCmdLightOn ||
      command > packet::kCmdTestMode ||
      (command == packet::kCmdTestMode && param > packet::kMaxTestMinutes) ||
      pending_[node_id].active || results_[node_id].has_value()) {
    return false;
  }
  pending_[node_id] = {true, next_seq_[node_id]++, command, param, 0, 0, rpc_id};
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

void GatewayState::finish(uint8_t node_id, bool success, uint8_t result_code) {
  Pending& pending = pending_[node_id];
  // A successful ACK confirms the new state, so the node does not have to
  // spend another SF12 STATUS on it.
  Node& node = nodes_[node_id];
  if (success && node.has_status) {
    if (pending.command == packet::kCmdLightOn) node.status.light_state = 1;
    else if (pending.command == packet::kCmdLightOff) node.status.light_state = 0;
    else if (pending.command == packet::kCmdSetMode) node.status.mode = pending.param ? 1 : 0;
    else if (pending.command == packet::kCmdTestMode) node.status.test_mode = pending.param ? 1 : 0;
  }
  results_[node_id] = CommandResult{node_id, pending.rpc_id, success, result_code};
  pending.active = false;
}

}  // namespace hybrid_lora
