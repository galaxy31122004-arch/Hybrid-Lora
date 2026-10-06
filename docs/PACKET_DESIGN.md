# Hybrid LoRa — Packet Design

## Common frame
SOF | VER | TYPE | NODE_ID | SEQ | LEN | DATA | CRC16

Initial proposal:
- SOF: 1 byte
- VER: 1 byte
- TYPE: 1 byte
- NODE_ID: 1 byte
- SEQ: 1 byte
- LEN: 1 byte
- DATA: variable
- CRC16: 2 bytes

## Message types
0x01 COMMAND
0x02 ACK
0x03 STATUS
0x04 HEARTBEAT
0x05 CONFIG
0x06 ERROR

## COMMAND
Keep minimal: CMD + PARAM. Candidate commands: RELAY_ON, RELAY_OFF, LOCAL_MODE, REMOTE_MODE, REQUEST_STATUS.

## ACK
ACK_TYPE + ACK_SEQ + RESULT.

## STATUS
One packet should contain the current state the Gateway actually needs. Candidate fields: RELAY_STATE, LIGHT_LEVEL, POWER_STATUS, PRESENCE, FAULT_FLAGS. Exact fields depend on final sensor selection.

## HEARTBEAT
Smaller than STATUS when possible. It proves the Node is alive and can optionally carry compact state/fault information.

## Binary encoding
Avoid strings such as NODE=05,LUX=123,RELAY=ON. Use fixed-size binary values to reduce payload, Time-on-Air and parsing overhead.

## Optimization rule
Do not split one logical STATUS into many tiny packets. Prefer one combined STATUS when it remains small enough.

Final Time-on-Air optimization must consider SF, BW, CR, preamble, header mode, CRC and payload length together.
