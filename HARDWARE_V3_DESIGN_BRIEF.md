# Hardware v3 Design Brief — Coil Integration, Battery Resilience, Flex-PCB Options

*Prepared 2026-09-29 on branch `tim`. Sources are cited inline; every hardware claim traces to a repo artifact or a linked external reference.*

## 1. Context

Reid Orthotic v2 (R4 board) prototypes have performed well in the field. The only recurring failure modes are:

1. **Recharge-coil wire breakage** — the RX coil is a discrete wound part attached by hand-soldered flying wires.
2. **Battery failures** — bare unprotected cell, hand-soldered tabs, deep-discharge destruction chain (documented in `SLEEP_LOGIC_V2_PROPOSAL.md`), and a charge-current setting above the cell's rating.

This brief surveys the design artifacts we now hold, analyses the two failure modes at the design level, and evaluates three iteration tracks for a v3 board: **(a)** integrating the RX coil into the PCB copper, **(b)** a battery change and/or attachment change, and **(c)** flexible / semi-flexible board construction.

## 2. Inventory of design artifacts

### 2.1 In this repo

| Artifact | Path | Notes |
|---|---|---|
| R4 schematic (5 sheets + assembly view) | `artefacts/Reid Orthotic v2 R4.pdf` (identical copy in the Design Verification pack) | CAL1020, Carbon Circuits, Alex Gilmour, sheets updated 5/09/2024 |
| R4 gerbers, LHS + RHS | `artefacts/SSII Orthotics Electronics - Design Verification/Reid Orthotic v2 R4 {LHS,RHS} Gerbers/` | Exported 05/09/2024 from `Reid Orthotic v2 {LHS,RHS}.CSPCBDoc` |
| R4 BOM (full MPNs + Digi-Key PNs) | `.../Reid Orthotic v2 R4 BOM.xlsx` | See §2.3 highlights |
| Pick-and-place, LHS + RHS | `.../Reid Orthotic v2 R4 {LHS,RHS} PnP.csv` | |
| R3 mechanicals | `artefacts/Reid Orthotic v2 R3 MECH/` | SS-483 V1 LHS/RHS, STEP + STL exports only |
| R1/prototype PCB layout (editable) | `artefacts/Reid Orthotic Prototype LHS.CSPCBDoc` | CircuitStudio PCB 6.0; internal refs say "Reid Orthotic Prototype", "REID Sensor LHS R1" — an ancestor of v2, not the R4 board |
| Charger puck: full CircuitStudio project + mech CAD | `imports/alex_jira_duplicates/firmware/Reid Orthotic v2 Charger R2 Design Files/`, `...Charger Mech/` | Schematic PDF alongside; spec in `artefacts/orthotic-charger-puck-1` |
| Coil/charging engineering log | `artefacts/carbon-circuits-1` | Coil selection, dead-battery boot fix, rework mods, magnet polarity |
| Cell datasheet | `artefacts/Routejade-FLPB301031-HPMW30-30.pdf` | 76 mAh nom / 70 mAh min, 4.35 V chemistry |
| Sensor layout | `artefacts/{CAP,FSR}-{L,R}.png`, `artefacts/all_sensor_coordinates.csv` | 19 FSR / 6 CAP / 5 TMP per foot |
| Battery failure analysis | `SLEEP_LOGIC_V2_PROPOSAL.md` §hardware findings | Deep-discharge chain, ISET rework rec., re-celling checklist |

### 2.2 Board construction (from the R4 fab drawing, GM3 layer)

- **4-layer FR4, 0.4 mm finished thickness**, 1 oz (35 µm) copper, ENIG, black soldermask, white silk, IPC-A-600 Class 2, e-tested, no panelization.
- Physical board outline: **21.25 × 46.25 mm** (one closed loop on the GM4 "Board Outline" layer; the GM3 sheet is the fab drawing). Vias down to 0.2 mm. (Note: raw copper-layer extents read larger because the gerbers draw their "Top/Bottom Layer" captions as copper strokes.)
- At 0.4 mm the board is already thin enough to flex appreciably — the v2 design implicitly relies on thin-FR4 compliance rather than a rated flex construction.

### 2.3 Electrical facts relevant to this brief (R4 schematic + BOM)

