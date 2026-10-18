# Firmware layout

The board uses one firmware image. `firmware/src/main.cpp` reads the `MODE`
jumper during boot:

- `MODE = LOW`: Gateway, initializes both SX1278 radios.
- `MODE = HIGH`: Node, initializes radio 1 and the relay control loop.

Pin assignments are centralized in `firmware/common/pins.h` and follow the
schematic net labels. GPIO12 is currently assigned to the second LoRa DIO0
net; verify that the module does not hold this ESP32 strapping pin high during
reset before production use.

The current SX1278 adapter covers register initialization, FIFO receive/send,
and shared SPI transactions. Gateway command queues, ACK retry state, sensor
drivers, Wi-Fi bridge, and RTC integration remain the next implementation
steps; the entrypoint currently keeps the scheduler non-blocking and sends
BEACON/STATUS/HEARTBEAT frames through the packet library.
