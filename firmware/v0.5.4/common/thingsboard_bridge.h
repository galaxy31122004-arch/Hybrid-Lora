#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <optional>

#include "gateway_state.h"

class ThingsBoardBridge {
 public:
  void begin();
  void post_node(uint8_t node_id, const hybrid_lora::GatewayState::Node& node);
  void post_result(const hybrid_lora::GatewayState::CommandResult& result);
  // Uplink-channel noise floor, published as the gateway's own telemetry.
  void post_noise(int16_t dbm);
  bool take_command(uint8_t& node_id, uint8_t& command, uint8_t& param,
                    uint32_t& rpc_id);

 private:
  struct CloudEvent {
    enum class Type : uint8_t { Node, Result, Noise } type;
    uint8_t node_id;
    int16_t noise_dbm;
    hybrid_lora::GatewayState::Node node;
    hybrid_lora::GatewayState::CommandResult result;
  };
  struct CloudCommand {
    uint8_t node_id;
    uint8_t command;
    uint8_t param;
    uint32_t rpc_id;
  };
  struct CachedNode {
    bool seen{false};
    bool connected{false};
    bool dirty{false};
    bool published_online{false};
    // Known from an earlier boot (NVS) but not heard since this one: its
    // ThingsBoard "online" may still say true, so publish false once.
    bool offline_pending{false};
    uint32_t interval_ms{0};  // gap between the last two packets received
    hybrid_lora::GatewayState::Node node{};
  };
  static uint32_t online_timeout_ms(const CachedNode& node);
  void load_known_nodes();
  void save_known_nodes();

  static void task_entry(void* context);
  void task_loop();
  void on_message(char* topic, uint8_t* payload, unsigned int length);
  void handle_event(const CloudEvent& event);
  void publish_one_node();
  bool reply(uint8_t node_id, uint32_t rpc_id, bool success, const char* error);
  static void device_name(uint8_t node_id, char* out, size_t size);

  QueueHandle_t events_{nullptr};
  QueueHandle_t commands_{nullptr};
  WiFiClientSecure tls_;
  PubSubClient mqtt_{tls_};
  CachedNode nodes_[hybrid_lora::GatewayState::kMaxNodes + 1]{};
  std::optional<hybrid_lora::GatewayState::CommandResult>
      pending_results_[hybrid_lora::GatewayState::kMaxNodes + 1]{};
  uint8_t next_node_{1};
  std::optional<int16_t> pending_noise_;
  uint64_t known_nodes_{0};  // bit n: Node-n seen on some boot (NVS)
  bool known_dirty_{false};
};
