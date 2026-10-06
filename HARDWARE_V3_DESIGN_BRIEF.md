# Smart Insole Hardware v3: Design Brief

*Calceus Health. Last updated 2026-10-05. Working files are on branch `tim` of `sensor-skins-firmware`. Linear: [SEN-179](https://linear.app/calceus-health/issue/SEN-179/hardware-v3-pcb-integrated-recharge-coil-protected-battery-flex).*

This brief is for readers new to the work. Section 1 gives the whole picture on one page. Later sections give the design, the evidence behind it, and the options we set aside. Paths are relative to the repository root; drawings referenced as `figures` are in `hardware/r4-kicad/v3_concept/`.

---

## 1. Summary

**The problem.** The current prototypes (Reid Orthotic v2, board revision R4) work well in the field. The only recurring failures are:
1. **The recharge-coil wires.** The receive coil is a separate wound part whose fine leads are hand-soldered to the board and flex with every step.
2. **The battery.** Physical stress on the pouch cell (it is stacked on top of the components), the firmware letting it discharge too far, or damage to the cell's small protection board.

**The v3 answer, in one line:** no hand-soldered wires and nothing pressing on the battery. The coil moves onto the board, the battery lies flat in its own pocket on a plug, and everything that needs servicing sits behind one hatch.

**Decisions so far**

| Area | Decision | Status |
|---|---|---|
| Device format | **Flat insole first**: a sandwich of bottom layer, electronics layer and top cover. The shaped Dolapro shell was uncomfortable in prototypes. | Decided |
| Coil | Coil sits on a **tab at the heel end of the board**. Preferred: today's **TDK wound coil glued flat to the tab**, with its leads soldered to pads right beside it, which removes the wire run that fails. An etched copper coil charges far slower in simulation. | Preferred; confirm on a test coupon |
| Charging | Charge from the **top** through the 1–2 mm top cover. Ferrite goes under the coil. Board sits at the top of the bay. | Decided |
| Magnets | **One C-shaped N52 magnet in the insole, and a ring with a single cut in the puck.** ~2.5× today's hold at **any** puck angle, and one magnet part per insole. Supplied pre-assembled. | Recommended; quote spec ready |
| Battery position | **In front of (toe-ward of) the board**, lying flat at 45° in a die-cut pocket, never on top of components. | Decided |
| Battery connection | Small **plug and header (JST ACH, 1.4 mm high)** instead of soldered wires. Replaceable without a soldering iron. | Recommended |
| Battery protection | Today's pack already has a protection board (PCM). Add a second, known-threshold protection chip on the main board. Fix the charge current setting, which is above the cell's rating. | Recommended |
| Service | Components, connector and **all test points face down** to a hatch in the bottom layer. The top cover stays continuous for comfort. | Recommended |
| Charger puck | Test new **puck firmware R5** (one line: drive at ~170 kHz instead of ~189 kHz). Simulation says it charges reliably over a much wider range of puck placements. | A/B test next |

**Main open items:** test puck firmware R5 against R4; build a coil test coupon; get magnet quotes; get the battery protection thresholds from the supplier; check the layout against the smallest insole size (XXS). The full list is in section 6.

---

## 2. Background

### 2.1 The product today

- **Insole electronics board (R4):** 4-layer FR4, 0.4 mm thick, 21.25 × 46.25 mm, sitting under the arch. It carries:
  - nRF52832 Bluetooth MCU
  - LSM6DSM IMU
  - five analog multiplexers for the sensors
  - BQ24210 battery charger
  - 2.4 GHz chip antenna
  - The same board design is used for every insole size; left and right are mirror-image builds.
- **Sensor layer (FPC):** a printed-silver-on-PET membrane made by Reid Print, one design per size. Each foot has 19 force (FSR) pads, 6 capacitive sensors and 5 temperature sensors. It joins the board on two 20-pin tails.
- **Battery:** Routejade **FLPB301031-HPMW30-30**, a 76 mAh pouch cell (31 × 10.2 × 3.2 mm). It has a small protection board (PCM RJD404HP) folded over its top and two 30 mm wire leads, which are hand-soldered to the board. In the current build the cell is taped on top of the components.
- **Charger puck:** an ATtiny1616 drives a coil through a single transistor from USB 5 V. The puck magnet is flush with the lid. The receive coil in the insole is an identical TDK coil with a 4 × 3 mm alignment magnet glued in its centre.

### 2.2 Why the failures happen

**Coil wire.** The coil is not on the parts list: it is hand-attached, and its fine leads are soldered to the board. The first builds had no strain relief. The current process adds heatshrink, a C-bend and glue (`artefacts/assembly-process.md`), but the joint is still hand-made in five manual steps and the wire still flexes where it leaves the heatshrink.

**Battery.** Three failure modes, each with its own fix in the v3 design (section 3.4):

| Failure mode | What happens today |
|---|---|
| **Physical stress** | The pouch is taped on top of the components, so body weight presses component corners into its soft side. This repeated point load is a known route to internal shorts; tearing down a failed cell and looking for dents would confirm it. The hand-soldered leads also take heat and flex at the joint. |
| **Discharged too far** | The firmware switches the device off below 3.1 V, but only if it is running correctly; brown-out reset is disabled. After switch-off the board still draws a small current that has not yet been measured (SEN-49). The pack's protection board is the backstop, but its trip voltage is unpublished; boards of this type trip anywhere from 2.0 to 3.0 V. |
| **Protection board damaged** | The 0.4 mm protection board sits on the end of the cell, in the zone that is loaded and flexed, next to the hand-soldered leads. If it fails short, the cell is silently unprotected; if it fails open, the unit reads 0 V although the cell is fine. |

The charge current setting is also too high. The charger is set for ~77–82 mA into a cell rated 70 mA maximum. It is harmless on the wireless puck, which only delivers ~5 mA, but not on a bench supply (SEN-105).

**Diagnosing a dead unit.** Measure across the cell itself and across the leads:

| Reading | Meaning |
|---|---|
| Cell 2–3 V, leads ~0 V | The protection board has tripped and locked out |
| Cell normal, leads ~0 V | The protection board is damaged |
| Cell ~0 V | The cell itself is dead |

---

## 3. The v3 design

Figures: `plan_front_{LHS,RHS}.png` (layout), `section.png` (cross-section), `battery_connection.png` (connection and service).

### 3.1 Build and thickness

- **Flat sandwich:** bottom layer / electronics layer / top cover (1–2 mm).
- **Board:** mounted at the **top** of the electronics bay. Its flat back faces the foot and its components hang down, which puts the coil as close as possible to a puck placed on top.
- **Thickness is set by the battery alone.** Nothing is stacked any more: the electronics bay is the cell thickness plus ~0.2 mm, about 3.4 mm with today's cell. Today's stack is 5.4 mm because the cell sits on top of the components.

  | Item | Height |
  |---|---|
  | Board | 0.4 mm |
  | Tallest parts and the battery connector | ~1.4 mm |
  | Magnets | 1 mm |
  | Battery | 3.2 mm |

- **A thinner cell would make the whole insole thinner.** For example, a ~2 × 15 × 33 mm pouch of similar capacity (estimated; to confirm with Master Instruments) gives a ~2.2 mm bay.
- **The board design is shared across all seven sizes (XXS–XXL).** The layout must therefore fit the smallest insole; it has only been checked against size S so far.

### 3.2 Coil and charging

- **Position:** a 15 mm coil on a tab directly at the board's heel edge. This makes the board 21.4 × 63.3 mm, and the coil sits ~9.5 mm closer to the board than today, inside space the coil and its plastic bridge already use. The tab is clear of the sensor layer and all sensor pads (size S).
- **Coil type:**
  - **Preferred:** the existing **TDK WR151580-48F2-G** wound coil (27.1 µH, 15 mm), **glued flat to the tab with its leads soldered to pads right beside it**. There is no free wire span to flex, and charging performance is kept.
  - **Alternative:** a coil etched into the board copper. It removes the part entirely but charges far slower in simulation: 0–12 mA against 8.5–25 mA for the TDK coil (section 4.4).
  - A test coupon should compare the two before the board is laid out.
- **Ferrite** goes **under** the coil, on the side away from the puck.
- **Charge path:** keep the insole's single-diode rectifier. Bridge and doubler versions were worse in simulation.
- **Charging speed:** today ~5 mA, roughly 15–20 h from flat. The cell can take ~35 mA. The wireless link, not the charger chip, is the limit. The levers, in order:

  | Lever | Status |
  |---|---|
  | Puck drive frequency (R5) | see 3.5 |
  | Keep a good wound coil | 3.2 above |
  | Close coupling (coil at the top, magnets out of the coil centre) | in the design |
  | Lowest-power mode while on the charger | firmware, SEN-64 |
  | Raise the charge-current limit only once the link delivers more | |

### 3.3 Alignment magnets

- **Insole:** **one sintered N52 C-shaped magnet**, a 180° arc 4 mm wide and 1 mm thick, outer diameter 25.2 mm. It sits on the free side of the coil, ~0.6 mm clear of the sensor layer.
- **Puck:** a **full ring with one radial cut**. The cut stops the ring acting as a closed metal loop around the charging coil, which would absorb power. The ring can also be made as 2–4 arc pieces.
- **Performance at the real gap (top cover 1–2 mm; the puck magnet is flush with its lid):** about **2.5× today's holding force at any puck angle**, with stronger self-centring. If that is more than wanted, a thinner or narrower arc reduces it.
- **Why not today's centre magnet:** it sits in the coil's strongest field, where it wastes charging power as heat and partly saturates the ferrite.
- **Production:** no hand-placing of individual magnets. The magnet goes in as **one pre-assembled part**: pre-magnetised, polarity-checked, on a die-cut adhesive carrier, placed in one step. The magnets must never go through reflow soldering; standard grades are rated to 80 °C and reflow reaches ~250 °C.
- **Quotes:** spec in `magnet_carrier_RFQ.md`. First supplier lead: **AMF Magnetics** (Rozelle NSW, 02 9700 0055).

### 3.4 Battery

**Position.** Toe-ward of the board, flat, at 45°, 2.4 mm from the board edge, in a die-cut pocket (+0.5 mm) in the electronics layer. It is never on components. A heel-ward position also fits, but it needs ~45 mm custom leads and takes heel-strike load.

**Connection: robust in use, replaceable without soldering**
- **Prototype:** Master Instruments crimps a **JST ACH** plug (1.2 mm pitch, 1.4 mm high, takes AWG30) onto the existing pack's leads. The header sits on the board's free outer edge; the other edges carry the sensor tails. The stock 30 mm leads reach (18 mm needed).
- **Production:** a custom pack with a flexible tail into a Hirose BM28 board-to-flex connector (0.6 mm stacked), as phones do. No wires at all.
- **Strain relief:** the leads leave the protection-board end of the pack, run in a slack loop outside the board edge and the coil keep-out, and are taped or glued to the electronics layer, never to the cell. The layers clamp the mated plug in place.

**Protection**
- Keep the pack's own protection board.
- **Add a protection chip on the main board** (TI BQ29700 family with a dual transistor). It disconnects everything at a known ~2.8–3.0 V, whether or not the firmware is running, then draws ~0.1 µA until the charger is applied. Being on the rigid, machine-soldered board, it is far less exposed to damage than the pack's.
- Keep the firmware cut-off as a gentler first line.
- **Charge current:**
  - Existing boards: change resistor R56 from 5.1 k to 10 k (~40 mA, SEN-105).
  - v3: consider a charger chip matched to the cell (TI BQ25100H, 4.35 V) at 25–35 mA, plus a fuel-gauge chip (MAX17048) to fix the erratic battery-% readings.
  - Today's charger stops at 4.2 V, so the 4.35 V cell only ever reaches ~85–90 % of its capacity.

**Test points.** The battery test points must keep working the way R4 units are tested today. That method is: a handheld multimeter, black probe on pad 1 and red probe on pad 2 or 3, read alongside `;QB` (vbat and battery level) in the mobile and desktop apps (`artefacts/battery-debug-summary-2026-04-17.md`). On R4 the pads face the top cover. On v3 they can face the bottom hatch instead, as long as the same method still works:
- **Pads 1–3 stay hand-probeable:** 1 mm bare round pads (R4 size) spaced about as far apart as on R4 (~4–6 mm), numbered 1, 2, 3 on the silkscreen in R4's order, so a probe tip can't bridge two of them. A tight 1.7 mm-pitch strip is too easy to short (VBAT to 0V) with a handheld probe.
- **R4 mapping, from the copper (`hardware/r4-kicad/pcb/`) and the pad numbers in `artefacts/pins.png`:** 1 = 0V, 2 = VBAT_F (battery after fuse F1), 3 = VSYS (rectified coil = charger input, ~0 V off the puck), 4 = 3V3. This agrees with the April measurements (pad 2 ≈ 3.41 V off the puck; on the puck pad 2 ≈ 4.06 V, pad 3 ≈ 4.17 V). It **disagrees** with the 2026-05-01 handoff, which swaps 2 and 3. A single meter check settles it: pad 3 reads ~0 V off the puck.
- **New on v3:** a VBAT point on the battery side of the fuse. If the on-board protection chip is added, put a point on each side of it, so a locked-out protection can be told apart from a damaged one without opening anything. These follow the same spacing rule.
- Programming stays on the Tag-Connect TC2030 footprint (J5, the 6-pad grid with alignment holes, pads 5–10 in `pins.png`), also under the hatch. 3V3 can sit next to it.

**Replacing a battery** (a few minutes' bench job, no soldering):
1. Open the hatch in the bottom layer (its lid is on re-closable adhesive).
2. Lift the tape holding the leads.
3. Unplug the connector. It is small, so tweezers or a fingernail help. It is polarised and only fits one way.
4. Peel the old pack out of its pocket (low-tack tape).
5. Press in the new pack, plug it in, re-tape the leads with their slack loop, and close the hatch.

Conditions:
- Replacement packs must come with the plug already crimped; Master Instruments can supply them.
- The plug is held by the layers and the tape. Whether this connector series also has a latch is not yet confirmed; check on first samples.
- The hatch adhesive must survive repeated opening; test on prototypes.
- A tool-free, click-in battery would need a spring-contact holder. That was avoided because heel impacts can make spring contacts chatter and reset the device.

### 3.5 Charger puck

The puck hardware (revision R2) and all firmware revisions are in the repo (section 7.1).

The **R4 firmware** in the repo drives the coil at **188.7 kHz**. The circuit simulation (section 4.4) shows that this frequency only charges when the puck and insole coils are very tightly coupled. A slightly misplaced puck or a thicker cover can drop it to zero. Around **170 kHz** charges near its best at every coupling tested.

**Firmware R5** (`firmware/Reid Orthotic v2 Charger Firmware R5`) is R4 with one line changed (`PWM_TOP` 52 → 58, i.e. 169.5 kHz).

**A/B test before adopting it:**
1. Flash one puck with R4 and one with R5. Start two units at the same battery level.
2. Charge for 30 minutes. Log battery voltage, USB current and puck temperature.
3. Repeat with the puck centred, 3 mm off-centre, and over the thickest top cover.
4. Adopt R5 if it is equal or better everywhere.

Also confirm which firmware the field pucks run.

Two related findings:
- **The "25 % duty" in Alex's notes is the firmware's power setting.** The actual switch on-time is ~11–15 % of each cycle (a scope will confirm).
- **No puck hardware change is needed** for v3, apart from the magnet ring. The puck lid needs the matching split ring.

---

## 4. Evidence

### 4.1 The R4 board, rebuilt and verified in KiCad

The original design files were not available, so both boards were rebuilt as editable KiCad 7 projects (`hardware/r4-kicad/`) from:
- the production gerbers, BOM and pick-and-place files
- the schematic PDF

Verification:
- **Copper:** gerbers re-exported from the rebuilt boards match the production gerbers on every fab layer and every drill hole (`hardware/r4-kicad/validation/COMPARE_REPORT.md`).
- **Netlist:** derived from the copper and cross-checked against the firmware pin map (19 of 19 functions per side). KiCad's own netlist export matches it exactly.

A review package for Alex is in `hardware/r4-kicad/review/`. It lists the few things copper cannot settle:
- transistor pin mapping
- diode polarity
- two debug-pad pins

It also lists where the copper differs from the old PDF schematic. These files are the base for the v3 layout.

### 4.2 Layout and placement study

Script: `hardware/r4-kicad/tools/layout_study.py`. It combines, in one millimetre-accurate drawing:
- the 3D assembly model (sensor layer and board position)
- the KiCad board outline (overlap 0.97/0.98 with the model)
- the sensor pad positions
- the size-S orthotic outline from Reid Print's CAL1020 drawing

It checked the coil tab and searched every flat battery position (1 mm / 15° steps) that keeps ≥ 3 mm inside the orthotic edge and ≥ 1 mm clear of the sensor layer, the tails and the board.

Results are in section 3. Outputs: `hardware/r4-kicad/layout_study/`.

**Caveat:** the sensor-layer outline in the model is the trace layer. Reid's laminate and RF-shield layers are slightly wider, so placements need a final check against their full outline and against every size.

### 4.3 Magnet simulations

Script: `tools/magnet_sim.py` (magpylib; N52; insole and puck patterns attracting). Data: `v3_concept/magnet_sim.json`.

| Insole magnet | Puck magnet | Pull at 1–2 mm gap vs today | Works at any puck angle? |
|---|---|---|---|
| Today: Ø4 × 3 mm in coil centre | same | 100 % | yes |
| **C-arc 180°, 1 mm** | **split ring** | **245–268 %** | **yes** |
| C-arc 180° | matching C-arc | 267–288 % aligned, ~0 when turned 180° | no |
| 3 discs Ø5 × 1 mm | 3 discs | ~150 % aligned, ~0 when turned 60° | no |
| 3 discs Ø5 × 1 mm | split ring | 103–117 % | yes |
| MagSafe-style ring of 12 tiny magnets | same | 77–127 % | yes, but doesn't fit |
| Flexible magnet arc | strong puck magnets | 15–33 % | — |

These results cover holding force only. They do not model the ferrite or charging efficiency.

### 4.4 Charging circuit simulation

Script: `tools/charging_sim.py` and `tools/charging_study.py` (ngspice). Write-up: `v3_concept/CHARGING_SIM.md`.

The model covers the whole charge path:
- **Puck:** USB, the drive transistor at the firmware's real timing, the coil and its 33 nF capacitor.
- **Insole:** the coil and capacitor, the rectifier and clamp, and the charger into a 3.8 V cell.

**Calibration.** The coupling between the coils (k) was fitted to Alex's bench measurements:

| Measurement | Alex | Model |
|---|---|---|
| Voltage into 470 Ω | 3.18 V | used for the fit |
| Voltage into 1 kΩ | 4.9 V | 4.74 V |
| Charge current | 5.5 mA | 6.8 mA |
| USB current | 130 mA | does not match; Alex's own later note doubts it |

Absolute currents are therefore approximate. Comparisons between options are the reliable part.

**Charge current (mA) by drive frequency and coupling:**

| Drive frequency | k = 0.08 | 0.11 | 0.15 | 0.20 | 0.25 | 0.30 | 0.40 |
|---|---|---|---|---|---|---|---|
| 188.7 kHz (R4) | 0 | 0 | 0 | 2.5 | 9.7 | 17.3 | 33.4 |
| 169.5 kHz (R5) | 5.5 | 8.5 | 12.8 | 18.7 | 24.7 | 29.5 | 29.0 |

**Other results:**

| Change | Result |
|---|---|
| Bridge or doubler rectifier | 5–45 % worse |
| Etched coil (estimated values) instead of the TDK coil | 0–12 mA vs 8.5–25 mA |
| Longer switch on-time | only small gains |

### 4.5 Limits of the models

- The magnet model leaves out the ferrite and any eddy-current loss.
- The circuit model uses datasheet coil resistance and estimates for the etched coils.
- Neither model knows the real coupling in today's top-charging setup. Tight coupling through a 1–2 mm cover is likely k ≈ 0.2–0.4.
- The models rank the options; the coupon test and the R4/R5 A/B test confirm them on hardware.

---

## 5. Options considered and set aside

| Option | Why not (or not yet) |
|---|---|
| Etched copper coil instead of the wound coil | Charges far slower in simulation. Keep only as a coupon comparison. |
| Coil printed into the sensor FPC | Printed silver ink is ~90× more resistive than copper, so the coil cannot work at today's frequency. |
| Flexible-polyimide or rigid-flex board | A valid future path ("rigid island + flex webs", as Moticon uses), at ~2–4× (flex) or ~7–10× (rigid-flex) board cost. Deferred: flat devices first, rigid board plus tab. |
| Semi-flex FR4 | Rated for ~5 bends, not per-step flexing. Ruled out. |
| Coin cells (VARTA CP1254 / LIR2032) | CP1254 is 5.4 mm thick. LIR2032 is the same thickness as today with ~2/3 the capacity. Neither helps thickness. |
| Battery under the coil | Needs ferrite between them, is ~0.7 mm thicker, and conflicts with the magnet. |
| Matching 3–4 disc magnets | Lose all hold when the puck is rotated. Superseded by the C + split ring. |
| MagSafe-style ring of many tiny magnets | Weaker at our gap, 12 parts per side, and doesn't fit on the sensor side. |
| Solid closed ring magnet | Acts as a shorted turn around the coil and absorbs charging power. |
| Flexible magnets | 15–33 % of today's hold. |
| Magnets on the board, magnetised after soldering | A workable automation route, but a risk to the electronics from the magnetising pulse. Not needed while the pre-made carrier is available. |
| Full-bridge or voltage-doubler rectifier | Worse in simulation. |
| NFC wireless charging (13.56 MHz, e.g. Renesas PTX30W) | Only worth it if the puck is redesigned anyway. It would replace the coil, rectifier and charger chip. |
| Spring-contact battery holder | Heel impacts can make the contacts chatter and reset the device. |

---

## 6. Open questions and next actions

**Charging and coil**
- [ ] A/B test puck firmware R4 vs R5 (section 3.5). Confirm which firmware the field pucks run.
- [ ] Measure the puck's real USB current; it recalibrates the simulation.
- [ ] Coil coupon test. Compare the bonded TDK coil, an etched coil and today's coil, each with the C-magnet and split ring fitted. Measure inductance and Q, charge voltage against puck offset, pull force through a 1–2 mm cover, and temperature.

**Battery**
- [ ] Get the RJD404HP protection-board thresholds (trip voltages, current limit, standby current) from Master Instruments.
- [ ] Get a Master Instruments quote for packs with a crimped JST ACH plug. Also ask about a thinner (~2 mm) protected cell.
- [ ] Measure dead units across the cell vs across the leads (section 2.2) to tell protection lockout, protection damage and cell death apart.
- [ ] Measure the board's sleep current after firmware cut-off (SEN-49).
- [ ] Rework existing boards: R56 5.1 k → 10 k (SEN-105).
- [ ] Confirm whether the JST ACH plug latches, and test the hatch adhesive over repeated opening.

**Magnets**
- [ ] Send the magnet RFQ (`v3_concept/magnet_carrier_RFQ.md`), starting with AMF Magnetics. Decide which pole faces up on insole and puck.

**Layout and fit**
- [x] Add the per-size sensor drawings (XXS–XXL) to `hardware/` (CAL1000–CAL1060).
- [x] Front-battery layout drawn for every size (`v3_concept/sizes/`). Result: S–XXL fit; XS is marginal (0.5 mm instead of 1 mm clearance to the sensor layer); **XXS does not fit today's 31 mm pack** in front of the board (≤ ~17 mm long at 10.2 mm wide), and its board sits only ~1.9 mm from the orthotic edge.
- [ ] Decide the XXS battery: a shorter cell, a different position, or a smaller board for XXS/XS.
- [ ] Confirm the R4 pad 2 / pad 3 mapping with one meter reading off the puck (section 3.4, Test points).
- [ ] Regenerate the v3 layout figures with the battery test points at R4 spacing (they still show the 1.7 mm strip).
- [ ] Get the flat insole's outline and layer build-up (bottom layer and top cover thickness, materials, any heat in lamination).
- [ ] Re-validate the 2.4 GHz antenna matching on the new board outline.

**People**
- [ ] Alex: review the KiCad package (`hardware/r4-kicad/review/`). Also ask him:
  - why production moved from two outside magnets (R3) to one centre magnet
  - the per-batch rework list
  - whether an antenna tuning report exists

---

## 7. Reference

### 7.1 Files

| What | Where |
|---|---|
| This brief | `HARDWARE_V3_DESIGN_BRIEF.md` |
| v3 drawings, simulation write-ups, magnet RFQ | `hardware/r4-kicad/v3_concept/` (start with its `README.md`) |
| Layout and battery placement studies | `hardware/r4-kicad/layout_study/` |
| R4 board in KiCad (schematics, boards), validation, review package | `hardware/r4-kicad/{sch,pcb,validation,review}/` |
| Tools that regenerate everything | `hardware/r4-kicad/tools/` (Python venv in `hardware/r4-kicad/.venv`) |
| R4 production record: gerbers, BOM, pick-and-place | `artefacts/SSII Orthotics Electronics - Design Verification/` |
| R4 schematic PDF | `artefacts/Reid Orthotic v2 R4.pdf` |
| R3 3D assembly model (sensor layer, board, battery, coil) | `artefacts/Reid Orthotic v2 R3 MECH/` |
| Sensor drawings (Reid Print), one per size: CAL1000 = XXS (orthotic 240 × 81.1 mm) … CAL1020 = S … CAL1060 | `hardware/CAL10x0 V2 Rev*.jpg` |
| Test-point pad numbering on R4 (photo; gitignored, local copy only) | `artefacts/pins.png` |
| Photo of the current build (coil on bridge) | `hardware/RHS.jpg` |
| Sensor pad positions | `artefacts/all_sensor_coordinates.csv`, `artefacts/{FSR,CAP}-{L,R}.png` |
| Battery: cell spec and pack drawing (with PCM) | `artefacts/Routejade-FLPB301031-HPMW30-30.pdf`, `..._pack-drawing.pdf` |
| Coil datasheet | `artefacts/TDK-WR151580-48F2-G_Spec.pdf` |
| Assembly process (current) | `artefacts/assembly-process.md` |
| Alex's engineering notes | `artefacts/carbon-circuits-1`, `artefacts/orthotic-charger-puck-1` |
| Charger puck: design files, schematic, case | `imports/alex_jira_duplicates/firmware/Reid Orthotic v2 Charger R2*` |
| Charger puck firmware R2–R5 | `firmware/Reid Orthotic v2 Charger Firmware R*/` |
| Battery and sleep firmware analysis | `SLEEP_LOGIC_V2_PROPOSAL.md` |

### 7.2 Key technical facts

**Insole charge path (R4).** Coil → C37 33 nF → D1 Schottky (half-wave) → D2 5.1 V clamp → VSYS → BQ24210 (fixed 4.2 V; R56 5.1 k ≈ 77–82 mA) → F1 200 mA fuse → battery. Every off-board connection is a hand-soldered joint; the connector footprints J1–J4 are not fitted.

**Coil (TDK WR151580-48F2-G).**
- 27.1 µH and ≤ 0.5 Ω at 100 kHz.
- Ø15 mm air coil on 0.8 mm ferrite; 2.63 mm total.
- With 33 nF it resonates at 168 kHz.

**Battery pack.**
- Cell: FLPB301031, 76 mAh, 4.35 V chemistry, 31 × 10.2 × 3.2 mm.
- Protection: PCM RJD404HP (IC "UP71AC-ITM").
- Leads: AWG30, 30 mm.

**Puck.**
- ATtiny1616 at 10 MHz. Single PMOS high-side switch into the coil with C7 33 nF in parallel; the low-side transistors are not fitted.
- Firmware R3: 172.4 kHz. R4: 188.7 kHz. R5: 169.5 kHz.

**Sensors (CAL1020).**
- FSR pad 8 × 8 mm; capacitive sensor 17 × 8 mm.
- Six-layer PET stack, including an RF shield.
- Joined to the board with 1.27 mm Nicomatic pins.

### 7.3 Corrections made during the work

Recorded so that earlier notes or messages are not misread:
- **Battery protection:** the pack *does* have a protection board. The July "bare cell" reading came from the cell-only spec sheet.
- **Magnet layout:** 3-disc magnet patterns were recommended, then withdrawn once puck rotation was simulated.
- **Rectifier:** a full-bridge rectifier was suggested, then withdrawn after simulation.
- **RHS mux pads:** the "different mask/paste on the RHS mux pads" finding was a reconstruction bug, not a board difference.
- **Board size:** the R4 board is 21.25 × 46.25 mm, not ~40 × 74 mm as first estimated from copper extents.

### 7.4 Linear

| Issue | Relevance |
|---|---|
| [SEN-179](https://linear.app/calceus-health/issue/SEN-179/hardware-v3-pcb-integrated-recharge-coil-protected-battery-flex) | Hardware v3 tracking issue |
| [SEN-105](https://linear.app/calceus-health/issue/SEN-105/sleep-v2-11-hardware-charge-current-80-ma-exceeds-cell-max-70-ma-iset) | Charge-current resistor rework |
| [SEN-49](https://linear.app/calceus-health/issue/SEN-49/battery-drain-and-sleep-current-audit-certify-no-firmware-drain-path) | Sleep-current audit |
| [SEN-64](https://linear.app/calceus-health/issue/SEN-64/firmware-auto-drop-stream-rate-on-charge-in-sleep-power-saving) | Low power while charging |
| [SEN-106](https://linear.app/calceus-health/issue/SEN-106/sleep-v2-12-firmware-uvlo-system-off-below-3100-mv-wake-on-charger-pg) | Firmware cut-off (done) |
| [SEN-87](https://linear.app/calceus-health/issue/SEN-87/uat-recharge-add-a-placement-markguide-on-the-orthotic-for-the-usb) | Puck placement guide |
| [SEN-172](https://linear.app/calceus-health/issue/SEN-172/build-and-commission-the-50-pair-fleet-for-november) | November fleet build; the reworks apply to it either way |

### 7.5 Key external sources

- [TI BQ24210](https://www.ti.com/product/BQ24210)
- [TI BQ25100](https://www.ti.com/product/BQ25100)
- [JST ACH connector](https://uk.rs-online.com/web/p/pcb-headers/6880984P)
- [Hirose BM28](https://www.mouser.co.id/hirose-bm28-connectors)
- [NdFeB grades and temperatures](https://radialmagnet.com/neodymium-magnet-grades-chart/)
- [Moticon insole construction](https://www.moticon.de/insole3-specs/)
- [IPC-2223 flex design](https://cdn.hackaday.io/files/1644617036299424/IPC-2223-Design-Standard-for-Flex-and-Rigid-Flex-Circuits.pdf)
- [Electrodag 479SS ink](https://www.mouser.lt/datasheet/3/1398/1/LOCTITE_EDAG_479SS_EC_en_GL.pdf)
- [Master Instruments FLPB301031-HPMW30-30](https://www.master-instruments.com.au/products/66658/FLPB301031-HPMW30-30.html)
