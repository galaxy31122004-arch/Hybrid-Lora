# Work log

## 2026-11-15 — v0.5.2: one-way field test (TASK-10)
- Request: one-way range test that mimics real traffic (real STATUS frames with test data in place of unused fields); no gateway ACK/downlink needed.
- Actions: `firmware/v0.5.2/` copied from v0.5.1. Test STATUS = STATUS with test bit, LUX → 16-bit seq, health bits → TX dBm (`protocol/packet.h`). Gateway counts lost packets / PDR(20) (`protocol/gateway_state.*`), prints a `TEST ...` line per packet, sends `seq/pdr/lost/txPower` to ThingsBoard. Node Serial `TEST` (saved in NVS, restarts on power-up), `TEST <min>`, `TEST 0`; gateway Serial `QUIET 1/0` (listen only). Dashboard test tab: PDR and lost columns. New env/test target for v0.5.2; v0.5.1 kept as `esp32dev-v0_5_1`.
- Result: `pio run -e esp32dev` SUCCESS; host test `tests/protocol_v052` passes (new test for loss counting, duplicate, restart, seq wrap).
- Not verified: nothing run on a board; dashboard v0.5.2 not imported to ThingsBoard; Serial commands untested on hardware.

## 2026-11-15 — v0.5.2: Wi-Fi via esp32-wifi-provisioning
- Request: apply the new esp32-wifi-provisioning skill to the firmware.
- Actions: ran `make-wifi-secrets.ps1 -Project firmware0.5.2` (generated gitignored `include/wifi_secrets.h`, copied `include/wifi_nvs.h` + `src/wifi_nvs.cpp`, created `firmware/v0.5.2/AGENTS.md` + `CLAUDE.md`). Removed CLOUD_WIFI_SSID/PASSWORD; ThingsBoard task now calls `wifinvs::begin()/loop()` (core 0). main.cpp forwards Serial lines starting with `#` to `wifinvs::handleSerialLine` (`-DWIFI_NVS_SERIAL_CMDS=0`). platformio.ini: include dir + wifi_nvs.cpp. README setup updated.
- Result: `pio run -e esp32dev` SUCCESS (RAM 10.6 %, flash 43.5 %); `wifi_secrets.h` confirmed ignored by git.
- Not verified: not flashed. Script warning: the PC is on a 5 GHz network and the only saved network is not in range, so the board would open the setup portal until a 2.4 GHz network is added.

## 2026-11-15 — Dashboard: test log, field points (GPS), phone layout
- Request: show the gateway's test output on the web, record test spots from the phone, usable on a phone.
- Actions: `scripts/make_dashboard.py`: timeseries table "Nhật ký test" (seq/rssi/snr/lost/pdr per packet, CSV), table "Điểm đo" with header buttons "Ghi điểm đo" / "Đặt vị trí Gateway" (browser geolocation → telemetry pointLat/pointLon/pointAcc/pointLabel/pointDist and server attributes latitude/longitude on the gateway device), mobileOrder/mobileHeight per widget. Regenerated `firmware/v0.5.2/dashboard/hybrid-lora-dashboard.json`. On ThingsBoard: created device `Hybrid-LoRa-Gateway` (gateway flag) id 16aaf230-e0cf-11f1-8cd9-6b9990469fa2; imported dashboard 20db2180-e0cf-11f1-ba8b-c1a2c9300913, then replaced by fada7930-e0cf-11f1-ab8f-09854acec19a (Vận hành / Test nhanh tab chips in the banner, element-click actions); deleted old 74f2e0b0-db10-11f1-bf4d-5ba6e9bd2b60 and 20db2180-…; wrote 1 h of demo test data (seq/lost/pdr) to DEMO Node-01..03.
- Result: test tab loads; log shows per-packet rows per node; points table and its two buttons appear.
- Not verified: GPS buttons not pressed (need a phone and location permission); phone layout not viewed on a phone. Changes not committed.

## 2026-11-15 — ThingsBoard: removed demo devices
- Request: delete the fake data.
- Actions: deleted devices Node-01 (be2eb3e0-…), Node-02 (be8deae0-…), Node-03 (bee6e050-…), all labelled "DEMO - dữ liệu thử", with their telemetry. Hybrid-LoRa-Gateway checked: no telemetry stored.
- Result: DELETE returned 200 for all three.
- Not verified: none.

## 2026-11-15 — Dashboard: field map
- Request: GPS "error", no map to see the points.
- Actions: tested "Đặt vị trí Gateway" in the desktop browser: GPS works (saved, ±103 m). Added a map (system route_map, ThingsBoard 4.4): field points as a dark path with dots (tooltip: note, distance, accuracy, time) and the gateway marker from attributes latitude/longitude; widget time window 24 h. Imported as dashboard 021f52f0-e0d1-11f1-8cd9-6b9990469fa2, deleted fada7930-…; set dark colours in place. Wrote 3 temporary points to check the map, then deleted all point* telemetry and the gateway latitude/longitude attributes (the desktop position was not the real gateway site).
- Result: map shows tiles, points and gateway marker.
- Not verified: GPS buttons on a phone (the reported error was not reproduced on desktop).

