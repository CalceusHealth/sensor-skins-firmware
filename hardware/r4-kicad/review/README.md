# Reid Orthotic v2 R4 — KiCad reconstruction, for review

These KiCad 7 projects were rebuilt from the R4 production data (Sep-2024
gerbers, drill files, BOM, pick-and-place and the schematic PDF). We did not
have the CircuitStudio sources.

## What has been verified mechanically

- **Copper:** gerbers and drills re-exported from these boards match the
  original production gerbers on every fab layer and every hole, on both
  sides. Details are in `COMPARE_REPORT.md`.
- **Netlist:** derived from copper connectivity and cross-checked against the
  firmware pin map (`gpio.h`, 19/19 functions per side). The schematics'
  netlist equals the copper netlist exactly; KiCad's own netlist export
  confirms this.

## What we'd like you to confirm

Copper can't settle these:

1. **Q1 / Q2 / Q3 (SOT-323):** we assumed pad 1 = G, 2 = S, 3 = D.
2. **D1 / D2 polarity:** inferred from the circuit. D1 has its anode on the
   coil side and its cathode on VSYS. D2's cathode is on VSYS.
3. **J5 (TC2030) pins 1 and 5:** assumed to be VTref → 3V3 and GND → 0V.
   Pins 2–4 (SWDIO, RESET, SWDCLK) are traced from copper.

## What looks different from the R4 schematic PDF

Please sanity-check these:

4. **C13 is on DEC3, and U2's DEC2 pin is unconnected.** The PDF suggests
   C13 is on DEC2.
5. **C8 decouples VDD**, not DEC4. DEC4 carries L2 and C9.
6. **F1 splits the battery rail.** VBAT is on the J4 side and VBAT_F is on
   the charger/LDO side. The PDF draws them as one net.
7. **U8 (BQ24210):** PG̅ and EN̅ are tied together, with R20 in series to
   the BQ_PG GPIO.
8. **TP1–TP7 have no copper pads.**
9. **RHS only:** J2.6 is not grounded (R35.2 goes to J2.6), unlike LHS.

## Files

| File | Contents |
|---|---|
| `Reid_Orthotic_v2_{LHS,RHS}_schematic.pdf` | Schematics, 4 sheets (PWR / MCU / MUX / IO) |
| `Reid_Orthotic_v2_{LHS,RHS}_copper.pdf` | Copper layers |
| `sch/` | KiCad schematic projects |
| `pcb/` | KiCad boards |
| `COMPARE_REPORT.md` | Copper validation detail |
| `SCHEMATIC_NOTES.md`, `PCB_NOTES.md` | Reconstruction conventions and assumptions |
