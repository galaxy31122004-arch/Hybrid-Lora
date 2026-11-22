# Firmware layout

## v0.5.4 radio update (long range: matches the old 20 dBm / CR 4/8 sketch)

All gateway/node builds, including field-test builds, use SF12 / **31.25 kHz** /
CR **4/8**, preamble **12** symbols, Low Data Rate Optimize on (131.072 ms
symbols), **20 dBm** in every mode. A 5-byte frame takes about **4.2 s** on air
for about 6 dB more sensitivity than 125 kHz. Flash gateway and nodes
together; builds with another bandwidth or preamble cannot talk to these.

Because of the air time the schedules changed: BEACON every 480 s (1% duty
cycle at 20 dBm), node fallback after 24 min without a beacon, ACK window
9.5 s / retry 10 s, test STATUS every 60 s ± 12 s. `main.cpp` checks these
against the PHY settings at compile time.

The Ra-02 has no TCXO. At 31.25 kHz the receiver tolerates a carrier offset of
about ±7.8 kHz (±BW/4) between the two boards (measured: about 650 Hz,
2026-11-16). Every received packet prints it as `FERR=<Hz>`; if it approaches
±5 kHz (for example with temperature), go back to 62.5 kHz.

### Node Wi-Fi report and online status (v0.5.4)

- The **node** now joins Wi-Fi too (same `wifi_nvs` module and networks as
  the gateway; in the field use a 2.4 GHz phone hotspot). Every 10 s it
  publishes for its device `Node-XX`: `nodeSeen` (epoch s), `nodeTx` (LoRa
  frames sent since boot), `nodeSeq` (last test seq sent), `nodeTxPower`,
  `nodeWifi` (Wi-Fi RSSI). The dashboard's **Node WiFi** column shows
  `● BẬT` while reports are younger than 30 s, so it tells whether the node
  is really powered, independently of the LoRa link; **Đã phát** next to
  **Mất** shows what was sent. Without Wi-Fi the node runs LoRa as before
  (and opens the setup portal AP).
- Transport: one HTTPS POST every 10 s to
  `https://thingsboard.cloud/api/v1/<NODE_ACCESS_TOKEN>/telemetry`
  (keep-alive; CA ISRG Root X1 in `cloud_config.h`). Set the `Node-XX`
  device token in `cloud_config_local.h` per `NODE_ID`; without a token the
  node sends no report. Not MQTT: ThingsBoard Cloud keeps one session per
  device, and the gateway already holds the `Node-XX` session through the
  gateway API, so a node MQTT session and the gateway closed each other
  every 6–10 s (`state -3`, tested 2026-11-16; sharing the gateway token did
  the same).
- Gateway `online`: in test mode a node is now offline after 4 packet gaps
  (at least 15 s, at most 120 s): about 15 s after a continuous field-test
  node stops. Normal mode keeps ~50 min. Node ids ever heard are stored in
  NVS (`hl-nodes`); after a gateway reboot those not heard yet are published
  `online=false` once, so a stale "ONLINE" no longer survives a reboot
  (from the second boot of 0.5.4 on: the list starts empty).

### Log over Wi-Fi (no USB cable)

Gateway and node copy their Serial log to TCP port 23 once Wi-Fi is up
(`common/net_log.h`). The last 8 KB are sent first, so boot messages are
kept. Names over mDNS: `hybrid-node-NN.local`, `hybrid-gateway.local`; the
node also reports its address as `nodeIp` on ThingsBoard. Use the IP:
the `.local` names did not resolve on the Windows PC (2026-11-16).

```
powershell -ExecutionPolicy Bypass -File scripts\netlog.ps1 -Target 192.168.1.5   # own window
python scripts\netlog.py 192.168.1.5 --seconds 60                                  # read 60 s, exit
```

Output is saved to `logs/net-<host>-*.log`. Commands typed there (TEST,
TEST 0, QUIET 1, ISO) work as on Serial; `#` Wi-Fi provisioning lines are
refused (Serial only), and the `#wifi` listing with passwords is never sent
over the network. Not authenticated: anyone on the same LAN can read the
log. The PC must be on the same network (or hotspot) as the board.

