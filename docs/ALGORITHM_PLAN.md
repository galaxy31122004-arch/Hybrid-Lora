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

Hai radio dùng chung SPI theo schematic: MOSI/MISO/SCK chung, CS riêng. Một
bộ điều phối sở hữu bus, chỉ cho phép một SPI transaction tại một thời điểm;
ISR radio chỉ báo sự kiện để bộ điều phối xử lý. Giữ CS của cả hai radio ở
mức không chọn khi khởi tạo. Việc LoRa 2 TX có làm LoRa 1 mất RX hay không
phải đo trên PCB thật trước khi cho phép hai radio hoạt động đồng thời.
Nếu chọn hai channel khác nhau, Node một-radio còn cần lịch chuyển channel
và cửa sổ nghe COMMAND; chưa có lịch này trong V1.

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
- Lưu `last_SEQ`, payload cuối và `last_RESULT`; chỉ resend ACK khi cả SEQ
  và payload trùng command gần nhất. Trùng SEQ khác payload là protocol error.

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

Communication Status ở Node chỉ được cập nhật từ packet Gateway hợp lệ.
HEARTBEAT Node -> Gateway không chứng minh Gateway còn online với Node.
V1 dùng BEACON addressed (TYPE 0x05, payload 0) làm liveness Gateway -> Node:
lịch khởi đầu một BEACON mỗi 30s cho mỗi Node; Node vào REMOTE fallback sau
90s không nhận BEACON hợp lệ đúng địa chỉ (ba kỳ liên tiếp). COMMAND hợp lệ
có thể refresh cùng deadline nhưng không thay thế lịch BEACON định kỳ; lịch
30s/90s phải đo và xác nhận lại trên phần cứng. Khi timeout,
giữ Operating Mode=REMOTE nhưng chạy fallback policy. Khi link phục hồi,
Node báo STATUS trạng thái thực tế; Gateway đối chiếu trạng thái đó trước
khi áp dụng command cũ còn trong queue. Quy tắc chuyển đèn cụ thể khi vào
fallback vẫn phải chốt ở Phase A.

### N5 — LoRa manager
- RX/TX
- CRC16
- SEQ chống duplicate
- ACK cho command quan trọng
- Timeout/retry giới hạn
- CRC lỗi thì không ACK

### N6 — Heartbeat / Status
- Heartbeat định kỳ để Gateway biết Node còn online
- STATUS chứa relay state và lux; HEARTBEAT mang các cờ sức khỏe đã định nghĩa
- BH1750 lỗi: STATUS.LUX=`0xFFFF` và clear bit `BH1750 OK`.
- DS3231 chưa set/lỗi: STATUS.TIMESTAMP=0 và clear bit `DS3231 OK`.
- `LIGHT_STATE` chỉ là commanded output state ở V1; không được coi là tiếp
  điểm relay thực tế vì schematic chưa có mạch feedback.
- Ưu tiên một packet vừa đủ thay vì nhiều packet nhỏ

### N7 — Fault handling
- Phát hiện lỗi sensor/LoRa/driver
- Fault flags: DS3231/BH1750/LoRa/output map vào HEARTBEAT STATUS_FLAGS
- Trạng thái an toàn đã định nghĩa
- Báo lỗi khi có thể

NODE_ID được nạp cố định trong firmware/configuration khi commissioning, chỉ
dùng `0x01..0xFE`. Gateway duy trì danh sách ID đã cấp và cảnh báo khi nhận
cùng một ID từ các nguồn RF không nhất quán (RSSI/SNR hoặc trạng thái thay
đổi bất thường); đây là cảnh báo commissioning, không phải cơ chế định danh
bảo mật. Không tự động cấp lại ID trên không trung trong V1.

## Công việc chính của Gateway

### G1 — Dual-LoRa manager
Gateway có hai radio nhưng **một bộ điều phối chung**:
- LoRa 1: ưu tiên RX Node
- LoRa 2: ưu tiên TX COMMAND/ACK-related traffic
- Không để hai radio tự chạy độc lập không kiểm soát
- ESP32 duy trì queue TX và bảng trạng thái Node
- Mọi thao tác SPI đi qua cùng một bộ điều phối; không cho hai task truy cập
  LoRa độc lập hoặc giữ mutex trong lúc chờ ACK qua vô tuyến

