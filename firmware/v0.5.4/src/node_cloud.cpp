#include "node_cloud.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "cloud_config.h"
#include "net_log.h"
#include "status_led.h"
#include "wifi_nvs.h"

namespace {
WiFiClientSecure tls;
HTTPClient http;
}  // namespace

void NodeCloud::begin(uint8_t node_id) {
  node_id_ = node_id;
  if (!NODE_ACCESS_TOKEN[0] ||
      strstr(CLOUD_HTTP_ROOT_CA, "-----BEGIN CERTIFICATE-----") == nullptr) {
    Log.println("Node cloud disabled: set NODE_ACCESS_TOKEN in cloud_config_local.h");
    // Wi-Fi still runs (log over Wi-Fi, setup portal).
    xTaskCreatePinnedToCore([](void*) {
      wifinvs::begin();
      for (;;) {
        wifinvs::loop();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }, "nodewifi", 4096, nullptr, 1, nullptr, 0);
    return;
  }
  latest_ = xQueueCreate(1, sizeof(Report));
  if (!latest_) return;
  const Report empty{};
  xQueueOverwrite(latest_, &empty);
  xTaskCreatePinnedToCore(task_entry, "nodecloud", 8192, this, 1, nullptr, 0);
}

void NodeCloud::update(const Report& report) {
  if (latest_) xQueueOverwrite(latest_, &report);
}

void NodeCloud::task_entry(void* context) {
  static_cast<NodeCloud*>(context)->task_loop();
}

// One HTTPS POST to /api/v1/<token>/telemetry. The TLS connection is kept
// open between reports (keep-alive), so a report normally costs no handshake.
// Returns the HTTP status (200 ok), or a negative HTTPClient error.
int NodeCloud::publish(const Report& report) {
  StaticJsonDocument<256> doc;
  doc["nodeSeen"] = static_cast<uint32_t>(time(nullptr));
  doc["nodeTx"] = report.tx_count;
  if (report.test_mode) doc["nodeSeq"] = report.test_seq;
  doc["nodeTxPower"] = report.tx_dbm;
  doc["nodeWifi"] = WiFi.RSSI();
  doc["nodeIp"] = WiFi.localIP().toString();  // for scripts/netlog.py
  char json[256];
  const size_t length = serializeJson(doc, json, sizeof(json));
  char path[64];
  snprintf(path, sizeof(path), "/api/v1/%s/telemetry", NODE_ACCESS_TOKEN);
  // begin() reuses the open connection; end() keeps it open (setReuse).
  if (!http.begin(tls, CLOUD_HTTP_HOST, 443, path, true)) return -1;
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(reinterpret_cast<uint8_t*>(json), length);
  if (status > 0) http.getString();  // drain the body so the connection can be reused
  http.end();
  return status;
}

void NodeCloud::task_loop() {
  wifinvs::begin();
  tls.setCACert(CLOUD_HTTP_ROOT_CA);
  http.setReuse(true);
  http.setTimeout(5000);

  bool clock_started = false;
  uint32_t last_publish = 0;
  bool published = false;
  int last_status = 0;
  while (true) {
    const uint32_t now = millis();
    wifinvs::loop();  // reconnects, or serves the setup portal
    if (!wifinvs::isConnected()) {
      clock_started = false;
      published = false;
    } else {
      if (!clock_started) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        clock_started = true;
      }
      if (time(nullptr) > 1609459200 && (!published || now - last_publish >= kPeriodMs)) {
        Report report{};
        xQueuePeek(latest_, &report, 0);
        const int status = publish(report);
        last_publish = now;  // retry after a full period on failure too
        published = true;
        if (status == 200) status_led.client_seen();
        if (status != last_status) {  // log changes only, not every report
          if (status == 200) Log.println("Node cloud: ThingsBoard report OK (HTTPS)");
          else Log.printf("Node cloud: ThingsBoard report failed (%d)\n", status);
          last_status = status;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
