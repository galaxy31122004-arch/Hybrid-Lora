#pragma once

#include <Arduino.h>

// Gateway field-test web page: http://<gateway IP>:8080 on a phone joined to
// the same hotspot. The page reads the phone's GPS (browser geolocation),
// polls the packets the gateway received, and puts every packet (and every
// lost one) on a map at the phone's position: carry the phone with the node.
// It downloads KML/CSV for Google My Maps / Google Earth.
// Geolocation needs a secure page: on Android Chrome set
// chrome://flags/#unsafely-treat-insecure-origin-as-secure to
// http://<gateway IP>:8080 once (see README "Range map").
// Port 8080, not 80: the Wi-Fi setup portal uses 80.
class FieldWeb {
 public:
  static constexpr uint16_t kPort = 8080;
  static constexpr size_t kEvents = 512;  // ~14 min of back-to-back packets

  // Starts the server task (core 0); it waits for Wi-Fi.
  void begin();
  // One received test STATUS. lost = packets missing just before it.
  void add(uint8_t node, uint16_t seq, int16_t rssi_dbm, float snr_db, int32_t ferr_hz,
           uint8_t lost, int8_t tx_dbm, int8_t offset_db);

  struct Event {
    uint32_t id;
    uint32_t ms;  // gateway millis() when received
    int32_t ferr_hz;
    uint16_t seq;
    int16_t rssi_dbm;
    int16_t snr_x4;
    uint8_t node;
    uint8_t lost;
    int8_t tx_dbm;
    int8_t offset_db;
  };

 private:
  static void task_entry(void* context);
  void task_loop();

  Event events_[kEvents]{};
  uint32_t next_id_ = 1;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

extern FieldWeb field_web;
