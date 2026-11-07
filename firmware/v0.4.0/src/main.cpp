#include <Arduino.h>
#include <SPI.h>
// Not used here directly: lets PlatformIO's default LDF find the libraries
// that thingsboard_bridge.cpp needs.
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "packet.h"
#include "gateway_state.h"
#include "pins.h"
#include "sx1278_radio.h"
#include "thingsboard_bridge.h"

namespace {
constexpr uint8_t kNodeId = 1;
constexpr uint8_t kMaxNodes = 50;
// Schedules sized for SF12 airtime (STATUS ~0.99 s, BEACON ~0.83 s) and the
// 1% duty-cycle limit of the SX1278 at +20 dBm:
//  - STATUS every 600 s +/- 60 s jitter: 50 nodes use ~8% of the uplink.
//  - One broadcast BEACON every 120 s: ~0.7% of the gateway's air time.
//  - A node falls back after 3 missed beacons.
constexpr uint32_t kStatusPeriodMs = 600000;
constexpr uint32_t kStatusJitterMs = 60000;
constexpr uint32_t kBeaconPeriodMs = 120000;
constexpr uint32_t kGatewayTimeoutMs = 3 * kBeaconPeriodMs;
// Separate channels inside the 433.05-434.79 MHz ISM band so the gateway's
// downlink radio does not blind its own uplink receiver on the same board.
constexpr long kUplinkHz = 433175000;    // Node -> Gateway radio1
constexpr long kDownlinkHz = 433575000;  // Gateway radio2 -> Node

SPIClass lora_spi(VSPI);
Sx1278Radio radio1(lora_spi, hybrid_lora::pins::LORA1_CS,
                   hybrid_lora::pins::LORA_RESET,
                   hybrid_lora::pins::LORA1_DIO0);
Sx1278Radio radio2(lora_spi, hybrid_lora::pins::LORA2_CS,
                   hybrid_lora::pins::LORA_RESET,
                   hybrid_lora::pins::LORA2_DIO0);
hybrid_lora::GatewayState gateway;
ThingsBoardBridge cloud;
bool gateway_role = false;
bool remote_mode = false;
bool beacon_sent = false;
uint32_t next_status_ms = 0;
uint32_t last_beacon = 0;
uint32_t last_gateway_beacon = 0;
uint8_t status_seq = 0;
uint8_t beacon_seq = 0;

bool send_frame(Sx1278Radio& radio, const hybrid_lora::packet::Frame& frame) {
  const auto bytes = hybrid_lora::packet::encode(frame);
  return !bytes.empty() && radio.send(bytes.data(), bytes.size());
}

// Random offset so nodes powered on together do not report in lockstep.
uint32_t status_delay(uint32_t base_ms) {
  return base_ms - kStatusJitterMs + esp_random() % (2 * kStatusJitterMs);
}

bool send_status() {
  return send_frame(radio1, hybrid_lora::packet::make_status(
      kNodeId, status_seq++, 0xFFFF, digitalRead(hybrid_lora::pins::RELAY1),
      remote_mode ? 1 : 0, 0x03));
}

void handle_node_command(const hybrid_lora::packet::Frame& frame) {
  hybrid_lora::packet::CommandPayload command{};
  if (!hybrid_lora::packet::read_command(frame, command)) return;
  uint8_t result = 0x00;
  if (command.command == 0x01) digitalWrite(hybrid_lora::pins::RELAY1, HIGH);
  else if (command.command == 0x02) digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  else if (command.command == 0x03 && command.param <= 1) remote_mode = command.param == 1;
  else if (command.command == 0x04) {
    next_status_ms = millis() + 2000;  // send STATUS once the ACK has gone out
  } else {
    result = 0x01;
  }
  send_frame(radio1, hybrid_lora::packet::make_ack(kNodeId, 0, frame.seq, result));
}

void process_node_radio() {
  if (!radio1.packet_available()) return;
  uint8_t buffer[64]{};
  const size_t length = radio1.receive(buffer, sizeof(buffer));
  const auto decoded = hybrid_lora::packet::decode(buffer, length);
  if (!decoded.ok) return;
  if (decoded.frame.type == hybrid_lora::packet::Type::Beacon) {
    last_gateway_beacon = millis();  // broadcast: every node accepts it
  } else if (decoded.frame.type == hybrid_lora::packet::Type::Command &&
             decoded.frame.node_id == kNodeId) {
    handle_node_command(decoded.frame);
  }
}

void process_gateway_radio() {
  if (!radio1.packet_available()) return;
  uint8_t buffer[64]{};
  const size_t length = radio1.receive(buffer, sizeof(buffer));
  const auto decoded = hybrid_lora::packet::decode(buffer, length);
  if (!decoded.ok || decoded.frame.node_id == 0 || decoded.frame.node_id > kMaxNodes) return;
  gateway.on_frame(decoded.frame, millis());
  if (decoded.frame.type == hybrid_lora::packet::Type::Status ||
      decoded.frame.type == hybrid_lora::packet::Type::Ack) {
    cloud.post_node(decoded.frame.node_id, gateway.node(decoded.frame.node_id));
  }
}

void run_node(uint32_t now) {
  process_node_radio();
  // Retry on the next loop if the radio is still busy; never drop a report.
  if (static_cast<int32_t>(now - next_status_ms) >= 0 && send_status()) {
    next_status_ms = now + status_delay(kStatusPeriodMs);
  }
  if (remote_mode && now - last_gateway_beacon >= kGatewayTimeoutMs) {
    digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  }
}

void run_gateway(uint32_t now) {
  process_gateway_radio();
  radio2.packet_available();  // Complete TX and return the second radio to RX.
  uint8_t node_id = 0;
  uint8_t command = 0;
  uint8_t param = 0;
  uint32_t rpc_id = 0;
  while (cloud.take_command(node_id, command, param, rpc_id)) {
    if (!gateway.queue_command(node_id, command, param, rpc_id)) {
      cloud.post_result({node_id, rpc_id, false, 0xFE});
    }
  }
  bool sent_command = false;
  const auto due = gateway.next_due_command(now);
  if (due.has_value() && send_frame(radio2, *due)) {
    gateway.mark_sent(due->node_id, now);
    sent_command = true;
  }
  while (const auto result = gateway.take_result()) {
    cloud.post_result(*result);
  }
  if (!sent_command && !due.has_value() &&
      (!beacon_sent || now - last_beacon >= kBeaconPeriodMs)) {
    if (!send_frame(radio2, hybrid_lora::packet::make_beacon(beacon_seq++))) return;
    last_beacon = now;
    beacon_sent = true;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("Hybrid-LoRa firmware v" FW_VERSION);
  pinMode(hybrid_lora::pins::MODE, INPUT_PULLUP);
  digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  digitalWrite(hybrid_lora::pins::RELAY2, LOW);
  digitalWrite(hybrid_lora::pins::RELAY3, LOW);
  pinMode(hybrid_lora::pins::RELAY1, OUTPUT);
  pinMode(hybrid_lora::pins::RELAY2, OUTPUT);
  pinMode(hybrid_lora::pins::RELAY3, OUTPUT);

  gateway_role = digitalRead(hybrid_lora::pins::MODE) == LOW;
  Serial.printf("Role: %s\n", gateway_role ? "Gateway" : "Node");
  pinMode(hybrid_lora::pins::LORA1_CS, OUTPUT);
  pinMode(hybrid_lora::pins::LORA2_CS, OUTPUT);
  digitalWrite(hybrid_lora::pins::LORA1_CS, HIGH);
  digitalWrite(hybrid_lora::pins::LORA2_CS, HIGH);
  lora_spi.begin(hybrid_lora::pins::LORA_SCK, hybrid_lora::pins::LORA_MISO,
                 hybrid_lora::pins::LORA_MOSI, hybrid_lora::pins::LORA1_CS);
  // Gateway: radio1 listens on uplink, radio2 sends on downlink.
  // Node: its only radio listens on downlink and sends on uplink.
  const bool radio1_ok = gateway_role ? radio1.begin(kUplinkHz, kUplinkHz)
                                      : radio1.begin(kDownlinkHz, kUplinkHz);
  const bool radio2_ok =
      !gateway_role || radio2.begin(kDownlinkHz, kDownlinkHz, false);
  Serial.printf("LoRa1: %s, LoRa2: %s\n", radio1_ok ? "OK" : "FAIL",
                gateway_role ? (radio2_ok ? "OK" : "FAIL") : "disabled");
  if (gateway_role) cloud.begin();
  last_gateway_beacon = millis();
  next_status_ms = millis() + esp_random() % 30000;  // first report within 30 s
}

void loop() {
  const uint32_t now = millis();
  if (gateway_role) run_gateway(now);
  else run_node(now);
  yield();
}
