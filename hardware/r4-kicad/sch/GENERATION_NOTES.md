# R4 KiCad schematic reconstruction — generation notes

Generated 2026-10-01 by `hardware/r4-kicad/tools/gen_schematics.py` from the
copper netlists (`hardware/r4-kicad/netlist/copper_netlist_{lhs,rhs}.json`).
**Netlist authority is the gerber copper, not the original Altium PDF.**
Values/MPN/DNF status come from `Reid Orthotic v2 R4 BOM.xlsx`.

Regenerate / verify:

```
python3 hardware/r4-kicad/tools/gen_schematics.py
python3 hardware/r4-kicad/tools/check_sch_netlist.py      # hard gate
```

## Projects

One KiCad 7 project per side (`sch/Reid_Orthotic_v2_LHS/`, `sch/Reid_Orthotic_v2_RHS/`),
each with a root sheet and four child sheets mirroring the original PDF
organization: **PWR** (coil input, BQ24210 charger, fuse, battery, LDO),
**MCU** (nRF52832, crystals, decoupling, antenna match, reset, VBAT divider
Q1/Q2, I2C pullups, TC2030), **MUX** (5× 74LV4051BQ + channel filters + mux
power switch Q3), **IO** (J1/J2 tail connectors, R1–R38 series resistors,
LSM6DSM). Symbols live in the project-local `R4_symbols.kicad_sym`
(`sym-lib-table` provided) and are also embedded in each sheet.

Connectivity style: short wire stubs with labels (reconstruction for review,
not routed artwork). Named nets use global labels; power nets (`0V`, `3V3`,
`VBAT`, `VBAT_F`, `VSYS`) use power symbols; unnamed copper nets that stay on
one sheet get local labels `N<id>`, cross-sheet ones get global labels `N<id>`
(ids = copper net ids, so they can be traced straight back to the json).
Single-pin nets carry no-connect flags, except named single-pin nets
(`SWDIO`, `SWDCLK`, `IS_LHS` on LHS, `DEC2`), which keep a visible label.

## Equivalence check result (hard gate)

`tools/check_sch_netlist.py` re-derives the netlist from the `.kicad_sch`
files themselves — it parses the s-expressions, re-computes absolute pin
positions from the embedded symbol libs and instance transforms, then builds
the wire/label/power-symbol connectivity graph and compares the resulting
(ref, pad) partition against the copper json.

Result: **ZERO diffs on LHS and ZERO diffs on RHS** — every one of the 425
pads per side lands in exactly the copper net group, including all single-pin
(no-connect) nets. The check also passes in a stricter endpoint-only
connectivity mode (no point-on-segment joins), i.e. KiCad's own connectivity
rules will see the same netlist.

Residuals (by design, excluded from the compare and reported by the checker):

* **J5 (TC2030)** is schematic-only: it exists on the board as unnumbered
  B.Cu tag-connect pads, not in the PnP/BOM, so the copper json has no `J5`
  pads. Copper evidence (diagnostics `j5_tc2030_candidates`) ties three of
  its pads to `MCU_RST`, `SWDCLK`, `SWDIO`; J5 pins 2/3/4 are wired to those
  nets. Pins 1 (VTref→3V3) and 5 (GND→0V) are **assumed** per standard
  TC2030 SWD wiring; pin 6 (SWO) left no-connect.
* `#PWR*` power symbols (no physical pads).
* `kicad-cli` is not installed on this machine, so `sch erc` /
  `sch export netlist` were not run; the gate relies on the parser above.
  Running ERC later will flag benign warnings (single-pin named nets,
  power-input nets driven through the fuse/LDO modelling, DNF parts).

## Symbol pin conventions & assumptions

* **Q1 (BSS816NW, N-ch) / Q2 (DMP2110UW, P-ch) / Q3 (RU1C002ZP, P-ch),
  SOT-323**: pad mapping **assumed 1=G, 2=S, 3=D** (marked as such on the
  symbols). Net semantics are consistent with this: Q1 gate=`VBAT_ON`
  (P0.06), source=0V, drain→Q2 gate (N28); Q2 source=`VBAT_F`, drain→R43 top
  of the ADC divider; Q3 gate=`MUX_ON` (P0.31), source=3V3, drain→R54 (10R)
  toward the mux rail.
* **D1 (CUS08F30 Schottky)**: drawn pin1=A, pin2=K from net semantics —
  series element from the coil/RF input node (N16: J3.2/C37/C38) into `VSYS`
  reservoir caps, so anode on the RF side, cathode on VSYS. **Note:** many
  SOD-323 datasheets number the *cathode* as pin 1; our pad numbers are the
  gerber engine's, so only the orientation (A at RF, K at VSYS) is asserted,
  not the physical pin-1 mark.
* **D2 (MM3Z5V1 zener)**: copper has pad1→0V, pad2→VSYS ⇒ anode 0V, cathode
  VSYS — a 5.1 V clamp on VSYS. Same pad-number caveat as D1.
