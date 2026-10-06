# Hybrid LoRa — Packet Design

## V1 scope

Current firmware phase uses:
- ESP32
- 2x SX1278 LoRa
- DS3231 RTC
- BH1750 light sensor

The V1 protocol is intentionally compact to reduce LoRa airtime.

## Design principles

1. Prefer binary fields over ASCII.
2. Combine related telemetry into one STATUS packet instead of splitting it into multiple packets.
3. Keep COMMAND, ACK, and HEARTBEAT as small as practical.
4. Do not add fields only for future flexibility unless they provide a clear V1 benefit.
5. CRC-16 is retained for packet integrity.
6. V1 does **not** include a protocol-version byte (VER) to save one byte from every packet.

## Common frame

```text
TYPE | NODE_ID | SEQ | LEN | PAYLOAD | CRC16
```

| Field | Size | Description |
|---|---:|---|
| TYPE | 1 B | Packet/message type |
| NODE_ID | 1 B | Source Node identifier |
| SEQ | 1 B | Sequence number |
| LEN | 1 B | Payload length in bytes |
| PAYLOAD | N B | Type-specific binary data |
| CRC16 | 2 B | CRC-16-CCITT over TYPE through PAYLOAD |

Total frame size:

```text
4 + LEN + 2 = 6 + LEN bytes
```

### Integer byte order

All multi-byte integer fields use **little-endian**.

Examples:

```text
uint16_t 0x1234 -> 34 12
uint32_t 0x12345678 -> 78 56 34 12
```

### CRC16

V1 uses:
- Algorithm: CRC-16-CCITT
- Polynomial: 0x1021
- Initial value: 0xFFFF
- Coverage: all bytes from TYPE through the final PAYLOAD byte
- CRC field itself is excluded from the CRC calculation
- CRC is transmitted little-endian

## Message types

| TYPE | Name | Direction | Purpose | ACK |
|---|---|---|---|---|
| 0x01 | COMMAND | Gateway -> Node | Remote control command | Yes |
| 0x02 | ACK | Node -> Gateway | Confirm COMMAND processing | No |
| 0x03 | STATUS | Node -> Gateway | Report Node state + sensor data | No |
| 0x04 | HEARTBEAT | Node -> Gateway | Report Node alive/health | No |

No CONFIG or ERROR packet is included in V1.

## SEQ

SEQ is one byte (uint8_t) and wraps:

```text
0xFE -> 0xFF -> 0x00 -> 0x01
```

For V1, the Gateway uses SEQ to match COMMAND/ACK and the Node uses it to detect duplicate COMMAND packets.

## COMMAND

Payload size: **2 bytes**

```text
Byte 0: CMD
Byte 1: PARAM
```

Commands:

| CMD | Name | PARAM |
|---|---|---|
| 0x01 | LIGHT_ON | 0x00 |
| 0x02 | LIGHT_OFF | 0x00 |
| 0x03 | SET_MODE | 0x00=LOCAL, 0x01=REMOTE |
| 0x04 | REQUEST_STATUS | 0x00 |

Full packet:

```text
TYPE NODE_ID SEQ LEN CMD PARAM CRC_L CRC_H
```

Total: **8 bytes**.

Example for Node 3, SEQ 25, LIGHT_ON:

```text
01 03 19 02 01 00 CRC_L CRC_H
```

## ACK

Payload size: **2 bytes**

```text
Byte 0: ACK_SEQ
Byte 1: RESULT
```

Result codes:

| RESULT | Meaning |
|---|---|
| 0x00 | SUCCESS |
| 0x01 | UNKNOWN_COMMAND |
| 0x02 | INVALID_PARAM |
| 0x03 | EXECUTION_ERROR |

Full packet:

```text
TYPE NODE_ID SEQ LEN ACK_SEQ RESULT CRC_L CRC_H
```

Total: **8 bytes**.

ACK_SEQ identifies the COMMAND being acknowledged. The ACK own SEQ may be incremented independently.

## STATUS

STATUS is the main telemetry packet.

### Timestamp decision

Use a **4-byte Unix timestamp** instead of six separate date/time bytes.

Reason:
- saves 2 bytes;
- simple numerical time comparison;
- easy calculation of elapsed time;
- convenient for Gateway/server;
- sufficient for the project lifetime.

TIMESTAMP is an unsigned uint32_t Unix epoch time in seconds. This representation is valid through 2106, which is more than sufficient for this project.

### Payload

```text
Byte 0-3: TIMESTAMP  uint32_t
Byte 4-5: LUX        uint16_t
Byte 6:   LIGHT_STATE
Byte 7:   MODE
```

Values:

```text
LIGHT_STATE:
0x00 = OFF
0x01 = ON

MODE:
0x00 = LOCAL
0x01 = REMOTE
```

NODE_ID is already present in the common header and is **not duplicated inside STATUS**.

Payload size: **8 bytes**

Full packet:

```text
TYPE NODE_ID SEQ LEN TIMESTAMP[4] LUX[2] STATE MODE CRC16
```

Total:

```text
1 + 1 + 1 + 1 + 8 + 2 = 14 bytes
```

## HEARTBEAT

HEARTBEAT is intentionally small.

Payload size: **1 byte**

```text
Byte 0: STATUS_FLAGS
```

Flags:

```text
bit 0 = LoRa communication OK
bit 1 = DS3231 OK
bit 2 = BH1750 OK
bit 3 = Control/output OK
bit 4-7 = Reserved
```

Full packet:

```text
TYPE NODE_ID SEQ LEN STATUS_FLAGS CRC16
```

Total: **7 bytes**.

## Reliability rules

1. COMMAND uses ACK.
2. If ACK is not received before timeout, Gateway may retry the same COMMAND.
3. Initial maximum retry count: 3.
4. Retry timing must be validated using actual LoRa Time-on-Air.
5. If CRC fails, discard the packet and do not ACK.
6. If a COMMAND with an already-processed SEQ is received, do not execute it again; resend the previous ACK.
7. STATUS and HEARTBEAT do not require ACK in V1.
8. STATUS is best-effort; a later STATUS refreshes Gateway state.

## Hybrid communication consideration

V1 HEARTBEAT is defined as Node -> Gateway.

For Node-side REMOTE -> FALLBACK detection, the Node must also observe valid Gateway traffic. The implementation should use a Gateway heartbeat/beacon or valid Gateway packet activity as the liveness signal rather than relying only on Node -> Gateway HEARTBEAT.

The exact fallback timeout will be selected after measuring the actual LoRa communication interval and network behavior.

## Packet size summary

| Packet | Payload | Total |
|---|---:|---:|
| COMMAND | 2 B | **8 B** |
| ACK | 2 B | **8 B** |
| STATUS | 8 B | **14 B** |
| HEARTBEAT | 1 B | **7 B** |

This keeps frequent control/health packets short while keeping all STATUS information in one transmission.

## Optimization note

The main V1 optimization target is not merely the smallest possible payload. It is:

```text
fewer transmissions
+
short binary payloads
+
no duplicated fields
+
appropriate LoRa PHY settings
```

Actual Time-on-Air depends on SF, BW, CR, preamble, explicit/implicit header mode, CRC setting, and payload length. These parameters must be measured/calculated together before selecting the final PHY configuration.

## Next step

Implement:
1. packet structs;
2. encode/decode;
3. CRC-16-CCITT;
4. COMMAND/ACK reliability;
5. STATUS/HEARTBEAT handling;
6. LoRa Time-on-Air calculation for SF7-SF12.
