# Hybrid LoRa — Packet Design

## V1 scope

Current firmware phase uses:
- ESP32
- 2x SX1278 LoRa
- DS3231 RTC
- BH1750 light sensor

No additional sensors are included in the V1 packet definition.

## Common frame

```
VER | TYPE | NODE_ID | SEQ | PAYLOAD | CRC16
```

Fields:
- **VER**: protocol version.
- **TYPE**: packet/message type.
- **NODE_ID**: source or destination Node identifier.
- **SEQ**: sequence number used for duplicate detection and ACK matching.
- **PAYLOAD**: type-specific data.
- **CRC16**: packet integrity check.

The exact byte widths and CRC-16-CCITT implementation will be finalized before firmware implementation.

## Message types

V1 defines four packet types:

| TYPE | Name | Direction | Purpose | ACK |
|---|---|---|---|---|
| 0x01 | COMMAND | Gateway -> Node | Remote control/configuration command | Yes |
| 0x02 | ACK | Node -> Gateway | Confirm COMMAND reception/processing | No |
| 0x03 | STATUS | Node -> Gateway | Report current Node state and sensor data | No |
| 0x04 | HEARTBEAT | Node -> Gateway | Confirm Node is alive / communication health | No |

Additional packet types such as CONFIG and ERROR are deferred until required.

## COMMAND

COMMAND is a reliable packet.

Candidate V1 commands:
- `LIGHT_ON`
- `LIGHT_OFF`
- `SET_MODE_LOCAL`
- `SET_MODE_REMOTE`
- `REQUEST_STATUS`

A COMMAND must carry a sequence number. The Node executes a new sequence number once and returns an ACK.

If the Gateway retries the same command with the same sequence number, the Node must not execute the command again; it should resend the ACK.

## ACK

ACK confirms a COMMAND.

Conceptual payload:

```
ACK_SEQ | RESULT
```

Where:
- **ACK_SEQ** identifies the COMMAND being acknowledged.
- **RESULT** indicates success or failure.

The exact result codes will be defined with the firmware protocol.

## STATUS

STATUS is the main periodic telemetry packet.

**DS3231 and BH1750 are intentionally combined in one STATUS packet.** They are not sent as separate sensor packets because splitting them would increase the number of LoRa transmissions and therefore airtime.

V1 STATUS data:

| Field | Source | Purpose |
|---|---|---|
| NODE_ID | Node | Identify source |
| TIMESTAMP | DS3231 | Current date/time |
| LUX | BH1750 | Ambient light level |
| LIGHT_STATE | Node control | Current lamp state |
| MODE | Node control | LOCAL or REMOTE |

STATUS is periodic/best-effort. Losing one STATUS packet is not critical because a later STATUS packet can refresh the Gateway state.

## HEARTBEAT

HEARTBEAT is intentionally smaller than STATUS.

Purpose:
- prove that a Node is still communicating;
- allow Gateway to detect an offline Node;
- support REMOTE -> FALLBACK transition logic.

HEARTBEAT should not duplicate the complete sensor payload.

## Reliability rules

1. COMMAND uses ACK.
2. If ACK is not received before timeout, Gateway may retry the same COMMAND.
3. Retry count is initially limited to 3; timing will be validated against actual LoRa Time-on-Air.
4. CRC failure: discard packet and do not ACK.
5. Duplicate COMMAND with the same sequence number: do not execute again; resend ACK.
6. STATUS and HEARTBEAT do not require ACK in V1.

## Binary encoding

Avoid text payloads such as:

```
NODE=05,LUX=123,RELAY=ON
```

Use compact binary fields instead to reduce payload size, airtime, and parsing overhead.

## Optimization rule

Do not split one logical STATUS into multiple sensor packets.

Preferred approach:

```
one STATUS = timestamp + lux + lamp state + mode
```

The final Time-on-Air optimization must consider SF, BW, CR, preamble, header mode, CRC, and payload length together.

## Next step

Before writing the protocol implementation, define the exact byte layout for:
1. COMMAND
2. ACK
3. STATUS
4. HEARTBEAT

Then implement encode/decode and CRC16 before integrating the Hybrid control state machine.
