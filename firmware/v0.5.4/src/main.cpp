#include <algorithm>
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
// Not used here directly: lets PlatformIO's default LDF find the libraries
// that thingsboard_bridge.cpp needs.
#include <ESPmDNS.h>  // net_log.cpp
#include <HTTPClient.h>  // node_cloud.cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "packet.h"
#include "gateway_state.h"
#include "field_oled.h"
#include "field_web.h"
#include "net_log.h"
#include "status_led.h"
#include "node_cloud.h"
#include "pins.h"
#include "sx1278_radio.h"
#include "thingsboard_bridge.h"
#include "wifi_nvs.h"

// Role is chosen at build time (platformio.ini env "gateway" or "node"), not
// by a pin: a wrong jumper can no longer turn the gateway into a node.
#ifndef HYBRID_ROLE_GATEWAY
#error "Build with env gateway or node (HYBRID_ROLE_GATEWAY is not set)"
#endif
#ifndef NODE_ID
#define NODE_ID 1
#endif
// FIELD_TEST=1 (envs gateway-test / node-test): the node sends test STATUS
// from power-up and the gateway only listens, so a field test needs no
// Serial command and no reset.
#ifndef FIELD_TEST
#define FIELD_TEST 0
#endif

namespace {
constexpr bool gateway_role = HYBRID_ROLE_GATEWAY != 0;
constexpr uint8_t kNodeId = NODE_ID;  // node only; unique per board (1..50)
static_assert(NODE_ID >= 1 && NODE_ID <= 50, "NODE_ID must be 1..50");
constexpr uint8_t kMaxNodes = 50;
// Schedules sized for SF12 / 31.25 kHz airtime (STATUS, COMMAND, ACK, BEACON:
// ~4.2 s each at CR 4/8, preamble 12) and the 1% duty-cycle limit of the SX1278 at +20 dBm:
//  - STATUS (5 B, ~4.2 s) every 900 s +/- 90 s, plus 0..10 s after any
//    state change. That includes changes made by a command: the node cannot
//    tell whether its ACK arrived, so the STATUS confirms the new state.
//    50 nodes use ~23% of the uplink (~63% pure-ALOHA success).
//  - One broadcast BEACON every 480 s: ~0.9% of the gateway's air time
//    (every 240 s would be 1.8%, over the 1% limit).
//  - A node falls back after 3 missed beacons (24 min).
constexpr uint32_t kStatusPeriodMs = 900000;
constexpr uint32_t kStatusJitterMs = 90000;
constexpr uint32_t kChangeReportMaxDelayMs = 10000;  // random 0..10 s
// Test mode (RPC setTestMode): denser samples for field tests. The node
// stays at 20 dBm (as in the old sketch that reached further),
// and sends STATUS every 60 s +/- 12 s (4.2 s / 60 s = 7% air time, under
// the usual 10% for the band). The gateway adds any power offset back to
// the RSSI/SNR it reports, so test readings match normal 20 dB operation.
// Use it on a few nodes at a time; it switches itself off after the
// requested minutes.
// In test mode each STATUS is a test STATUS (packet.h make_test_status):
// same size and path as a real one, with a sequence number in place of lux
// and the TX power in place of the health bits.
// One-way field test: type TEST on the node's Serial. It runs until TEST 0
// and is saved, so the node restarts it on every power-up (no laptop needed
// in the field). Type QUIET 1 on the gateway so it sends nothing (no beacon,
// no commands) while it listens.
constexpr uint32_t kTestStatusPeriodMs = 60000;
constexpr uint32_t kTestStatusJitterMs = 12000;
constexpr int8_t kTestTxPowerDbm = 20;  // same as normal mode (lora_phy::kTxPowerDbm)
// node-test build: pause between test STATUS frames. 4.2 s on air + 1 s
// pause ~= 80% duty at 20 dBm (the datasheet rates +20 dBm for 1%; the user
// accepted this for field range tests: the RA-02 stayed cool at 36%).
constexpr uint32_t kFieldTestGapMs = 1000;
// Gateway: sample the uplink channel's noise floor while idle.
constexpr uint32_t kNoiseSampleMs = 2000;   // one reading every 2 s
constexpr uint8_t kNoiseSamplesPerReport = 5;  // report the mean every ~10 s
constexpr uint32_t kBeaconPeriodMs = 480000;
constexpr uint32_t kGatewayTimeoutMs = 3 * kBeaconPeriodMs;
// Keep the timings consistent with the PHY settings in sx1278_radio.h.
constexpr uint32_t kFrameAirMs = lora_phy::airtime_ms(5);  // STATUS/COMMAND/ACK
static_assert(kFrameAirMs * 100 <= kBeaconPeriodMs,
              "BEACON exceeds the 1% duty cycle allowed at +20 dBm");
static_assert(kTestStatusPeriodMs - kTestStatusJitterMs >= kFrameAirMs * 10,
              "test STATUS exceeds 10% air time");
static_assert(hybrid_lora::GatewayState::kAckWindowMs >= 2 * kFrameAirMs + 500,
              "ACK window shorter than COMMAND + ACK air time");
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
// Gateway downlink transmitter: radio2 normally. If radio2 does not answer at
// boot, radio1 takes over and sends the downlink too (single-radio gateway,
// half duplex like a node: it cannot hear uplink while it transmits).
Sx1278Radio* downlink = &radio2;
hybrid_lora::GatewayState gateway;
ThingsBoardBridge cloud;
NodeCloud node_cloud;      // node only: own Wi-Fi report (powered, frames sent)
uint32_t node_tx_count = 0;  // node only: LoRa frames sent since boot
bool remote_mode = false;
bool beacon_sent = false;
uint32_t next_status_ms = 0;
uint32_t last_noise_sample = 0;
int32_t noise_sum = 0;
uint8_t noise_count = 0;
uint32_t last_beacon = 0;
uint32_t last_gateway_beacon = 0;
uint8_t beacon_seq = 0;
uint8_t known_state = 0;  // status flags (light | remote | test) last sent in a STATUS
bool test_mode = false;
bool test_forever = false;  // started with TEST on Serial: no time limit
uint32_t test_mode_until_ms = 0;
uint16_t test_seq = 0;
bool gateway_quiet = false;  // QUIET 1: gateway transmits nothing
Preferences prefs;

void handle_serial();  // Serial commands (defined below)

void report_node_cloud();

bool send_frame(Sx1278Radio& radio, const hybrid_lora::packet::Frame& frame) {
  const auto bytes = hybrid_lora::packet::encode(frame);
  if (bytes.empty() || !radio.send(bytes.data(), bytes.size())) return false;
  if (!gateway_role) {
    ++node_tx_count;
    report_node_cloud();
  }
  return true;
}

// Next report time, with a random offset so nodes powered on together do not
// report in lockstep.
uint32_t next_status_delay() {
  // node-test build: a short pause after each test STATUS (kFieldTestGapMs)
  // keeps the 20 dBm PA from transmitting all the time.
  // The delay counts from the start of the TX, so add the frame air time.
  if (FIELD_TEST && test_mode) return kFrameAirMs + kFieldTestGapMs;
  const uint32_t base = test_mode ? kTestStatusPeriodMs : kStatusPeriodMs;
  const uint32_t jitter = test_mode ? kTestStatusJitterMs : kStatusJitterMs;
  return base - jitter + esp_random() % (2 * jitter);
}

// Node: hand the latest LoRa counters to the Wi-Fi report task.
void report_node_cloud() {
  // test_seq is the next number; the last one sent is one less.
  node_cloud.update({node_tx_count, static_cast<uint16_t>(test_seq - 1),
                     test_mode ? kTestTxPowerDbm : lora_phy::kTxPowerDbm, test_mode});
}

// Light, mode and test bits in the STATUS flags layout (packet.h), so the
// change check and the report share one definition.
uint8_t current_state() {
  namespace pk = hybrid_lora::packet;
  return (digitalRead(hybrid_lora::pins::RELAY1) ? pk::kFlagLightOn : 0) |
         (remote_mode ? pk::kFlagRemoteMode : 0) |
         (test_mode ? pk::kFlagTestMode : 0);
}

bool send_status() {
  namespace pk = hybrid_lora::packet;
  const uint8_t state = current_state();
  if (test_mode) {
    if (!send_frame(radio1, pk::make_test_status(
            kNodeId, test_seq, (state & pk::kFlagLightOn) ? 1 : 0,
            (state & pk::kFlagRemoteMode) ? 1 : 0, kTestTxPowerDbm))) {
      return false;
    }
    status_led.packet();  // node LED: one flash per frame sent
    Log.printf("TX test STATUS seq=%u\n", test_seq);
    ++test_seq;
    report_node_cloud();  // now with the seq just sent
    known_state = state;
    return true;
  }
  if (!send_frame(radio1, pk::make_status(
          kNodeId, 0xFFFF, (state & pk::kFlagLightOn) ? 1 : 0,
          (state & pk::kFlagRemoteMode) ? 1 : 0, 0x03,
          (state & pk::kFlagTestMode) ? 1 : 0))) {
    return false;
  }
  status_led.packet();
  known_state = state;
  return true;
}

void print_signal(const char* what, uint8_t node_id) {
  // FERR: sender's carrier offset; the link fails near +/-BW/4 (7.8 kHz).
  Log.printf("RX %s node=%u RSSI=%d dBm SNR=%.2f dB FERR=%ld Hz\n", what,
                node_id, radio1.last_rssi_dbm(), radio1.last_snr_db(),
                static_cast<long>(radio1.last_freq_error_hz()));
}

// Switch test mode on/off. Changes the TX power, so wait for any TX to end.
void set_test_mode(bool on, uint32_t minutes, bool forever) {
  while (radio1.transmitting()) radio1.packet_available();
  test_mode = on;
  test_forever = on && forever;
  test_mode_until_ms = millis() + minutes * 60000UL;
  radio1.set_power_dbm(on ? kTestTxPowerDbm : lora_phy::kTxPowerDbm);
  next_status_ms = millis() + (on ? 2000 : next_status_delay());
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
    // Switch power before the ACK so every frame carrying the new test flag
    // is sent at the matching power (the gateway compensates on that flag).
    set_test_mode(command.param > 0, command.param, false);
  } else {
    result = 0x01;
  }
  // A received ACK already updates the dashboard. known_state is left alone
  // on purpose: if every ACK is lost the gateway never learns the change, so
  // run_node() still reports it with a STATUS. It also keeps a change the
  // node made by itself (beacon-loss fallback) from being hidden by an ACK.
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
  status_led.packet();
  gateway.on_frame(decoded.frame, millis());
  // A node in test mode transmits at lower power; report what it would
  // measure at normal power. (SNR is only shifted while above the noise.)
  // A test STATUS carries its own TX power (on_frame just stored it); an ACK
  // uses the last known values, which the setTestMode ACK itself updates.
  const auto& node = gateway.node(decoded.frame.node_id);
  const int8_t node_tx = !node.status.test_mode ? lora_phy::kTxPowerDbm
                         : node.test.tx_dbm    ? static_cast<int8_t>(node.test.tx_dbm)
                                               : kTestTxPowerDbm;
  const int8_t offset = lora_phy::kTxPowerDbm - node_tx;
  const int16_t rssi = radio1.last_rssi_dbm() + offset;
  const float snr = radio1.last_snr_db() + offset;
  gateway.set_signal(decoded.frame.node_id, rssi, snr);
  if (decoded.frame.type == hybrid_lora::packet::Type::Status &&
      node.status.test_mode && node.test.active) {
    Log.printf("TEST node=%u seq=%u tx=%ddBm RSSI=%d SNR=%.2f (+%d dB) "
                  "lost=%u PDR(%u)=%u%% total=%lu/%lu\n",
                  decoded.frame.node_id, node.test.last_seq, node_tx, rssi, snr,
                  offset, node.test.last_gap_lost,
                  hybrid_lora::GatewayState::kPdrWindow, node.pdr_percent(),
                  static_cast<unsigned long>(node.test.received),
                  static_cast<unsigned long>(node.test.received + node.test.lost));
    // Field web page (phone GPS range map).
    field_web.add(decoded.frame.node_id, node.test.last_seq, rssi, snr,
                  radio1.last_freq_error_hz(), node.test.last_gap_lost, node_tx, offset);
    field_oled.on_packet(node.test.last_seq, rssi, snr, node.pdr_percent(), node.test.received,
                         node.test.lost);
  }
  print_signal(decoded.frame.type == hybrid_lora::packet::Type::Status ? "STATUS" : "ACK",
               decoded.frame.node_id);
  if (decoded.frame.type == hybrid_lora::packet::Type::Status ||
      decoded.frame.type == hybrid_lora::packet::Type::Ack) {
    cloud.post_node(decoded.frame.node_id, gateway.node(decoded.frame.node_id));
  }
}

