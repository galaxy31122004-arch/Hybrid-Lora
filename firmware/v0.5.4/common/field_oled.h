#pragma once

#include <Arduino.h>

// Gateway OLED (SSD1306 128x64, I2C 0x3C on SDA 22 / SCL 23) for range tests
// with the gateway carried around and Wi-Fi off. Shows the last test packet
// (SNR, RSSI, seq), loss, and the channel noise floor measured between
// packets. The BOOT button (GPIO0) turns Wi-Fi off and on: Wi-Fi near the
// radio raises the noise floor.
class FieldOled {
 public:
  static constexpr int kButtonPin = 0;   // BOOT button, low when pressed
  static constexpr uint8_t kAddress = 0x3C;

  // Probes the display; without one only the button task runs.
  void begin();
  // Main loop (core 1): a test STATUS was received.
  void on_packet(uint16_t seq, int16_t rssi_dbm, float snr_db, uint8_t pdr_percent,
                 uint32_t received, uint32_t lost);
  // Main loop: one raw channel RSSI reading (any time; the lowest reading of
  // each few-second window is the noise floor between packets).
  void noise_sample(int16_t rssi_dbm);

 private:
  struct Snapshot {
    uint32_t packet_ms = 0;  // 0: no packet yet
    uint16_t seq = 0;
    int16_t rssi = 0;
    float snr = 0;
    uint8_t pdr = 0;
    uint32_t received = 0;
    uint32_t lost = 0;
    int16_t noise = 0;       // 0: no reading yet
  };

  static void task_entry(void* context);
  void task_loop();
  void poll_button(uint32_t now);

  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snap_;
  bool display_ok_ = false;
  // Noise window (main loop only).
  int16_t window_min_ = 0;
  uint32_t window_start_ = 0;
  // Button debounce (OLED task only).
  bool button_down_ = false;
  uint32_t button_change_ = 0;
};

extern FieldOled field_oled;
