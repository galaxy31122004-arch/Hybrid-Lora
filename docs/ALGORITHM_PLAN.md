# Hybrid LoRa — Algorithm & Node Task Plan

## Mục tiêu
Node điều khiển và giám sát đèn; Gateway quản lý nhiều Node; Gateway kết nối Wi-Fi/Internet; Node vẫn hoạt động local khi mất kết nối trung tâm.

## Công việc chính của Node

### N1 — Khởi tạo
- GPIO relay/contactor driver
- LoRa
- Bus cảm biến
- NODE_ID và ROLE
- Self-check phần cứng

### N2 — Điều khiển đèn
- Nhận ON/OFF từ Gateway
- Validate command
- Thực thi và cập nhật relay state
- Không thực thi lại command trùng SEQ

### N3 — Cảm biến
- Đọc cảm biến theo chu kỳ
- Lưu giá trị mới nhất
- Chỉ đưa dữ liệu cần thiết vào STATUS
- Chưa khóa payload trước khi chốt sensor list

### N4 — Hybrid / Local
- LOCAL: logic cục bộ
- REMOTE: nhận điều khiển từ Gateway
- FALLBACK: mất liên lạc thì dùng local policy
- Link phục hồi: đồng bộ trạng thái rồi nhận command mới

### N5 — LoRa manager
- RX/TX
- CRC16
- SEQ chống duplicate
- ACK cho command quan trọng
- Timeout/retry giới hạn
- CRC lỗi thì không ACK

### N6 — Heartbeat / Status
- Heartbeat định kỳ để Gateway biết Node còn online
- STATUS chứa relay state và sensor/fault cần thiết
- Ưu tiên một packet vừa đủ thay vì nhiều packet nhỏ

### N7 — Fault handling
- Phát hiện lỗi sensor/LoRa/driver
- Fault flags
- Trạng thái an toàn đã định nghĩa
- Báo lỗi khi có thể

## Công việc chính của Gateway
- Khởi tạo LoRa và Wi-Fi
- Bảng Node: NODE_ID, Last Seen, Online/Offline, relay state, fault, RSSI/SNR nếu cần
- Bridge LoRa ↔ server
- Forward STATUS/HEARTBEAT
- Nhận COMMAND từ server và gửi xuống Node
- Retry command khi cần
- Timeout heartbeat/status để đánh dấu OFFLINE
- Ưu tiên command điều khiển hơn telemetry định kỳ

## Node state machine
INIT → SELF_CHECK → LOCAL/REMOTE
REMOTE + mất liên lạc → FALLBACK
FALLBACK + link phục hồi → REMOTE sau khi sync
Lỗi nghiêm trọng → FAULT
Timer → READ SENSOR → STATUS
Timer → HEARTBEAT
Command → VALIDATE → EXECUTE → ACK

## Receive algorithm
RX packet → check length → CRC → NODE_ID → SEQ → validate TYPE/DATA → execute → ACK nếu cần.

CRC sai: discard, không ACK.
SEQ trùng: không execute lại, resend ACK nếu command yêu cầu.

## Reliability
Command quan trọng cần ACK. Timeout chọn sau khi đo Time-on-Air thực tế. Retry tối đa 3 lần là giá trị khởi đầu. STATUS/HEARTBEAT định kỳ không nhất thiết ACK từng packet.

## Tối ưu LoRa
Ưu tiên: ít lần truyền không cần thiết → payload vừa đủ ngắn → binary thay vì ASCII → gom dữ liệu STATUS liên quan → COMMAND thật ngắn.

Không tách một STATUS thành nhiều packet chỉ để giảm vài byte. Với mạng nhiều Node, thời gian chiếm kênh và tổng số lần truyền quan trọng hơn tối thiểu hóa từng packet riêng lẻ.

## Chưa chốt
- Sensor list cuối cùng
- POWER_STATUS: có điện áp hay đèn thực sự có dòng
- STATUS payload cuối cùng
- Chu kỳ STATUS/HEARTBEAT
- SF/BW/CR
- ACK timeout dựa trên ToA đo thực tế
