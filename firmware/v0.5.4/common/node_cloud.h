#pragma once

#include <Arduino.h>

// Node side of ThingsBoard: the node joins Wi-Fi itself (wifi_nvs module)
// and reports that it is really powered, independently of the LoRa link.
// Every kPeriodMs it publishes, for device Node-XX:
//   nodeSeen  epoch seconds (the dashboard shows the node off when it is old)
//   nodeTx    LoRa frames sent since boot
//   nodeSeq   last test STATUS sequence number sent (compare with the
//             gateway's seq/lost to see what was sent but not received)
//   nodeTxPower, nodeWifi (Wi-Fi RSSI dBm)
// Wi-Fi and HTTPS run in their own task on core 0, so LoRa never waits on
// them; without Wi-Fi the node works exactly as before.
// Sent as one HTTPS POST (keep-alive) to CLOUD_HTTP_HOST with
// NODE_ACCESS_TOKEN (cloud_config_local.h); no token: no report. Not MQTT:
// ThingsBoard keeps one session per device, and the gateway already holds
// one for Node-XX.
class NodeCloud {
 public:
  struct Report {
    uint32_t tx_count;
    uint16_t test_seq;
    int8_t tx_dbm;
    bool test_mode;
  };

  void begin(uint8_t node_id);
  // Latest LoRa counters; cheap, safe to call from the LoRa loop.
  void update(const Report& report);

 private:
  static constexpr uint32_t kPeriodMs = 10000;
  static void task_entry(void* context);
  void task_loop();
  int publish(const Report& report);

  uint8_t node_id_{0};
  QueueHandle_t latest_{nullptr};
};
