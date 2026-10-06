# Hybrid-Lora

ESP32 + LoRa hybrid smart streetlight control and monitoring system.

## Architecture

- LoRa star topology: Nodes -> Gateway -> Wi-Fi/Internet -> Server
- Same PCB can operate as Gateway or Node
- Hybrid operation: centralized remote control + local fallback
- LoRa communication uses compact packets with CRC and sequence numbers

## Current status

Algorithm and task planning phase. Sensor set and final payload format are intentionally not frozen yet.
