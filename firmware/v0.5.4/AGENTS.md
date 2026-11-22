# v0.5.4

## Nạp firmware

- Luôn nạp bằng `powershell -ExecutionPolicy Bypass -File scripts/flash.ps1 -Env <env> -Port COM<n>` (từ thư mục gốc repo). Script tự thử lại ở 115200 nếu 921600 lỗi và **mở Serial monitor trong cửa sổ terminal riêng** sau khi nạp (log vào `logs/device-monitor-*.log`).
- Mỗi lần nạp chỉ một terminal: `flash.ps1` tự đóng các cửa sổ nạp/monitor cũ (và monitor đang giữ cổng) trước khi nạp; nạp xong mở monitor mới rồi tự đóng cửa sổ nạp. Lỗi thì chờ Enter rồi đóng.
- Agent chạy `flash.ps1` **trong cửa sổ terminal riêng, không có `-NoExit`** (`Start-Process powershell -ArgumentList '-ExecutionPolicy','Bypass','-File','scripts\flash.ps1','-Env',<env>,'-Port',<COMn>`), không chạy ẩn trong tool: người dùng cần thấy lời nhắc "giữ BOOT, nhấn-thả EN" và bấm Enter; chạy ẩn thì Read-Host bỏ qua, board không vào chế độ nạp → "No serial data received".
- Không nạp bằng `pio run -t upload` trực tiếp. Nếu buộc phải nạp cách khác, nạp xong phải mở terminal monitor: `scripts/monitor.ps1 -Env <env> -Port COM<n>` trong cửa sổ mới.
- Cổng COM đang bị monitor giữ: đọc log trong `logs/`, không tắt cửa sổ monitor của người dùng.
- Board không cắm USB (nguồn rời): đọc log qua Wi-Fi — `python scripts/netlog.py <IP> --seconds 60` (IP: `nodeIp` trên ThingsBoard hoặc dòng `Net log: ... (IP ...)`; tên `.local` không phân giải được trên PC này), log lưu ở `logs/net-*.log`. Cho người dùng: `scripts/netlog.ps1` mở cửa sổ riêng.

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
