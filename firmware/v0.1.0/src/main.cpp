#include <Arduino.h>
#include <SPI.h>

#include "packet.h"
#include "gateway_state.h"
#include "pins.h"
#include "sx1278_radio.h"
#include "thingsboard_bridge.h"

namespace {
constexpr uint8_t kNodeId = 1;
constexpr uint8_t kMaxNodes = 50;
constexpr uint32_t kHeartbeatPeriodMs = 60000;
constexpr uint32_t kStatusPeriodMs = 300000;
constexpr uint32_t kBeaconSlotMs = 600;  // 50 nodes each receive a beacon every 30 s.
constexpr uint32_t kGatewayTimeoutMs = 90000;
constexpr long kLoRaFrequencyHz = 433000000;

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
uint32_t last_heartbeat = 0;
uint32_t last_status = 0;
uint32_t last_beacon = 0;
uint32_t last_gateway_beacon = 0;
uint8_t beacon_node = 1;

bool send_frame(Sx1278Radio& radio, const hybrid_lora::packet::Frame& frame) {
  const auto bytes = hybrid_lora::packet::encode(frame);
  return !bytes.empty() && radio.send(bytes.data(), bytes.size());
}

void handle_node_command(const hybrid_lora::packet::Frame& frame) {
  hybrid_lora::packet::CommandPayload command{};
  if (!hybrid_lora::packet::read_command(frame, command)) return;
  uint8_t result = 0x00;
  if (command.command == 0x01) digitalWrite(hybrid_lora::pins::RELAY1, HIGH);
  else if (command.command == 0x02) digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  else if (command.command == 0x03 && command.param <= 1) remote_mode = command.param == 1;
  else if (command.command == 0x04) {
    send_frame(radio1, hybrid_lora::packet::make_status(
        kNodeId, 0, 0, 0xFFFF, digitalRead(hybrid_lora::pins::RELAY1),
        remote_mode ? 1 : 0));
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
  if (!decoded.ok || decoded.frame.node_id != kNodeId) return;
  if (decoded.frame.type == hybrid_lora::packet::Type::Beacon) {
    last_gateway_beacon = millis();
  } else if (decoded.frame.type == hybrid_lora::packet::Type::Command) {
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
      decoded.frame.type == hybrid_lora::packet::Type::Heartbeat) {
    cloud.post_node(decoded.frame.node_id, gateway.node(decoded.frame.node_id));
  }
}

void run_node(uint32_t now) {
  process_node_radio();
  if (now - last_heartbeat >= kHeartbeatPeriodMs) {
    last_heartbeat = now;
    send_frame(radio1, hybrid_lora::packet::make_heartbeat(kNodeId, 0, 0x0D));
  }
  if (now - last_status >= kStatusPeriodMs) {
    last_status = now;
    send_frame(radio1, hybrid_lora::packet::make_status(
        kNodeId, 0, 0, 0xFFFF, digitalRead(hybrid_lora::pins::RELAY1),
        remote_mode ? 1 : 0));
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
  if (!sent_command && !due.has_value() && now - last_beacon >= kBeaconSlotMs) {
    if (!send_frame(radio2, hybrid_lora::packet::Frame{
            hybrid_lora::packet::Type::Beacon, beacon_node, 0, {}})) return;
    last_beacon = now;
    beacon_node = beacon_node == kMaxNodes ? 1 : beacon_node + 1;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
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
  const bool radio1_ok = radio1.begin(kLoRaFrequencyHz);
  const bool radio2_ok = !gateway_role || radio2.begin(kLoRaFrequencyHz, false);
  Serial.printf("LoRa1: %s, LoRa2: %s\n", radio1_ok ? "OK" : "FAIL",
                gateway_role ? (radio2_ok ? "OK" : "FAIL") : "disabled");
  if (gateway_role) cloud.begin();
  last_gateway_beacon = millis();
}

void loop() {
  const uint32_t now = millis();
  if (gateway_role) run_gateway(now);
  else run_node(now);
  yield();
}
