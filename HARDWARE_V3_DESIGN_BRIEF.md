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

The RX coil is absent from the BOM entirely — it is a hand-attached wound component whose enamel wires are soldered to the J3 pads and then experience per-step flex and shear inside the insole. There is no strain relief called out anywhere in the design record. Failure at this joint is a predictable outcome of an unconstrained wire-to-pad joint in a dynamic-flex environment, not a workmanship anomaly.

### 3.2 Battery

Three compounding design-level causes, all already evidenced in the repo:

1. **No cell protection**: bare cell, no PCM; deep discharge below damage threshold was possible until the firmware UVLO (System OFF < 3100 mV) was added — and firmware protection cannot cover a disconnected/latched-off state the way a PCM does.
2. **Charge current above rating**: ISET gives ~80 mA vs the cell's 70 mA max (35 mA standard). In practice the ~5 mA wireless input means the full 80 mA is rarely reached, but any bench/direct charge hits it.
3. **Hand-soldered tab joints** (J4 DNF) — same mechanical failure class as the coil wires. One fielded unit (REIDLHS E1:2F:FB:FA:78:EB) already shows the dead-cell/connection signature.

## 4. Option analysis

*(sections below populated from external research — pending)*

### 4.1 Track A — PCB-integrated RX coil

**Current coil, verified:** the RX coil is a **TDK WR151580-48F2-G** (the engineering log's "large coil"): 27.1 µH, 500 mΩ DCR, 15 mm dia wound part, estimated Q ≈ 42 at 125 kHz ([Digi-Key](https://www.digikey.com/en/products/detail/tdk-corporation/WR151580-48F2-G/8019506); Q derived from DCR — not published). It resonates with C37 (33 nF) on the board.

**Etched spiral feasibility (honest numbers):** at 125 kHz a PCB spiral is ~10–20× worse in Q than the wound coil — Mohan/current-sheet estimates for 1 oz copper give ~6 µH / Q≈2 at 15 mm OD, ~18 µH / Q≈3 at 30 mm OD ([Mohan et al.](https://web.stanford.edu/~boyd/papers/pdf/inductance_expressions.pdf), applied per [TI SNOA930](https://ti.com/document-viewer/lit/html/SNOA930C/GUID-BD74982D-17B2-4C89-9F5B-396B12381139)). **This mostly costs alignment margin, not feasibility** — at our ~20 mW / 5 mA charge level, link efficiency is nearly irrelevant; the requirement is that the rectified voltage clears the BQ24210's input minimum (~3.5 V) at worst-case alignment. Levers to claw back Q: largest OD that fits (the insole has area the 15 mm wound coil never used), widest traces, both spare copper layers in series, 2 oz outer copper, and — since the link is proprietary — **raising the TX frequency to 250–500 kHz recovers Q linearly** (the ATtiny TX makes this a firmware + cap change).

**Ferrite backing is mandatory, not optional:** the spiral sits over the board's planes/battery, and without a ferrite layer L and Q collapse from eddy loading ([TI SLYT479](http://www.ti.com/lit/an/slyt479/slyt479.pdf)). Use an adhesive-backed flexible sintered sheet — Würth **WE-FSFS** (0.1–0.5 mm, survives bending; [ANP022](https://community.element14.com/products/manufacturers/wuerth-elektronik/w/documents/3558/anp022-selection-and-characteristics-of-we-fsfs)), TDK Flexield, or KEMET Flex Suppressor.

**Coil-in-flex is proven practice** at exactly this power class — Minco FlexCoils supplies polyimide-flex WPT/telemetry coils for hearing aids and implantables ([Minco](https://www.minco.com/wp-content/uploads/Minco_FlexCoils.pdf)) — but flex copper is 0.5–1 oz, roughly doubling DCR again. **Put the coil on a rigid island / stiffened zone with 1–2 oz copper** rather than in a flexing web.

**Retuning:** keep the TX tank; scale the RX cap by C′ = 33 nF × (27.1 µH / L_new); tune in situ over ferrite + board (not in air); sweep TX PWM frequency/duty against DC output on hardware. Validate on a **cheap 2-layer coil coupon** before committing the board.

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
