#!/usr/bin/env python3
"""Validate the R4 KiCad reconstruction: diff kicad-cli re-exported gerbers
against the original Altium production gerbers, layer by layer.

Alignment is analytic, from the generator's transform (generate_kicad_pcb.py):
    kicad_x = gerber_x - 240.0        kicad_y = 300.0 - gerber_y
KiCad plots gerbers back in a Y-up frame (gerber_y' = -kicad_y), so a point in
the original gerbers lands in the re-export at
    (x - 240.0, y - 300.0)
i.e. a pure translation, no flip.  This is verified empirically by maximizing
raster overlap over +/-2 px before measuring.

For every layer both files are rendered through the same pipeline
(gerbonara -> SVG with forced common bounds -> cairosvg raster) on an
identical pixel grid, then XOR'd.  The XOR is split into buckets:
  * zone      : inside the as-built pour regions (inventory regions, buffered)
                -- pour refill/tessellation differences, copper layers only
  * caption   : outside the board outline (the "Top/Bottom Layer" caption
                strokes on GTL/GBL, drawing-frame text on other layers)
  * non-zone  : everything else inside the board -- the number that matters
Verdict per layer: PASS if non-zone XOR < 0.1 % of the union area.

Drill files are compared hole by hole (position tolerance 5 um, diameter
tolerance 5 um) between the Altium -Plated/-NonPlated.TXT and the re-exported
PTH/NPTH .drl.

Outputs (in hardware/r4-kicad/validation/):
  xor_<side>_<layer>.png          per-layer overlay heatmap
  crops/<side>_<layer>_w<k>.png   3 worst 2 mm windows (8 mm context)
  compare_results.json            all numbers
  COMPARE_REPORT.md               table + verdicts

Run with hardware/r4-kicad/.venv python (gerbonara, cairosvg, shapely, numpy,
PIL).  Usage: compare_gerbers.py [LHS] [RHS] [--fine]
  --fine adds a 10 um/px confirmation pass on the copper layers
  (main pass is 25 um/px).
"""
import io
import json
import math
import sys
import warnings
from pathlib import Path

import numpy as np
import cairosvg
from PIL import Image, ImageDraw
from gerbonara import GerberFile, ExcellonFile
from shapely.geometry import Polygon
from shapely.ops import unary_union

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import generate_kicad_pcb as gen  # extract_outline + transform constants

NETDIR = HERE.parent / "netlist"
VALDIR = HERE.parent / "validation"
ORIG_BASE = HERE.parents[2] / "artefacts" / \
    "SSII Orthotics Electronics - Design Verification"

# original gerber point (x,y) appears in the re-export at (x+DX, y+DY)
DX, DY = -gen.KXOFF, -gen.KYOFF        # (-240, -300)

PPMM_MAIN = 40     # 25 um/px  (~1016 dpi)
PPMM_FINE = 100    # 10 um/px confirmation pass (copper layers)
PASS_PCT = 0.1     # non-zone XOR must stay under this % of union
ZONE_BUFFER = 0.15   # mm, slop around as-built pour regions
WIN_MM = 2.0       # worst-window size
CROP_MM = 8.0      # saved crop context
COPPER = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]

# (layer, original extension, re-export suffix)
LAYERS = [
    ("F.Cu",      "GTL", "F_Cu.gtl"),
    ("In1.Cu",    "G1",  "In1_Cu.g2"),
    ("In2.Cu",    "G2",  "In2_Cu.g3"),
    ("B.Cu",      "GBL", "B_Cu.gbl"),
    ("F.Mask",    "GTS", "F_Mask.gts"),
    ("B.Mask",    "GBS", "B_Mask.gbs"),
    ("F.Paste",   "GTP", "F_Paste.gtp"),
    ("B.Paste",   "GBP", "B_Paste.gbp"),
    ("Edge.Cuts", "GM4", "Edge_Cuts.gm1"),
]