void run_node(uint32_t now) {
  handle_serial();
  process_node_radio();
  // Retry on the next loop if the radio is still busy; never drop a report.
  if (test_mode && !test_forever &&
      static_cast<int32_t>(now - test_mode_until_ms) >= 0 &&
      radio1.set_power_dbm(lora_phy::kTxPowerDbm)) {  // waits for any TX to end
    test_mode = false;
    // Report at once: until the gateway sees test=0 it adds the test-mode
    // offset to this node's readings.
    next_status_ms = now;
    Log.println("Test mode ended");
  }
  if (static_cast<int32_t>(now - next_status_ms) >= 0 && send_status()) {
    next_status_ms = now + next_status_delay();
  }
  if (remote_mode && now - last_gateway_beacon >= kGatewayTimeoutMs) {
    digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  }
  // Report any state change after a short random delay.
  if (current_state() != known_state &&
      static_cast<int32_t>(next_status_ms - now) > static_cast<int32_t>(kChangeReportMaxDelayMs)) {
    next_status_ms = now + esp_random() % kChangeReportMaxDelayMs;
  }
}

// Noise floor of the uplink channel: sample while idle, report the mean.
void sample_noise(uint32_t now) {
  if (now - last_noise_sample < kNoiseSampleMs) return;
  last_noise_sample = now;
  // Skip while the downlink transmits: radio2's 20 dBm, 400 kHz away on the
  // same board, leaks into radio1 and would read as noise.
  if (downlink->transmitting()) return;
  const int16_t rssi = radio1.channel_rssi_dbm();
  if (rssi == Sx1278Radio::kNoReading) return;  // a packet is arriving
  noise_sum += rssi;
  if (++noise_count < kNoiseSamplesPerReport) return;
  // Round to nearest (the sum is negative; plain division rounds up).
  const int16_t mean = static_cast<int16_t>(
      (noise_sum - noise_count / 2) / noise_count);
  noise_sum = 0;
  noise_count = 0;
  Log.printf("NOISE uplink=%d dBm\n", mean);
  cloud.post_noise(mean);
}

