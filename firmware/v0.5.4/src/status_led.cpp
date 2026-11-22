#include "status_led.h"

#include <WiFi.h>
#include <esp_system.h>

#include "net_log.h"
#include "wifi_nvs.h"

StatusLed status_led;

namespace {
const char* reset_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_BROWNOUT: return "CRASH (brown-out: supply too weak)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}
}  // namespace

void StatusLed::begin() {
  const esp_reset_reason_t r = esp_reset_reason();
  crashed_ = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
             r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
  Log.printf("Last reset: %s\n", reset_name(r));
#if !HYBRID_ROLE_GATEWAY
  client_timeout_ms_ = 30000;  // node: one cloud report every 10 s
#endif
  pinMode(kPin, OUTPUT);
  digitalWrite(kPin, kActiveLow ? HIGH : LOW);  // start dark
#if HYBRID_ROLE_GATEWAY
  pinMode(kStatusPin, OUTPUT);
  digitalWrite(kStatusPin, kActiveLow ? HIGH : LOW);  // red dark
#endif
  xTaskCreatePinnedToCore(task_entry, "statusled", 2048, this, 1, nullptr, 0);
}

void StatusLed::task_entry(void* context) { static_cast<StatusLed*>(context)->task_loop(); }

void StatusLed::task_loop() {
  const uint32_t boot = millis();
  while (true) {
    const uint32_t now = millis();
    const uint32_t phase = now % 2000;  // 2 s frame
    bool on;
    if (crashed_ && now - boot < 15000) {
      on = (now / 50) % 2;  // 10 Hz
    } else if (wifinvs::isPortalActive()) {
      on = phase < 120 || (phase >= 300 && phase < 420);  // double blink
    } else if (!wifinvs::isEnabled()) {
      on = false;  // Wi-Fi turned off on purpose (WIFI 0 / BOOT button)
    } else if (WiFi.status() != WL_CONNECTED) {
      on = (now / 500) % 2;  // 1 Hz
    } else if (client_ms_ && now - client_ms_ < client_timeout_ms_) {
#if HYBRID_ROLE_GATEWAY
      on = phase < 60;  // gateway: phone page open -> short blip
#else
      on = false;  // node: ThingsBoard report OK -> dark
#endif
    } else {
#if HYBRID_ROLE_GATEWAY
      on = false;  // gateway: all fine -> dark
#else
      on = phase < 60;  // node: short blip
#endif
    }
    const bool packet = packet_ms_ && now - packet_ms_ < kPacketFlashMs;
#if HYBRID_ROLE_GATEWAY
    // Gateway: board LED (GPIO2) lights only for a received packet; every
    // other pattern goes to the red LED on kStatusPin.
    digitalWrite(kStatusPin, (on != kActiveLow) ? HIGH : LOW);
    on = packet;
#else
    if (packet) on = !on;  // packet flash
#endif
    digitalWrite(kPin, (on != kActiveLow) ? HIGH : LOW);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
