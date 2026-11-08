# Firmware

Each firmware version has its own folder with a full copy of its source:

| Folder | Version | Contents | Build env |
|---|---|---|---|
| `v0.5.1/` | **0.5.1 (current)** | Fixes from the v0.5.0 review: STATUS after command-made changes (lost ACK), RSSI formula for SNR >= 0, noise floor skips own TX and rounds, test-mode offset known before first STATUS, RPC id 0 accepted, empty CA detected, neutral dashboard key colour; one command in flight at a time (2.2 s ACK window, retry 2.5 s); `ISO` antenna isolation self-test on Serial; quieter table hover. Same packet format as v0.5.0 | `esp32dev` |
| `v0.5.0/` | 0.5.0 | RSSI/SNR on Serial and ThingsBoard; STATUS 5 B (one SF12 block); STATUS every 900 s + on unsolicited change; ACK updates state; test mode; ThingsBoard dashboard (`dashboard/`, tabs Vận hành / Test nhanh) | `esp32dev-v0_5_0` |
| `v0.4.0/` | 0.4.0 | Max range (SF12, 20 dBm) + packet format v2 (smaller packets, broadcast BEACON, no HEARTBEAT); own copy of packet/gateway state in `protocol/` | `esp32dev-v0_4_0` |
| `v0.3.0/` | 0.3.0 | SX1278 fixes: RX continuous, PA_BOOST, explicit PHY settings, separate uplink/downlink channels | `esp32dev-v0_3_0` |
| `v0.2.0/` | 0.2.0 | LoRa gateway/node + ThingsBoard Cloud bridge | `esp32dev-v0_2_0` |
| `v0.1.0/` | 0.1.0 | LoRa gateway/node scaffold | — (archive only: does not compile) |

Only the current folder is edited. Older folders are frozen copies of each
release; Git tags `v0.1.0`, `v0.2.0` mark the same states. From v0.2.0 on,
each folder keeps a build env so the version can be rebuilt exactly.

## Build

From the repository root:

```text
pio run -e esp32dev                 # current version (v0.5.1)
pio run -e esp32dev-v0_2_0          # an older version
pio run -e esp32dev -t upload --upload-port COM3
```

Each build prints the version at boot (`FW_VERSION`) and copies the image to
`releases/v<version>/hybrid-lora-<env>-v<version>.bin` (plus `.elf`). The
`releases/` folder is not tracked by Git; published binaries are attached to
the GitHub Release with the same tag.

## Starting a new version

1. Copy the current folder, e.g. `firmware/v0.2.0` to `firmware/v0.3.0`.
2. In `platformio.ini`, point `[env:esp32dev]` at the new folder and set
   `custom_fw_version`; add an `[env:esp32dev-v0_2_0]` section for the old one
   (copy the `esp32dev` section with the old folder and version; env names
   cannot contain dots).
3. Add a row to the table above. When merged to `main`, tag the merge
   `v0.3.0` and attach the `.bin` to a GitHub Release.

Version numbers: patch for fixes, minor for new features, major for
incompatible changes (packet format, pinout).

The local Cloud config `cloud_config_local.h` lives inside each version's
`common/` folder and is ignored by Git.