// Isolation self-test (type ISO on Serial, gateway only): how much of
// radio2's own transmission leaks into radio1. radio1 needs about 65 dB of
// isolation to still hear the weakest nodes while radio2 transmits. Move or
// separate the antennas and run it again. Blocks the gateway for ~6 s and
// sends one 64-byte junk frame on the downlink (nodes drop it).
int16_t mean_rssi(int samples, uint32_t gap_ms) {
  int32_t sum = 0;
  for (int i = 0; i < samples; ++i) {
    sum += radio1.rssi_now();
    delay(gap_ms);
  }
  return static_cast<int16_t>((sum - samples / 2) / samples);
}

void isolation_test() {
  if (downlink != &radio2) {
    Log.println("ISO: single-radio gateway (LoRa2 FAIL), nothing to measure");
    return;
  }
  while (radio2.transmitting()) radio2.packet_available();  // let any TX end
  const int16_t idle = mean_rssi(20, 20);
  uint8_t junk[64];
  memset(junk, 0x55, sizeof(junk));  // TYPE 0x55 is not a valid packet type
  if (!radio2.send(junk, sizeof(junk))) {
    Log.println("ISO: radio2 busy, try again");
    return;
  }
  delay(400);  // skip the PA ramp; 64 B is on the air ~5.6 s
  const int16_t leak = mean_rssi(20, 100);
  while (radio2.transmitting()) radio2.packet_available();
  const int16_t isolation = lora_phy::kTxPowerDbm - leak;
  Log.printf("ISO: radio1 idle %d dBm, while radio2 sends %d dBm -> "
                "isolation %d dB (target >= 65 dB)%s\n",
                idle, leak, isolation,
                leak > -30 ? " [reading near saturation: real isolation is lower]" :
                leak - idle < 3 ? " [leak at noise floor: isolation is at least this]" : "");
}

