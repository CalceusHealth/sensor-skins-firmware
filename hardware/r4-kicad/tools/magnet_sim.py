#!/usr/bin/env python3
"""Alignment-magnet comparison for the charger puck <-> insole coil (magnetostatic).

Each configuration is a set of magnets on the insole side and the identical
pattern on the puck side, both magnetised along +z so they attract. For each
face-to-face gap we compute, with magpylib's force solver:
  - pull: attraction holding puck and insole together (N)
  - centring: lateral restoring force when the puck is 1 mm and 2 mm off-centre (N)

Coupling and eddy-current effects (Q, L, ferrite saturation) are NOT modelled
here; that needs an AC solver (FEMM / Elmer / Maxwell).

Run with the hardware/r4-kicad/.venv python.
"""
import json
import math
from pathlib import Path

import magpylib as magpy
import numpy as np

J_N52 = 1.43      # T, sintered NdFeB N52 polarisation
J_BONDED = 0.65   # T, compression-bonded NdFeB (non-conductive)
OUT = Path(__file__).resolve().parents[1] / "v3_concept"


def cyl(d, h, x, y, J=J_N52):
    return dict(kind="cyl", dim=(d, h), pos=(x, y), J=J)


def blk(a, b, h, x, y, rot_deg=0.0, J=J_N52):
    return dict(kind="blk", dim=(a, b, h), pos=(x, y), rot=rot_deg, J=J)


def ring_of(n, r, maker, start_deg=90.0, span_deg=360.0):
    out = []
    step = span_deg / n if span_deg < 360 else 360.0 / n
    first = start_deg if span_deg >= 360 else start_deg - span_deg / 2 + step / 2
    for k in range(n):
        a = math.radians(first + k * step)
        out.append(maker(r * math.cos(a), r * math.sin(a), math.degrees(a)))
    return out


R = 10.5  # pitch radius just outside the 15 mm coil (mm)
CONFIGS = {
    "A today: 1x dia4x3 centre": [cyl(4, 3, 0, 0)],
    "B R3: 2x dia3x2 outside (diagonal)": [cyl(3, 2, 7.0, 7.0), cyl(3, 2, -7.0, -7.0)],
    "C 3x dia4x1 outside": ring_of(3, R, lambda x, y, a: cyl(4, 1, x, y)),
    "D 4x dia4x1 outside": ring_of(4, R, lambda x, y, a: cyl(4, 1, x, y), start_deg=45),
    "E 3x dia6x1 outside": ring_of(3, R + 0.5, lambda x, y, a: cyl(6, 1, x, y)),
    "F 12x 2x2x1 blocks, full ring (MagSafe-style)":
        ring_of(12, R, lambda x, y, a: blk(2, 2, 1, x, y, a)),
    "G 8x 2x2x1 blocks, 240 deg free arc":
        ring_of(8, R, lambda x, y, a: blk(2, 2, 1, x, y, a), start_deg=0, span_deg=240),
    "H bonded NdFeB C-arc 3 mm wide x 1 mm, 240 deg":
        ring_of(16, R, lambda x, y, a: blk(2.6, 3.0, 1, x, y, a, J=J_BONDED),
                start_deg=0, span_deg=240),
}


def build(spec, z_face, side, dx=0.0, mesh=40):
    """side='insole' : magnets sit ABOVE z_face (bottom face at z_face).
       side='puck'   : magnets sit BELOW z_face (top face at z_face)."""
    mags = []
    for m in spec:
        h = m["dim"][-1]
        zc = z_face + h / 2 if side == "insole" else z_face - h / 2
        x, y = m["pos"]
        # geometry is specified in mm; magpylib works in SI (metres)
        mm = 1e-3
        dim = tuple(v * mm for v in m["dim"])
        x, y, zc, dx_m = x * mm, y * mm, zc * mm, dx * mm
        pol = (0, 0, m["J"])
        if m["kind"] == "cyl":
            g = magpy.magnet.Cylinder(polarization=pol, dimension=dim,
                                      position=(x + dx_m, y, zc))
        else:
            from scipy.spatial.transform import Rotation as Rot
            g = magpy.magnet.Cuboid(polarization=pol, dimension=dim,
                                    position=(x + dx_m, y, zc),
                                    orientation=Rot.from_euler("z", m["rot"], degrees=True))
        g.meshing = mesh
        mags.append(g)
    return mags


def force_on_insole(spec, gap, dx):
    insole = build(spec, gap / 2, "insole")
    puck = build(spec, -gap / 2, "puck", dx=dx)
    F, _ = magpy.getFT(puck, insole)
    # total force on all insole magnets from all puck magnets (N)
    return np.asarray(F).reshape(-1, 3).sum(axis=0)


def volume(spec):
    v = 0.0
    for m in spec:
        if m["kind"] == "cyl":
            d, h = m["dim"]
            v += math.pi * (d / 2) ** 2 * h
        else:
            a, b, h = m["dim"]
            v += a * b * h
    return v


if __name__ == "__main__":
    gaps = (2.0, 3.0, 4.0)
    rows = {}
    for name, spec in CONFIGS.items():
        hmax = max(m["dim"][-1] for m in spec)
        r = {"magnets": len(spec), "volume_mm3": round(volume(spec), 1),
             "height_mm": hmax}
        for g in gaps:
            pull = -force_on_insole(spec, g, 0.0)[2]
            c1 = -force_on_insole(spec, g, 1.0)[0]
            c2 = -force_on_insole(spec, g, 2.0)[0]
            r[f"gap{g:g}"] = {"pull_N": round(pull, 3),
                              "centring_1mm_N": round(c1, 3),
                              "centring_2mm_N": round(c2, 3)}
        rows[name] = r
        print(name, json.dumps(r))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "magnet_sim.json").write_text(json.dumps(rows, indent=1))
