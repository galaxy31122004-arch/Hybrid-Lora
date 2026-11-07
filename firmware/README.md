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

## ThingsBoard Cloud setup

1. In ThingsBoard Cloud, create a device for the gateway, enable **Is gateway**,
   and copy its access token. Use the Cloud region associated with your account.
2. Copy `firmware/common/cloud_config_local.example.h` to
   `firmware/common/cloud_config_local.h`. Enter the Wi-Fi SSID and password,
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
   firmware. `online` becomes false 120 seconds after the last valid report.
5. Configure a server-side RPC control widget for each child device. Method:
   `setLight`; params: `true` for on or `false` for off. An object such as
   `{"on":true}` is also accepted. The RPC response contains `success`; a
   failure contains `error` (`busy`, `timeout`, or `node_rejected`). The gateway
   waits for a matching LoRa ACK before reporting success.

The gateway keeps LoRa processing in the main loop while Wi-Fi/MQTT reconnects
in a separate task. Without a local configuration file, Cloud is disabled and
the LoRa firmware continues to run. A functional Cloud test requires a gateway
board, a reporting node, and valid ThingsBoard credentials.
