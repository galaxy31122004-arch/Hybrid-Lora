# Firmware layout

The board uses one firmware image. `firmware/src/main.cpp` reads the `MODE`
jumper during boot:

- `MODE = LOW`: Gateway, initializes both SX1278 radios.
- `MODE = HIGH`: Node, initializes radio 1 and the relay control loop.

Pin assignments are centralized in `firmware/common/pins.h` and follow the
schematic net labels. GPIO12 is currently assigned to the second LoRa DIO0
net; verify that the module does not hold this ESP32 strapping pin high during
reset before production use.

The gateway maintains up to 50 node states and relays telemetry and commands
through ThingsBoard Cloud. Sensor drivers and RTC integration remain future
work. The current Node ID is fixed to 1 in `firmware/src/main.cpp`; assign
distinct IDs before deploying more nodes.

## Firmware versions

The version is set in one place: `custom_fw_version` in `platformio.ini`.
`scripts/fw_version.py` passes it to the code as `FW_VERSION` (printed on
Serial at boot) and, after each build, copies the image to
`releases/v<version>/hybrid-lora-<env>-v<version>.bin` (plus `.elf`). The
`releases/` folder is not tracked by Git.

To release a new version: raise `custom_fw_version` (patch for fixes, minor for
features), build, merge to `main`, tag the merge `v<version>`, and attach the
`.bin` to a GitHub Release with the same tag.

## LoRa radio settings

All settings are written explicitly at start-up (`common/sx1278_radio.h`,
namespace `lora_phy`, and the channel constants in `src/main.cpp`):

| Setting | Value |
|---|---|
| Uplink channel (Node → Gateway radio 1) | 433.175 MHz |
| Downlink channel (Gateway radio 2 → Node) | 433.575 MHz |
| Spreading factor / bandwidth / coding rate | **SF12** / 125 kHz / 4/5, Low Data Rate Optimize on |
| Preamble / sync word / header | 8 symbols / 0x12 / explicit |
| Hardware CRC | on (packets without CRC are dropped) |
| Output | PA_BOOST (RA-02), **20 dBm** (PA DAC high power, OCP 140 mA) |
| Receive mode | RX continuous |

v0.4.0 is tuned for maximum range (sensitivity about −137 dBm, 14 dB more
than SF7). 20 dBm is above the ~10 mW usually allowed in this band; confirm
the local rule before deploying. The SX1278 allows at most 1% duty cycle at
+20 dBm, which sets the schedules below.

## Packets (format v2) and schedule

Frame: `TYPE | NODE_ID | SEQ | PAYLOAD`. No LEN byte and no CRC-16 (the LoRa
header has the length; the radio's hardware CRC checks integrity).
HEARTBEAT is removed: STATUS carries the health bits, and an ACK also counts
as a sign of life.

| Packet | Bytes (v1 → v2) | Time on air at SF12 | Sent |
|---|---|---|---|
| STATUS | 14 → 6 (lux, flags: light, mode, health) | ≈ 0.99 s | every 600 s ± 60 s per node, plus on request |
| COMMAND / ACK | 8 → 5 | ≈ 0.83 s | on demand; retry after 4 s, 4 attempts |
| BEACON | 6 → 3, one broadcast (`NODE_ID = 0xFF`) instead of one per node | ≈ 0.83 s | every 120 s |

Channel use with 50 nodes: uplink ≈ 8%, gateway transmitter ≈ 0.7% plus
commands. A node falls back to local mode after 360 s without a beacon;
ThingsBoard marks a node offline after 33 min without a report.

v0.4.0 devices cannot talk to v0.3.0 or older (different PHY and packet
format): flash the gateway and every node together. Range has not been
measured yet.

## ThingsBoard Cloud setup

1. In ThingsBoard Cloud, create a device for the gateway, enable **Is gateway**,
   and copy its access token. Use the Cloud region associated with your account.
2. Copy `firmware/v0.2.0/common/cloud_config_local.example.h` to
   `firmware/v0.2.0/common/cloud_config_local.h`. Enter the Wi-Fi SSID and password,
   gateway access token, MQTT host, and the PEM root CA certificate from the
   device's **Check connectivity** dialog. This local file is ignored by Git.
   The North America MQTT host is `mqtt.thingsboard.cloud`; for another region,
   use the host shown in your Cloud account. TLS uses port 8883.
3. Set `MODE = LOW` on the gateway board and upload from the repository root:

   ```text
   pio run -e esp32dev -t upload --upload-port COM3
   ```

4. After a node sends a valid `STATUS` or `HEARTBEAT`, the gateway announces it
   as `Node-01` through `Node-50`. In ThingsBoard, select that child device and
   add its `online`, `healthFlags`, `lux`, `lightState`, and `mode` telemetry to a
   dashboard. `lux = 65535` means the sensor value is unavailable in this
   firmware. `online` becomes false 33 minutes after the last valid report.
5. Configure a server-side RPC control widget for each child device. Method:
   `setLight`; params: `true` for on or `false` for off. An object such as
   `{"on":true}` is also accepted. The RPC response contains `success`; a
   failure contains `error` (`busy`, `timeout`, or `node_rejected`). The gateway
   waits for a matching LoRa ACK before reporting success.

The gateway keeps LoRa processing in the main loop while Wi-Fi/MQTT reconnects
in a separate task. Without a local configuration file, Cloud is disabled and
the LoRa firmware continues to run. A functional Cloud test requires a gateway
board, a reporting node, and valid ThingsBoard credentials.
