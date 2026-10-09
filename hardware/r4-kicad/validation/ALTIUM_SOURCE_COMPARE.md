# R4 KiCad reconstruction vs Alex's original CircuitStudio source

Alex's editable R4 project arrived 2026-10-09 (`hardware/Reid Orthotic v2 Design Files/`).
Its PCB docs were saved 2024-09-05 11:41, 13 minutes after the R4 production gerber export,
so they are the R4 source. `tools/compare_altium_source.py` imports both `.CSPCBDoc` boards
with KiCad 7's CircuitStudio importer and compares them with `pcb/Reid_Orthotic_v2_{LHS,RHS}.kicad_pcb`
by matching copper pads on position and layer.

## Verdict: connectivity identical on both boards

| | LHS | RHS |
|---|---|---|
| Copper pads matched (position + layer, < 60 µm) | 436 / 436 recon | 436 / 436 recon |
| Original nets split across recon nets | none | none |
| Recon nets merging original nets | none | none |
| Component placement | identical up to a fixed offset (+101.126, +73.629 mm); Q1/Q2 origins differ by 0.23 mm, pads coincide | same |

Every net in the original maps to exactly one reconstructed net and back. The gerber re-export
check (`COMPARE_REPORT.md`) already showed the copper geometry matches; this confirms the
netlist built from it matches the designer's.

## Differences, all naming/modelling only

- **Test points:** original `TP1`–`TP7` = recon `PAD1`–`PAD7` (recon numbering was arbitrary and differs per side).
  TP1 = 0V, TP2 = VSYS, TP3 = VBAT (battery after F1), TP4 = 3V3, TP5 = SWDIO, TP6 = SWDCLK, TP7 = MCU_RST.
  TP reference designators are hidden on the silkscreen.
- **J5 Tag-Connect TC2030-NL (SWD):** the recon has its 3 NPTH locating holes (`NP1`–`NP3`) and one pad
  (`PAD8`); the other 5 pads exist as via-in-pad copper on the same nets. Electrically the same.
- **Pin numbering:** J1/J2 (20 pins each), J3, J4, D1, D2 (original A/K, recon 1/2), X2, ANT1 are numbered
  differently in the recon; the pads and their nets are the same.
- **Net names (recon → original):** VBAT_F → VBAT, VBAT → NetF1_1 (battery connector side of F1),
  ANT_FEED → ANT_OUT, BQ_CHG → BAT_CHG, BQ_PG → NetR20_1, SDA/SCL → MCU_SDA/MCU_SCL,
  CAP_CH0/1 → CAP_ADC0/1, FSR_CH0/1/2 → FSR_ADC0/1/2; recon `NETnn` auto-names → original names.
- **U9 value:** stray invisible character before `TCR3UF33A,LM(CT` in the original.

## Not covered

- **Schematics.** KiCad 7 imports Altium `.SchDoc` only in the GUI. `Reid Orthotic v2 IO.SchDoc` was
  modified 2025-03-05, after R4 went to fab; since the PCB connectivity matches the fabricated gerbers,
  any change in it never reached a board. Ask Alex what it was.

## Running it

KiCad 7.0.11 aborts importing these files (`Duplicate netclass name 'All Nets'`, the class stream holds
two of them); the tool renames them in a scratch copy. Needs `pcbnew` and `olefile`:

    PYTHONPATH=<site-packages with olefile> python3 hardware/r4-kicad/tools/compare_altium_source.py <scratch_dir>