- **Charge chain:** RX coil → J3 "RF_CONN" pads → C37 **33 nF C0G** resonant cap (C38 DNF) → D1 CUS08F30 Schottky (half-wave) → D2 5.1 V zener clamp → 2× 100 µF → VSYS → BQ24210 VBUS.
- **BQ24210:** ISET R56 = 5.1 k, schematic note "R = 400/Iout, 5.1k = 80 mA". TS pin populated: R59 22 k + R60 NCU15XH103 10 k NTC (B=3380) — battery temp sense exists on-board.
- **Battery path:** cell → F1 200 mA fuse → VBAT; J4 "BAT_CONN" footprint present but **DNF**.
- **All connectors DNF:** J1/J2 (sensor membrane, 20-pin, 100R series), J3 (coil), J4 (battery) are DO-NOT-FIT on the BOM → every off-board connection is a hand-soldered wire joint. **Both field failure modes are exactly these joints.**
- **RF:** Abracon AMCA31-2R450G chip antenna, matching L3 3.9 nH + C15 1.5 pF (C16 DNF) — any stackup/outline change forces re-validation.
- 3V3 LDO TCR3UF33A; UVLO divider provision (R57/R58) unfitted; firmware System-OFF UVLO at 3100 mV instead.

### 2.4 What is still missing (ask-list for Alex)

