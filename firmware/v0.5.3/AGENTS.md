# v0.5.3

<!-- esp32-wifi-provisioning -->
## WiFi ESP32

Firmware dùng WiFi trong workspace này theo skill `esp32-wifi-provisioning` (`D:\Personal-Skills\esp32-wifi-provisioning\SKILL.md`). Đọc skill đó trước khi sửa phần WiFi.

- Không ghi SSID/mật khẩu vào code, `platformio.ini`, tài liệu hay log. Mật khẩu chỉ nằm ở `D:\Secrets\wifi-credentials.json` (ngoài git) và `include/wifi_secrets.h` (sinh tự động, có trong `.gitignore`).
- Module kết nối: `include/wifi_nvs.h` + `src/wifi_nvs.cpp`. Gọi `wifinvs::begin()` trong `setup()` và `wifinvs::loop()` trong `loop()`.
- Trước mỗi lần nạp (board đã cắm): đọc mạng đã nhập trên board → sinh lại file mạng → build → upload:
  - `powershell -ExecutionPolicy Bypass -File D:\Personal-Skills\esp32-wifi-provisioning\scripts\pull-wifi-from-board.ps1 -Port COM<n>`
  - `powershell -ExecutionPolicy Bypass -File D:\Personal-Skills\esp32-wifi-provisioning\scripts\make-wifi-secrets.ps1 -Project <thư mục có platformio.ini>`
- Dự án đã bật WiFi: `.` <!-- wifi-projects -->
<!-- /esp32-wifi-provisioning -->
