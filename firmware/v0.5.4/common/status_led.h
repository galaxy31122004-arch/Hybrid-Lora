#pragma once

#include <Arduino.h>

// Board LED U21: two-colour, common anode to 3.3 V, GREEN on GPIO2 and RED
// on GPIO15 through 330 ohm (schematic 2026-10-18). LOW lights a colour, so
// kActiveLow is true. The node uses only green.
// Patterns, every 2 s unless noted:
//   10 Hz flashing for 15 s after boot   restarted after a crash (panic,
//                                         watchdog, brown-out)
//   1 Hz blink                            looking for Wi-Fi
//   double blink                          no network: setup portal AP is open
// Gateway: green (GPIO2) only shows packets, one 200 ms flash per received
// LoRa packet. The other patterns are shown in red (GPIO15, kStatusPin):
//   off                                   Wi-Fi up, nothing to report
//   short blip                            phone page open (polled < 5 s ago)
// Node (dark while all is fine):
//   off                                   last ThingsBoard report OK (< 30 s)
//   short blip                            Wi-Fi up, no cloud report yet
//   one 200 ms flash                      a LoRa frame was sent
// Stuck on with no rhythm: firmware hung. A boot loop before the firmware
// starts (GPIO12 strap, "boot:0x33") leaves the LED off.
class StatusLed {
 public:
  static constexpr int kPin = 2;
  static constexpr bool kActiveLow = true;  // common-anode LED: LOW lights it
  static constexpr int kStatusPin = 15;     // gateway: red half of U21 (GPIO15, strap pin: only driven after boot)
  static constexpr uint32_t kPacketFlashMs = 200;

  // Starts the LED task (core 0) and records whether the last reset was a
  // crash; prints the reset reason to the log.
  void begin();
  // Phone page polled (gateway) or cloud report OK (node).
  void client_seen() { client_ms_ = millis(); }
  // A packet was received (gateway) or sent (node): 200 ms flash.
  void packet() { packet_ms_ = millis(); }

 private:
  static void task_entry(void* context);
  void task_loop();

  volatile uint32_t client_ms_ = 0;
  volatile uint32_t packet_ms_ = 0;
  bool crashed_ = false;
  uint32_t client_timeout_ms_ = 5000;
};

extern StatusLed status_led;