* **U8 BQ24210 (WSON-10)**: pin *functions* assigned per TI SLUSA76B mapped
  onto our pad numbers: 1 VBUS, 2 ISET, 3 VSS, 4 VTSB, 5 TS, 6 PG̅, 7 EN̅,
  8 CHG̅, 9 NC, 10 BAT, EP=VSS. Evidence: pad1→VSYS caps; pad2→R56 (5.1k,
  ISET); pad4→R59 (22k)→pad5 node with R60 (NCU15XH103 NTC)→0V (classic
  VTSB→TS bias); pads 6+7 tied together, pulled to `VBAT_F` via R62 (100k)
  and taken through series R20 to P0.10 (`BQ_PG`); pad8 pulled to 3V3 via
  R61 → `BQ_CHG` GPIO; pad10→`VBAT_F`. PG̅/EN̅ tied is a deliberate-looking
  board trick (charger enabled whenever input power is good).
* **U9 TCR3UF33A (SOT23-5 LDO)**: 1 VIN(`VBAT_F`), 2 GND, 3 CT/EN (pulled up
  to `VBAT_F` via R57 100k; DNF pulldown R58), 4 NC, 5 VOUT(`3V3`).
* **U2 nRF52832-QFAA**: pin numbers and names are **exactly** the
  `u2_pin_table` from the copper json (`P0.xx[/function]`, VDD, VSS, DEC1-4,
  DCC, XC1/2, ANT, SWDIO/SWDCLK, NC, PAD=VSS(EP)). Identical pkg names on
  both sides; only the net hookup differs (see below).
* **U3–U7 74LV4051BQ (DHVQFN16+EP)**: standard Nexperia pinout
  (3=Z, 6=E̅, 7=VEE, 8=GND, 9/10/11=S2/S1/S0, 16=VDD,
  Y0..Y7 = pads 13,14,15,12,1,5,2,4).
* **U1 LSM6DSM (LGA-14)**: standard ST pinout; SDO/SA0, SDx, SCx grounded
  (I2C addr 0x6A), CS tied 3V3 (I2C mode), INT2/NC pins floating.
* **X1 (NX1610SA 32.768k)**: 2-pad crystal. **X2 (XRCGB32M 32 MHz)**: 4-pad;
  pads 1/3 are the crystal terminals (per the engine's `numbering`), pads
  2/4 are case/GND pads that are **floating on this board** → no-connect.
* **ANT1 (AMCA31 chip antenna)**: pad2 is the fed pad (L3/C16 match node);
  pad1 floats → no-connect.
* **J1/J2 (IO_CONN, 20 pads)**: generic 20-pin connector symbols, pin names =
  pad numbers. **J3/J4**: 2-pad wire connectors (RF coil in, battery).
* **DNF parts** (per BOM: C16, C18, C22, C23, C38, R50, R51, R58, and the
  J1–J4 connectors): included with `DNF` in the value, `dnp` set and
  excluded-from-BOM; their copper connectivity is real and included.

## Differences vs the original PDF schematic (copper wins)

* **F1 fuse split**: `VBAT` (battery connector J4.2 – F1.1) is a separate net
  from `VBAT_F` (F1.2 → charger BAT, LDO VIN, Q2, R57/R62, C35/C43). Kept
  split; do not merge.
* **C13 decouples DEC3** (U2 pin 33), not DEC2. DEC2 (U2 pin 32) floats with
  no cap — visible as named single-pin net `DEC2`.
* **C8 is a 3V3/VDD decoupling cap** (0V–3V3), not whatever the PDF shows.
* **MUX_VCC does not exist as a separate net in the copper json** — ⚠️ *top
  item for human review*. In both sides' copper extraction, the switched mux
  rail (U3–U7 pin 16 VDD, both pads of C24–C28, R54.1) is part of net 1
  (`0V`), and Q3's drain reaches that same group through R54. Taken
  literally this grounds the mux VDD and shorts 3V3→GND through Q3+R54 when
  `MUX_ON` is asserted, which contradicts working firmware — it is almost
  certainly a plane/pour merge in the copper extraction (MUX_VCC pour
  touching the GND pour at raster resolution, or a genuine board short).
  Per the "copper is authority" rule the schematic reproduces the merge
  as-is; Alex should confirm against the editable CAD.
* **Original-PDF net-name outliers** recorded in the json diagnostics
  (`schematic_crosscheck`) are reproduced as copper says, e.g. LHS/RHS GPIO
  assignments differ between sides (mirrored layouts):
  LHS: FSR_CH2/1/0 = AIN0/1/2, FSR_S0..2 = P0.09/08/07, SDA/SCL = P0.26/27,
  BQ_CHG = P0.12, IMU INT1 = P0.25, IS_LHS (P0.22) floating.
  RHS: FSR_CH0/1/2 = AIN0/1/2, FSR_S0..2 = P0.27/26/25, SDA/SCL = P0.08/09,
  BQ_CHG = P0.11 (P0.12 takes IMU INT1), CAP_S0/S2 and the TEMP_S* ports
  permuted, P0.22 strapped to 0V (the IS_LHS side strap), P0.07 unconnected.
  RHS-only: R35.2 goes to J2.6 instead of 0V (so RHS has one extra
  unnamed 2-pin net and J2.6 is not grounded).
* **SWDIO/SWDCLK carry no copper tracks** (single-pin nets at U2); debug is
  via the unnumbered TC2030 pad cluster on B.Cu (see J5 above).
* Copper nets with ids ≥130 in the json are empty (no pads) — nothing to
  draw; the checker ignores them on both sides.