// NOISE (gateway): 200 raw RSSI readings of the uplink channel over ~1 s.
// Turn the node off first, or its packets count as noise. Ideal floor at
// 31.25 kHz is about -123 dBm; every dB above it is lost range.
void measure_noise() {
  int16_t v[200];
  for (int i = 0; i < 200; ++i) {
    v[i] = radio1.rssi_now();
    delay(5);
  }
  std::sort(v, v + 200);
  Log.printf("NOISE now: min %d, median %d, max %d dBm (Wi-Fi %s; ideal ~-123)\n",
             v[0], v[100], v[199], wifinvs::isEnabled() ? "on" : "off");
}

void run_serial_command(char* line) {
  // Lines starting with '#' belong to the Wi-Fi module (#wifi-list for
  // pull-wifi-from-board.ps1); it does not read Serial itself here.
  if (line[0] == '#') {
    wifinvs::handleSerialLine(String(line));  // gateway and node both use Wi-Fi
    return;
  }
  char* arg = strchr(line, ' ');
  if (arg) *arg++ = 0;
  const long value = arg ? strtol(arg, nullptr, 10) : -1;
  if (gateway_role && strcasecmp(line, "ISO") == 0) {
    isolation_test();
  } else if (gateway_role && strcasecmp(line, "NOISE") == 0) {
    measure_noise();
  } else if (strcasecmp(line, "WIFI") == 0 && value >= 0) {
    wifinvs::setEnabled(value != 0);  // WIFI 0: radio off (noise test), WIFI 1: on
    Log.printf("WIFI %ld: Wi-Fi radio %s\n", value, value ? "on" : "off");
  } else if (gateway_role && strcasecmp(line, "QUIET") == 0) {
    gateway_quiet = value != 0;  // QUIET or QUIET 1: on, QUIET 0: off
    Log.printf("QUIET %s: gateway %s\n", gateway_quiet ? "on" : "off",
                  gateway_quiet ? "only listens (no beacon, commands rejected)"
                                : "sends beacons and commands again");
  } else if (!gateway_role && strcasecmp(line, "TEST") == 0) {
    // TEST: until TEST 0, saved for power-up. TEST n: n minutes, not saved.
    const bool forever = value < 0;
    const bool on = forever || value > 0;
    if (!forever && value > hybrid_lora::packet::kMaxTestMinutes) {
      Log.println("TEST: minutes 0..120");
      return;
    }
    set_test_mode(on, forever ? 0 : value, forever);
    prefs.putBool("test", forever);
    Log.printf("TEST %s\n", !on ? "off" : forever
                  ? "on until TEST 0 (also after power-up)" : "on for the given minutes");
  } else if (line[0]) {
    Log.println(gateway_role ? "Commands: ISO, NOISE, QUIET 1, QUIET 0, WIFI 0, WIFI 1"
                                : "Commands: TEST, TEST <minutes>, TEST 0, WIFI 0, WIFI 1");
  }
}

