#pragma once

#include <Arduino.h>

namespace hybrid_lora::pins {

// Net labels taken from the supplied schematic. Keep all board-specific values here.
constexpr int LORA1_CS = 19;
constexpr int LORA2_CS = 18;
constexpr int LORA_SCK = 4;
constexpr int LORA_MISO = 16;
constexpr int LORA_MOSI = 17;
constexpr int LORA_RESET = 21;
constexpr int LORA1_DIO0 = 14;
constexpr int LORA2_DIO0 = 12;  // GPIO12 is a boot strap pin; verify reset behavior.

constexpr int RELAY1 = 33;
constexpr int RELAY2 = 25;
constexpr int RELAY3 = 26;
constexpr int MODE = 32;
constexpr int I2C_SDA = 22;
constexpr int I2C_SCL = 23;

}  // namespace hybrid_lora::pins