1. **The editable `Reid Orthotic v2` CircuitStudio project** (R4): both `Reid Orthotic v2 LHS/RHS.CSPCBDoc` — the exact files the Sep-2024 gerbers were exported from (export logs reference a `Reid Orthotic v2` folder on "Jimmy"'s machine) — plus all `.SchDoc` sheets (IO/MCU/MUX/PWR), `.PrjPcb`/`.PrjPcbStructure`, `.SchLib`/`.PcbLib`, `.OutJob`, and anything newer than R4.
2. **Native mechanical CAD**: Fusion 360 `.f3d` for SS-483 and the DXF/outline that drives the PCB shape (we hold only STEP/STL).
3. **RX coil work instruction**: attachment drawing, wire gauge, pad locations, strain-relief method, and confirmation of resonant component values for the WR151580-48F2-G pairing.
4. **Fab & assembly package**: which board house built R4, quoted stackup, assembly drawings, fab design rules.
5. **Battery attachment detail**: tab join method (spot-weld vs hand-solder); why J4 was made DNF; any protected-cell evaluations already done.
6. **Chip-antenna tuning report** (L3/C15 matching), if one exists.
7. **As-built truth**: confirm fielded units are R4 and obtain the per-batch hand-rework list (the `carbon-circuits-1` log mentions enable-divider and bulk-cap mods).

> Suggested one-liner: *"Can you send the complete CircuitStudio project folder for Reid Orthotic v2 (R4) — both LHS and RHS .CSPCBDoc, all .SchDoc sheets, .PrjPcb and libraries — the one the Sep-2024 R4 gerbers were generated from? We already have the Prototype/R1 LHS layout."*

**Contingency if the CAD never materialises:** the as-built record (gerbers + PnP + BOM + schematic PDF with printed net names) is sufficient to reverse-engineer an editable KiCad project. Estimated days of careful work with transcription risk; the antenna matching region and cap-electrode geometries must be copied exactly, never re-derived. This is *not* a blocker for v3: the layout is substantially redrawn for any of the tracks below anyway.

## 3. Failure analysis

### 3.1 Coil wire

The RX coil is absent from the BOM entirely. It is a hand-attached wound component whose leads are soldered to the J3 pads, and those leads see per-step flex and shear inside the insole.

The first builds had no strain relief (`artefacts/carbon-circuits-1`: "Coils soldered with wires exiting the base of the PCBA. No strain relief added"). The current process does relieve it (`artefacts/assembly-process.md`):
- 10 mm heatshrink over the leads
- a C-shaped bend
- glue at both ends of the heatshrink
- a 4 × 3 mm alignment magnet superglued into the coil centre

Coil-wire failures continuing under that process would point to the fine wire flexing right at the heatshrink exit or the solder fillet. Which builds the failed units came from is worth checking. Either way, the joint stays hand-made in five manual steps per unit.

A PCB-etched coil (Track A) removes all five steps and the joint itself. It has to keep a central clearance for the magnet, and the magnet sits in the coil's field, so tuning must be checked with it fitted.

### 3.2 Battery

Three compounding design-level causes, all already evidenced in the repo:

1. **No cell protection**: bare cell, no PCM; deep discharge below damage threshold was possible until the firmware UVLO (System OFF < 3100 mV) was added — and firmware protection cannot cover a disconnected/latched-off state the way a PCM does.
2. **Charge current above rating**: ISET gives ~80 mA vs the cell's 70 mA max (35 mA standard). In practice the ~5 mA wireless input means the full 80 mA is rarely reached, but any bench/direct charge hits it.
3. **Hand-soldered tab joints** (J4 DNF) — same mechanical failure class as the coil wires. One fielded unit (REIDLHS E1:2F:FB:FA:78:EB) already shows the dead-cell/connection signature.

### 3.3 Battery failure modes → design requirements (priority 2, after the coil)

The cell is confirmed bare from its datasheet (`artefacts/Routejade-FLPB301031-HPMW30-30.pdf`):
- bare 2.0 mm tabs
- no protection circuit in the spec
- safety tests run on the naked cell

Alex's note that dead cells read "high impedance due to protection circuit" is more consistent with a deeply discharged cell; which cells were fitted should be confirmed with him. Today's only over-discharge protection is the firmware cutoff.

| Failure mode | Mechanism in the current design | v3 requirement |
|---|---|---|
| **Physical stress** | <ul><li>The pouch is stacked on the components on a 0.6 mm tape pad (R3 model), so body weight pushes 0805/QFN/SOT-23 corners into the pouch as point loads. Repeated loading of a pouch's soft side is a known route to separator damage and internal micro-shorts. This is a hypothesis: tear down failed cells and look for dents matching the board layout.</li><li>Hand-soldered tabs: iron heat at the pouch seal, plus fatigue at the joint.</li></ul> | <ul><li>Never stack the cell on components. Place it flat (see the placement study) or behind a rigid stiffener.</li><li>Use a low-load zone or a foam pocket.</li><li>Use factory-welded tabs or leads to a board-mounted connector; no iron on the cell.</li><li>A steel-can coin cell where load is high.</li></ul> |
| **Firmware drains the cell past protection** | <ul><li>Protection is firmware-only: System OFF below 3100 mV.</li><li>It works only while firmware runs correctly; the MCU brownout reset is disabled and a crash or hang bypasses it.</li><li>After cutoff the board still draws its System OFF current from the cell, and nothing disconnects it. A unit left off the puck slides below 3.0 V in weeks; how fast depends on the System OFF current, which has not been measured (SEN-49).</li></ul> | <ul><li>**Hardware disconnect independent of firmware.** A protection IC with dual FET on the main board (e.g. BQ29700 family) cuts the entire load at ~2.8–3.0 V, then sits at ~0.1 µA power-down, and recovers only when the charger is present.</li><li>Firmware cutoff stays as a graceful first line (3.1–3.25 V).</li><li>Do not use the LDO-enable divider (R57/R58) as the cutoff: it doesn't disconnect the rest of the load and its own divider current drains the cell.</li></ul> |
| **Protection itself damaged** | <ul><li>Not possible today, because there is no hardware protection.</li><li>On a protected-pack design, the PCM sits on a tiny board at the cell, in the flex/impact zone, near the hand-soldered joints. ESD, iron heat or a cracked joint there would leave the cell silently unprotected.</li></ul> | <ul><li>Put the protection on the rigid main board: reflow-soldered, ESD-protected at the battery connector.</li><li>Optionally keep a PCM-protected cell as a second, independent layer, so one damaged layer doesn't leave the cell exposed.</li><li>Fix the charge current regardless (ISET, SEN-105).</li></ul> |

## 4. Option analysis

*(sections below populated from external research — pending)*

### 4.1 Track A — PCB-integrated RX coil

**Current coil, from the TDK datasheet** (`artefacts/TDK-WR151580-48F2-G_Spec.pdf`): the coil is a **TDK WR151580-48F2-G** (the engineering log's "large coil"), used on both the charger and the orthotic.

- **Electrical:** Ls 27.1 µH typ at 100 kHz; Rs 0.5 Ω max at 100 kHz. That gives Q ≥ 34 at 100 kHz. TDK publishes no Q at the drive frequency.
- **Construction:** a 4-layer air coil, Ø15.0 ± 0.3 mm, bonded to its own ferrite sheet.
- **Stack thickness** (typ / max, mm): coil + resin 1.69 / 1.78, adhesive 0.135, ferrite 0.80 / 0.88. Total 2.63 / 2.80 mm.
- **Leads:** 15 mm, solder-coated tips.
- **Note:** the 27.1 µH is measured on that ferrite, so the ferrite is part of the inductance.

**Tuning check:** both tanks use 33 nF C0G (orthotic C37, charger C7). With 27.1 µH each tank resonates at **168 kHz** on its own. The charger firmware drives at 10 MHz ÷ 53 = **189 kHz** (`Charger Firmware R4/coil.c`), assuming the ATtiny's factory-default 20 MHz oscillator; no fuse setting is stored in the repo. With a 16 MHz fuse it would be 151 kHz.

When two identical tuned coils couple, the single resonance splits into two peaks at f₀/√(1 ± k), where k is the coupling. At k ≈ 0.2 the peaks are 154 and 188 kHz. Either clock setting therefore lands on one of the two coupled peaks rather than on 168 kHz. The likeliest explanation is that the drive was tuned empirically to the coupled pair, not to the coil alone. This is inferred, not measured. A scope on the TX coil (frequency, plus V_RX against TX frequency) would settle it.

**Implication for a PCB coil:** match the product L·C = 27.1 µH × 33 nF ≈ 0.894 µH·µF, then re-find the coupled peak on hardware. For example, 6 µH needs ≈ 149 nF, 12 µH ≈ 75 nF, and 18 µH ≈ 50 nF.

Height is a second win. The wound coil's 2.6 mm stack (0.8 mm of it ferrite) would become copper inside the PCB plus a 0.1–0.3 mm ferrite sheet, saving roughly 2 mm of insole height.

Two constraints carry over to any replacement: the 4 × 3 mm alignment magnet glued in the coil centre (`artefacts/assembly-process.md`) needs a central clearance, and its effect on L and Q has to be measured with it fitted.

**Etched spiral feasibility (honest numbers):** at 125 kHz a PCB spiral is ~10–20× worse in Q than the wound coil — Mohan/current-sheet estimates for 1 oz copper give ~6 µH / Q≈2 at 15 mm OD, ~18 µH / Q≈3 at 30 mm OD ([Mohan et al.](https://web.stanford.edu/~boyd/papers/pdf/inductance_expressions.pdf), applied per [TI SNOA930](https://ti.com/document-viewer/lit/html/SNOA930C/GUID-BD74982D-17B2-4C89-9F5B-396B12381139)). **This mostly costs alignment margin, not feasibility** — at our ~20 mW / 5 mA charge level, link efficiency is nearly irrelevant; the requirement is that the rectified voltage clears the BQ24210's input minimum (~3.5 V) at worst-case alignment. Levers to claw back Q: largest OD that fits (the insole has area the 15 mm wound coil never used), widest traces, both spare copper layers in series, 2 oz outer copper, and — since the link is proprietary — **raising the TX frequency to 250–500 kHz recovers Q linearly** (the ATtiny TX makes this a firmware + cap change).

**Ferrite backing is mandatory, not optional:** the spiral sits over the board's planes/battery, and without a ferrite layer L and Q collapse from eddy loading ([TI SLYT479](http://www.ti.com/lit/an/slyt479/slyt479.pdf)). Use an adhesive-backed flexible sintered sheet — Würth **WE-FSFS** (0.1–0.5 mm, survives bending; [ANP022](https://community.element14.com/products/manufacturers/wuerth-elektronik/w/documents/3558/anp022-selection-and-characteristics-of-we-fsfs)), TDK Flexield, or KEMET Flex Suppressor.

**Coil-in-flex is proven practice** at exactly this power class — Minco FlexCoils supplies polyimide-flex WPT/telemetry coils for hearing aids and implantables ([Minco](https://www.minco.com/wp-content/uploads/Minco_FlexCoils.pdf)) — but flex copper is 0.5–1 oz, roughly doubling DCR again. **Put the coil on a rigid island / stiffened zone with 1–2 oz copper** rather than in a flexing web.

**Retuning:** keep the TX tank; scale the RX cap by C′ = 33 nF × (27.1 µH / L_new); tune in situ over ferrite + board (not in air); sweep TX PWM frequency/duty against DC output on hardware. Validate on a **cheap 2-layer coil coupon** before committing the board.

**Placement study (to scale):** `hardware/r4-kicad/layout_study/layout_{LHS,RHS}.png`, regenerated by `tools/layout_study.py`.

Sources:
- The R3 assembly model, for the sensor layer and the R3 board position.
- The R4 KiCad board outline, which overlaps the R3 board at 0.97 IoU on LHS and 0.98 on RHS.
- The sensor pad CSV, registered onto the sensor layer.
- The orthotic outline, traced from the Reid Print CAL1020 rev 2 drawing (`hardware/CAL1020 V2 Rev0.jpg`; 93.10 × 270.00 mm, membrane inset 11.75 / 25.00 mm).

Results:
- **Current build:** the 15 mm coil's centre sits ~18 mm heel-ward of the board's heel edge, on a clear bridge (`hardware/RHS.jpg`).
- **Coil tab:** an etched 15 mm coil on a tab directly at that edge (1 mm gap, 1 mm margin) makes the board **21.4 × 63.3 mm**. The coil sits ~9.5 mm closer to the board than today's, inside space the coil and bridge already occupy.
- **Clearances:** the tab overlaps no part of the sensor layer, with no FSR/CAP/temperature pad within 2 mm. The closest the board-plus-tab comes to the orthotic edge is 8.25 mm, set by the existing board, not the tab.
- **Battery:**
  - A VARTA CP1254 coin (Ø12.1) fits within the board footprint where the pouch cell is stacked today.
  - A LIR2032 (Ø20) only just fits across the board's 21.25 mm width.

**Caveat on size:** the PCB is shared across all seven orthotic sizes (XXS–XXL), but the membrane and orthotic outline scale with size. This study covers only the CAL1020 drawing, which is size **S (Small)**. The tab and battery placement must be re-checked against the **smallest (XXS)** membrane and orthotic outline before the board outline is committed; drawings for all sizes are to be added to `hardware/`. The 8.25 mm edge clearance will shrink on smaller sizes.

**Direction (2026-10-03): flat devices first.** The Dolapro shaped shell has been uncomfortable in the current prototypes, so the first v3 devices will be flat insoles. Consequences:

- **No shell pocket.** Thickness adds directly under the foot unless the battery and board sit in a pocket in the insole's foam base, as in fully embedded insoles such as Moticon.
- **Edge clearances are provisional.** The study's figures use the Dolapro outline from CAL1020 as a stand-in; the flat insole's own outline and layer build-up are still to be supplied.
- **Comfort ranks placement.** Low-load zones (under the medial arch) are preferred. Toe-ward and heel-ward battery positions favour a thin cell, or a cell pocketed in the foam.

**Thickness (design goal: thinnest possible orthotic).** Measured from the R3 assembly model:

| Item | Height |
|---|---|
| Sensor layer | 0.6 mm |
| PCB | 0.4–0.5 mm |
| Tallest real parts on the board (100 µF 0805 capacitors, SOT-23) | ~1.2–1.4 mm |
| Battery tape pad | 0.6 mm |
| Pouch cell | 3.2 mm |
| **Overall today** | **5.4 mm** |

The overall figure is high because the battery is stacked on top of the components. The model also shows a 4 × 4 × 4.7 mm object on the board; it is most likely the Tag-Connect plug model (J5), which is not fitted on R4.

- **Biggest lever: stop stacking.** With the battery beside the board instead of on it, the orthotic's thickest point becomes the cell itself: about 3.3–3.5 mm with today's pouch, roughly 2 mm thinner. This holds for any battery choice.
- **Coin cells do not help thickness.** The CP1254 (5.4 mm) is ruled out. The LIR2032 (3.2 mm) matches today's thickness with about two-thirds of the capacity, so it gains robustness only.
- **Battery under the coil, on the tab:** possible electrically, but worse on thickness and in conflict with the magnet:
  - A metal battery directly behind the coil absorbs the charging field. It needs a ferrite sheet between coil and battery, as phones use, and the coupling must be re-tuned with the battery fitted.
  - The stack becomes PCB 0.4 + ferrite ~0.3 + cell, about 0.7 mm thicker than placing the cell beside the board.
  - The 4 × 3 mm alignment magnet in the coil centre would sit directly over the battery.
- **Battery placement search** (`layout_study/battery_options_{LHS,RHS}.png`, size S): a 1 mm / 15° grid search for flat, unstacked positions. Each candidate must sit ≥ 3 mm inside the orthotic outline and ≥ 1 mm clear of the sensor layer, the sensor tails, and the board with its coil tab. The result is the nearest legal position to the board in each zone:
  - **Toe-ward** (Reid's "battery in front of the PCB" layout):
    - Today's 31 × 10.2 pouch fits diagonally at 45°, 2.2 mm from the board on LHS (upright at 6.3 mm on RHS, whose tails differ slightly).
    - A LIR2032 fits 10 mm from the board.
    - Reid's two sketched positions on LHS clip the sensor-layer edge (≈14 and 32 mm²) and sit 17–24 mm from the board. The search position is the same idea, pulled in tight.
  - **Heel-ward, behind the coil tab:**
    - Pouch at ~105°, 1.3 mm away.
    - Thin 33 × 15 × 2 pouch (estimated cell), 1.2 mm away.
    - LIR2032, 1.0 mm away.
    - This area takes heel-strike load, which suits a steel-can coin cell better than a pouch.
  - **Beside the board:** nothing fits; the sensor tails occupy it.
  - The thin pouch fits only heel-ward.
  - **Caveat:** tail and sensor-layer outlines come from the R3 model's trace layer. The real laminate/RF-shield outline is somewhat wider, so placements should be re-checked against Reid's full-stack outline and every size, XXS first.
- **Next lever: a thinner cell with a larger footprint.** Pouch capacity scales roughly with volume. Catalogue cells show the trend: 4 × 20 × 30 mm ≈ 200 mAh and 5 × 20 × 30 mm ≈ 250 mAh. By extrapolation, a ~2 × 20 × 30 mm cell should roughly match today's 76 mAh at about two-thirds the thickness; this is an estimate to confirm with a supplier, e.g. a Master Instruments custom pack with protection and a connector. Flexible cells such as Jenax J.Flex (0.5–2.3 mm, 10 mAh upward; dynamic-bend tested at 20 mm radius) are a longer-shot lead for a cell that tolerates insole flexing.

**Alternative worth a serious look — NFC WLC (13.56 MHz):** purpose-built for etched PCB antennas (only ~1–5 µH needed; PCB Q of 30–60 is easy), with power classes from 250 mW ([NFC Forum WLC](https://nfc-forum.org/build/specifications/wireless-charging/)). The **Renesas PTX30W** listener IC integrates the rectifier *and* a 5–250 mA Li-ion charger + LDO in 1.78 mm² — it would replace the coil, the rectifier chain (D1/D2/C37/C40/C42), *and* the BQ24210 ([Renesas](https://www.renesas.com/en/products/wireless-connectivity/nfc/ptx30w-highly-integrated-scalable-nfc-wlc-listener-i-c-interface-and-board-pmic-ldo)). Cost: the charger puck must be respun around a PTX130W-class poller, and both sides need 13.56 MHz tuning discipline. **If the puck is being redesigned anyway, this is a genuinely strong candidate; if the puck must stay, the retuned 125 kHz PCB spiral is the pragmatic fix.**

### 4.2 Track B — Flexible / semi-flexible / rigid-flex construction

**Semi-flex FR4 is ruled out.** It is depth-milled FR4 rated for flex-to-install only — Eurocircuits: "1-time bend… no dynamic flex"; TTM quotes a bend life of typically ~5 cycles ([Eurocircuits](https://www.eurocircuits.com/technical-guidelines/designing-flex-install-pcbs/), [TTM](https://www.ttm.com/sites/default/files/documents/Semi-FlexPrintedCircuitTechnology.pdf)). An insole sees ~1M+ flex cycles/year.

**The architecture that fits is the "Moticon pattern": a small rigid electronics island + flexible webs.** Moticon's fully-embedded Insole3 uses exactly this — a circular rigid electronics module plus a separate sensor foil layer ([Moticon specs](https://www.moticon.de/insole3-specs/)); NURVV went further and moved electronics outside the shoe entirely. Our R4 already half-follows the pattern (the ~40×74 mm cluster sits in the arch), but implements it as 0.4 mm-thin FR4 that flexes without being rated for it, with hand-soldered wires crossing every boundary.

Two viable constructions:

| | Polyimide flex + stiffeners | Rigid-flex |
|---|---|---|
| Structure | 2–4-layer flex; FR4/PI stiffeners glued under component zones | Rigid 4-layer islands + 1–2-layer flex webs |
| Meets 0.2 mm via / 0.1 mm trace? | Yes, in stiffened zones (JLCPCB 0.15 mm vias, PCBWay 0.1 mm track) | Yes, in rigid islands |
| Cost vs rigid 4-layer | ~2–4× | ~7–10× ([Minco](https://www.minco.com/the-drivers-behind-higher-cost-rigid-flex-pcbs/)) |
| Fab options | Many (JLCPCB, PCBWay, Würth, Epec, Cirexx) | Fewer; verify dynamic (IPC-2223 Use B) rating — Würth RIGID.flex explicitly supports it |

**Design rules that will bind the layout** (IPC-2223, dynamic = "Use B", specify cycle count on the drawing):
- Dynamic bend radius ≥100× thickness → flex webs must be thin (0.1–0.15 mm, ideally single copper layer on the neutral axis) and use **rolled-annealed copper** (ED copper fatigues).
- **No vias or components in flex zones**; vias ≥20 mil from rigid/stiffener transitions.
- Traces perpendicular to bend lines, curved corners, staggered on opposite layers, hatched pours only, teardrops everywhere, **coverlay not soldermask** in flexing areas.

**Sensor membrane integration:** printing FSR carbon ink over gold-plated copper electrodes on polyimide flex is established practice ([FSR integration guide](https://pololu.com/file/0J749/FSR400-Series-Integration-Guide-13.pdf)), and would eliminate the J1/J2 tail joints entirely — but it puts copper under repeated pressure/flex, stiffens the sensor area vs the current PET membrane, makes sensor-geometry iteration a PCB respin, and restricts fabricator choice. **Lower-risk capture of most of the win: keep the printed-PET membrane, replace its hand-soldered joints with a ZIF or hot-bar-bonded flex tail.**

### 4.3 Track C — Battery change & attachment

**A hidden finding first: the current cell is chronically undercharged.** The BQ24210 regulates to a fixed 4.2 V ±1 % ([TI](https://www.ti.com/product/BQ24210)) but the FLPB301031 is 4.35 V chemistry — so today's cells only ever reach ~85–90 % of rated capacity (~65 mAh effective). Any 4.2 V-chemistry replacement cell gives up less real capacity than the datasheet numbers suggest.

**Cell options** (honest availability picture — protected cells at 3 × 10 × 31 mm are rare):

| Option | Part | Specs | Trade-off |
|---|---|---|---|
| Off-the-shelf protected + wired | **Renata ICP331319PM** ([Mouser](https://www.mouser.fr/new/renata/renata-icp-series)) | 50 mAh, 4.2 V, ≤3.7 × 12.8 × 21 mm, built-in safety circuit, AWG30 wires | Different footprint (wider/shorter); real loss vs today's undercharged cell only ~15 mAh |
| Same-footprint, wired, unprotected | Renata ICP281029HPG | 68 mAh, 4.35 V, 3.3 × 10.2 × 30.5 mm, AWG30 wires, 34 mA std / 68 mA max charge | Fixes the tab-solder joint, not protection |
| Custom protected pack | **Master Instruments pack-up** of FLPB301031 or a 4.2 V LP301030 (80 mAh class) | PCM + PicoBlade/JST-SH pigtail, thickness stays ~3 mm (PCM folds behind cell) | MOQ/lead time; but they already re-cell for us |
| Keep bare cell, protect on-board | TI BQ29700 (+dual FET, ~15 mm² total) or DW01-class | Hardware UV/OV/OCD backstop under the 3100 mV firmware cutoff | Doesn't fix the tab joint |

Varta CoinPower/EZPack and stocked Jauch PCM packs are all too thick or too big; SMT holders for pouch cells effectively don't exist at this size — the industry pattern is **PCM + wire pigtail + 1.0–1.25 mm connector** (Molex PicoBlade 51021 / JST SH) on the rigid PCB section.

**Charger correctness (verified):** BQ24210 K_ISET ≈ 395 AΩ, valid range 50–800 mA — our 5.1 k gives ~77–82 mA into a 70 mA-max cell, confirmed out of spec. It "works by accident" today only because the ~5 mA wireless source droops into the VBUS-DPM regulation; any stiff 5 V bench/test source will push the full ~77 mA. Fixes:
- **Interim (existing boards):** ISET 5.1 k → 10 k (~40 mA; technically below the 50 mA validated range, accuracy unspecified but safe-direction).
- **v3:** replace with **TI BQ25100** family — 10–250 mA range, chemistry-matched variants (BQ25100 = 4.20 V, **BQ25100H = 4.35 V**), 75 nA battery leakage. Target 25–35 mA (0.35–0.5C). Note termination = ISET/10, so a 30 mA setting terminates at 3 mA — still workable under the 5 mA coil budget, whereas today's 8 mA termination point arguably never triggers cleanly on the pad.
- Add **MAX17048** fuel gauge (0.9 × 1.7 mm WLP, 3–4 µA) — directly addresses the long-standing erratic battery-% (curve-flip/reseed issues) rather than patching firmware heuristics over a raw divider.

**Mechanical:** cell always on a rigid island, never spanning a flex zone (conformal-wearable patents US11064604/US11251497 consistently do this); wire leads with a service loop; adhesive staking over joints; connector on the rigid section with leads exiting away from the flex direction.

## 5. Recommendations

The unifying observation: **every field failure is a hand-soldered flying-wire joint crossing a flex boundary** (coil at J3, battery tabs at J4 — both DNF connector footprints). The v3 design goal is therefore: *no hand-soldered wires, nothing rigid spanning a flex zone.*

### Recommended v3 architecture

1. **Construction:** polyimide flex with stiffeners (IPC-2223 Use B, RA copper, specified cycle count), Moticon-pattern — one stiffened electronics island in the arch carrying the MCU/IMU/charger/antenna cluster and the battery; 1–2-layer flex webs to the sensor zones. Rigid-flex (~7–10× cost vs ~2–4×) is the step-up only if the stiffener transitions or cluster density fail.
2. **Coil:** etch the RX spiral into copper on a stiffened zone (largest OD that fits, ≥25–30 mm, 2 oz if possible) over a WE-FSFS-class ferrite sheet; retune C37 and consider raising the link to 250–500 kHz. Validate with a 2-layer coupon first (≥3.5 V rectified at worst-case alignment). **Decision gate:** if the charger puck gets respun anyway, evaluate NFC WLC (Renesas PTX30W/PTX130W EVK) head-to-head against the coupon — it deletes the coil, rectifier chain, and BQ24210 in one move.
3. **Battery:** connectorized protected pack — Master Instruments custom pack-up (FLPB301031 or 4.2 V LP301030 class) with PCM + PicoBlade/JST-SH pigtail; fit the J4-class receptacle on the island (stop leaving it DNF). Off-the-shelf fallback: Renata ICP331319PM (50 mAh protected, wired, 4.2 V — only ~15 mAh worse than today's undercharged 76 mAh cell) if 12.8 × 21 mm fits.
4. **Charger:** BQ25100 (4.2 V cell) or BQ25100H (4.35 V) at 25–35 mA — unless NFC WLC absorbs the charger function. Add a MAX17048 fuel gauge. If any bare-cell path survives, add BQ29700 + dual FET as a hardware backstop.
5. **Immediate rework on existing prototypes (no respin):** ISET 5.1 k → 10 k (~40 mA) so bench charging can't exceed the cell rating; adhesive-stake the coil and battery joints with a service loop.

### Sequencing

1. Send Alex the ask-list (§2.4) — the v2 CAD halves the layout effort.
2. Build the coil coupon + ferrite test (cheap 2-layer board, days not weeks) and, in parallel, get a Master Instruments quote for the protected pack.
3. Decide the puck question (keep 125 kHz vs NFC WLC) based on coupon + EVK results.
4. Then commit the v3 stackup with a flex-experienced fab (Würth, Epec, Cirexx quote alongside PCBWay/JLCPCB).
5. Re-validate the 2.4 GHz chip antenna matching (L3/C15) on the new stackup — mandatory regardless of track.

## 6. Research provenance & confidence notes

- PCB-coil L/Q figures are Mohan-formula estimates (±10–20 %), not measurements; WR151580 Q is derived from DCR (not published by TDK).
- Flex/rigid-flex cost multipliers are industry-typical ranges from fab sources, not quotes.
- Protected-cell availability at 3 × 10 × 31 mm was searched across Renata, Jauch, Varta, EEMB, LiPol, Grepow, PowerStream — the "rare, go custom via Master Instruments" conclusion reflects genuine scarcity, not a thin search.

## 7. Linear references

No PCB/CAD design artifacts are stored in Linear; the hardware trail lives in these issues:

- **[SEN-179](https://linear.app/calceus-health/issue/SEN-179/hardware-v3-pcb-integrated-recharge-coil-protected-battery-flex)** — *this work*: Hardware v3 tracking issue (design brief + next actions checklist), Firmware project.
- **[SEN-105](https://linear.app/calceus-health/issue/SEN-105/sleep-v2-11-hardware-charge-current-80-ma-exceeds-cell-max-70-ma-iset)** (Backlog) — the ISET 5.1k → 10k charge-current rework; §5.5's interim fix. Should be executed on existing prototypes regardless of v3.
- **[SEN-102](https://linear.app/calceus-health/issue/SEN-102/sleep-v2-8-vbat-scale-is-a-timing-artifact-divider-is-2-31-constant)** (In Progress) — vbat ÷2 divider / ×3.1 timing artifact; superseded in v3 by the MAX17048 fuel-gauge recommendation.
- **[SEN-106](https://linear.app/calceus-health/issue/SEN-106/sleep-v2-12-firmware-uvlo-system-off-below-3100-mv-wake-on-charger-pg)** (Done) — firmware UVLO (System OFF < 3100 mV); the PCM in the recommended protected pack becomes the hardware backstop beneath it.
- **[SEN-87](https://linear.app/calceus-health/issue/SEN-87/uat-recharge-add-a-placement-markguide-on-the-orthotic-for-the-usb)** (Backlog, UAT) — puck placement mark/guide; directly interacts with Track A's alignment-margin trade-off (a larger PCB spiral relaxes it).
- **[SEN-172](https://linear.app/calceus-health/issue/SEN-172/build-and-commission-the-50-pair-fleet-for-november)** (Backlog, iOrthotics Trial) — 50-pair November fleet build; the schedule constraint that decides whether v3 lands before or after the fleet (the §5.5 reworks apply to the fleet either way).
- **[SEN-49](https://linear.app/calceus-health/issue/SEN-49/battery-drain-and-sleep-current-audit-certify-no-firmware-drain-path)** (Backlog) — battery-drain audit certifying no firmware drain path; complements the hardware protection story.
