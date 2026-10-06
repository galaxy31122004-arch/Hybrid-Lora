# Hybrid LoRa — Algorithm & Node Task Plan

## Mục tiêu
Node điều khiển và giám sát đèn; Gateway quản lý nhiều Node; Gateway kết nối Wi-Fi/Internet; Node vẫn hoạt động local khi mất kết nối trung tâm.

## Kiến trúc LoRa V1

- **Node:** 1 × SX1278 LoRa.
- **Gateway:** 2 × SX1278 LoRa.
- ESP32 Gateway quản lý đồng thời hai radio.
- Hai LoRa tại Gateway được thiết kế để tách nhiệm vụ RX/TX nhằm giảm thời gian chờ đổi trạng thái radio.
- Không giả định hai radio có thể TX/RX đồng thời trên cùng tần số mà không gây nhiễu; việc chọn channel/tần số và RF isolation phải được kiểm thử thực tế.

### Vai trò Gateway V1

```text
                 GATEWAY
              ESP32 + Wi-Fi
                    |
          +---------+---------+
          |                   |
       LoRa 1              LoRa 2
       PRIMARY              TX
       RX Node            Command
          |                   |
          +---------+---------+
                    |
              Node 1-LoRa
```

V1 ưu tiên mô hình **LoRa 1 = RX / LoRa 2 = TX** ở mức logic. Nếu PHY/channel cuối cùng yêu cầu khác, có thể đổi vai trò bằng cấu hình mà không thay đổi packet format.

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

### N4 — Hybrid / Local
Tách **Operating Mode** khỏi **Communication Status**:
- LOCAL + ONLINE: chạy local logic
- LOCAL + OFFLINE: vẫn chạy local logic
- REMOTE + ONLINE: Gateway/server điều khiển
- REMOTE + OFFLINE: chuyển sang fallback/local policy
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

### G1 — Dual-LoRa manager
Gateway có hai radio nhưng **một bộ điều phối chung**:
- LoRa 1: ưu tiên RX Node
- LoRa 2: ưu tiên TX COMMAND/ACK-related traffic
- Không để hai radio tự chạy độc lập không kiểm soát
- ESP32 duy trì queue TX và bảng trạng thái Node

### G2 — RX pipeline
Luồng nhận:

```text
LoRa RX
  -> packet available?
  -> read packet
  -> length check
  -> CRC check
  -> parse TYPE/NODE_ID/SEQ
  -> update Node table
  -> route STATUS/HEARTBEAT/ACK
  -> tạo task/response nếu cần
```

CRC sai thì bỏ packet, không tạo ACK.

### G3 — TX scheduler
Ưu tiên:
1. COMMAND đang chờ retry/ACK
2. COMMAND mới từ server
3. các phản hồi/traffic điều khiển cần thiết
4. telemetry định kỳ nếu có

Không gửi STATUS/HEARTBEAT của tất cả Node liên tục nếu không cần thiết.

### G4 — Node table
Gateway lưu tối thiểu:
- NODE_ID
- Last Seen
- Online/Offline
- LIGHT_STATE
- MODE
- LUX
- Fault flags
- RSSI/SNR nếu cần

### G5 — Reliability
- COMMAND có ACK.
- Gateway giữ pending command theo NODE_ID + SEQ.
- Timeout thì retry cùng COMMAND/SEQ.
- Tối đa 3 retry ở V1.
- ACK đúng SEQ thì kết thúc transaction.
- Duplicate COMMAND ở Node không được thực thi lần hai.

## Gateway main flow

Không dùng một state machine khổng lồ. Dùng scheduler/non-blocking loop:

```text
INIT
  |
SELF_CHECK
  |
INIT LoRa1 + LoRa2 + Wi-Fi
  |
RUN
  |
  +--> Check LoRa1 RX
  |       |
  |       +--> Process packet
  |
  +--> Check pending ACK/timeout
  |
  +--> Check server command queue
  |
  +--> Check LoRa2 TX queue
  |
  +--> Update node online timeout
  |
  +--> Periodic telemetry/server update
  |
  +--> repeat
```

Không dùng `delay()` dài trong flow chính vì sẽ làm tăng latency và làm Gateway bỏ lỡ cơ hội xử lý packet.

## Gateway TX queue

Mỗi COMMAND nên có transaction:

```text
CREATE COMMAND
   |
assign SEQ
   |
queue to LoRa2
   |
TX
   |
wait ACK
   |
 +---------+
 |         |
ACK       timeout
 |         |
DONE    retry <= 3
             |
          timeout
             |
           FAIL
```

## Multi-node scheduling

Với nhiều Node, không cho tất cả Node truyền STATUS cùng thời điểm.

V1 có thể dùng:
- Heartbeat lệch pha theo NODE_ID.
- STATUS định kỳ lệch pha.
- Gateway điều phối COMMAND theo queue.
- Khi mạng lớn hơn, bổ sung time-slot/polling hoặc random backoff.

Mục tiêu là giảm collision và giảm tổng Time-on-Air của mạng.

## Receive algorithm
RX packet → check length → CRC → NODE_ID → SEQ → validate TYPE/DATA → update state → response/ACK nếu cần.

CRC sai: discard, không ACK.
SEQ trùng: không execute lại, resend ACK nếu command yêu cầu.

## Reliability
Command quan trọng cần ACK. Timeout chọn sau khi đo Time-on-Air thực tế. Retry tối đa 3 lần là giá trị khởi đầu. STATUS/HEARTBEAT định kỳ không nhất thiết ACK từng packet.

## Tối ưu LoRa

Ưu tiên:

```text
ít lần truyền không cần thiết
        ↓
payload binary vừa đủ
        ↓
không lặp field
        ↓
scheduler tránh collision
        ↓
chọn SF/BW/CR phù hợp
```

Không tách một STATUS thành nhiều packet chỉ để giảm vài byte. Với mạng nhiều Node, tổng airtime và số lần chiếm kênh quan trọng hơn việc tối thiểu hóa từng packet riêng lẻ.

## Chưa chốt
- Sensor list cuối cùng
- POWER_STATUS: có điện áp hay đèn thực sự có dòng
- Chu kỳ STATUS/HEARTBEAT
- SF/BW/CR
- ACK timeout dựa trên ToA đo thực tế
- Channel/tần số riêng cho LoRa 1 và LoRa 2 nếu cần
- Cơ chế chống collision cuối cùng cho nhiều Node