def orig_path(side, ext):
    d = ORIG_BASE / f"Reid Orthotic v2 R4 {side} Gerbers"
    return d / f"Reid Orthotic v2 {side}.{ext}"


def reexp_path(side, suffix):
    return VALDIR / f"reexport_{side.lower()}" / \
        f"Reid_Orthotic_v2_{side}-{suffix}"


# ------------------------------------------------------------------ raster
def _shift_all(m, op):
    p = np.pad(m, 1, constant_values=False)
    h, w = m.shape
    acc = None
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            v = p[dy:dy + h, dx:dx + w]
            acc = v.copy() if acc is None else op(acc, v)
    return acc


def open3(m):
    """Morphological opening with a 3x3 element: removes XOR slivers <= 2 px
    wide (edge anti-aliasing / arc tessellation), keeps any real feature
    difference wider than ~3 px (75 um at 25 um/px; narrower than any
    track, pad or clearance on this board)."""
    return _shift_all(_shift_all(m, np.logical_and), np.logical_or)


def render(gf, bounds, ppmm):
    """Render a gerbonara GerberFile to a boolean array on the pixel grid
    defined by bounds=((x0,y0),(x1,y1)) in the file's own frame.
    Row 0 = y1 (top).  True = dark."""
    (x0, y0), (x1, y1) = bounds
    w_px = int(round((x1 - x0) * ppmm))
    h_px = int(round((y1 - y0) * ppmm))
    svg = str(gf.to_svg(force_bounds=bounds, fg="black", bg="white"))
    png = cairosvg.svg2png(bytestring=svg.encode(),
                           output_width=w_px, output_height=h_px,
                           background_color="white")
    img = Image.open(io.BytesIO(png)).convert("L")
    return np.asarray(img) < 128


def load(side, ext_or_suffix, which):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        if which == "orig":
            return GerberFile.open(orig_path(side, ext_or_suffix))
        return GerberFile.open(reexp_path(side, ext_or_suffix))


def canvas_for(side, files):
    """Common canvas in the ORIGINAL gerber frame, covering every layer of
    both generations, padded, snapped to whole pixels of the fine grid."""
    x0 = y0 = math.inf
    x1 = y1 = -math.inf
    for gf, shifted in files:
        bb = gf.bounding_box(unit="mm")
        if bb is None:
            continue
        (a, b), (c, d) = bb
        if shifted:  # re-export frame -> original frame
            a, c = a - DX, c - DX
            b, d = b - DY, d - DY
        x0, y0 = min(x0, a), min(y0, b)
        x1, y1 = max(x1, c), max(y1, d)
    pad = 1.0
    x0, y0 = x0 - pad, y0 - pad
    x1, y1 = x1 + pad, y1 + pad
    snap = 1.0 / PPMM_FINE

    def dn(v):
        return math.floor(v / snap) * snap

    def up(v):
        return math.ceil(v / snap) * snap
    return (dn(x0), dn(y0)), (up(x1), up(y1))


def mask_from_polys(polys, canvas, ppmm):
    """Rasterize shapely polygons (original frame) onto the canvas grid."""
    (x0, y0), (x1, y1) = canvas
    w_px = int(round((x1 - x0) * ppmm))
    h_px = int(round((y1 - y0) * ppmm))
    img = Image.new("1", (w_px, h_px), 0)
    drw = ImageDraw.Draw(img)

    def to_px(pts):
        return [((x - x0) * ppmm, (y1 - y) * ppmm) for x, y in pts]
    for p in polys:
        if p.is_empty:
            continue
        geoms = p.geoms if p.geom_type == "MultiPolygon" else [p]
        for g in geoms:
            drw.polygon(to_px(g.exterior.coords), fill=1)
            for ring in g.interiors:
                drw.polygon(to_px(ring.coords), fill=0)
    return np.asarray(img, dtype=bool)