### G2 — RX pipeline
Luồng nhận:

```text
LoRa RX
  -> packet available?
  -> read packet
  -> require at least 6 bytes
  -> read LEN and require physical length == 6 + LEN
  -> check known TYPE and exact fixed LEN
  -> check NODE_ID in 0x01..0xFE
  -> CRC check
  -> dispatch valid packet
  -> update Node table only for STATUS/HEARTBEAT
  -> refresh BEACON liveness only for addressed BEACON
  -> close ACK transaction (ACK does not update Last Seen)
  -> tạo task/response nếu cần
```

CRC sai thì bỏ packet, không tạo ACK.

LoRa RSSI/SNR được đọc ngay sau khi nhận packet hợp lệ từ radio và gắn với
STATUS/HEARTBEAT trong Node table. Không dùng RSSI/SNR của packet lỗi để cập
nhật online state; ACK chỉ dùng để hoàn tất transaction đang chờ.

### G3 — TX scheduler
Ưu tiên:
1. COMMAND đang chờ retry/ACK
2. COMMAND mới từ server
3. các phản hồi/traffic điều khiển cần thiết
4. telemetry định kỳ nếu có

Không forward STATUS/HEARTBEAT của tất cả Node lên Wi-Fi/server liên tục;
LoRa RX vẫn phải tiếp nhận chúng. Uplink có rate limit/coalescing riêng.
Mỗi NODE_ID có tối đa một pending COMMAND. Lệnh mới cùng Node thay thế lệnh
cũ chưa TX; lệnh đã TX và đang chờ ACK được retry trước khi lệnh mới được
đưa vào. Các Node khác được phục vụ round-robin.
Queue Gateway BEACON có một mục lịch cho mỗi Node và không dùng chung queue
COMMAND retry.

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
- Kiểm tra cả NODE_ID nguồn, ACK_SEQ và RESULT; ACK lỗi kết thúc transaction
  với trạng thái lỗi, không báo thành công.
- Duplicate COMMAND ở Node không được thực thi lần hai.
- `REQUEST_STATUS` được xử lý ngay: Node đọc snapshot gần nhất, gửi STATUS
  trong cửa sổ TX kế tiếp và vẫn trả ACK SUCCESS cho COMMAND.

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
RX packet → require 6 bytes → read LEN → exact total length → known TYPE and
fixed LEN → NODE_ID 0x01..0xFE → CRC → dispatch. Chỉ STATUS/HEARTBEAT hợp lệ
cập nhật Node table; BEACON hợp lệ và đúng đích refreshes Gateway liveness;
ACK chỉ đóng pending transaction. Packet lỗi không được cập nhật Last Seen,
RSSI/SNR hay relay state.

CRC sai: discard, không ACK.
SEQ trùng: so sánh cả payload với command gần nhất; chỉ trùng cả hai mới
resend ACK, còn trùng SEQ khác payload thì báo protocol error và bỏ packet.

## Reliability
Command quan trọng cần ACK. Timeout chọn sau khi đo Time-on-Air thực tế. Retry tối đa 3 lần là giá trị khởi đầu. STATUS/HEARTBEAT định kỳ không nhất thiết ACK từng packet.

Nếu timer bắt đầu ngay trước khi gửi COMMAND, giá trị khởi đầu phải lớn hơn
`ToA_COMMAND + radio_turnaround + Node_processing + ToA_ACK + scheduling_jitter`.
Tính các thành phần theo SF/BW/CR/preamble và đo lại trên phần cứng; tăng
margin theo packet loss và độ trễ queue thực tế. Không chốt một số mili giây
chung cho mọi cấu hình PHY.

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
- Đo và xác nhận lịch BEACON 30s / fallback 90s
- Cách khởi tạo/đồng bộ DS3231, giá trị timestamp khi RTC lỗi
- Quy tắc làm tròn/giới hạn LUX và biểu diễn khi BH1750 lỗi
- Xác thực COMMAND trước khi triển khai trên kênh radio không tin cậy
- Cách nạp NODE_ID cố định `0x01..0xFE`, kiểm tra trùng ID khi commissioning
- Quy tắc relay feedback: commanded state hay contact feedback khi báo STATUS
