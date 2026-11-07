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
//  - STATUS (5 B, ~0.83 s) every 900 s +/- 90 s, plus soon after a state
//    change the gateway did not command (e.g. beacon-loss fallback). Changes
//    made by a command are confirmed by the ACK instead. 50 nodes use ~4.6%
//    of the uplink (~91% pure-ALOHA success).
//  - One broadcast BEACON every 120 s: ~0.7% of the gateway's air time.
//  - A node falls back after 3 missed beacons.
constexpr uint32_t kStatusPeriodMs = 900000;
constexpr uint32_t kStatusJitterMs = 90000;
constexpr uint32_t kChangeReportMaxDelayMs = 10000;  // random 0..10 s
// Test mode (RPC setTestMode): denser samples for field tests. The node
// drops to 17 dBm, where the SX1278's 1% limit for +20 dBm no longer applies,
// and sends STATUS every 15 s +/- 3 s (0.83 s / 15 s = 5.5% air time, under
// the usual 10% for the band). The gateway adds kTestPowerOffsetDb back to
// the RSSI/SNR it reports, so test readings match normal 20 dB operation.
// Use it on a few nodes at a time; it switches itself off after the
// requested minutes.
constexpr uint32_t kTestStatusPeriodMs = 15000;
constexpr uint32_t kTestStatusJitterMs = 3000;
constexpr int8_t kTestTxPowerDbm = 17;
constexpr int8_t kTestPowerOffsetDb = lora_phy::kTxPowerDbm - kTestTxPowerDbm;
// Gateway: sample the uplink channel's noise floor while idle.
constexpr uint32_t kNoiseSampleMs = 2000;   // one reading every 2 s
constexpr uint8_t kNoiseSamplesPerReport = 5;  // report the mean every ~10 s
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
uint32_t last_noise_sample = 0;
int32_t noise_sum = 0;
uint8_t noise_count = 0;
uint32_t last_beacon = 0;
uint32_t last_gateway_beacon = 0;
uint8_t beacon_seq = 0;
uint8_t known_state = 0;  // light | mode | test bits the gateway already knows
bool test_mode = false;
uint32_t test_mode_until_ms = 0;

bool send_frame(Sx1278Radio& radio, const hybrid_lora::packet::Frame& frame) {
  const auto bytes = hybrid_lora::packet::encode(frame);
  return !bytes.empty() && radio.send(bytes.data(), bytes.size());
}

// Next report time, with a random offset so nodes powered on together do not
// report in lockstep.
uint32_t next_status_delay() {
  const uint32_t base = test_mode ? kTestStatusPeriodMs : kStatusPeriodMs;
  const uint32_t jitter = test_mode ? kTestStatusJitterMs : kStatusJitterMs;
  return base - jitter + esp_random() % (2 * jitter);
}

uint8_t current_state() {
  return (digitalRead(hybrid_lora::pins::RELAY1) ? 0x01 : 0) |
         (remote_mode ? 0x02 : 0) | (test_mode ? 0x04 : 0);
}

bool send_status() {
  const uint8_t state = current_state();
  if (!send_frame(radio1, hybrid_lora::packet::make_status(
          kNodeId, 0xFFFF, state & 0x01, (state & 0x02) ? 1 : 0, 0x03,
          (state & 0x04) ? 1 : 0))) {
    return false;
  }
  known_state = state;
  return true;
}

void print_signal(const char* what, uint8_t node_id) {
  Serial.printf("RX %s node=%u RSSI=%d dBm SNR=%.2f dB\n", what, node_id,
                radio1.last_rssi_dbm(), radio1.last_snr_db());
}