void handle_serial() {
  static char line[32];
  static uint8_t length = 0;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      line[length] = 0;
      run_serial_command(line);
      length = 0;
    } else if (length < sizeof(line) - 1) {
      line[length++] = c;
    }
  }
  // Same commands typed over the Wi-Fi log (scripts/netlog.py); '#' lines
  // are refused there.
  char net_line[NetLog::kLineMax];
  while (Log.take_line(net_line, sizeof(net_line))) {
    Log.printf("> %s\n", net_line);
    run_serial_command(net_line);
  }
}

void run_gateway(uint32_t now) {
  handle_serial();
  process_gateway_radio();
  sample_noise(now);
  static uint32_t last_oled_noise = 0;
  if (now - last_oled_noise >= 50 && !downlink->transmitting()) {
    last_oled_noise = now;
    field_oled.noise_sample(radio1.rssi_now());  // lowest reading per window = floor
  }
  downlink->packet_available();  // Complete TX and return the radio to RX.
  uint8_t node_id = 0;
  uint8_t command = 0;
  uint8_t param = 0;
  uint32_t rpc_id = 0;
  while (cloud.take_command(node_id, command, param, rpc_id)) {
    if (gateway_quiet || !gateway.queue_command(node_id, command, param, rpc_id)) {
      cloud.post_result({node_id, rpc_id, false, 0xFE});
    }
  }
  if (gateway_quiet) {  // one-way test: listen only
    while (const auto result = gateway.take_result()) cloud.post_result(*result);
    return;
  }
  bool sent_command = false;
  const auto due = gateway.next_due_command(now);
  if (due.has_value() && send_frame(*downlink, *due)) {
    gateway.mark_sent(due->node_id, now);
    sent_command = true;
  }
  while (const auto result = gateway.take_result()) {
    cloud.post_result(*result);
  }
  if (!sent_command && !due.has_value() && !gateway.awaiting_ack(now) &&
      (!beacon_sent || now - last_beacon >= kBeaconPeriodMs)) {
    if (!send_frame(*downlink, hybrid_lora::packet::make_beacon(beacon_seq++))) return;
    last_beacon = now;
    beacon_sent = true;
  }
}
}  // namespace

