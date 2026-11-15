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