void handle_node_command(const hybrid_lora::packet::Frame& frame) {
  hybrid_lora::packet::CommandPayload command{};
  if (!hybrid_lora::packet::read_command(frame, command)) return;
  namespace pk = hybrid_lora::packet;
  uint8_t result = 0x00;
  if (command.command == pk::kCmdLightOn) digitalWrite(hybrid_lora::pins::RELAY1, HIGH);
  else if (command.command == pk::kCmdLightOff) digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  else if (command.command == pk::kCmdSetMode && command.param <= 1) remote_mode = command.param == 1;
  else if (command.command == pk::kCmdRequestStatus) {
    next_status_ms = millis() + 2000;  // explicit request: STATUS after the ACK
  } else if (command.command == pk::kCmdTestMode && command.param <= pk::kMaxTestMinutes) {
    test_mode = command.param > 0;
    test_mode_until_ms = millis() + command.param * 60000UL;
    // Switch power before the ACK so every frame carrying the new test flag
    // is sent at the matching power (the gateway compensates on that flag).
    radio1.set_power_dbm(test_mode ? kTestTxPowerDbm : lora_phy::kTxPowerDbm);
    next_status_ms = millis() + (test_mode ? 2000 : next_status_delay());
  } else {
    result = 0x01;
  }
  // The gateway learns command-made changes from this ACK; if the ACK is
  // lost, the gateway retries and the node answers again.
  if (send_frame(radio1, hybrid_lora::packet::make_ack(kNodeId, 0, frame.seq, result))) {
    known_state = current_state();
  }
}

void process_node_radio() {
  if (!radio1.packet_available()) return;
  uint8_t buffer[64]{};
  const size_t length = radio1.receive(buffer, sizeof(buffer));
  const auto decoded = hybrid_lora::packet::decode(buffer, length);
  if (!decoded.ok) return;
  if (decoded.frame.type == hybrid_lora::packet::Type::Beacon) {
    last_gateway_beacon = millis();  // broadcast: every node accepts it
    print_signal("BEACON", 0);       // downlink quality for field tests
  } else if (decoded.frame.type == hybrid_lora::packet::Type::Command &&
             decoded.frame.node_id == kNodeId) {
    print_signal("COMMAND", kNodeId);
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
  // A node in test mode transmits kTestPowerOffsetDb lower; report what it
  // would measure at normal power. (SNR is only shifted while above the noise.)
  const bool test = gateway.node(decoded.frame.node_id).status.test_mode;
  const int16_t rssi = radio1.last_rssi_dbm() + (test ? kTestPowerOffsetDb : 0);
  const float snr = radio1.last_snr_db() + (test ? kTestPowerOffsetDb : 0);
  gateway.set_signal(decoded.frame.node_id, rssi, snr);
  print_signal(decoded.frame.type == hybrid_lora::packet::Type::Status ? "STATUS" : "ACK",
               decoded.frame.node_id);
  if (decoded.frame.type == hybrid_lora::packet::Type::Status ||
      decoded.frame.type == hybrid_lora::packet::Type::Ack) {
    cloud.post_node(decoded.frame.node_id, gateway.node(decoded.frame.node_id));
  }
}

void run_node(uint32_t now) {
  process_node_radio();
  // Retry on the next loop if the radio is still busy; never drop a report.
  if (test_mode && static_cast<int32_t>(now - test_mode_until_ms) >= 0 &&
      radio1.set_power_dbm(lora_phy::kTxPowerDbm)) {  // waits for any TX to end
    test_mode = false;  // reported below as an unsolicited change
    Serial.println("Test mode ended");
  }
  if (static_cast<int32_t>(now - next_status_ms) >= 0 && send_status()) {
    next_status_ms = now + next_status_delay();
  }
  if (remote_mode && now - last_gateway_beacon >= kGatewayTimeoutMs) {
    digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  }
  // Report a change the gateway did not cause, after a short random delay.
  if (current_state() != known_state &&
      static_cast<int32_t>(next_status_ms - now) > static_cast<int32_t>(kChangeReportMaxDelayMs)) {
    next_status_ms = now + esp_random() % kChangeReportMaxDelayMs;
  }
}

// Noise floor of the uplink channel: sample while idle, report the mean.
void sample_noise(uint32_t now) {
  if (now - last_noise_sample < kNoiseSampleMs) return;
  last_noise_sample = now;
  const int16_t rssi = radio1.channel_rssi_dbm();
  if (rssi == Sx1278Radio::kNoReading) return;  // a packet is arriving
  noise_sum += rssi;
  if (++noise_count < kNoiseSamplesPerReport) return;
  const int16_t mean = static_cast<int16_t>(noise_sum / noise_count);
  noise_sum = 0;
  noise_count = 0;
  Serial.printf("NOISE uplink=%d dBm\n", mean);
  cloud.post_noise(mean);
}

void run_gateway(uint32_t now) {
  process_gateway_radio();
  sample_noise(now);
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