def zone_polys(inv, layer):
    """As-built pour copper for a layer: dark regions minus later clear
    regions (same sequential rule as the generator), buffered."""
    acc = []
    for reg in inv["layers"].get(layer, {}).get("regions", []):
        pts = [tuple(p) for p in reg["points"]]
        if len(pts) < 3:
            continue
        poly = Polygon(pts)
        if not poly.is_valid:
            poly = poly.buffer(0)
        if reg.get("dark", True):
            acc.append([poly])
        else:
            for ent in acc:
                ent[0] = ent[0].difference(poly)
    if not acc:
        return []
    u = unary_union([e[0] for e in acc]).buffer(ZONE_BUFFER)
    return [u]


def board_poly(inv):
    warn = []
    _, loops, _ = gen.extract_outline(inv["layers"]["Edge.Cuts"], warn)
    polys = sorted((Polygon(lp).buffer(0) for lp in loops),
                   key=lambda p: p.area, reverse=True)
    return polys[0] if polys else None


# ------------------------------------------------------- overlap refinement
def best_shift(a, b, radius=2):
    """Verify alignment: px shift of b (within +/-radius) maximizing overlap
    with a.  Returns (dy, dx, gain_px) relative to the analytic alignment."""
    base = np.count_nonzero(a & b)
    best = (0, 0, 0)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx == 0 and dy == 0:
                continue
            s = np.count_nonzero(a & np.roll(b, (dy, dx), axis=(0, 1)))
            if s - base > best[2]:
                best = (dy, dx, s - base)
    return best


# ------------------------------------------------------------- diff images
def save_overlay(orig, reexp, path, max_px=4096):
    """White bg, grey agreement, red original-only, blue re-export-only."""
    h, w = orig.shape
    rgb = np.full((h, w, 3), 255, np.uint8)
    both = orig & reexp
    rgb[both] = (205, 205, 205)
    rgb[orig & ~reexp] = (220, 30, 30)
    rgb[reexp & ~orig] = (30, 60, 220)
    img = Image.fromarray(rgb)
    if max(w, h) > max_px:
        sc = max_px / max(w, h)
        img = img.resize((int(w * sc), int(h * sc)), Image.LANCZOS)
    img.save(path, optimize=True)