## 2026-11-15 — v0.5.3: build-time role, flashed gateway
- Request: board on COM3 is the gateway; drop the MODE pin; faster upload; monitor opens by itself after flashing; reply in Vietnamese.
- Actions: `firmware/v0.5.3/` (copy of v0.5.2 + uncommitted dashboard work; v0.5.2 folder restored to its tag). Role by build flag: envs `gateway` (-DHYBRID_ROLE_GATEWAY=1) and `node` (-DNODE_ID=1), MODE pin (GPIO25) removed; v0.5.2 kept as env `esp32dev-v0_5_2`. platformio.ini: upload_speed 921600, monitor filters time+log2file. New `scripts/flash.ps1` (upload, fallback 115200 via esptool, then opens `scripts/monitor.ps1` in its own window) and `scripts/monitor.ps1` (logs to `logs/`, gitignored). Flashed env gateway to COM3 (FTDI FT232R, ESP32-D0WD-V3, MAC 28:05:a5:33:a8:c4).
- Result: boot log: `Hybrid-LoRa firmware v0.5.3`, `Role: Gateway (build env gateway)`, `LoRa1: OK, LoRa2: FAIL`, ThingsBoard disabled (no token/CA).
- Not verified / open: LoRa2 (CS GPIO5) does not answer (version register != 0x12). Earlier flashes failed when EN was pressed during writing.

## 2026-11-15 — v0.5.3: single-radio gateway fallback
- Request: LoRa2 fails; let LoRa1 send the commands; node unchanged.
- Actions: `firmware/v0.5.3/src/main.cpp`: downlink transmitter pointer; if radio2 fails at boot, radio1 is re-initialised with RX uplink / TX downlink and sends beacons and commands (half duplex); noise sampling and ISO follow it. `scripts/flash.ps1` builds first, then waits for Enter before uploading. Flashed env gateway to COM3.
- Result: log `LoRa1: OK, LoRa2: FAIL`, `Single-radio gateway: LoRa1 receives uplink and sends the downlink`. Builds gateway and node SUCCESS.
- Not verified: no node on the air yet, so downlink/uplink in single-radio mode untested; LoRa2 hardware fault still open.

## 2026-11-15 — Flashed node 1 (v0.5.3)
- Actions: `scripts/flash.ps1 -Env node -Port COM3` on the second board.
- Result: log `Role: Node 1 (build env node)`, `LoRa1: OK, LoRa2: disabled` (ESP32 ROM dated 2016, older silicon than the gateway).
- Not verified: TEST not started yet; no over-the-air exchange with the gateway seen yet.

