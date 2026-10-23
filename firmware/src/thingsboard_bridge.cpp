#include "thingsboard_bridge.h"

#include <time.h>

#include "cloud_config.h"

namespace {
constexpr char kConnectTopic[] = "v1/gateway/connect";
constexpr char kTelemetryTopic[] = "v1/gateway/telemetry";
constexpr char kRpcTopic[] = "v1/gateway/rpc";
constexpr uint32_t kReconnectMs = 10000;
constexpr uint32_t kNodeOnlineMs = 120000;
}  // namespace

void ThingsBoardBridge::begin() {
  if (!CLOUD_WIFI_SSID[0] || !CLOUD_ACCESS_TOKEN[0] || !CLOUD_ROOT_CA[0]) {
    Serial.println("ThingsBoard disabled: configure Wi-Fi, token and CA cert");
    return;
  }
  events_ = xQueueCreate(64, sizeof(CloudEvent));
  commands_ = xQueueCreate(16, sizeof(CloudCommand));
  if (!events_ || !commands_) {
    Serial.println("ThingsBoard disabled: queue allocation failed");
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
  WiFi.mode(WIFI_STA);
  tls_.setCACert(CLOUD_ROOT_CA);
  tls_.setTimeout(1000);
  mqtt_.setServer(CLOUD_MQTT_HOST, 8883);
  mqtt_.setBufferSize(768);
  mqtt_.setSocketTimeout(1);
  mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
    on_message(topic, payload, length);
  });

  uint32_t last_wifi_attempt = 0;
  uint32_t last_mqtt_attempt = 0;
  bool clock_started = false;
  while (true) {
    CloudEvent event{};
    while (xQueueReceive(events_, &event, 0) == pdTRUE) handle_event(event);

    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (last_wifi_attempt == 0 || now - last_wifi_attempt >= kReconnectMs) {
        last_wifi_attempt = now;
        WiFi.begin(CLOUD_WIFI_SSID, CLOUD_WIFI_PASSWORD);
      }
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
          Serial.println("ThingsBoard connected");
        }
      }
      if (mqtt_.connected()) {
        mqtt_.loop();
        publish_one_node();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void ThingsBoardBridge::handle_event(const CloudEvent& event) {
  if (event.type == CloudEvent::Type::Result) {
    reply(event.result.node_id, event.result.rpc_id, event.result.success,
          event.result.result_code == 0xFF ? "timeout" : "node_rejected");
    return;
  }
  if (event.node_id == 0 ||
      event.node_id > hybrid_lora::GatewayState::kMaxNodes) return;
  CachedNode& cached = nodes_[event.node_id];
  cached.seen = true;
  cached.dirty = true;
  cached.node = event.node;
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
    if (!cached.seen || !cached.dirty) continue;

    char name[16];
    device_name(id, name, sizeof(name));
    if (!cached.connected) {
      StaticJsonDocument<96> connect;
      connect["device"] = name;
      char json[96];
      serializeJson(connect, json, sizeof(json));
      if (!mqtt_.publish(kConnectTopic, json)) return;
      cached.connected = true;
    }

    StaticJsonDocument<320> telemetry;
    JsonArray values = telemetry.createNestedArray(name);
    JsonObject snapshot = values.createNestedObject();
    snapshot["online"] = millis() - cached.node.last_seen_ms < kNodeOnlineMs;
    snapshot["healthFlags"] = cached.node.health_flags;
    if (cached.node.has_status) {
      snapshot["lux"] = cached.node.status.lux;
      snapshot["lightState"] = cached.node.status.light_state;
      snapshot["mode"] = cached.node.status.mode;
    }
    char json[320];
    serializeJson(telemetry, json, sizeof(json));
    if (mqtt_.publish(kTelemetryTopic, json)) cached.dirty = false;
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
  const uint32_t rpc_id = request["data"]["id"] | 0;
  unsigned id = 0;
  char expected[16];
  if (sscanf(device, "Node-%u", &id) != 1 || id == 0 ||
      id > hybrid_lora::GatewayState::kMaxNodes) return;
  device_name(id, expected, sizeof(expected));
  if (strcmp(device, expected) != 0 || !nodes_[id].seen) return;
  if (strcmp(method, "setLight") != 0 || rpc_id == 0) {
    reply(id, rpc_id, false, "invalid_request");
    return;
  }

  JsonVariant params = request["data"]["params"];
  JsonVariant on = params.is<JsonObject>() ? params["on"] : params;
  if (!on.is<bool>()) {
    reply(id, rpc_id, false, "invalid_params");
    return;
  }
  CloudCommand command{static_cast<uint8_t>(id),
                       static_cast<uint8_t>(on.as<bool>() ? 1 : 2), 0, rpc_id};
  if (xQueueSend(commands_, &command, 0) != pdTRUE) {
    reply(id, rpc_id, false, "busy");
  }
}

void ThingsBoardBridge::reply(uint8_t node_id, uint32_t rpc_id, bool success,
                               const char* error) {
  if (!mqtt_.connected()) return;
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
  mqtt_.publish(kRpcTopic, json);
}
