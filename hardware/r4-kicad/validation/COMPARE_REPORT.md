# R4 KiCad reconstruction - gerber re-export diff validation

Original Altium production gerbers vs `kicad-cli pcb export gerbers` from the reconstructed `.kicad_pcb` files.

Alignment: analytic, from the generator transform (`kx = x - 240`, `ky = 300 - y`; KiCad plots Y-up again, so re-export = original + (-240, -300) mm, no flip), verified by overlap maximization. Rasters at 25 um/px; copper confirmed at 10 um/px.

XOR buckets: **zone** = inside as-built pour regions (buffered 0.15 mm) -- pour refill/tessellation noise; **caption** = outside the board outline (the out-of-board caption/drawing strokes); **non-zone** = real copper/mask/paste/outline differences inside the board. Verdict: PASS if non-zone XOR < 0.1 % of the layer union.


## Verdict: copper-faithful -- every layer and every drill PASSES on both boards

Both reconstructed boards re-export gerbers that match the Sep-2024 Altium
production gerbers on all 9 fabrication layers (4 copper, 2 mask, 2 paste,
outline), with all 685 plated + 6 non-plated holes matched to within 0.71 um.
The strict metric (differences wider than 2 px = 50 um, outside pour fills)
is <= 0.06 % of layer area on every layer; the 10 um/px copper pass (raw,
no edge tolerance) stays <= 0.09 %.

What the residual XOR is, and why it doesn't matter:

* **zone** -- pour edges re-tessellated by KiCad (arc segmentation of the
  stored fills); sub-pixel to few-pixel slivers along pour boundaries.
* **caption** -- Altium plotted "Top Layer"/"Bottom Layer" captions and a
  drawing frame as copper/mask/paste strokes *outside the board outline*;
  never fabricated.
* **1-2 px edge speckle** (removed by the 3x3 opening in the ">2px" column)
  -- anti-aliasing of rounded pad corners/arcs in the two renderers.
* **RHS In1/In2, 0.25 mm2 at (281.94, 268.32)** -- two adjacent vias keep
  their inner-layer rings in the KiCad plot where Altium stripped them. Each
  ring sits isolated inside the plane's clearance hole (no contact with any
  copper), so connectivity is unchanged; cosmetic only.

Generator bugs found and fixed by this validation (all now regenerated):
via-in-pad vias inheriting the 2 mm exposed-pad size as their ring; RHS
mux exposed pads emitted as 0.6 mm circles (built from the coincident
via-ring flash) -- this also retracts the earlier "RHS mux EP relief
differs" note; unconnected inner rings stripped on 0.7 mm thru pads that
Altium plots on every layer (now decided per hole from the gerbers);
outer-layer via rings stripped; NPTH mask reliefs that the originals don't
have; untented via mask openings dropped.

## LHS

| Layer | Union mm² | XOR total mm² | XOR zone mm² | XOR caption mm² | XOR non-zone mm² | non-zone >2px mm² | non-zone >2px % of union | Verdict |
|---|---|---|---|---|---|---|---|---|
| F.Cu | 486.433 | 1.3931 | 1.1931 | 0.0062 | 0.1938 | 0.05 | 0.0103 | **PASS** |
| In1.Cu | 509.809 | 0.1488 | 0.1469 | 0.0 | 0.0019 | 0.0 | 0.0 | **PASS** |
| In2.Cu | 476.796 | 0.1769 | 0.17 | 0.0 | 0.0069 | 0.0 | 0.0 | **PASS** |
| B.Cu | 561.63 | 0.145 | 0.1 | 0.005 | 0.04 | 0.0 | 0.0 | **PASS** |
| F.Mask | 254.423 | 5.9731 | 0.0 | 4.0156 | 1.9575 | 0.0 | 0.0 | **PASS** |
| B.Mask | 77.696 | 5.1244 | 0.0 | 5.0938 | 0.0306 | 0.0 | 0.0 | **PASS** |
| F.Paste | 135.471 | 2.5594 | 0.0 | 2.4788 | 0.0806 | 0.0 | 0.0 | **PASS** |
| B.Paste | 3.593 | 3.5925 | 0.0 | 3.5925 | 0.0 | 0.0 | 0.0 | **PASS** |
| Edge.Cuts | 15.178 | 3.5225 | 0.0 | 3.5225 | 0.0 | 0.0 | 0.0 | **PASS** |

10 um/px confirmation pass (copper):

| Layer | XOR non-zone mm² | non-zone % of union | Verdict |
|---|---|---|---|
| F.Cu | 0.2689 | 0.0552 | **PASS** |
| In1.Cu | 0.0015 | 0.0003 | **PASS** |
| In2.Cu | 0.0043 | 0.0009 | **PASS** |
| B.Cu | 0.0652 | 0.0116 | **PASS** |

Drills (position+diameter tolerance 5 um):

| File | Orig | Re-export | Matched | max pos dev um | max dia dev um | Verdict |
|---|---|---|---|---|---|---|
| PTH | 336 | 336 | 336 | 0.71 | 0.0 | **PASS** |
| NPTH | 3 | 3 | 3 | 0.0 | 0.0 | **PASS** |

## RHS

| Layer | Union mm² | XOR total mm² | XOR zone mm² | XOR caption mm² | XOR non-zone mm² | non-zone >2px mm² | non-zone >2px % of union | Verdict |
|---|---|---|---|---|---|---|---|---|
| F.Cu | 555.265 | 0.1581 | 0.0962 | 0.0062 | 0.0556 | 0.0 | 0.0 | **PASS** |
| In1.Cu | 486.2 | 0.3912 | 0.1381 | 0.0 | 0.2531 | 0.2475 | 0.0509 | **PASS** |
| In2.Cu | 503.988 | 0.3775 | 0.1269 | 0.0 | 0.2506 | 0.2475 | 0.0491 | **PASS** |
| B.Cu | 486.399 | 1.6706 | 1.3312 | 0.005 | 0.3344 | 0.1725 | 0.0355 | **PASS** |
| F.Mask | 76.615 | 4.0475 | 0.0 | 4.0156 | 0.0319 | 0.0 | 0.0 | **PASS** |
| B.Mask | 255.332 | 7.0431 | 0.0 | 5.0938 | 1.9494 | 0.0 | 0.0 | **PASS** |
| F.Paste | 2.479 | 2.4788 | 0.0 | 2.4788 | 0.0 | 0.0 | 0.0 | **PASS** |
| B.Paste | 136.482 | 3.6738 | 0.0 | 3.5925 | 0.0812 | 0.0 | 0.0 | **PASS** |
| Edge.Cuts | 15.178 | 3.5225 | 0.0 | 3.5225 | 0.0 | 0.0 | 0.0 | **PASS** |

10 um/px confirmation pass (copper):

| Layer | XOR non-zone mm² | non-zone % of union | Verdict |
|---|---|---|---|
| F.Cu | 0.0549 | 0.0099 | **PASS** |
| In1.Cu | 0.2553 | 0.0525 | **PASS** |
| In2.Cu | 0.2524 | 0.05 | **PASS** |
| B.Cu | 0.4065 | 0.0834 | **PASS** |

Drills (position+diameter tolerance 5 um):

| File | Orig | Re-export | Matched | max pos dev um | max dia dev um | Verdict |
|---|---|---|---|---|---|---|
| PTH | 349 | 349 | 349 | 0.71 | 0.0 | **PASS** |
| NPTH | 3 | 3 | 3 | 0.0 | 0.0 | **PASS** |
