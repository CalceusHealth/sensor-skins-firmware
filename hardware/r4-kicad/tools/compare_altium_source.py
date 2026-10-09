#!/usr/bin/env python3
"""Compare Alex's original CircuitStudio R4 boards against the KiCad reconstruction.

Imports `hardware/Reid Orthotic v2 Design Files/Reid Orthotic v2 {LHS,RHS}.CSPCBDoc`
with KiCad's CircuitStudio importer, then matches copper pads by position and
layer and checks that the net partitions agree (every original net maps to
exactly one reconstructed net and vice versa).

KiCad 7.0.11's importer aborts on these files ("Duplicate netclass name
'All Nets'", altium_pcb.cpp ParseClasses6Data): the class stream holds two
classes with that name. We patch a scratch copy, renaming them to unique
same-length names; the originals are not touched.

usage: compare_altium_source.py <scratch_dir>     (needs pcbnew + olefile)
"""
import collections
import math
import shutil
import sys
from pathlib import Path

import olefile
import pcbnew

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "hardware" / "Reid Orthotic v2 Design Files"
RECON = ROOT / "hardware" / "r4-kicad" / "pcb"
# Original (CircuitStudio) coordinates = reconstruction + this offset, in mm
# (median over all common footprints; identical for LHS and RHS).
OX, OY = 101.126, 73.629
MATCH_MM = 0.06


def patched_import(side, scratch):
    dst = scratch / f"patched_{side}.CSPCBDoc"
    shutil.copy(SRC / f"Reid Orthotic v2 {side}.CSPCBDoc", dst)
    ole = olefile.OleFileIO(str(dst), write_mode=True)
    for st in ole.listdir():
        data = ole.openstream(st).read()
        if b"All Nets" in data:
            data = data.replace(b"All Nets", b"AllNets1", 1).replace(b"All Nets", b"AllNets2", 1)
            ole.write_stream(st, data)
    ole.close()
    board = pcbnew.IO_MGR.Load(pcbnew.IO_MGR.ALTIUM_CIRCUIT_STUDIO, str(dst))
    pcbnew.SaveBoard(str(scratch / f"alex_{side}.kicad_pcb"), board)
    return board


def pads(board, dx=0.0, dy=0.0):
    out = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            c = p.GetPosition()
            out.append(dict(ref=fp.GetReference(), num=p.GetNumber(), net=p.GetNetname(),
                            x=pcbnew.ToMM(c.x) - dx, y=pcbnew.ToMM(c.y) - dy,
                            top=p.IsOnLayer(pcbnew.F_Cu), bot=p.IsOnLayer(pcbnew.B_Cu)))
    return out


def compare(side, scratch):
    # LoadBoard first: it sets up the KiCad project the importer needs
    # (importing first trips "no project in list" in SETTINGS_MANAGER::Prj).
    recon = pcbnew.LoadBoard(str(RECON / f"Reid_Orthotic_v2_{side}.kicad_pcb"))
    alex = patched_import(side, scratch)
    A, R = pads(alex, OX, OY), pads(recon)

    grid = collections.defaultdict(list)
    for q in R:
        grid[(round(q["x"], 1), round(q["y"], 1))].append(q)
    match, un_a = [], []
    for p in A:
        best = None
        for gx in (-0.1, 0, 0.1):
            for gy in (-0.1, 0, 0.1):
                for q in grid.get((round(p["x"] + gx, 1), round(p["y"] + gy, 1)), []):
                    if (q["top"] and p["top"]) or (q["bot"] and p["bot"]):
                        d = math.hypot(p["x"] - q["x"], p["y"] - q["y"])
                        if d < MATCH_MM and (best is None or d < best[0]):
                            best = (d, q)
        (match.append((p, best[1])) if best else un_a.append(p))
    used = {id(q) for _, q in match}
    un_r = [q for q in R if id(q) not in used]

    a2r = collections.defaultdict(set)
    r2a = collections.defaultdict(set)
    for p, q in match:
        if p["net"]:
            a2r[p["net"]].add(q["net"])
        if q["net"] and p["net"]:
            r2a[q["net"]].add(p["net"])
    split = {n: s for n, s in a2r.items() if len(s) > 1}
    merged = {n: s for n, s in r2a.items() if len(s) > 1}

    print(f"\n== {side}: footprints original={len(alex.GetFootprints())} recon={len(recon.GetFootprints())}; "
          f"pads original={len(A)} recon={len(R)} matched={len(match)}")
    print(f"  unmatched original pads: {sorted({(p['ref'], p['num'], p['net']) for p in un_a})}")
    print(f"  unmatched recon pads:    {sorted({(q['ref'], q['num'], q['net']) for q in un_r})}")
    print(f"  original nets split across recon nets: {split or 'none'}")
    print(f"  recon nets merging original nets:      {merged or 'none'}")
    renamed = sorted({(q["net"], p["net"]) for p, q in match
                      if p["net"] and q["net"] and q["net"] != p["net"] and not q["net"].startswith("NET")})
    print(f"  named nets, recon -> original: {renamed}")
    renum = collections.Counter((p["ref"], q["ref"]) for p, q in match
                                if p["ref"] != q["ref"] or p["num"] != q["num"])
    print(f"  pads with a different ref/number: {dict(renum)}")
    for p, q in sorted(match, key=lambda m: m[0]["ref"]):
        if p["ref"].startswith("TP"):
            print(f"    {p['ref']:4} {p['net']:8} = recon {q['ref']} {q['net']}")
    return not split and not merged


if __name__ == "__main__":
    scratch = Path(sys.argv[1])
    scratch.mkdir(parents=True, exist_ok=True)
    ok = all([compare(s, scratch) for s in ("LHS", "RHS")])
    print("\nconnectivity:", "IDENTICAL" if ok else "DIFFERS")
    sys.exit(0 if ok else 1)
