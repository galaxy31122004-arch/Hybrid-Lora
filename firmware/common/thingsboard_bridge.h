#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "gateway_state.h"

class ThingsBoardBridge {
 public:
  void begin();
  void post_node(uint8_t node_id, const hybrid_lora::GatewayState::Node& node);
  void post_result(const hybrid_lora::GatewayState::CommandResult& result);
  bool take_command(uint8_t& node_id, uint8_t& command, uint8_t& param,
                    uint32_t& rpc_id);

 private:
  struct CloudEvent {
    enum class Type : uint8_t { Node, Result } type;
    uint8_t node_id;
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
    hybrid_lora::GatewayState::Node node{};
  };

  static void task_entry(void* context);
  void task_loop();
  void on_message(char* topic, uint8_t* payload, unsigned int length);
  void handle_event(const CloudEvent& event);
  void publish_one_node();
  void reply(uint8_t node_id, uint32_t rpc_id, bool success, const char* error);
  static void device_name(uint8_t node_id, char* out, size_t size);

  QueueHandle_t events_{nullptr};
  QueueHandle_t commands_{nullptr};
  WiFiClientSecure tls_;
  PubSubClient mqtt_{tls_};
  CachedNode nodes_[hybrid_lora::GatewayState::kMaxNodes + 1]{};
  uint8_t next_node_{1};
};