The radio driver also writes RegDetectOptimize (0x31) and
RegDetectionThreshold (0x37) explicitly, so a radio started without a reset
does not depend on earlier register contents.

The role is selected at build time in the root `platformio.ini`:

- `gateway`: initializes both radios; falls back to LoRa1 if LoRa2 fails.
- `node`: initializes radio 1 and the relay control loop (`NODE_ID=1` by default).
- `gateway-test`: listens only from boot (QUIET).
- `node-test`: sends a test STATUS every ~5 s (4.2 s on air + 1 s pause) at
  20 dBm from boot. That is ~80% duty at 20 dBm, above the datasheet's 1%
  rating; accepted for range tests only.

GPIO25 (formerly MODE) is unused. Set a unique `NODE_ID` (1..50) per node.

Pin assignments are centralized in `firmware/common/pins.h` and follow the
schematic net labels. GPIO12 is currently assigned to the second LoRa DIO0
net; verify that the module does not hold this ESP32 strapping pin high during
reset before production use.

The gateway maintains up to 50 node states and relays telemetry and commands
through ThingsBoard Cloud. Sensor drivers and RTC integration remain future
work. Set distinct NODE_ID build flags in platformio.ini before deploying
more nodes.

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
| Spreading factor / bandwidth / coding rate | **SF12** / 31.25 kHz / 4/8, Low Data Rate Optimize on |
| Preamble / sync word / header | 12 symbols / 0x12 / explicit |
| Hardware CRC | on (packets without CRC are dropped) |
| Output | PA_BOOST (RA-02), **20 dBm** (PA DAC high power, OCP 140 mA) |
| Receive mode | RX continuous |

These settings are tuned for maximum range (sensitivity about −143 dBm:
SF12 is about 14 dB better than SF7, and 31.25 kHz about 6 dB better than
125 kHz). 20 dBm is above the ~10 mW usually allowed in this band; confirm
the local rule before deploying. The SX1278 allows at most 1% duty cycle at
+20 dBm, which sets the schedules below.

## Packets (format v3) and schedule

