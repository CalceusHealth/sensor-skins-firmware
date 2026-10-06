# v3 concept: drawings, simulations and specs

Start with the design brief, `HARDWARE_V3_DESIGN_BRIEF.md` at the repo root. This folder holds its figures and supporting files. Plans are drawn to scale for orthotic size **S**, viewed from above with the toe up. They are concepts for review, not a board layout.

## Drawings

| File | Shows |
|---|---|
| `plan_front_LHS.png`, `plan_front_RHS.png` | **Chosen layout.** Coil tab at the board's heel end; battery in front of the board (toe-ward) at 45° in a die-cut pocket; JST ACH header on the free outer edge; leads routed with a service loop; test points grouped beside the header; bottom-layer service hatch (dashed blue). |
| `sizes/plan_front_<size>_<side>.png`, `sizes/summary.json` | Front-battery layout for every size XXS–XXL (Reid CAL1000–CAL1060), `tools/size_plans.py`. Same board-relative layout at every size: battery at 45° parallel to the FPC tail edge, 2.2 mm in front of the board (set by size S), same header, two-wire lead route and hatch; 120° C magnet flush with the board top, one orientation; tails meet the sensor layer as at size S. Battery test pads 1–4 kept at R4 positions (hand-probeable), new VBAT pad by the header. `sizes/section_<size>_<side>.png`: true section A–A per size through the coil and battery centres. **S–XXL fit. XS: the 31 mm pack misses by one corner (≤ 24 mm fits). XXS: the board itself crosses the orthotic edge with full-length tails (pack ≤ 12 mm).** |
| `plan_behind_LHS.png`, `plan_behind_RHS.png` | Alternative: battery behind the coil tab (needs ~45 mm custom leads). |
| `section.png` | Cross-section of the flat sandwich: top cover (1–2 mm, charging puck on top), board at the top of the bay, coil with ferrite under it, C-shaped magnet just under the cover (schematic; per-size true sections are in `sizes/`), battery in its pocket, service hatch below. Vertical scale ×4. |
| `battery_connection.png` | How the battery connects, stays put, is replaced and is tested. |

## Specs and write-ups

| File | Content |
|---|---|
| `magnet_carrier_RFQ.md` | Quote spec: C-shaped insole magnet and split-ring puck magnet, pre-magnetised on a carrier. |
| `CHARGING_SIM.md` | Charging-circuit simulation: method, calibration, results, and the R4 vs R5 puck A/B test plan. |
| `charging_sim.json`, `magnet_sim.json`, `summary.json` | Raw results behind the brief's tables. |

## Replacing a battery

A bench job of a few minutes, with no soldering:

1. Open the hatch in the bottom layer (re-closable adhesive lid).
2. Lift the tape holding the leads.
3. Unplug the connector (tweezers help; it is polarised).
4. Peel the pack out of its pocket.
5. Press in the new pack, plug it in, re-tape the leads with their loop, and close the hatch.

Replacement packs come with the plug pre-crimped (Master Instruments). Still to confirm on prototypes: whether the plug latches, and how many openings the hatch adhesive survives.

## Regenerating

Run from `hardware/r4-kicad/` with `.venv/bin/python`:

| Command | Produces |
|---|---|
| `tools/battery_drawings.py` | Drawings in this folder |
| `tools/layout_study.py` | `../layout_study/` |
| `tools/magnet_sim.py` | `magnet_sim.json` (the C-arc, rotation and flexible-magnet cases were run as follow-ups) |
| `tools/charging_study.py` | `charging_sim.json` |
