#include "thingsboard_bridge.h"

#include <Preferences.h>
#include <time.h>

#include "cloud_config.h"
#include "net_log.h"
#include "wifi_nvs.h"

namespace {
constexpr char kConnectTopic[] = "v1/gateway/connect";
constexpr char kTelemetryTopic[] = "v1/gateway/telemetry";
constexpr char kRpcTopic[] = "v1/gateway/rpc";
constexpr char kGatewayTelemetryTopic[] = "v1/devices/me/telemetry";  // the gateway itself
constexpr uint32_t kReconnectMs = 10000;
// Nodes report every 900 s +/- 90 s; offline after three missed reports.
constexpr uint32_t kNodeOnlineMs = 3 * 990000;
// Test mode: offline after 4 packet gaps, at least 15 s (continuous field
// test, one packet every ~1.7 s) and at most 120 s (test every 30 s).
constexpr uint32_t kTestOnlineMinMs = 15000;
constexpr uint32_t kTestOnlineMaxMs = 120000;
}  // namespace

uint32_t ThingsBoardBridge::online_timeout_ms(const CachedNode& node) {
  if (!node.node.status.test_mode) return kNodeOnlineMs;
  if (node.interval_ms == 0) return kTestOnlineMaxMs;  // gap not known yet
  return constrain(4 * node.interval_ms, kTestOnlineMinMs, kTestOnlineMaxMs);
}

void ThingsBoardBridge::load_known_nodes() {
  // First boot (no list yet): assume Node-01, the default NODE_ID, exists in
  // ThingsBoard, so a stale "online" left by older firmware is cleared too.
  // Other ids are never published before they are heard (that would create
  // devices in ThingsBoard).
  constexpr uint64_t kDefaultKnown = 1ULL << 1;
  Preferences prefs;
  if (prefs.begin("hl-nodes", true)) {
    known_nodes_ = prefs.getULong64("known", kDefaultKnown);
    prefs.end();
  } else {
    known_nodes_ = kDefaultKnown;  // namespace absent on first boot
  }
  for (uint8_t id = 1; id <= hybrid_lora::GatewayState::kMaxNodes; ++id) {
    if (known_nodes_ & (1ULL << id)) nodes_[id].offline_pending = true;
  }
}

// Written only when a node is heard for the first time ever: rare flash writes.
void ThingsBoardBridge::save_known_nodes() {
  Preferences prefs;
  if (!prefs.begin("hl-nodes", false)) return;
  prefs.putULong64("known", known_nodes_);
  prefs.end();
  known_dirty_ = false;
}

void ThingsBoardBridge::begin() {
  // The example file leaves an empty PEM block (a newline), so look for the
  // certificate header rather than an empty string.
  if (!CLOUD_ACCESS_TOKEN[0] ||
      strstr(CLOUD_ROOT_CA, "-----BEGIN CERTIFICATE-----") == nullptr) {
    Log.println("ThingsBoard disabled: configure token and CA cert");
    return;
  }
  events_ = xQueueCreate(64, sizeof(CloudEvent));
  commands_ = xQueueCreate(16, sizeof(CloudCommand));
  if (!events_ || !commands_) {
    Log.println("ThingsBoard disabled: queue allocation failed");
    return;
  }
  xTaskCreatePinnedToCore(task_entry, "thingsboard", 10240, this, 1, nullptr, 0);
}

void ThingsBoardBridge::post_node(
    uint8_t node_id, const hybrid_lora::GatewayState::Node& node) {
  if (!events_) return;
  CloudEvent event{};
  event.type = CloudEvent::Type::Node;
  event.node_id = node_id;
  event.node = node;
  xQueueSend(events_, &event, 0);
}

void ThingsBoardBridge::post_result(
    const hybrid_lora::GatewayState::CommandResult& result) {
  if (!events_) return;
  CloudEvent event{};
  event.type = CloudEvent::Type::Result;
  event.result = result;
  xQueueSend(events_, &event, 0);
}

void ThingsBoardBridge::post_noise(int16_t dbm) {
  if (!events_) return;
  CloudEvent event{};
  event.type = CloudEvent::Type::Noise;
  event.noise_dbm = dbm;
  xQueueSend(events_, &event, 0);
}

bool ThingsBoardBridge::take_command(uint8_t& node_id, uint8_t& command,
                                     uint8_t& param, uint32_t& rpc_id) {
  if (!commands_) return false;
  CloudCommand queued{};
  if (xQueueReceive(commands_, &queued, 0) != pdTRUE) return false;
  node_id = queued.node_id;
  command = queued.command;
  param = queued.param;
  rpc_id = queued.rpc_id;
  return true;
}

void ThingsBoardBridge::task_entry(void* context) {
  static_cast<ThingsBoardBridge*>(context)->task_loop();
}