Frame: `TYPE | NODE_ID | SEQ | PAYLOAD`, except STATUS which has no SEQ:
`TYPE | NODE_ID | PAYLOAD`. No LEN byte and no CRC-16 (the LoRa header has
the length; the radio's hardware CRC checks integrity). At SF12 the payload
is sent in 5-byte blocks, so every packet is kept at 5 bytes or less.

| Packet | Bytes | Time on air (SF12 / 31.25 kHz / 4/8) | Sent |
|---|---|---|---|
| STATUS | 5 (lux, flags: light, mode, health) | ≈ 4.2 s | every 900 s ± 90 s, within 10 s of any state change (including one made by a command, in case the ACK was lost), and on request |
| COMMAND / ACK | 5 | ≈ 4.2 s | on demand, one at a time: after a COMMAND the gateway sends nothing for 9.5 s (or until the ACK arrives) so its own downlink cannot drown the ACK; retry after 10 s, 4 attempts. A successful ACK updates the light/mode shown in ThingsBoard. |
| BEACON | 3, one broadcast (`NODE_ID = 0xFF`) | ≈ 4 s | every 480 s |

Channel use with 50 nodes: uplink ≈ 23% (≈ 63% of reports get through
without a collision), gateway transmitter ≈ 0.9% plus commands. A node falls
back to local mode after 1440 s without a beacon; ThingsBoard marks a node
offline after about 50 min without a report or ACK.

v0.5.x devices cannot talk to v0.4.0 or older (v0.5.0 and v0.5.1 can be mixed) (STATUS format changed):
flash the gateway and every node together.

## Measuring range (RSSI / SNR)

Every received packet prints its signal on Serial (115200 baud):

```text
Gateway:  RX STATUS node=1 RSSI=-118 dBm SNR=-7.25 dB FERR=-2310 Hz   (uplink)
Node:     RX BEACON node=0 RSSI=-121 dBm SNR=-9.50 dB FERR=2290 Hz    (downlink)
```

`FERR` is the sender's carrier offset seen by the receiver. The link fails
near ±7.8 kHz at 31.25 kHz; keep it within about ±5 kHz.

The gateway also sends `rssi` and `snr` for each node to ThingsBoard (tab
**Test nhanh** of the dashboard). For a field test, either carry a powered
node away from the gateway and watch the BEACON lines on its Serial (one
every 480 s), or press **Test 30 phút** for that node and watch its RSSI/SNR
update on the dashboard every ~60 s (test mode sends at 20 dBm). At SF12 the link fails near SNR −20 dB; the margin
left is roughly `SNR + 20` dB. With 10 dB or more to spare at the farthest
node, a lower SF (faster, fewer collisions) is likely to work.

## One-way field test (v0.5.2)

Measures range with the real uplink traffic: the node sends **test STATUS**
frames, which are ordinary 5-byte STATUS frames (same type, length and air
time, ≈ 4.2 s) with the test bit set. Two fields carry test data instead:

| STATUS byte | Normal | Test STATUS |
|---|---|---|
| 0 TYPE, 1 NODE_ID | STATUS, node id | same |
| 2–3 LUX | light level | 16-bit sequence number (packet loss) |
| 4 flags bit 0–1 | light, mode | same (real values) |
| 4 flags bit 2–6 | health | TX power in dBm (gateway scales to 20 dBm) |
| 4 flags bit 7 | test mode | 1 |

1. **Node:** type `TEST` on its Serial once. It sends a test STATUS every
   60 s ± 12 s at 20 dBm until `TEST 0`, and the setting is saved, so after a
   power cut it starts again by itself: carry it with a power bank, no laptop.
   `TEST <minutes>` runs for a limited time and is not saved.
2. **Gateway:** type `QUIET 1` so it only listens (no beacon, no commands;
   dashboard commands are answered `busy`). `QUIET 0` returns to normal.
3. Each received test STATUS prints one line on the gateway's Serial:

   ```text
   TEST node=1 seq=137 tx=17dBm RSSI=-118 SNR=-7.25 (+3 dB) lost=1 PDR(20)=95% total=130/138
   ```

   `RSSI`/`SNR` are already scaled to 20 dBm (`+3 dB`). `lost` is the gap
   before this packet, `PDR(20)` the share of the last 20 packets that
   arrived, `total` received/sent since the first packet. The same values go
   to ThingsBoard (`seq`, `pdr`, `lost`, `txPower`; tab **Test nhanh**).
4. Stay 3–5 minutes at each spot and note the time and place. At SF12 the
   link fails near SNR −20 dB; a spot with SNR ≥ −10 dB and PDR ≥ 90 % has
   margin.

A node that restarts begins again at seq 0; the gateway treats that as a new
run, not as lost packets. The dashboard's **Test 30 phút** button uses the
same test STATUS.

## Range map (where the link is strong, weak or lost)

The node has no GPS: a phone carried with the node records the position.

### On the phone, live (gateway web page)

The gateway serves `http://<gateway IP>:8080` (`common/field_web.h`,
page in `common/field_page.h`). Phone on the same hotspot as the gateway,
carried with the node:

- One-time on Android Chrome: `chrome://flags/#unsafely-treat-insecure-origin-as-secure`
  → add `http://<gateway IP>:8080` → Enabled → relaunch. Browsers give GPS
  only to secure pages; the page explains this if GPS is blocked. iPhone
  Safari has no such switch (use the GPX method below).
- At the gateway press "Đặt gateway ở đây", then walk. The strip on top shows
  the last 60 packets (height = SNR, cross = lost) and one word: Nghe rõ /
  Nghe được / Yếu / Sát giới hạn / Mất tín hiệu. The map marks every packet
  at the phone's position (lost ones at their estimated send time).
- "Tải KML cho Google Maps" / "Tải CSV" download the walk; data stay on the
  phone (localStorage) until "Xoá dữ liệu". The gateway keeps the last 512
  packets, so a phone that reconnects within ~14 min misses nothing.
- Map tiles: Esri World Street Map in grey (CARTO now needs a key;
  tile.openstreetmap.org did not load from the test network). Needs mobile
  data; without it the strip, table and downloads still work.

### Afterwards on the PC (GPX app)

1. Gateway fixed, running `gateway-test`; keep its log on a PC: plugged in
   (monitor window → `logs/device-monitor-*.log`) or over Wi-Fi
   (`python scripts/netlog.py <gateway IP>` → `logs/net-*.log`).
2. Phone: start a GPX logger app (fix every 1–5 s), carry it with the
   `node-test` node, walk away and back. Export the `.gpx` file to the PC.
   The PC clock and the phone (GPS time) must both be on network time.
3. `python scripts/range_map.py --gpx walk.gpx --log logs/device-monitor-YYMMDD-*.log --gateway <lat>,<lon>`
4. Import `logs/range-map-*.kml` in Google My Maps (Create → Import) or open
   it in Google Earth; the `.csv` has one row per packet (time, seq, lat/lon,
   distance, RSSI, SNR, FERR).

Colors by SNR: green ≥ 0 dB, yellow −10..0, orange −15..−10, red < −15
(SF12 decodes down to about −20 dB), black X = lost packet, placed at its
estimated send time. Tested 2026-11-16 with a real gateway log and a
simulated GPX track (not yet with a real walk).

## Antenna isolation self-test (gateway)

The gateway's two radios share one board: while radio2 transmits the
downlink at 20 dBm, radio1 can be deafened even though it listens on another
channel. To hear the weakest nodes (about −135 dBm) at the same time, radio1
needs roughly 65 dB of isolation from radio2.

Open Serial (115200 baud) on the gateway and type `ISO` + Enter. The gateway
measures radio1 idle, sends one 64-byte junk frame on radio2 (~5.6 s, nodes
drop it) and measures again:

```text
ISO: radio1 idle -118 dBm, while radio2 sends -52 dBm -> isolation 72 dB (target >= 65 dB)
```

Below 65 dB: move the antennas apart (stacked vertically on one mast works
best, about 5 m apart), shield each module, and run `ISO` again. The gateway
pauses for ~6 s during the test.

## ThingsBoard Cloud setup

1. In ThingsBoard Cloud, create a device named **`Hybrid-LoRa-Gateway`** (the dashboard looks for this name), enable **Is gateway**,
   and copy its access token. Use the Cloud region associated with your account.
2. Copy `firmware/v0.5.4/common/cloud_config_local.example.h` to
   `firmware/v0.5.4/common/cloud_config_local.h`. Enter the gateway access
   token, MQTT host, and the PEM root CA certificate from the device's
   **Check connectivity** dialog. This local file is ignored by Git.
   The North America MQTT host is `mqtt.thingsboard.cloud`; for another region,
   use the host shown in your Cloud account. TLS uses port 8883.
3. Wi-Fi (since v0.5.2) follows the `esp32-wifi-provisioning` skill: no SSID or
   password in this repository. Networks live in `D:\Secrets\wifi-credentials.json`
   and are compiled into `include/wifi_secrets.h` (generated, ignored by Git).
   The gateway keeps up to 8 networks in NVS, joins the strongest known one,
   and opens a setup portal (AP `ESP32-Setup-XXXX`, http://192.168.4.1) when
   none works. Wi-Fi runs in the ThingsBoard task, so it never stalls LoRa.
   Before each flash, with the board plugged in:

   ```text
   powershell -ExecutionPolicy Bypass -File D:\Personal-Skills\esp32-wifi-provisioning\scripts\pull-wifi-from-board.ps1 -Port COM3
   powershell -ExecutionPolicy Bypass -File D:\Personal-Skills\esp32-wifi-provisioning\scripts\make-wifi-secrets.ps1 -Project firmware\v0.5.4
   ```

   The ESP32 uses 2.4 GHz only. Wi-Fi starts only when step 2 is filled in.
4. Upload the gateway build from the repository root (MODE is unused):

   ```text
   powershell -ExecutionPolicy Bypass -File scripts/flash.ps1 -Env gateway -Port COM3
   ```

5. After a node sends a valid `STATUS` or ACK, the gateway announces it as
   `Node-01` through `Node-50`, with telemetry `online`, `healthFlags`, `lux`,
   `lightState`, `mode`, `testMode`, `rssi`, and `snr`. `lux = 65535` means the
   sensor value is unavailable in this firmware. `online` becomes false about
   50 minutes after the last valid report.
6. Import the dashboard (below).

## ThingsBoard dashboard

`dashboard/hybrid-lora-dashboard.json` (generated by
`scripts/make_dashboard.py`). Import: **Dashboards → + → Import dashboard**,
choose the file. It lists every device whose name starts with `Node-`.

| Tab | Shows | Buttons per node |
|---|---|---|
| **Vận hành** (opens first) | Online, đèn, chế độ, lux, health | Bật đèn, Tắt đèn |
| **Test nhanh** | Online, RSSI, SNR, test mode, đèn + smoothed RSSI/SNR charts (1 h) + gateway noise floor (~10 s) | Ping, Test 30 phút, Tắt test, Bật/Tắt đèn |

Tab **Test nhanh** also has **Nhật ký test** (every received packet with its time: seq, RSSI, SNR, lost, PDR; one tab per node, CSV download) and **Điểm đo**: on the phone, press *Đặt vị trí Gateway* once at the gateway, then *Ghi điểm đo* at each spot. The browser's GPS position, a note and the distance to the gateway are saved on the `Hybrid-LoRa-Gateway` device, so spots line up by time with the log. The browser asks for location permission (HTTPS). On a phone the widgets stack in one column.

Switch tabs with the button in each table's header. Each button sends a
server-side RPC to that node and shows a toast with the result; at SF12 /
62.5 kHz a command and its ACK take about 3.5 s, up to ~18 s with retries. Commands to several nodes are sent one after another, about 4 s apart.

Imported and opened on ThingsBoard Cloud 4.4.0 (both tabs, tab switching and
the chart load). The per-node buttons could not be tried yet because no
`Node-xx` device existed. If a widget fails to load, rebuild it by hand: an **Entities table** on the alias "Tất cả Node" (entity name starts with
`Node-`) with the keys above, and **Action cell buttons** of type *Custom
action* that call
`widgetContext.deviceService.sendTwoWayRpcCommand(entityId.id, {method, params, timeout: 60000})`.

### RPC methods (one per node device)

| Method | Params | Effect |
|---|---|---|
| `setLight` | `true` / `false` or `{"on": true}` | relay on/off |
| `ping` | none | node ACKs, then sends a STATUS (fresh `rssi`/`snr`) |
| `setTestMode` | minutes `0..120` (`0` = off), `true` (= 30) / `false`, or `{"minutes": n}` | test mode: 17 dBm, STATUS every 30 s, ends by itself |

The reply contains `success`; a failure contains `error` (`busy`, `timeout`,
`node_rejected`, `invalid_params`, `invalid_request`). The gateway waits for
the node's LoRa ACK before replying. Keep test mode to a few nodes at a time:
each test node uses ~5.5% air time (1.655 s every 30 s) and the channel is shared.

The gateway keeps LoRa processing in the main loop while Wi-Fi/MQTT reconnects
in a separate task. Without a local configuration file, Cloud is disabled and
the LoRa firmware continues to run. A functional Cloud test requires a gateway
board, a reporting node, and valid ThingsBoard credentials.

The continuous `node-test` build differs from timed test mode: it sends back
to back and is not suitable for verifying downlink commands. Use the normal
`node` build for COMMAND/ACK tests. Cloud telemetry keeps only the latest
snapshot per node while disconnected; the dashboard is not a lossless packet
recorder. Use gateway Serial logs when every received packet must be retained.
