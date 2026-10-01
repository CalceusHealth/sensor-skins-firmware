#!/usr/bin/env python3
"""Inventory the R4 gerbers: flashes (pads/vias), draws (tracks), regions (zones).

Produces a JSON summary per side used by the .kicad_pcb generator and by the
validation diff. Run with the scratchpad venv's python (needs gerbonara).
"""
import json
import sys
from pathlib import Path

from gerbonara import GerberFile, ExcellonFile
from gerbonara.graphic_objects import Flash, Line, Arc, Region

GERBER_DIR = Path(__file__).resolve().parents[3] / "artefacts" / \
    "SSII Orthotics Electronics - Design Verification"

LAYERS = {
    "GTL": "F.Cu", "G1": "In1.Cu", "G2": "In2.Cu", "GBL": "B.Cu",
    "GTS": "F.Mask", "GBS": "B.Mask", "GTP": "F.Paste", "GBP": "B.Paste",
    "GTO": "F.Silkscreen", "GBO": "B.Silkscreen", "GM3": "Edge.Cuts",
}


def aperture_desc(ap):
    if ap is None:
        return None
    d = {"type": type(ap).__name__}
    for attr in ("diameter", "w", "h", "side_length", "rotation", "n_vertices", "hole_dia"):
        v = getattr(ap, attr, None)
        if v is not None:
            d[attr] = round(float(v), 4) if isinstance(v, (int, float)) else v
    return d


def inventory_side(side):
    base = GERBER_DIR / f"Reid Orthotic v2 R4 {side} Gerbers"
    stem = f"Reid Orthotic v2 {side}"
    out = {"side": side, "layers": {}}
    for ext, kicad_layer in LAYERS.items():
        f = base / f"{stem}.{ext}"
        if not f.exists():
            continue
        g = GerberFile.open(f)
        flashes, lines, arcs, regions = [], [], [], []
        for obj in g.objects:
            if isinstance(obj, Flash):
                flashes.append({"x": round(obj.x, 4), "y": round(obj.y, 4),
                                "ap": aperture_desc(obj.aperture)})
            elif isinstance(obj, Line):
                lines.append({"x1": round(obj.x1, 4), "y1": round(obj.y1, 4),
                              "x2": round(obj.x2, 4), "y2": round(obj.y2, 4),
                              "ap": aperture_desc(obj.aperture)})
            elif isinstance(obj, Arc):
                arcs.append({"x1": round(obj.x1, 4), "y1": round(obj.y1, 4),
                             "x2": round(obj.x2, 4), "y2": round(obj.y2, 4),
                             "cx": round(obj.cx, 4), "cy": round(obj.cy, 4),
                             "clockwise": obj.clockwise,
                             "ap": aperture_desc(obj.aperture)})
            elif isinstance(obj, Region):
                try:
                    pts = [(round(p[0], 4), round(p[1], 4)) for p in obj.outline]
                except Exception:
                    pts = []
                regions.append({"n_points": len(pts), "points": pts})
        out["layers"][kicad_layer] = {
            "file": f.name, "flashes": flashes, "lines": lines,
            "arcs": arcs, "regions": regions,
            "counts": {"flashes": len(flashes), "lines": len(lines),
                       "arcs": len(arcs), "regions": len(regions)},
        }
    # Drills
    for kind in ("Plated", "NonPlated"):
        f = base / f"{stem}-{kind}.TXT"
        if not f.exists():
            continue
        e = ExcellonFile.open(f)
        holes = []
        for obj in e.objects:
            if isinstance(obj, Flash):
                holes.append({"x": round(obj.x, 4), "y": round(obj.y, 4),
                              "dia": round(float(obj.aperture.diameter), 4)})
        out["layers"][f"drill_{kind.lower()}"] = {
            "file": f.name, "holes": holes, "counts": {"holes": len(holes)}}
    return out


if __name__ == "__main__":
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    for side in ("LHS", "RHS"):
        inv = inventory_side(side)
        dest = outdir / f"inventory_{side.lower()}.json"
        dest.write_text(json.dumps(inv, indent=1))
        summary = {layer: d["counts"] for layer, d in inv["layers"].items()}
        print(side, json.dumps(summary, indent=1))
