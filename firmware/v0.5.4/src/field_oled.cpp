#include "field_oled.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "net_log.h"
#include "pins.h"
#include "wifi_nvs.h"

FieldOled field_oled;

namespace {
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
constexpr uint32_t kNoiseWindowMs = 6000;  // > one packet + pause (~5.2 s)
constexpr uint32_t kSignalLostMs = 20000;  // as the phone page
}  // namespace

void FieldOled::begin() {
  pinMode(kButtonPin, INPUT_PULLUP);
  Wire.begin(hybrid_lora::pins::I2C_SDA, hybrid_lora::pins::I2C_SCL);
  Wire.beginTransmission(kAddress);
  display_ok_ = Wire.endTransmission() == 0;
  if (display_ok_) {
    display.setI2CAddress(kAddress << 1);
    display.setBusClock(400000);
    display.begin();
    Log.println("OLED: SSD1306 found (BOOT button: Wi-Fi off/on)");
  } else {
    Log.println("OLED: none at 0x3C (BOOT button still toggles Wi-Fi)");
  }
  xTaskCreatePinnedToCore(task_entry, "oled", 4096, this, 1, nullptr, 0);
}

void FieldOled::on_packet(uint16_t seq, int16_t rssi_dbm, float snr_db, uint8_t pdr_percent,
                          uint32_t received, uint32_t lost) {
  portENTER_CRITICAL(&lock_);
  snap_.packet_ms = millis();
  if (!snap_.packet_ms) snap_.packet_ms = 1;
  snap_.seq = seq;
  snap_.rssi = rssi_dbm;
  snap_.snr = snr_db;
  snap_.pdr = pdr_percent;
  snap_.received = received;
  snap_.lost = lost;
  portEXIT_CRITICAL(&lock_);
}

void FieldOled::noise_sample(int16_t rssi_dbm) {
  const uint32_t now = millis();
  if (!window_start_ || rssi_dbm < window_min_) window_min_ = rssi_dbm;
  if (!window_start_) window_start_ = now;
  if (now - window_start_ < kNoiseWindowMs) return;
  portENTER_CRITICAL(&lock_);
  snap_.noise = window_min_;
  portEXIT_CRITICAL(&lock_);
  window_start_ = 0;
}

void FieldOled::task_entry(void* context) { static_cast<FieldOled*>(context)->task_loop(); }

void FieldOled::poll_button(uint32_t now) {
  // A press counts only after the pin stays low for 80 ms: a single low
  // reading (glitch) must not toggle Wi-Fi.
  const bool down = digitalRead(kButtonPin) == LOW;
  if (down == button_down_) { button_change_ = now; return; }
  if (now - button_change_ < 80) return;  // must stay in the new state
  button_change_ = now;
  button_down_ = down;
  if (!down) return;  // act on press
  const bool on = !wifinvs::isEnabled();
  wifinvs::setEnabled(on);
  Log.printf("BOOT button: Wi-Fi %s\n", on ? "on" : "off");
}

void FieldOled::task_loop() {
  uint32_t last_draw = 0;
  while (true) {
    const uint32_t now = millis();
    poll_button(now);
    if (display_ok_ && now - last_draw >= 250) {
      last_draw = now;
      Snapshot s;
      portENTER_CRITICAL(&lock_);
      s = snap_;
      portEXIT_CRITICAL(&lock_);
      char line[32];
      display.clearBuffer();
      display.setFont(u8g2_font_6x10_tf);
      display.drawStr(0, 9, wifinvs::isEnabled() ? "WiFi ON" : "WiFi OFF");
      if (s.noise) {
        snprintf(line, sizeof(line), "noise %d", s.noise);
        display.drawStr(128 - display.getStrWidth(line), 9, line);
      }
      display.drawHLine(0, 12, 128);
      const uint32_t age = s.packet_ms ? (now - s.packet_ms) / 1000 : 0;
      if (!s.packet_ms || now - s.packet_ms > kSignalLostMs) {
        display.setFont(u8g2_font_9x15B_tf);
        display.drawStr(0, 36, s.packet_ms ? "NO SIGNAL" : "WAITING");
        display.setFont(u8g2_font_6x10_tf);
        if (s.packet_ms) {
          snprintf(line, sizeof(line), "last packet %lus ago", static_cast<unsigned long>(age));
          display.drawStr(0, 50, line);
        }
      } else {
        display.setFont(u8g2_font_logisoso22_tn);
        snprintf(line, sizeof(line), "%.1f", s.snr);
        display.drawStr(0, 39, line);
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(92, 28, "SNR");
        display.drawStr(92, 39, "dB");
        snprintf(line, sizeof(line), "RSSI %d  #%u", s.rssi, s.seq);
        display.drawStr(0, 51, line);
      }
      if (s.packet_ms) {
        snprintf(line, sizeof(line), "PDR %u%% lost %lu %lus", s.pdr,
                 static_cast<unsigned long>(s.lost), static_cast<unsigned long>(age));
        display.drawStr(0, 63, line);
      }
      display.sendBuffer();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
