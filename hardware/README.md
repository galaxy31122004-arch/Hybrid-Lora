# Hardware / BOM

Source BOM: user-provided export dated 2026-10-06.

## Main hardware

- ESP32-S module: 1
- SX1278 LoRa modules: 2
- LM2596S-3.3 buck regulator: 1
- 16 A relays: 3
- 2SC1815-GR drivers: 3
- PC817 optocouplers: 3
- DC input connector: 1
- KF8500 2-pin terminal blocks: 4
- 3-pin jumpers: 2
- 2-pin jumper: 1
- Buttons: 2
- 2-LED common-anode indicator: 1

## Power / protection / passive

- 470 uF: 2
- 330 nF: 6
- 10 uF: 2
- TSS54LB: 1
- SR360: 1
- 1N4007: 3
- 33 uH inductor: 1
- 4.7 kΩ: 4
- 2.2 kΩ: 3
- 1 kΩ: 3
- 330 Ω: 2
- 10 kΩ: 1
- M2 screws: 3

## Design notes

The BOM is the current hardware snapshot; it is not yet the final sensor BOM.

The current firmware plan expects:
- LoRa: 2 radios, shared SPI with independent chip-selects.
- Gateway/Node role: same PCB, selected by firmware/configuration.
- I2C/UART/ADC headers can be populated according to the final sensor selection.
- Final sensor list is still intentionally open.

## BOM verification flags

Before production, verify these entries against the schematic/datasheet:
1. L1 is named 33 uH but the listed manufacturer part is YT0650-4R7M; verify the actual inductance.
2. D2 is named SR360 but the manufacturer part field says 10A10; verify the actual diode.
3. D3-D5 are named 1N4007 but the manufacturer part field also says 10A10; verify the actual diode.
4. C3-C6/C9-C10 are named 330 nF while the listed manufacturer part description contains 0.1 uF; verify the actual capacitance.
5. The two LoRa entries are generic SX1278 footprints; verify that the physical module/connector matches the selected RA-02 module before fabrication.

These are BOM consistency checks only; do not silently change the user's component selection.
