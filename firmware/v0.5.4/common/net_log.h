#pragma once

#include <Arduino.h>

// Log over Wi-Fi: everything printed through `Log` goes to Serial and to a
// TCP client on port 23, so a board on its own power supply (no USB cable)
// can still be read from a PC on the same network:
//   python scripts/netlog.py hybrid-node-01.local   (or the board's IP)
// The last kHistory bytes are sent first when a client connects, so the boot
// messages are not lost. One client at a time; a new one replaces the old.
// Lines the client types are handed to the firmware's command parser
// (TEST, QUIET, ISO...). Lines starting with '#' (Wi-Fi provisioning) are
// refused over the network: they are Serial only.
// Not authenticated: anyone on the same LAN can read the log. Never print
// secrets through Log (the '#wifi' listing stays on Serial).
class NetLog : public Print {
 public:
  static constexpr uint16_t kPort = 23;
  static constexpr size_t kHistory = 8192;
  static constexpr size_t kLineMax = 32;

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t size) override;

  // Starts the server task (core 0). It waits for Wi-Fi, then announces
  // <hostname>.local over mDNS and accepts clients. Call once from setup().
  void begin(const char* hostname);

  // Next command line typed by the network client. Call from loop().
  bool take_line(char* out, size_t size);

 private:
  static void task_entry(void* context);
  void task_loop();
  size_t copy_since(uint32_t& cursor, uint8_t* out, size_t size);

  uint8_t ring_[kHistory]{};
  uint32_t head_ = 0;  // total bytes ever written; ring index = head_ % kHistory
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  char hostname_[32]{};
  void* lines_ = nullptr;  // QueueHandle_t of char[kLineMax]
};

extern NetLog Log;
