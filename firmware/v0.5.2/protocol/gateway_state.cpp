#include "gateway_state.h"

namespace hybrid_lora {

namespace {
// A jump of more than this many packets is a node restart, not a loss.
constexpr uint16_t kMaxCountedGap = 1000;

void count_test_packet(GatewayState::Node::Test& test, uint16_t seq) {
  const uint16_t gap = static_cast<uint16_t>(seq - test.last_seq);
  if (test.active && gap == 0) return;  // duplicate
  if (!test.active || gap > kMaxCountedGap) {
    test = {};  // first packet, or the node restarted
    test.active = true;
    test.window = 1;
    test.window_len = 1;
  } else {
    test.lost += gap - 1;
    test.last_gap_lost = static_cast<uint8_t>(gap - 1 > 255 ? 255 : gap - 1);
    test.window = (gap >= 32 ? 0 : test.window << gap) | 1;
    test.window_len = static_cast<uint8_t>(
        test.window_len + gap > 32 ? 32 : test.window_len + gap);
  }
  test.last_seq = seq;
  ++test.received;
}
}  // namespace

uint8_t GatewayState::Node::pdr_percent() const {
  const uint8_t n = test.window_len < kPdrWindow ? test.window_len : kPdrWindow;
  if (n == 0) return 0;
  uint8_t got = 0;
  for (uint8_t i = 0; i < n; ++i) got += (test.window >> i) & 1;
  return static_cast<uint8_t>((got * 100 + n / 2) / n);
}

void GatewayState::on_frame(const packet::Frame& frame, uint32_t now_ms) {
  if (frame.node_id == 0 || frame.node_id > kMaxNodes) return;
  Node& node = nodes_[frame.node_id];
  if (frame.type == packet::Type::Status) {
    packet::StatusPayload status{};
    if (!packet::read_status(frame, status)) return;
    node.status = status;
    if (status.test_mode) {
      // Test STATUS: lux is the sequence number, health is the TX power.
      node.status.lux = 0xFFFF;
      count_test_packet(node.test, status.lux);  // may reset node.test
      node.test.tx_dbm = status.health;
    } else {
      node.health_flags = status.health;
      node.test.active = false;  // next test run starts fresh
    }
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

bool GatewayState::awaiting_ack(uint32_t now_ms) const {
  for (uint8_t id = 1; id <= kMaxNodes; ++id) {
    const Pending& pending = pending_[id];
    if (pending.active && pending.attempts > 0 &&
        now_ms - pending.sent_ms < kAckWindowMs) {
      return true;
    }
  }
  return false;
}

std::optional<packet::Frame> GatewayState::next_due_command(uint32_t now_ms) {
  // One command in flight at a time: sending to node B while node A's ACK
  // is arriving would lose that ACK (half duplex on one board).
  if (awaiting_ack(now_ms)) return std::nullopt;
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
  // The test flag is kept even before the first STATUS: the RSSI/SNR
  // compensation depends on it. The other fields are published only once a
  // STATUS exists, so they wait for it.
  if (success && pending.command == packet::kCmdTestMode) {
    node.status.test_mode = pending.param ? 1 : 0;
  }
  if (success && node.has_status) {
    if (pending.command == packet::kCmdLightOn) node.status.light_state = 1;
    else if (pending.command == packet::kCmdLightOff) node.status.light_state = 0;
    else if (pending.command == packet::kCmdSetMode) node.status.mode = pending.param ? 1 : 0;
  }
  results_[node_id] = CommandResult{node_id, pending.rpc_id, success, result_code};
  pending.active = false;
}

}  // namespace hybrid_lora
