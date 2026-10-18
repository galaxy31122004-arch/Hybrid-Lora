# Hybrid LoRa — Task Breakdown

## Phase A — Architecture
- [ ] Chốt Node/Gateway responsibilities
- [ ] Chốt Node state machine
- [ ] Chốt Gateway state machine
- [ ] Chốt sensor list
- [ ] Chốt local fallback behavior
- [ ] Chốt pinout từ schematic CAD và kiểm tra GPIO boot strap
- [ ] Chốt cách khởi tạo RTC và dữ liệu sensor khi lỗi

## Phase B — Communication
- [ ] LoRa initialization
- [ ] Packet encode/decode
- [ ] CRC16
- [ ] SEQ
- [ ] ACK
- [ ] Timeout/retry
- [ ] Duplicate packet handling
- [ ] Corrupted packet handling
- [ ] Chốt tín hiệu Gateway định kỳ cho Node phát hiện mất link
- [ ] Chốt phương án xác thực COMMAND trước triển khai thực tế

## Phase C — Node
- [ ] Relay/contactor control abstraction
- [ ] Sensor manager
- [ ] Local control
- [ ] Remote control
- [ ] Fallback
- [ ] Heartbeat
- [ ] Status reporting
- [ ] Fault flags

## Phase D — Gateway
- [ ] Node table
- [ ] Last Seen timeout
- [ ] LoRa RX/TX manager
- [ ] Một bộ điều phối SPI cho hai radio và đo nhiễu TX/RX trên PCB
- [ ] Server bridge
- [ ] Command queue/priority
- [ ] Online/offline tracking

## Phase E — Optimization
- [ ] Measure LoRa Time-on-Air
- [ ] Compare payload sizes
- [ ] Compare one combined STATUS vs multiple packets
- [ ] Measure packet loss
- [ ] Measure ACK/retry rate
- [ ] Tune heartbeat/status intervals
- [ ] Tune SF/BW/CR

## Phase F — Hybrid validation
- [ ] Remote control test
- [ ] Local control test
- [ ] Internet disconnect test
- [ ] Gateway disconnect test
- [ ] LoRa packet loss test
- [ ] Recovery/synchronization test
- [ ] Long-duration stability test
