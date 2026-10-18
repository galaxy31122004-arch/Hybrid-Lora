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
| NODE_ID | 1 B | Destination Node for COMMAND/BEACON; source Node for ACK, STATUS, HEARTBEAT |
| SEQ | 1 B | Sequence number |
| LEN | 1 B | Payload length in bytes |
| PAYLOAD | N B | Type-specific binary data |
| CRC16 | 2 B | CRC-16-CCITT over TYPE through PAYLOAD |

Total frame size:

```text
4 + LEN + 2 = 6 + LEN bytes
```

V1 assigns Node IDs `0x01` through `0xFE`. `0x00` and `0xFF` are reserved and
must be rejected as Node IDs. Broadcast control is not defined in V1: a Gateway
must send a separate COMMAND to each destination and track each ACK. A later
broadcast design must define its addressing, collision policy, and ACK behavior
before using `0xFF`.

### Frame validation

The receiver checks the physical frame length before reading fields. A V1
frame has at least 6 bytes and must have exactly `6 + LEN` bytes. Known types
have fixed payload lengths: COMMAND=2, ACK=2, STATUS=8, HEARTBEAT=1, BEACON=0. The
largest valid V1 payload is therefore 8 bytes. Reject unknown TYPE, invalid
NODE_ID, a LEN that differs from the type's fixed length, or an invalid CRC
before dispatching the packet. These are V1 protocol limits, not a claim about
the radio FIFO size.

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
- RefIn: false; RefOut: false; XorOut: 0x0000
- Coverage: all bytes from TYPE through the final PAYLOAD byte
- CRC field itself is excluded from the CRC calculation
- CRC is transmitted little-endian
- Check vector: ASCII `123456789` produces CRC `0x29B1`

## Message types

| TYPE | Name | Direction | Purpose | ACK |
|---|---|---|---|---|
| 0x01 | COMMAND | Gateway -> Node | Remote control command | Yes |
| 0x02 | ACK | Node -> Gateway | Confirm COMMAND processing | No |
| 0x03 | STATUS | Node -> Gateway | Report Node state + sensor data | No |
| 0x04 | HEARTBEAT | Node -> Gateway | Report Node alive/health | No |
| 0x05 | BEACON | Gateway -> Node | Prove Gateway liveness | No |

No CONFIG or ERROR packet is included in V1.

BEACON has an empty payload and is sent individually to each Node. The V1
starting schedule is one BEACON every 30 seconds per Node. A Node enters
REMOTE fallback after 90 seconds without a valid addressed BEACON (three
periods); these are starting values to validate against measured airtime and
collision behavior. BEACON is not acknowledged.

## SEQ

SEQ is one byte (uint8_t) and wraps:

```text
0xFE -> 0xFF -> 0x00 -> 0x01
```

For V1, the Gateway uses SEQ to match COMMAND/ACK and the Node uses it to detect duplicate COMMAND packets.

The Gateway matches an ACK by both source NODE_ID and ACK_SEQ against a
pending COMMAND. SEQ is not authentication or replay protection; the
one-byte counter wraps and can restart after a device reboot.

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

Here `03` is the destination Node, not the Gateway's source ID.

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

For example, Node 3 can answer the COMMAND above with its own SEQ=10:

```text
02 03 0A 02 19 00 CRC_L CRC_H
```

The Gateway matches `NODE_ID=03` and `ACK_SEQ=19`, then reads RESULT=00.

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

The RTC must be initialized before its timestamp is reported as valid. The
method of setting DS3231 time and representing an unavailable timestamp in
STATUS are open design decisions; do not infer clock validity from a plausible
numeric timestamp alone.

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

LUX is illuminance in whole lux. Values above `0xFFFE` saturate at `0xFFFE`;
`0xFFFF` means BH1750 data is unavailable. A missing/invalid RTC is encoded as
TIMESTAMP=0 and bit 1 of HEARTBEAT (`DS3231 OK`) is cleared. `LIGHT_STATE`
reports the commanded output state in V1; contact feedback is not present in
the current schematic and must not be inferred from this field.

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

## BEACON

BEACON has payload size **0 bytes**. Its `NODE_ID` is the destination Node and
its `SEQ` is a Gateway-origin sequence counter. A Node refreshes its Gateway
liveness deadline only after validating the complete frame and matching its
own `NODE_ID`.

```text
TYPE NODE_ID SEQ LEN CRC16
```

Total: **6 bytes**.

## Reliability rules

1. COMMAND uses ACK.
2. If ACK is not received before timeout, Gateway may retry the same COMMAND.
3. Initial maximum retry count: 3.
4. Retry timing must be validated using actual LoRa Time-on-Air.
5. If CRC fails, discard the packet and do not ACK.
6. For a retry of the most recently processed COMMAND with the same SEQ and payload, do not execute it again; resend the previous ACK. A repeated SEQ with different payload is a protocol error, not a retry.
7. STATUS, HEARTBEAT, and BEACON do not require ACK in V1.
8. STATUS is best-effort; a later STATUS refreshes Gateway state.

## Hybrid communication consideration

V1 HEARTBEAT is defined as Node -> Gateway. V1 BEACON is defined as Gateway
-> Node and is the liveness signal used by a Node in REMOTE mode.

For Node-side REMOTE -> FALLBACK detection, the Node observes addressed
BEACON frames. COMMAND traffic may refresh the same deadline when valid, but
does not replace the periodic BEACON schedule. The starting 30/90 second
periods above remain subject to hardware validation.

Valid Gateway COMMAND traffic alone is insufficient as a liveness signal when
remote control can be idle for longer than the fallback timeout. A periodic
Gateway-origin signal, its packet type/addressing, and its schedule must be
defined before REMOTE -> FALLBACK can be implemented reliably. Node-origin
HEARTBEAT only proves Node liveness to the Gateway.

## Security boundary

V1 CRC detects accidental corruption but does not authenticate the sender.
SEQ and the duplicate cache do not prevent deliberate replay. A secret CRC
seed or one-byte key would not solve this. Remote control over an untrusted
radio channel needs a specified authenticated protocol before deployment;
the current V1 frame describes prototype communication only.

## Packet size summary

| Packet | Payload | Total |
|---|---:|---:|
| COMMAND | 2 B | **8 B** |
| ACK | 2 B | **8 B** |
| STATUS | 8 B | **14 B** |
| HEARTBEAT | 1 B | **7 B** |
| BEACON | 0 B | **6 B** |

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
6. BEACON scheduling and fallback handling;
7. LoRa Time-on-Air calculation for SF7-SF12.
