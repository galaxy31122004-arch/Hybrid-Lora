#include "net_log.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/queue.h>

NetLog Log;

size_t NetLog::write(const uint8_t* data, size_t size) {
  Serial.write(data, size);
  portENTER_CRITICAL(&lock_);
  for (size_t i = 0; i < size; ++i) ring_[(head_ + i) % kHistory] = data[i];
  head_ += size;
  portEXIT_CRITICAL(&lock_);
  return size;
}

// Copies bytes written after `cursor` (at most `size`) and advances it. A
// cursor that fell more than kHistory behind skips to the oldest kept byte.
size_t NetLog::copy_since(uint32_t& cursor, uint8_t* out, size_t size) {
  portENTER_CRITICAL(&lock_);
  if (head_ - cursor > kHistory) cursor = head_ - kHistory;
  size_t n = head_ - cursor;
  if (n > size) n = size;
  for (size_t i = 0; i < n; ++i) out[i] = ring_[(cursor + i) % kHistory];
  cursor += n;
  portEXIT_CRITICAL(&lock_);
  return n;
}

void NetLog::begin(const char* hostname) {
  snprintf(hostname_, sizeof(hostname_), "%s", hostname);
  lines_ = xQueueCreate(4, kLineMax);
  if (!lines_) return;
  xTaskCreatePinnedToCore(task_entry, "netlog", 4096, this, 1, nullptr, 0);
}

bool NetLog::take_line(char* out, size_t size) {
  char line[kLineMax];
  if (!lines_ || xQueueReceive(static_cast<QueueHandle_t>(lines_), line, 0) != pdTRUE) {
    return false;
  }
  snprintf(out, size, "%s", line);
  return true;
}

void NetLog::task_entry(void* context) { static_cast<NetLog*>(context)->task_loop(); }

void NetLog::task_loop() {
  WiFiServer server(kPort);
  WiFiClient client;
  bool started = false;
  uint32_t cursor = 0;
  char line[kLineMax];
  size_t length = 0;
  uint8_t chunk[256];
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      if (client) client.stop();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!started) {
      server.begin();
      server.setNoDelay(true);
      MDNS.begin(hostname_);
      MDNS.addService("telnet", "tcp", kPort);
      started = true;
      Log.printf("Net log: telnet %s.local %u (IP %s)\n", hostname_, kPort,
                 WiFi.localIP().toString().c_str());
    }
    WiFiClient incoming = server.available();
    if (incoming) {
      if (client) client.stop();  // newest client wins
      client = incoming;
      client.setNoDelay(true);
      portENTER_CRITICAL(&lock_);
      cursor = head_ > kHistory ? head_ - kHistory : 0;  // send the history first
      portEXIT_CRITICAL(&lock_);
      length = 0;
    }
    if (client && client.connected()) {
      size_t n;
      while ((n = copy_since(cursor, chunk, sizeof(chunk))) > 0) {
        if (client.write(chunk, n) != n) {
          client.stop();
          break;
        }
      }
      while (client.available()) {
        const char c = static_cast<char>(client.read());
        if (c == '\n' || c == '\r') {
          line[length] = 0;
          if (length > 0 && line[0] != '#') {
            xQueueSend(static_cast<QueueHandle_t>(lines_), line, 0);
          } else if (length > 0) {
            client.print("'#' commands are Serial only\n");
          }
          length = 0;
        } else if (length < kLineMax - 1) {
          line[length++] = c;
        }
      }
    } else if (client) {
      client.stop();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
