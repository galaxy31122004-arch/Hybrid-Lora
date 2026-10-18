# Schematic review - 2026-10-18

Scope: review of [the schematic screenshot](schematic-2026-10-18.png) supplied
on 2026-10-18, compared with `hardware/BOM.tsv`. The editable schematic and
PCB files are not in this repo, so
pin connectivity, footprints, and electrical ratings still need confirmation in
the CAD source before fabrication. This is a planning-stage review, not a test
result from assembled hardware.

## Address before PCB fabrication

1. **Relay coil voltage and supply disagree.** K1-K3 coils are connected to the
   `12V` rail in the screenshot, while the BOM lists
   `W15-1A2T-DC5V/16A` and a `DC5V` footprint. Confirm the selected relay's
   coil rating. Use a 12 V coil part with a matching footprint, or redesign the
   coil supply and driver for 5 V. Do not populate the listed 5 V relay on the
   shown 12 V coil circuit.
2. **GPIO12 is used for the second LoRa DIO0 signal.** The screenshot maps
   `2DIO0` to ESP32 `IO12`. GPIO12 is a boot strapping pin, so a high level
   from the LoRa module during reset can prevent normal boot. Prefer a free
   non-strapping input GPIO and verify its pin mapping in the CAD source.
3. **Inductor BOM entry conflicts with the schematic.** L1 is labeled `33uH`
   in the screenshot and BOM, but the BOM manufacturer part is
   `YT0650-4R7M`. Confirm the actual inductance and current rating from the
   selected part's datasheet, then align the part number, value, and footprint.
4. **BOM ordering data still needs correction.** R1/R8/R11/R12 (4.7k),
   R2/R3/R6 (2.2k), R4/R5/R7 (now 470 ohm in the screenshot), R9/R10
   (330 ohm), and R13 (10k) share one manufacturer and supplier part number
   in the BOM. R14-R16 are absent from the BOM. C3-C6/C9-C10 are named
   330nF while their listed part describes 0.1uF; D2 and D3-D5 also have
   name/part-number mismatches. Re-export the BOM from the revised schematic
   and check each value, order code, and quantity before purchasing.

## Validate on the next revision

- **ESP32 supply decoupling:** C5 is the only capacitor shown next to U3,
  labeled 330nF. Verify the module's local bulk and high-frequency decoupling
  against its datasheet, especially during Wi-Fi transmission and relay
  switching. C2 (470uF) is on the shared 3.3 V rail but its physical distance
  from U3 is unknown from the screenshot.
- **Optocoupler/relay drive margin:** R4/R5/R7 are now 470 ohm at the
  PC817 LEDs; R2/R3/R6 are 2.2k from optocoupler outputs to Q1-Q3 bases.
  Calculate worst-case optocoupler output current, transistor saturation,
  and relay coil current for the actual selected parts and supply voltage.
- **Module and connector footprints:** Confirm the physical SX1278 module
  against U15/U16 footprints. U17-U20 are drawn as four-pin headers, while
  the BOM lists an eight-pin header part; confirm the orderable connector.
- **PCB validation:** Run CAD electrical-rule and design-rule checks, then
  inspect power traces, relay-contact creepage/clearance, RF layout, antenna
  connections, and 3.3 V rail behavior on a prototype. These cannot be
  validated from the screenshot alone.

## Changes visible since the earlier review

- R14-R16 (10k) now pull the three transistor bases to ground.
- R4/R5/R7 at the optocoupler inputs are now labeled 470 ohm.
- Each LoRa NSS line now shows a 10k pull-up to 3.3 V.
- U3 EN has a pull-up, reset capacitor, and reset button; IO0 has a pull-up
  and boot button. Verify the final netlist and power-up behavior in hardware.

No firmware or editable schematic is present in this repository yet. The
packet/algorithm documents describe intended behavior and are not evidence of
implemented or tested functionality.