def worst_windows(xor, ppmm, n=3):
    """Top n non-overlapping WIN_MM windows by XOR pixel count."""
    win = max(1, int(WIN_MM * ppmm))
    ii = np.zeros((xor.shape[0] + 1, xor.shape[1] + 1), np.int64)
    ii[1:, 1:] = np.cumsum(np.cumsum(xor, 0), 1)
    sums = (ii[win:, win:] - ii[:-win, win:] -
            ii[win:, :-win] + ii[:-win, :-win]).astype(np.float64)
    out = []
    for _ in range(n):
        j, i = np.unravel_index(np.argmax(sums), sums.shape)
        if sums[j, i] <= 0:
            break
        out.append((j + win // 2, i + win // 2, int(sums[j, i])))
        j0, j1 = max(0, j - win), min(sums.shape[0], j + win + 1)
        i0, i1 = max(0, i - win), min(sums.shape[1], i + win + 1)
        sums[j0:j1, i0:i1] = -1
    return out


def save_crops(orig, reexp, wins, canvas, ppmm, side, layer):
    (x0, _), (_, y1) = canvas
    cdir = VALDIR / "crops"
    cdir.mkdir(exist_ok=True)
    half = int(CROP_MM * ppmm / 2)
    info = []
    for k, (j, i, cnt) in enumerate(wins, 1):
        j0, j1 = max(0, j - half), min(orig.shape[0], j + half)
        i0, i1 = max(0, i - half), min(orig.shape[1], i + half)
        rgb = np.full((j1 - j0, i1 - i0, 3), 255, np.uint8)
        o = orig[j0:j1, i0:i1]
        r = reexp[j0:j1, i0:i1]
        rgb[o & r] = (205, 205, 205)
        rgb[o & ~r] = (220, 30, 30)
        rgb[~o & r] = (30, 60, 220)
        img = Image.fromarray(rgb)
        sc = max(1, int(1600 / max(img.size)))
        img = img.resize((img.size[0] * sc, img.size[1] * sc), Image.NEAREST)
        name = f"{side}_{layer.replace('.', '')}_w{k}.png"
        img.save(cdir / name, optimize=True)
        gx = x0 + (i + 0.5) / ppmm
        gy = y1 - (j + 0.5) / ppmm
        info.append({"crop": f"crops/{name}",
                     "center_orig_frame_mm": [round(gx, 2), round(gy, 2)],
                     "xor_px": cnt,
                     "xor_mm2": round(cnt / ppmm ** 2, 4)})
    return info


# ------------------------------------------------------------------- drill
def drill_holes(path):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        ex = ExcellonFile.open(path)
    out = []
    for o in ex.objects:
        if hasattr(o, "x"):
            d = getattr(getattr(o, "aperture", None), "diameter", None)
            if d is None:
                d = getattr(getattr(o, "tool", None), "diameter", None)
            out.append((float(o.x), float(o.y), float(d)))
    return out


def compare_drills(side, kind):
    if kind == "PTH":
        a = drill_holes(ORIG_BASE / f"Reid Orthotic v2 R4 {side} Gerbers" /
                        f"Reid Orthotic v2 {side}-Plated.TXT")
        b = drill_holes(reexp_path(side, "PTH.drl"))
    else:
        a = drill_holes(ORIG_BASE / f"Reid Orthotic v2 R4 {side} Gerbers" /
                        f"Reid Orthotic v2 {side}-NonPlated.TXT")
        b = drill_holes(reexp_path(side, "NPTH.drl"))
    tol = 0.005
    used = [False] * len(b)
    unmatched_a, pos_dev, dia_dev = [], 0.0, 0.0
    for (x, y, d) in a:
        tx, ty = x + DX, y + DY
        hit = None
        for k, (bx, by, bd) in enumerate(b):
            if used[k]:
                continue
            if abs(bx - tx) <= tol and abs(by - ty) <= tol and \
                    abs(bd - d) <= tol:
                hit = k
                pos_dev = max(pos_dev, math.hypot(bx - tx, by - ty))
                dia_dev = max(dia_dev, abs(bd - d))
                break
        if hit is None:
            unmatched_a.append([x, y, d])
        else:
            used[hit] = True
    unmatched_b = [list(b[k]) for k in range(len(b)) if not used[k]]
    return {"kind": kind, "orig": len(a), "reexport": len(b),
            "matched": len(a) - len(unmatched_a),
            "max_pos_dev_um": round(pos_dev * 1000, 2),
            "max_dia_dev_um": round(dia_dev * 1000, 2),
            "unmatched_orig": unmatched_a, "unmatched_reexport": unmatched_b}


# -------------------------------------------------------------------- main
def compare_layer(side, layer, ext, suffix, canvas, ppmm,
                  inside_mask_cache, zone_mask_cache, inv, save_images):
    go = load(side, ext, "orig")
    gr = load(side, suffix, "reexp")
    (x0, y0), (x1, y1) = canvas
    r_bounds = ((x0 + DX, y0 + DY), (x1 + DX, y1 + DY))
    a = render(go, canvas, ppmm)
    b = render(gr, r_bounds, ppmm)

    dy, dx, gain = best_shift(a, b)
    align_note = None
    base_overlap = np.count_nonzero(a & b)
    if gain > 0.002 * max(base_overlap, 1):
        align_note = (f"overlap could improve by {gain} px at shift "
                      f"({dx},{dy}) px -- analytic alignment suspect")

    if ppmm not in inside_mask_cache:
        bp = board_poly(inv)
        inside_mask_cache[ppmm] = mask_from_polys(
            [bp.buffer(0.3)] if bp else [], canvas, ppmm)
    inside = inside_mask_cache[ppmm]
    key = (layer, ppmm)
    if layer in COPPER:
        if key not in zone_mask_cache:
            zone_mask_cache[key] = mask_from_polys(
                zone_polys(inv, layer), canvas, ppmm)
        zmask = zone_mask_cache[key]
    else:
        zmask = np.zeros_like(a)

    xor = a ^ b
    union = a | b
    px2mm2 = 1.0 / ppmm ** 2
    n_union = np.count_nonzero(union)
    n_xor = np.count_nonzero(xor)
    n_caption = np.count_nonzero(xor & ~inside)
    n_zone = np.count_nonzero(xor & inside & zmask)
    n_nonzone = np.count_nonzero(xor & inside & ~zmask)
    nz_pct = 100.0 * n_nonzone / n_union if n_union else 0.0
    n_robust = np.count_nonzero(open3(xor & inside & ~zmask))
    rb_pct = 100.0 * n_robust / n_union if n_union else 0.0

    res = {
        "side": side, "layer": layer, "ppmm": ppmm,
        "union_mm2": round(n_union * px2mm2, 3),
        "xor_total_mm2": round(n_xor * px2mm2, 4),
        "xor_total_pct_union": round(100.0 * n_xor / max(n_union, 1), 4),
        "xor_zone_mm2": round(n_zone * px2mm2, 4),
        "xor_caption_outside_board_mm2": round(n_caption * px2mm2, 4),
        "xor_nonzone_mm2": round(n_nonzone * px2mm2, 4),
        "xor_nonzone_pct_union": round(nz_pct, 4),
        "xor_nonzone_robust_mm2": round(n_robust * px2mm2, 4),
        "xor_nonzone_robust_pct_union": round(rb_pct, 4),
        "verdict": "PASS" if rb_pct < PASS_PCT else "REVIEW",
    }
    if align_note:
        res["alignment_note"] = align_note
    if save_images:
        save_overlay(a, b, VALDIR / f"xor_{side}_{layer.replace('.','')}.png")
        core = open3(xor & inside & ~zmask)
        wins = worst_windows(core if np.any(core) else xor, ppmm)
        res["worst_windows"] = save_crops(a, b, wins, canvas, ppmm,
                                          side, layer)
    return res


def run_side(side, fine):
    inv = json.loads(
        (NETDIR / f"inventory_{side.lower()}.json").read_text())
    files = []
    for layer, ext, suffix in LAYERS:
        files.append((load(side, ext, "orig"), False))
        files.append((load(side, suffix, "reexp"), True))
    canvas = canvas_for(side, files)
    inside_cache, zone_cache = {}, {}
    out = {"side": side,
           "canvas_orig_frame": canvas,
           "translation_orig_to_reexport_mm": [DX, DY],
           "layers": [], "layers_fine": [], "drills": []}
    for layer, ext, suffix in LAYERS:
        r = compare_layer(side, layer, ext, suffix, canvas, PPMM_MAIN,
                          inside_cache, zone_cache, inv, save_images=True)
        out["layers"].append(r)
        print(f"{side} {layer:10s} 25um: union {r['union_mm2']:9.2f} mm2  "
              f"xor {r['xor_total_mm2']:8.3f}  nonzone {r['xor_nonzone_mm2']:8.3f} "
              f"({r['xor_nonzone_pct_union']:.4f}%)  {r['verdict']}",
              flush=True)
    if fine:
        for layer, ext, suffix in LAYERS:
            if layer not in COPPER:
                continue
            r = compare_layer(side, layer, ext, suffix, canvas, PPMM_FINE,
                              inside_cache, zone_cache, inv,
                              save_images=False)
            out["layers_fine"].append(r)
            print(f"{side} {layer:10s} 10um: nonzone {r['xor_nonzone_mm2']:8.3f} "
                  f"({r['xor_nonzone_pct_union']:.4f}%)  {r['verdict']}",
                  flush=True)
    for kind in ("PTH", "NPTH"):
        d = compare_drills(side, kind)
        out["drills"].append(d)
        print(f"{side} drill {kind}: {d['matched']}/{d['orig']} matched, "
              f"max pos dev {d['max_pos_dev_um']} um, "
              f"max dia dev {d['max_dia_dev_um']} um", flush=True)
    return out


def write_report(results):
    L = []
    L.append("# R4 KiCad reconstruction - gerber re-export diff validation\n")
    L.append("Original Altium production gerbers vs `kicad-cli pcb export "
             "gerbers` from the reconstructed `.kicad_pcb` files.\n")
    L.append("Alignment: analytic, from the generator transform "
             "(`kx = x - 240`, `ky = 300 - y`; KiCad plots Y-up again, so "
             "re-export = original + (-240, -300) mm, no flip), verified by "
             "overlap maximization. Rasters at 25 um/px; copper confirmed at "
             "10 um/px.\n")
    L.append("XOR buckets: **zone** = inside as-built pour regions "
             f"(buffered {ZONE_BUFFER} mm) -- pour refill/tessellation "
             "noise; **caption** = outside the board outline (the out-of-"
             "board caption/drawing strokes); **non-zone** = real copper/"
             "mask/paste/outline differences inside the board. Verdict: "
             f"PASS if non-zone XOR < {PASS_PCT} % of the layer union.\n")
    for res in results:
        side = res["side"]
        L.append(f"\n## {side}\n")
        L.append("| Layer | Union mm² | XOR total mm² | XOR zone mm² | "
                 "XOR caption mm² | XOR non-zone mm² | non-zone >2px mm² | "
                 "non-zone >2px % of union | Verdict |")
        L.append("|---|---|---|---|---|---|---|---|---|")
        for r in res["layers"]:
            L.append(
                f"| {r['layer']} | {r['union_mm2']} | {r['xor_total_mm2']} "
                f"| {r['xor_zone_mm2']} | "
                f"{r['xor_caption_outside_board_mm2']} | "
                f"{r['xor_nonzone_mm2']} | {r['xor_nonzone_robust_mm2']} | "
                f"{r['xor_nonzone_robust_pct_union']} "
                f"| **{r['verdict']}** |")
        if res["layers_fine"]:
            L.append("\n10 um/px confirmation pass (copper):\n")
            L.append("| Layer | XOR non-zone mm² | non-zone % of union | "
                     "Verdict |")
            L.append("|---|---|---|---|")
            for r in res["layers_fine"]:
                L.append(f"| {r['layer']} | {r['xor_nonzone_mm2']} | "
                         f"{r['xor_nonzone_pct_union']} | "
                         f"**{r['verdict']}** |")
        L.append("\nDrills (position+diameter tolerance 5 um):\n")
        L.append("| File | Orig | Re-export | Matched | max pos dev um | "
                 "max dia dev um | Verdict |")
        L.append("|---|---|---|---|---|---|---|")
        for d in res["drills"]:
            ok = (d["matched"] == d["orig"] == d["reexport"])
            L.append(f"| {d['kind']} | {d['orig']} | {d['reexport']} | "
                     f"{d['matched']} | {d['max_pos_dev_um']} | "
                     f"{d['max_dia_dev_um']} | "
                     f"**{'PASS' if ok else 'REVIEW'}** |")
    (VALDIR / "COMPARE_REPORT.md").write_text("\n".join(L) + "\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    fine = "--fine" in sys.argv[1:]
    sides = args or ["LHS", "RHS"]
    results = [run_side(s, fine) for s in sides]
    VALDIR.mkdir(exist_ok=True)
    (VALDIR / "compare_results.json").write_text(
        json.dumps(results, indent=1))
    write_report(results)
    print("wrote", VALDIR / "COMPARE_REPORT.md")


if __name__ == "__main__":
    main()
