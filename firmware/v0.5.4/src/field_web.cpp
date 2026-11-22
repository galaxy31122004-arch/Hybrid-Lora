#include "field_web.h"

#include <WebServer.h>
#include <WiFi.h>

#include "field_page.h"
#include "net_log.h"
#include "status_led.h"

FieldWeb field_web;

void FieldWeb::begin() {
  xTaskCreatePinnedToCore(task_entry, "fieldweb", 6144, this, 1, nullptr, 0);
}

void FieldWeb::add(uint8_t node, uint16_t seq, int16_t rssi_dbm, float snr_db, int32_t ferr_hz,
                   uint8_t lost, int8_t tx_dbm, int8_t offset_db) {
  portENTER_CRITICAL(&lock_);
  Event& e = events_[next_id_ % kEvents];
  e.id = next_id_++;
  e.ms = millis();
  e.ferr_hz = ferr_hz;
  e.seq = seq;
  e.rssi_dbm = rssi_dbm;
  e.snr_x4 = static_cast<int16_t>(lroundf(snr_db * 4));
  e.node = node;
  e.lost = lost;
  e.tx_dbm = tx_dbm;
  e.offset_db = offset_db;
  portEXIT_CRITICAL(&lock_);
}

void FieldWeb::task_entry(void* context) { static_cast<FieldWeb*>(context)->task_loop(); }

void FieldWeb::task_loop() {
  WebServer server(kPort);
  server.on("/", HTTP_GET, [&server]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", kFieldPageHtml);
  });
  // /api/packets?after=<id>: up to 64 events newer than id, oldest first.
  // "now" is the gateway clock, so the phone can convert event times.
  server.on("/api/packets", HTTP_GET, [this, &server]() {
    status_led.client_seen();  // the page polls every second while open
    const uint32_t after = server.hasArg("after") ? server.arg("after").toInt() : 0;
    String json;
    json.reserve(6000);
    json += "{\"now\":";
    json += millis();
    json += ",\"packets\":[";
    uint32_t newest;
    portENTER_CRITICAL(&lock_);
    newest = next_id_ - 1;
    portEXIT_CRITICAL(&lock_);
    uint32_t first = after + 1;
    if (newest >= kEvents && first < newest - kEvents + 1) first = newest - kEvents + 1;
    if (newest >= first && newest - first >= 64) first = newest - 63;  // keep replies small
    bool comma = false;
    char item[160];
    for (uint32_t id = first; id <= newest && id != 0; ++id) {
      Event e;
      portENTER_CRITICAL(&lock_);
      e = events_[id % kEvents];
      portEXIT_CRITICAL(&lock_);
      if (e.id != id) continue;  // overwritten meanwhile
      snprintf(item, sizeof(item),
               "%s{\"id\":%lu,\"ms\":%lu,\"node\":%u,\"seq\":%u,\"rssi\":%d,\"snr\":%.2f,"
               "\"ferr\":%ld,\"lost\":%u,\"tx\":%d,\"off\":%d}",
               comma ? "," : "", static_cast<unsigned long>(e.id),
               static_cast<unsigned long>(e.ms), e.node, e.seq, e.rssi_dbm, e.snr_x4 / 4.0f,
               static_cast<long>(e.ferr_hz), e.lost, e.tx_dbm, e.offset_db);
      json += item;
      comma = true;
    }
    json += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });

  bool started = false;
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!started) {
      server.begin();
      started = true;
      Log.printf("Field web: http://%s:%u (range map with phone GPS)\n",
                 WiFi.localIP().toString().c_str(), kPort);
    }
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