void setup() {
  // Core 0 runs the Wi-Fi tasks (ThingsBoard MQTT/HTTPS, net log, field web).
  // A TLS handshake computes for several seconds without yielding, which
  // starved IDLE0 past the 5 s task watchdog and reset the gateway in a loop
  // (seen 2026-11-16 with the field web task added). The tasks still yield
  // with vTaskDelay between iterations.
  disableCore0WDT();
  Serial.begin(115200);
  Log.println("Hybrid-LoRa firmware v" FW_VERSION);
  status_led.begin();  // board LED (GPIO2): Wi-Fi / phone / crash status
  digitalWrite(hybrid_lora::pins::RELAY1, LOW);
  digitalWrite(hybrid_lora::pins::RELAY2, LOW);
  digitalWrite(hybrid_lora::pins::RELAY3, LOW);
  pinMode(hybrid_lora::pins::RELAY1, OUTPUT);
  pinMode(hybrid_lora::pins::RELAY2, OUTPUT);
  pinMode(hybrid_lora::pins::RELAY3, OUTPUT);

  if (gateway_role) Log.println("Role: Gateway (build env gateway)");
  else Log.printf("Role: Node %u (build env node)\n", kNodeId);
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
  Log.printf("LoRa1: %s, LoRa2: %s\n", radio1_ok ? "OK" : "FAIL",
                gateway_role ? (radio2_ok ? "OK" : "FAIL") : "disabled");
  if (gateway_role && radio1_ok && !radio2_ok) {
    // radio1 now listens on uplink and transmits on downlink, like a node
    // with the channels swapped. No reset: radio2 shares the reset line.
    radio1.begin(kUplinkHz, kDownlinkHz, false);
    downlink = &radio1;
    Log.println("Single-radio gateway: LoRa1 receives uplink and sends the downlink");
  }
  if (gateway_role) {
    cloud.begin();
    field_web.begin();  // http://<IP>:8080 range map for the phone
    field_oled.begin();  // OLED + BOOT button (Wi-Fi off/on)
  } else {
    node_cloud.begin(kNodeId);
    field_oled.begin();  // BOOT button turns Wi-Fi off/on (no OLED on the node)
  }
  // Log over Wi-Fi (TCP 23): hybrid-gateway.local / hybrid-node-NN.local
  char hostname[24];
  if (gateway_role) snprintf(hostname, sizeof(hostname), "hybrid-gateway");
  else snprintf(hostname, sizeof(hostname), "hybrid-node-%02u", kNodeId);
  Log.begin(hostname);
  prefs.begin("hybrid-lora", false);
  if (FIELD_TEST && gateway_role) {
    gateway_quiet = true;
    Log.println("FIELD TEST build: gateway only listens (QUIET 0 to send again)");
  }
  if (FIELD_TEST && !gateway_role && radio1_ok) {
    set_test_mode(true, 0, true);
    Log.println("FIELD TEST build: test STATUS every ~5 s at 20 dBm from power-up");
  } else if (!gateway_role && radio1_ok && prefs.getBool("test", false)) {
    set_test_mode(true, 0, true);
    Log.println("TEST on (saved): test STATUS every ~60 s at 20 dBm; TEST 0 to stop");
  }
  last_gateway_beacon = millis();
  next_status_ms = millis() + (FIELD_TEST && !gateway_role ? 2000 : esp_random() % 30000);  // first report: field test after 2 s, else within 30 s
}

void loop() {
  const uint32_t now = millis();
  if (gateway_role) run_gateway(now);
  else run_node(now);
  yield();
}