void ThingsBoardBridge::task_loop() {
  // Wi-Fi runs in this task (core 0): scanning, connecting and the setup
  // portal can take seconds and must never stall the LoRa loop.
  load_known_nodes();
  wifinvs::begin();
  tls_.setCACert(CLOUD_ROOT_CA);
  tls_.setTimeout(1000);
  mqtt_.setServer(CLOUD_MQTT_HOST, 8883);
  mqtt_.setBufferSize(768);
  mqtt_.setSocketTimeout(1);
  mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
    on_message(topic, payload, length);
  });

  uint32_t last_mqtt_attempt = 0;
  bool clock_started = false;
  while (true) {
    CloudEvent event{};
    while (xQueueReceive(events_, &event, 0) == pdTRUE) handle_event(event);
    if (known_dirty_) save_known_nodes();

    const uint32_t now = millis();
    wifinvs::loop();  // reconnects, or serves the setup portal
    if (!wifinvs::isConnected()) {
      clock_started = false;
    } else {
      if (!clock_started) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        clock_started = true;
      }
      if (time(nullptr) > 1609459200 && !mqtt_.connected() &&
          (last_mqtt_attempt == 0 || now - last_mqtt_attempt >= kReconnectMs)) {
        last_mqtt_attempt = now;
        char client_id[32];
        snprintf(client_id, sizeof(client_id), "HybridLoRa-%04X%08X",
                 static_cast<unsigned>(ESP.getEfuseMac() >> 32),
                 static_cast<unsigned>(ESP.getEfuseMac()));
        if (mqtt_.connect(client_id, CLOUD_ACCESS_TOKEN, nullptr)) {
          mqtt_.subscribe(kRpcTopic);
          for (auto& node : nodes_) {
            node.connected = false;
            if (node.seen) node.dirty = true;
          }
          Log.println("ThingsBoard connected");
        }
      }
      if (mqtt_.connected()) {
        mqtt_.loop();
        for (uint8_t id = 1; id <= hybrid_lora::GatewayState::kMaxNodes; ++id) {
          auto& result = pending_results_[id];
          if (result.has_value() &&
              reply(id, result->rpc_id, result->success,
                    result->result_code == 0xFF ? "timeout" :
                    result->result_code == 0xFE ? "busy" : "node_rejected")) {
            result.reset();
          }
          CachedNode& node = nodes_[id];
          if (node.seen && !node.dirty &&
              (now - node.node.last_seen_ms < online_timeout_ms(node)) !=
                  node.published_online) {
            node.dirty = true;
          }
        }
        if (pending_noise_.has_value()) {
          char json[48];
          snprintf(json, sizeof(json), "{\"noiseFloor\":%d}", *pending_noise_);
          if (mqtt_.publish(kGatewayTelemetryTopic, json)) pending_noise_.reset();
        }
        publish_one_node();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void ThingsBoardBridge::handle_event(const CloudEvent& event) {
  if (event.type == CloudEvent::Type::Noise) {
    pending_noise_ = event.noise_dbm;  // keep only the latest reading
    return;
  }
  if (event.type == CloudEvent::Type::Result) {
    if (event.result.node_id > 0 &&
        event.result.node_id <= hybrid_lora::GatewayState::kMaxNodes) {
      pending_results_[event.result.node_id] = event.result;
    }
    return;
  }
  if (event.node_id == 0 ||
      event.node_id > hybrid_lora::GatewayState::kMaxNodes) return;
  CachedNode& cached = nodes_[event.node_id];
  if (cached.seen && event.node.last_seen_ms != cached.node.last_seen_ms) {
    cached.interval_ms = event.node.last_seen_ms - cached.node.last_seen_ms;
  }
  cached.seen = true;
  cached.offline_pending = false;  // heard now: real values follow
  cached.dirty = true;
  cached.node = event.node;
  const uint64_t bit = 1ULL << event.node_id;
  if (!(known_nodes_ & bit)) {
    known_nodes_ |= bit;
    known_dirty_ = true;
  }
}

void ThingsBoardBridge::device_name(uint8_t node_id, char* out, size_t size) {
  snprintf(out, size, "Node-%02u", node_id);
}

void ThingsBoardBridge::publish_one_node() {
  for (uint8_t checked = 0; checked < hybrid_lora::GatewayState::kMaxNodes;
       ++checked) {
    const uint8_t id = next_node_;
    next_node_ = id == hybrid_lora::GatewayState::kMaxNodes ? 1 : id + 1;
    CachedNode& cached = nodes_[id];
    char name[16];
    device_name(id, name, sizeof(name));
    if (!cached.seen && cached.offline_pending) {
      // Known from an earlier boot, not heard yet: clear a stale "online".
      char json[48];
      snprintf(json, sizeof(json), "{\"%s\":[{\"online\":false}]}", name);
      if (mqtt_.publish(kTelemetryTopic, json)) cached.offline_pending = false;
      return;
    }
    if (!cached.seen || !cached.dirty) continue;

    if (!cached.connected) {
      StaticJsonDocument<96> connect;
      connect["device"] = name;
      char json[96];
      serializeJson(connect, json, sizeof(json));
      if (!mqtt_.publish(kConnectTopic, json)) return;
      cached.connected = true;
    }

    StaticJsonDocument<448> telemetry;
    JsonArray values = telemetry.createNestedArray(name);
    JsonObject snapshot = values.createNestedObject();
    const bool online = millis() - cached.node.last_seen_ms < online_timeout_ms(cached);
    snapshot["online"] = online;
    snapshot["healthFlags"] = cached.node.health_flags;
    if (cached.node.has_signal) {
      snapshot["rssi"] = cached.node.rssi_dbm;  // uplink, measured at the gateway
      snapshot["snr"] = cached.node.snr_db;
    }
    if (cached.node.status.test_mode && cached.node.test.active) {
      // Test STATUS: sequence-based packet loss instead of lux.
      snapshot["seq"] = cached.node.test.last_seq;
      snapshot["pdr"] = cached.node.pdr_percent();
      snapshot["lost"] = cached.node.test.lost;
      snapshot["txPower"] = cached.node.test.tx_dbm;
    }
    if (cached.node.has_status) {
      if (!cached.node.status.test_mode) snapshot["lux"] = cached.node.status.lux;
      snapshot["lightState"] = cached.node.status.light_state;
      snapshot["mode"] = cached.node.status.mode;
      snapshot["testMode"] = cached.node.status.test_mode;
    }
    char json[384];
    serializeJson(telemetry, json, sizeof(json));
    if (mqtt_.publish(kTelemetryTopic, json)) {
      cached.dirty = false;
      cached.published_online = online;
    }
    return;
  }
}

void ThingsBoardBridge::on_message(char* topic, uint8_t* payload,
                                   unsigned int length) {
  if (strcmp(topic, kRpcTopic) != 0) return;
  StaticJsonDocument<384> request;
  if (deserializeJson(request, payload, length)) return;
  const char* device = request["device"] | "";
  const char* method = request["data"]["method"] | "";
  // ThingsBoard numbers RPCs from 0, so 0 is a valid id.
  const uint32_t rpc_id = request["data"]["id"] | 0;
  unsigned id = 0;
  char expected[16];
  if (sscanf(device, "Node-%u", &id) != 1 || id == 0 ||
      id > hybrid_lora::GatewayState::kMaxNodes) return;
  device_name(id, expected, sizeof(expected));
  if (strcmp(device, expected) != 0 || !nodes_[id].seen) return;

  namespace pk = hybrid_lora::packet;
  JsonVariant params = request["data"]["params"];
  const bool is_object = params.is<JsonObject>();
  CloudCommand command{static_cast<uint8_t>(id), 0, 0, rpc_id};
  if (strcmp(method, "setLight") == 0) {
    // params: true/false or {"on": true}
    JsonVariant on = is_object ? JsonVariant(params["on"]) : params;
    if (!on.is<bool>()) {
      reply(id, rpc_id, false, "invalid_params");
      return;
    }
    command.command = on.as<bool>() ? pk::kCmdLightOn : pk::kCmdLightOff;
  } else if (strcmp(method, "ping") == 0) {
    // Node ACKs, then sends a STATUS: refreshes rssi/snr on the dashboard.
    command.command = pk::kCmdRequestStatus;
  } else if (strcmp(method, "setTestMode") == 0) {
    // params: minutes (0 = off, max 120), true (= 30 min) / false, or {"minutes": n}
    JsonVariant value = is_object ? JsonVariant(params["minutes"]) : params;
    int minutes = -1;
    if (value.is<bool>()) minutes = value.as<bool>() ? 30 : 0;
    else if (value.is<int>()) minutes = value.as<int>();
    if (minutes < 0 || minutes > pk::kMaxTestMinutes) {
      reply(id, rpc_id, false, "invalid_params");
      return;
    }
    command.command = pk::kCmdTestMode;
    command.param = static_cast<uint8_t>(minutes);
  } else {
    reply(id, rpc_id, false, "invalid_request");
    return;
  }
  if (xQueueSend(commands_, &command, 0) != pdTRUE) {
    reply(id, rpc_id, false, "busy");
  }
}

bool ThingsBoardBridge::reply(uint8_t node_id, uint32_t rpc_id, bool success,
                               const char* error) {
  if (!mqtt_.connected()) return false;
  StaticJsonDocument<192> response;
  char name[16];
  device_name(node_id, name, sizeof(name));
  response["device"] = name;
  response["id"] = rpc_id;
  JsonObject data = response.createNestedObject("data");
  data["success"] = success;
  if (!success) data["error"] = error;
  char json[192];
  serializeJson(response, json, sizeof(json));
  return mqtt_.publish(kRpcTopic, json);
}
