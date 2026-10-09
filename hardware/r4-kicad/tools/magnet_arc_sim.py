#!/usr/bin/env python3
"""Pull of an N52 C-arc in the insole (ID 17.2 / OD 25.2 x 1 mm) against a
355 deg split ring in the puck, vs arc span. Used to size the flush 120 deg
insole magnet (brief 3.3 / 4.3). Arc and ring are built from segmented
magnet_sim.py blocks; 180 deg reproduces the stored C180 result within ~5 %.

Run with the hardware/r4-kicad/.venv python.
"""
import sys
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
import magnet_sim as M
import math
RM, W = 10.6, 4.0          # C-arc ID 17.2 / OD 25.2
def arc(span, n_per_deg=1/5, J=M.J_N52):
    n = max(2, round(span * n_per_deg))
    seg = 2 * math.pi * RM * span / 360 / n
    return M.ring_of(n, RM, lambda x, y, a: M.blk(W, seg * 1.0, 1, x, y, a, J=J), start_deg=0, span_deg=span)
puck = arc(355)
today = {1: 2.83, 1.5: 1.92, 2: 1.35}   # today: dia4x3 centre magnet, magnet_sim.json
def F(ins, gap, dx=0.0):
    a = M.build(ins, gap / 2, "insole"); b = M.build(puck, -gap / 2, "puck", dx=dx)
    f, _ = M.magpy.getFT(b, a)
    import numpy as np
    return np.asarray(f).reshape(-1, 3).sum(axis=0)
for span in (180, 150, 135, 120, 110):
    ins = arc(span)
    row = []
    for g in (1, 1.5, 2):
        p = -F(ins, g)[2]
        row.append(f"gap{g}: {p:.2f} N ({p/today[g]:.2f}x)")
    print(span, " | ".join(row), flush=True)