## 2026-11-15 — Field-test builds, ThingsBoard on the gateway
- Request: node transmits without commands (back to back), gateway only listens without resets, log on ThingsBoard.
- Actions: envs `gateway-test` (QUIET from power-up) and `node-test` (test STATUS back to back, 17 dBm) via -DFIELD_TEST=1. Local gitignored `firmware/v0.5.3/common/cloud_config_local.h` with the Hybrid-LoRa-Gateway access token and the root CAs of mqtt.thingsboard.cloud (Sectigo Public Server Authentication Root E46 + USERTrust ECC). Example config fixed (a multi-line raw string does not compile inside a #define). Flashed gateway-test to COM3.
- Result: gateway log: single-radio, QUIET, Wi-Fi DNT connected (192.168.1.6), `ThingsBoard connected`. All four envs build.
- Not verified: node-test not flashed yet; no packet received over the air yet.

## 2026-11-15 — First over-the-air test (node-test → gateway-test)
- Actions: flashed node-test (continuous test STATUS). Read COM3 (node) and ThingsBoard API.
- Result: node prints `TX test STATUS seq=0..`; gateway auto-created device Node-01 on ThingsBoard; at 20:37:31 seq=28, lost=0, pdr=100, snr=13.25 dB, txPower=17, testMode=1; 28 seq rows stored in 2 min (none missing). RSSI reads +24 dBm: boards a few cm apart, receiver saturated, value not meaningful at this range.
- Not verified: range at distance; downlink (commands) in single-radio mode.

## 2026-11-16 — LoRa bandwidth 250 kHz
- Request: widen LoRa bandwidth following the 250 kHz proposal.
- Actions: firmware/v0.5.3/common/sx1278_radio.h BW code 0x07 -> 0x08; SF12 and LDRO unchanged. Added bandwidth/airtime/compatibility note to v0.5.3 README.
- Result: source updated for all four gateway/node normal/test builds. Wi-Fi generation attempted with -NoPcFallback; failed writing the external credentials store (access denied). PlatformIO run for gateway, node, gateway-test, node-test attempted; stopped before compilation because pio-core/platforms.lock access was denied.
- Not verified: compilation and over-the-air behavior at 250 kHz. No board flashed.

## 2026-11-16 — Build and review 250 kHz firmware
- Request: finish building and check remaining issues.
- Actions: Wi-Fi header regenerated successfully (-NoPcFallback); built gateway, node, gateway-test and node-test with PlatformIO. Configured and built all CMake host tests; reran CTest with the compiler runtime directory first in PATH after initial Windows DLL entry-point errors. Updated current README and firmware index; flash script now prepares Wi-Fi and validates env, monitor recognizes gateway-test.
- Result: all four firmware builds SUCCESS; 8/8 host tests PASS; flash/monitor PowerShell syntax checks PASS.
- Open: no USB board detected (only Bluetooth COM7/COM8), so no upload or 250 kHz radio test. No usable saved Wi-Fi network currently visible. Existing LoRa2 hardware fault and single-radio COMMAND/ACK need hardware verification. Continuous node-test occupies the channel; Cloud snapshots are not a lossless packet log.


## 2026-11-16 — Verify supplied RA-02 and Semtech datasheets
- Request: check receiver limits against the two supplied PDFs.
- Actions: extracted text with installed PyMuPDF and visually checked RA-02 page 2 and Semtech page 21; page renders saved under sandbox/.
- Result: RA-02 v1.1 lists SF12/125 kHz -141 dBm; Semtech Rev.4 Band 3 typical table lists SF12 at 125/250/500 kHz as -137/-134/-130 dBm. Test conditions differ or are incompletely specified; these figures cannot be treated as identical guarantees. RA-02 module output specification is 18 +/-1 dBm.
- Not verified: actual sensitivity and RF output of the user's boards.


## 2026-11-16 — Restore LoRa bandwidth 125 kHz
- Request: prioritize receive sensitivity and restore BW 125 kHz.
- Actions: current v0.5.3 radio BW code restored to 0x07; SF12, CR4/5 and LDRO retained. Updated current README and firmware index. Regenerated Wi-Fi header (-NoPcFallback); no usable network visible.
- Result: PlatformIO gateway, node, gateway-test and node-test all SUCCESS; current binaries copied to releases/v0.5.3 by existing build hook.
- Not verified: not flashed, no over-the-air test in this task.



## 2026-11-16 09:16 — v0.5.4: narrower bandwidth trial (SF12 / 62.5 kHz) + radio review fixes
- Request: fix review items 1 (stale "v0.4.0" label in README) and 2 (registers left at reset defaults); skip the BW<500 kHz errata item (test setup has one LoRa per board); try a narrower bandwidth.
- Actions: copied firmware/v0.5.3 to firmware/v0.5.4. sx1278_radio.h: BW code 0x06 (62.5 kHz), kBandwidthHz, constexpr symbol time + airtime_ms(), static_assert on LDRO, explicit writes of 0x31 (DetectOptimize=3, RMW) and 0x37 (0x0A), carrier offset from RegFei (last_freq_error_hz). main.cpp: BEACON 240 s (1% duty at 20 dBm), test STATUS 30 s +/- 6 s, static_asserts of schedules vs air time, FERR printed per received packet, comments updated. gateway_state.h: ACK window 4200 ms, retry 4500 ms. packet.h comment, README, dashboard toast text ("mỗi 30 s"), scripts/make_dashboard.py text, platformio.ini (current envs -> v0.5.4, new env esp32dev-v0_5_3), firmware/README.md table.
- Result: pio run gateway, node, gateway-test, node-test, esp32dev-v0_5_3 all SUCCESS.
- Not verified: not flashed; no over-the-air test. 62.5 kHz is not compatible with 0.5.3 (flash both ends). Crystal offset margin (+/-15.6 kHz) must be checked from FERR in the field.
- Open point (observed by user): dashboard shows Node-01 ONLINE with RSSI while the node is off. Cause in code: online=false is only published for nodes seen since the gateway booted, so after a gateway reboot the last "online=true" telemetry stays in ThingsBoard. Not fixed yet.

## 2026-11-16 — v0.5.4: node Wi-Fi report + gateway online timeout
- Request: field test sends continuously from the node; dashboard showed Node-01 ONLINE with RSSI while the node was off. User chose: node joins Wi-Fi and reports its real state; fix online in gateway firmware.
- Actions: new common/node_cloud.h + src/node_cloud.cpp (Wi-Fi via wifi_nvs, NTP, MQTT TLS; every 10 s nodeSeen, nodeTx, nodeSeq, nodeTxPower, nodeWifi; NODE_ACCESS_TOKEN or gateway token). main.cpp: node counts LoRa frames, starts node_cloud, '#' Serial lines go to wifi_nvs on the node too. thingsboard_bridge: test-mode online timeout = 4 x packet gap (15..120 s); node ids heard stored in NVS "hl-nodes", unheard known nodes published online=false once after boot. cloud_config(.example).h: NODE_ACCESS_TOKEN. platformio.ini: node_cloud.cpp in v0_5_4 sources. scripts/make_dashboard.py: columns Node WiFi (age of nodeSeen, on < 30 s) and Đã phát; regenerated firmware/v0.5.4/dashboard. READMEs updated.
- Result: pio run gateway, node, gateway-test, node-test all SUCCESS; dashboard JSON valid.
- Not verified: not flashed; whether ThingsBoard Cloud accepts the gateway token from two sessions at once; dashboard not re-imported. Known-node list starts empty, so the stale Node-01 is cleared only once the node is heard by v0.5.4.

## 2026-11-16 10:30 — v0.5.4: clear stale Node-01 online on first boot
- Request: dashboard still showed Node-01 ONLINE.
- Actions: found boards still on v0.5.3 (never flashed; only Bluetooth COM7/COM8 present, no board plugged in). thingsboard_bridge.cpp load_known_nodes(): with no NVS list yet, Node-01 (default NODE_ID) counts as known, so the gateway publishes online=false for it after boot until it is heard.
- Result: pio run gateway, node, gateway-test, node-test SUCCESS.
- Not verified: not flashed yet; waiting for the user to plug in the boards.

## 2026-11-16 10:36 — Flash node-test v0.5.4 to COM3 + flash rule
- Request: flash node-test on the node; rule: every flash opens a terminal monitor.
- Actions: pull-wifi-from-board.ps1 -Port COM3 (2 built-in networks, nothing new); make-wifi-secrets.ps1 -Project firmware\v0.5.4 (1 network in range); direct pio upload at 921600 failed ("No serial data received"); scripts/flash.ps1 -Env node-test -Port COM3 succeeded and opened the monitor window. Rule added to firmware/v0.5.4/AGENTS.md and to Claude memory.
- Result: node boots v0.5.4, LoRa1 OK, test STATUS every ~1.65 s (seq 0..17 in 28 s), Wi-Fi connected, "Node cloud: ThingsBoard connected".
- Not verified: gateway still on v0.5.3 (125 kHz), so it cannot receive these packets; dashboard not re-imported.

## 2026-11-16 12:20 — Re-flash node-test v0.5.4 on COM3
- Request: "nạp đi đang ở node"; "mỗi lần nạp nhớ mở terminal note lại".
- Actions: pull-wifi-from-board.ps1 -Port COM3 (no answer in 40 s, exit 3); make-wifi-secrets.ps1 for v0.5.4 (version 0x472BE9D5); ran scripts/flash.ps1 -Env node-test -Port COM3 inside the tool → build OK, upload failed at 921600 and 115200 ("Failed to connect to ESP32: No serial data received") because the BOOT prompt was skipped.
- Fixed scripts/flash.ps1: Wi-Fi secrets path firmware/v0.5.3 → firmware/v0.5.4.
- Relaunched flash.ps1 in its own terminal window so the user can put the board in download mode; monitor opens there after success.
- Rule added to firmware/v0.5.4/AGENTS.md and memory: always run flash.ps1 in a visible window.
- Not verified: result of the windowed flash.

## 2026-11-16 12:30 — Node-test v0.5.4 running; cloud reconnect diagnosis
- Windowed flash.ps1 succeeded; log logs/device-monitor-261116-122151.log: v0.5.4, Node 1, LoRa1 OK, Wi-Fi connected, TX test STATUS every ~1.65 s.
- Observed: "Node cloud: ThingsBoard connected" repeated every ~12 s (12:22:12, 12:22:24, 12:22:36) = MQTT session dropped and reconnected.
- User: board was powered from the laptop USB (not enough power) — likely cause.
- Added to firmware/v0.5.4/src/node_cloud.cpp: log "ThingsBoard disconnected (state N, up S s)" and token type on connect. Built node + node-test: SUCCESS. Not flashed yet.
- Not verified: whether reconnects stop on a proper supply; shared gateway token (if gateway online) as alternative cause.

## 2026-11-16 12:45 — Log over Wi-Fi (no USB cable) for v0.5.4
- Request: "ko cần nối dây vẫn đọc đc log qua wifi".
- Actions: new firmware/v0.5.4/common/net_log.h + src/net_log.cpp (global `Log`: tee to Serial + 8 KB ring, TCP server port 23 on core 0, mDNS hybrid-node-NN.local / hybrid-gateway.local, history sent on connect, typed lines → command parser, '#' lines refused). main.cpp, node_cloud.cpp, thingsboard_bridge.cpp: Serial.print* → Log.print*; wifi_nvs.cpp: status lines via WIFI_LOG (Log if net_log.h exists), '#wifi' listing with passwords stays Serial only. node_cloud publishes nodeIp. platformio.ini [v0_5_4] adds net_log.cpp. New scripts/netlog.py (reconnecting reader, saves logs/net-<host>-*.log) and scripts/netlog.ps1 (own window). README + AGENTS.md updated.
- Result: builds gateway, node, gateway-test, node-test SUCCESS (node-test RAM 20.0%, flash 77.3%).
- Opened flash.ps1 -Env node-test -Port COM3 in its own window (old monitor 12:21:51 still holds COM3 but stopped logging at 12:22:44 after the power change).
- Not verified: flash result, TCP log on the board, mDNS name resolution on this PC.

## 2026-11-16 12:40 — Node-test v0.5.4 with Wi-Fi log flashed and checked
- Stopped the stale pio monitor (PIDs 28088, 18856; no data since 12:22:44) that held COM3; flash.ps1 -Env node-test -Port COM3 in own window: success, monitor log logs/device-monitor-261116-123237.log.
- Boot: v0.5.4, Node 1, LoRa1 OK, Wi-Fi IP 192.168.1.5, "Net log: telnet hybrid-node-01.local 23".
- Wi-Fi log verified: python scripts/netlog.py 192.168.1.5 --seconds 30 connected, history + live lines (logs/net-192.168.1.5-261116-123352.log). hybrid-node-01.local does not resolve on the PC (getaddrinfo, Resolve-DnsName, own mDNS query all failed) → scripts/docs switched to IP; mDNS query code removed.
- Node cloud on external power still: "ThingsBoard disconnected (state -3)" ~6 s after each connect, cycle ~12 s → not power. Likely ThingsBoard closing the older session when node and gateway share the gateway token. Fix proposed: NODE_ACCESS_TOKEN (Node-01 token).
- Not verified: gateway side of the session conflict (gateway runs 0.5.3, no net log).

## 2026-11-16 12:50 — Node-01 own ThingsBoard token
- Request: "1 bạn làm đi" (give the node its own token).
- Actions: read the Node-01 access token from ThingsBoard Cloud (user's signed-in Chrome, REST /api/device/{id}/credentials, type ACCESS_TOKEN); set NODE_ACCESS_TOKEN for NODE_ID 1 node builds in firmware/v0.5.4/common/cloud_config_local.h (gitignored; token not logged here). Built node-test + gateway-test: SUCCESS.
- Stopped the 12:32 monitor (held COM3) and opened flash.ps1 -Env node-test -Port COM3 in its own window.
- Not verified: flash result; expected log "ThingsBoard connected (node token)" with no state -3 drops.

## 2026-11-16 16:25 — Flash terminal rule + ThingsBoard drop diagnosis
- Request: "nạp xong thì đóng terminal và khi nạp mở terminal mới thì đóng cái cũ"; "viết luôn script nạp code"; "đọc log đi".
- scripts/flash.ps1: closes older flash.ps1/monitor.ps1 windows and pio device monitor processes (not itself/parent) before flashing; pulls Wi-Fi from the board for node envs too; launched without -NoExit so the flash window closes after success (monitor window opens). Parse check OK. AGENTS.md + memory updated.
- Node log (logs/device-monitor-261116-161510.log): "ThingsBoard connected (node token)" then "disconnected (state -3)" every ~10 s → own token did not fix it.
- Test: PC session with Node-01 token (scratchpad tb_session_test.py) kicked the node's session (one session per device) and was itself closed ~4 s later while the node was offline → a third client uses Node-01 on a ~10 s cadence.
- ThingsBoard REST: Node-01 additionalInfo.lastConnectedGateway set; gateway device active, lastConnectTime ~16:20.
- Likely cause: another board still running the 10:35 node-test build (Node 1, gateway token, gateway API publish every 10 s). Not verified: which board; asked user.

## 2026-11-16 16:27 — Node cloud stable; flash gateway-test v0.5.4
- Node Wi-Fi log 16:22:44–16:23:24: one "ThingsBoard connected (node token)", no disconnects (node had just rebooted; gateway board presumably off).
- Request "h nạp cho gateway": opened scripts/flash.ps1 -Env gateway-test -Port COM3 in its own window (no -NoExit; closes old windows, pulls Wi-Fi from board, builds, waits for BOOT).
- Not verified: flash result.

## 2026-11-16 16:55 — Gateway-test v0.5.4 flashed; node report moved to HTTPS
- Gateway flash (COM3, logs/device-monitor-261116-162904.log): v0.5.4 gateway, LoRa1 OK, LoRa2 FAIL → single-radio gateway; Wi-Fi IP 192.168.1.6; "ThingsBoard connected". Receives Node 1 test STATUS at 62.5 kHz: 494/494, PDR 100%, FERR about -0.94 to -1.1 kHz, RSSI +18 dBm / SNR ~9.5 dB (boards close together: receiver overloaded, RSSI not meaningful).
- Node Wi-Fi log: with the gateway publishing Node-01, node MQTT (own token) dropped every 6–10 s (state -3) → ThingsBoard keeps one session per device; gateway API session for Node-01 and direct node session close each other.
- Fix: firmware/v0.5.4/src/node_cloud.cpp now sends the report as HTTPS POST /api/v1/<token>/telemetry to thingsboard.cloud (HTTPClient, keep-alive, logs only status changes); no token → no report (Wi-Fi task still runs). cloud_config.h: CLOUD_HTTP_HOST, CLOUD_HTTP_ROOT_CA = ISRG Root X1 (exported from Windows store, thumbprint CABD2A79…A5E8; openssl verify of thingsboard.cloud chain with only this CA: ok). node_cloud.h, README updated. PC test: 4 POSTs with Node-01 token → 200, ~260 ms.
- Builds gateway, node, gateway-test, node-test: SUCCESS.
- Not verified: HTTPS report on the node board (not flashed yet); LoRa2 FAIL cause.

## 2026-11-16 16:56 — Node-test with HTTPS report flashed and verified
- flash.ps1 -Env node-test -Port COM3 (own window): success, logs/device-monitor-261116-165321.log (v0.5.4, Node 1, Wi-Fi 192.168.1.5).
- Node Wi-Fi log: "Node cloud: ThingsBoard report OK (HTTPS)" at 16:53:37, no failures in the following ~35 s.
- ThingsBoard REST (Node-01 latest telemetry): nodeSeen/nodeTx 38/nodeSeq 37/nodeWifi -67/nodeIp 192.168.1.5, all 0 s old.
- Gateway 192.168.1.6:23 not reachable (timed out) — presumably unpowered after unplugging from USB.
- Not verified: node HTTPS + gateway publishing Node-01 at the same time.

## 2026-11-16 17:15 — Range map script (log + GPS track → Google Maps)
- Request: link timestamps to positions to see on Google Maps where the node went, where packets were lost/strong.
- Added scripts/range_map.py: parses gateway logs (device-monitor/net logs, "TEST node= seq= RSSI= SNR= lost=" + "RX STATUS ... FERR="), date from file name, local time; places lost packets at estimated send times; interpolates position from a phone GPX track (UTC); outputs KML (colored by SNR, lost = black X, track line, gateway marker) + CSV (time, seq, status, lat/lon, distance, RSSI, SNR, FERR). Reads UTF-8 with or without BOM.
- Tests (scratchpad): real gateway log 261116-162904 + simulated GPX → 550 points, distance column OK; synthetic log with lost=2 → 2 lost points at evenly spaced times.
- README v0.5.4: "Range map" section.
- Not verified: real walk with a phone GPX; import into Google My Maps.

## 2026-11-16 17:45 — Gateway field web page (phone GPS range map)
- Request: gateway serves a web page, user opens it on the phone; positions linked to packets; then "dùng skill viết web chính chủ Anthropic" → frontend-design (claude-plugins-official 1.1.1) applied; user chose "GPS trực tiếp (Android)".
- Added firmware/v0.5.4/common/field_web.h + src/field_web.cpp (WebServer port 8080 in a core-0 task, ring of 512 received test packets, GET / and /api/packets?after=<id>), common/field_page.h (page: packet strip hero + verdict word, Leaflet map, GPS via watchPosition, gateway-clock offset from poll RTT, lost packets at interpolated times, localStorage, KML/CSV download, wake lock). main.cpp: field_web.begin() for gateway, field_web.add() per test STATUS. platformio.ini adds field_web.cpp. README "Range map" section.
- Design: light monochrome for sunlight (user's web-ui-style inverted), Archivo font, no all-caps labels.
- Preview in Chrome with scratchpad mock_gateway.py (fake packets + fake GPS): fixed Chrome auto-dark (color-scheme only light), CARTO tiles "API KEY REQUIRED" and OSM tiles blocked on this network → Esri World_Street_Map with grayscale filter (24/24 tiles loaded). Screenshots in C:\Users\letha\AppData\Local\Temp\claude-chrome-screenshots-tTfz93\.
- Builds: gateway, gateway-test, node, node-test SUCCESS (gateway-test flash 80.0%, RAM 23.8%).
- Not verified: on the real gateway and an Android phone (flag, GPS, wake lock).

## 2026-11-16 17:30 — Gateway boot loop (GPIO12) and task watchdog
- Gateway-test with field web flashed (logs/device-monitor-261116-172144.log). After the flash reset: boot loop "boot:0x33 ... spi_flash: Detected size(2k) ... Probe failed" (120 resets) → GPIO12 strapping pin (LoRa2 DIO0) high at reset → 1.8 V flash. User unplugged/replugged USB → boot:0x13 OK (inferred cause, not measured).
- Then: field web up (http://192.168.1.6:8080), Node 1 packets received (FERR ~-0.6 kHz), but "task_wdt: IDLE0 (CPU 0) ... CPU 0: thingsboard" reset every ~14 s (18 times), never "ThingsBoard connected".
- Fix: disableCore0WDT() at the start of setup() (firmware/v0.5.4/src/main.cpp) — TLS handshake on core 0 starves IDLE0 > 5 s. Rebuilt all four envs: SUCCESS. Opened flash.ps1 -Env gateway-test -Port COM3 in its own window.
- Not verified: new gateway run; after flashing a power cycle may be needed again (GPIO12).

## 2026-11-16 17:29 — Gateway-test with field web running
- logs/device-monitor-261116-172635.log: after flash 29 resets with boot:0x33 (GPIO12) until the user power-cycled (17:28:48 boot:0x13). Then: no task_wdt, Wi-Fi 192.168.1.6, "Field web: http://192.168.1.6:8080", "ThingsBoard connected" at 17:29:07, Node 1 test STATUS received (PDR 100%, FERR ~-0.9 kHz).
- From PC: GET /api/packets?after=0 → 14 packets (last seq 13, SNR 12.5); GET / → 200, 17607 bytes.
- Not verified: phone (Android Chrome flag, GPS, map) on the real gateway.

## 2026-11-16 18:05 — Status LED, gateway Wi-Fi network, web unreachable
- Request: board LED shows Wi-Fi search / phone connected / crash; gateway should use network "TMT Electronic"; web page not reachable any more.
- Added firmware/v0.5.4/common/status_led.h + src/status_led.cpp (GPIO2, task on core 0): 10 Hz for 15 s after a crash reset (panic/WDT/brown-out, reason logged as "Last reset: ..."), 1 Hz searching Wi-Fi, double blink portal AP, short blip Wi-Fi only, mostly on when the phone page polls (gateway) / cloud report OK (node), flick per received packet (gateway). Hooks in field_web.cpp, node_cloud.cpp, main.cpp; platformio.ini adds status_led.cpp. Builds: all four envs SUCCESS.
- Web unreachable: gateway log 17:57 shows IP 192.168.43.40 on "DNT" (phone hotspot subnet), not 192.168.1.6 → page is at http://192.168.43.40:8080 and the Chrome flag needs that origin; PC (192.168.1.x) cannot reach it.
- Wi-Fi: add-wifi.ps1 -Ssid "TMT Electronic" (password only in D:\Secrets\wifi-credentials.json). Opened flash.ps1 -Env gateway-test -Port COM3 (regenerates wifi_secrets.h).
- Not verified: flash, which network the gateway picks (last-good first, then strongest signal), LED patterns on the board.

## 2026-11-16 18:05 — Gateway on TMT Electronic, status LED confirmed by user
- logs/device-monitor-261116-180217.log: boot:0x33 once after flash, then power-on boot:0x13; "Last reset: power-on"; Wi-Fi "TMT Electronic", IP 192.168.43.40; Field web http://192.168.43.40:8080; ThingsBoard connected 18:03:02.
- User: "chạy ổn rồi". LED pattern table given to the user (README not yet updated with it — see status_led.h comment).

## 2026-11-16 18:30 — Field test radio settings matched to the old long-range sketch
- Request: range still shorter than an old Arduino LoRa sketch (20 dBm, SF12, 62.5 kHz, CR 4/8).
- Found: v0.5.4 test mode sent at 17 dBm (+3 dB added back only in the displayed readings) with CR 4/5.
- Changes (firmware/v0.5.4): kTestTxPowerDbm 17 -> 20; lora_phy::kCodingRateCode 4/5 -> 4/8 (5-byte frame ~1.85 s on air); node-test pause kFieldTestGapMs = 3300 ms between test STATUS (~36% duty at 20 dBm, above the datasheet 1% rating; user chose this); GatewayState kAckWindowMs 4200 -> 4600, kRetryMs 4500 -> 5000; comments/log texts updated.
- Builds: node-test SUCCESS, gateway-test SUCCESS. Not flashed yet; both boards need the new build (same CR on both ends).

## 2026-11-16 18:45 — 31.25 kHz bandwidth and preamble 12 for range
- Request: switch to BW 31.25 kHz; raise the preamble too.
- Changes (firmware/v0.5.4): kBandwidthCode 0x05 / kBandwidthHz 31250 (FERR tolerance now ~+/-7.8 kHz; measured offset ~650 Hz); kPreambleSymbols 8 -> 12; 5-byte frame ~4.2 s on air. Timings rescaled: kAckWindowMs 9500, kRetryMs 10000, kBeaconPeriodMs 480000 (gateway timeout 24 min), test mode every 60 s +/- 12 s, node-test pause kFieldTestGapMs 7500 (~12 s per packet, ~36% duty at 20 dBm). Field page: "Mất tín hiệu" after 30 s without a packet, hint text "~12 s".
- Builds: node-test, gateway-test, node, gateway all SUCCESS. Not flashed; both boards must get the new build (BW and preamble must match).

## 2026-11-16 18:55 — Gateway status LED dark when idle
- Request: gateway LED off when all is fine; light only on packets, problems or special states.
- Changes (firmware/v0.5.4 status_led.h/.cpp): gateway with Wi-Fi up and no phone -> off; phone page open -> 60 ms blip every 2 s; packet -> 200 ms flash (kPacketFlashMs). Crash 10 Hz, no Wi-Fi 1 Hz, portal double blink unchanged. Node patterns unchanged.
- Builds: gateway-test, node-test SUCCESS. Not flashed yet.

## 2026-11-16 20:20 — Status LED polarity: common-anode
- Finding: gateway blue LED lit steadily while firmware ran normally; user confirmed the LED is common-anode (GPIO2 LOW = lit), so every pattern was inverted.
- Change: StatusLed::kActiveLow = true (status_led.h/.cpp), LED starts dark. Builds gateway-test, node-test SUCCESS.

## 2026-11-16 20:35 — node-test sends faster
- Request: send faster; user reports the RA-02 stays cool.
- Change: kFieldTestGapMs 7500 -> 1000 ms (~5.2 s per test STATUS, ~80% duty at 20 dBm). Field page: "Mất tín hiệu" after 20 s, hint "~5 s". README updated.
- Builds: node-test, gateway-test SUCCESS. Flashing node-test on COM3.

## 2026-11-16 20:40 — node-test period fix
- Found in node log 20:24: test STATUS every 7.5 s, not ~12 s: next_status_delay() counts from TX start (send_frame is non-blocking), so the gap did not include the 4.2 s air time. With the 1 s gap the node would have sent back to back.
- Fix: field-test delay = kFrameAirMs + kFieldTestGapMs (~5.2 s period, ~80% duty); first field-test STATUS 2 s after boot (was random 0..30 s). node-test SUCCESS; reflashing.

## 2026-11-16 20:45 — Node status LED dark when OK, flash per TX
- Request: node LED like the gateway (user OK).
- Change: node with ThingsBoard report OK -> off; 200 ms flash on every STATUS sent (status_led.packet() in send_status); 1 Hz no Wi-Fi, double blink portal, short blip Wi-Fi without cloud, 10 Hz crash unchanged. Builds node-test, gateway-test SUCCESS; flashing node-test.

## 2026-11-16 21:45 — Range data analysis; NOISE and WIFI commands
- Input: phone export range-20261116-2041 CSV/KML (351 rows: 157 ok, 194 lost) in D:\Hybrid_lora.
- Analysis: weakest packets RSSI -123..-129 dBm all at SNR -15..-20 dB (SF12 limit ~-20). Noise floor estimate RSSI - SNR (SNR<0, 69 packets): mean -103.5 dBm (range -115..-93), vs ~-123 dBm thermal at 31.25 kHz: about 20 dB of local noise at the gateway. FERR -684..-604 Hz (one outlier 4828). dist_m values unreliable (gateway point set away from the gateway).
- Changes: gateway serial/net command NOISE (200 raw RSSI samples over ~1 s, prints min/median/max and Wi-Fi state); WIFI 0 / WIFI 1 on both roles (wifinvs::setEnabled, applied in wifinvs::loop; while off: no reconnect/portal). Help text updated.
- Builds: gateway-test, node-test, gateway, node SUCCESS. Not flashed.

## 2026-11-16 22:05 — Gateway OLED for walking range tests (Wi-Fi off)
- Request: user has a 0.96" SSD1306 OLED; will carry the gateway during range tests with Wi-Fi off.
- Added firmware/v0.5.4/common/field_oled.h + src/field_oled.cpp (U8g2, I2C SDA 22 / SCL 23, 0x3C, task on core 0, redraw 250 ms): Wi-Fi state, noise floor (lowest raw RSSI per 6 s window, sampled every 50 ms in run_gateway), last test packet SNR (large), RSSI, seq, PDR, lost, age; "NO SIGNAL" after 20 s. BOOT button (GPIO0) toggles Wi-Fi (wifinvs::setEnabled). Status LED stays dark while Wi-Fi is off on purpose.
- platformio.ini [v0_5_4]: lib_deps + olikraus/U8g2@^2.36.2; build_src_filter + field_oled.cpp.
- Builds: gateway-test, node-test, gateway, node SUCCESS (gateway flash 82.4%). Not flashed; OLED not tested on hardware yet.

## 2026-11-17 — Node Wi-Fi off, saved across reboots
- Request: turn the node's Wi-Fi off and keep it off.
- Actions: `wifi_nvs.cpp`/`wifi_nvs.h` save `setEnabled()` in NVS (namespace `wifi`, key `off`); `begin()` skips scan/portal when it is set. `main.cpp`: node also runs `field_oled.begin()` so the BOOT button toggles Wi-Fi (no OLED on the node).
- Build: node-test, node, gateway-test SUCCESS. Flash window for node-test on COM3 started (flash.ps1).
- Not verified: flash result; `WIFI 0` then reboot staying off; BOOT button on the node.

## 2026-11-17 — Gateway LED: board LED for packets only
- Request: gateway board LED should flash only when a packet is received; other states on the red LED.
- Actions: `status_led.cpp`/`status_led.h`: gateway GPIO2 lights only for 200 ms per received packet; crash/Wi-Fi/portal/page patterns moved to new `kStatusPin` GPIO25 (external LED, active-high). The on-board red LED is the power LED, not driven by firmware. Node unchanged.
- Build: gateway, gateway-test, node-test SUCCESS. Not flashed.
- Not verified: LED behaviour on hardware.

## 2026-11-17 — Gateway red LED is GPIO15 (schematic check)
- Request: user pointed out both colours of the common-anode LED go to signal pins.
- Checked `hardware/schematic-2026-10-18.png`: U21 "2LED common anode", anode 3.3 V, GREEN via R9 330 ohm to IO2, RED via R10 330 ohm to IO15. The earlier note that red is a power LED was wrong.
- Actions: `status_led.h`/`.cpp`: gateway status patterns now on red GPIO15 (active-low), GPIO25 external LED dropped; green GPIO2 = packets only.
- Build: gateway, gateway-test SUCCESS. Not flashed; not verified on hardware.

## 2026-11-17 — Flash gateway-test (LED split, OLED, NOISE/WIFI)
- Node-test flash confirmed by log `logs/device-monitor-261117-110715.log`: test STATUS every ~5.2 s; BOOT button turned Wi-Fi off and saved at 11:07:39; brown-out line at 11:13:56 just before the board disappeared from USB (likely unplug).
- Started flash.ps1 for gateway-test on COM3 in its own window.
- Not verified yet: flash result, LEDs, OLED.

## 2026-11-17 — Gateway-test flashed; BOOT button debounce
- Gateway log `logs/device-monitor-261117-111644.log`: v0.5.4 gateway-test boots, LoRa1 OK / LoRa2 FAIL (single-radio), OLED none at 0x3C, Wi-Fi TMT Electronic 192.168.43.40, ThingsBoard connected. Node packets seq 0-9: RSSI -10..-13 dBm, SNR 7.5-9.5 dB, FERR about -620..-720 Hz, PDR 100%. "BOOT button: Wi-Fi off" at 11:17:53 (saved): gateway Wi-Fi now off; field web page unreachable until turned on.
- Both boards logged a BOOT-button Wi-Fi off (node 11:07:39, gateway 11:17:53); user press not confirmed. `field_oled.cpp`: press must now stay low 80 ms (was: one low reading after 50 ms).
- Build: gateway-test, node-test SUCCESS. Debounce change not flashed.
- 2026-11-17: user confirmed both BOOT-button Wi-Fi offs were real presses (no glitch). Debounce change kept (80 ms hold), not urgent to flash.

## 2026-11-17 — Rebuild and flash gateway-test (debounce)
- Built gateway-test and gateway: SUCCESS (RAM 24.2%, flash 82.4%). Started flash.ps1 gateway-test on COM3 in its own window.
- Close-range log (11:19-11:21): RSSI -2..-13 dBm, a few at -50/-59 dBm (antenna moved/covered), PDR 100%.
- Not verified yet: flash result.
- 13:32: mistake: the board on COM3 was the node; gateway-test was flashed onto it (log `device-monitor-261117-133200.log`: Role Gateway, IP 192.168.43.31, connected to ThingsBoard with the gateway token, then Wi-Fi off by BOOT). Started flash.ps1 node-test on COM3 to restore it.
