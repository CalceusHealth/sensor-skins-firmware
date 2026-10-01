#!/usr/bin/env python3
"""To-scale layout study: sensor membrane + sensors + R4 board + coil/battery
options, in the R3 assembly model's mm frame.

Sources
  membrane, R3 PCB footprint : artefacts/Reid Orthotic v2 R3 MECH/SS-483 V1 <S>-STL.stl
  sensor pads (pixels)       : artefacts/all_sensor_coordinates.csv (+ <T>-<L/R>.png canvas)
  R4 board outline           : R4 gerbers, GM4 (board outline layer)

Registration
  R4 outline -> R3 PCB footprint: best of 8 rotations/mirrors by IoU.
  PNG -> membrane: canvas bbox -> membrane bbox (mirror choices tested by IoU).

Output: hardware/r4-kicad/layout_study/layout_<side>.svg/.png + layout.json
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from shapely import affinity
from shapely.geometry import Polygon, Point, box
from shapely.ops import unary_union, polygonize, linemerge

ROOT = Path(__file__).resolve().parents[3]
MECH = ROOT / "artefacts" / "Reid Orthotic v2 R3 MECH"
GERB = ROOT / "artefacts" / "SSII Orthotics Electronics - Design Verification"
OUT = ROOT / "hardware" / "r4-kicad" / "layout_study"

# Current build, measured from hardware/RHS.jpg against the 21.25 mm board
# width: coil centre ~18 mm heel-ward of the board's heel edge, centred on
# the board width. TDK WR151580: OD 15 mm.
COIL_OD = 15.0
CURRENT_COIL_OFFSET = 18.0
MAGNET_D = 4.0
TAB_GAP = 1.0          # board edge -> coil OD clearance on the proposed tab
TAB_MARGIN = 1.0       # copper keep-back from tab outline


def stl_parts(path):
    d = path.read_bytes()
    n = struct.unpack("<I", d[80:84])[0]
    a = np.frombuffer(d[84:84 + n * 50], dtype=np.dtype(
        [("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    t = a["v"].reshape(-1, 3, 3).astype(float)
    V = np.round(t.reshape(-1, 3), 3)
    _, inv = np.unique(V, axis=0, return_inverse=True)
    inv = inv.reshape(-1, 3)
    par = np.arange(inv.max() + 1)

    def f(x):
        while par[x] != x:
            par[x] = par[par[x]]
            x = par[x]
        return x
    for a_, b_, c_ in inv:
        ra = f(a_)
        par[f(b_)] = ra
        par[f(c_)] = ra
    roots = np.array([f(i) for i in inv[:, 0]])
    parts = {}
    for i, r in enumerate(roots):
        parts.setdefault(r, []).append(i)
    out = []
    for idx in parts.values():
        tri = t[idx]
        pts = tri.reshape(-1, 3)
        mn, mx = pts.min(0), pts.max(0)
        out.append({"tri": tri, "min": mn, "max": mx, "size": mx - mn})
    return out


def footprint(tri):
    polys = [Polygon(p[:, :2]) for p in tri]
    polys = [p for p in polys if p.is_valid and p.area > 1e-9]
    return unary_union(polys).buffer(0.01).buffer(-0.01)


def r4_outline(side):
    """Board outline from the reconstructed .kicad_pcb Edge.Cuts (validated
    as a single closed loop; KiCad frame, Y down -- registration below
    resolves orientation)."""
    import re
    from shapely.geometry import LineString
    t = (ROOT / "hardware" / "r4-kicad" / "pcb" /
         f"Reid_Orthotic_v2_{side}.kicad_pcb").read_text()
    lines = []
    num = r"(-?[\d.]+)"
    for m in re.finditer(r"\(gr_line \(start %s %s\) \(end %s %s\)[^\n]*?"
                         r"\(layer \"Edge\.Cuts\"\)" % (num, num, num, num), t):
        x1, y1, x2, y2 = map(float, m.groups())
        lines.append([(x1, y1), (x2, y2)])
    for m in re.finditer(r"\(gr_arc \(start %s %s\) \(mid %s %s\) \(end %s %s\)"
                         r"[^\n]*?\(layer \"Edge\.Cuts\"\)" % ((num,) * 6), t):
        (ax, ay, mx, my, bx, by) = map(float, m.groups())
        # circle through three points
        d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my))
        ux = ((ax*ax + ay*ay) * (my - by) + (mx*mx + my*my) * (by - ay)
              + (bx*bx + by*by) * (ay - my)) / d
        uy = ((ax*ax + ay*ay) * (bx - mx) + (mx*mx + my*my) * (ax - bx)
              + (bx*bx + by*by) * (mx - ax)) / d
        r = math.hypot(ax - ux, ay - uy)
        a1 = math.atan2(ay - uy, ax - ux)
        am = math.atan2(my - uy, mx - ux)
        a2 = math.atan2(by - uy, bx - ux)
        # sweep from a1 to a2 passing through am
        def norm(a):
            return a % (2 * math.pi)
        ccw = norm(am - a1) < norm(a2 - a1)
        span = norm(a2 - a1) if ccw else -norm(a1 - a2)
        lines.append([(ux + r * math.cos(a1 + span * k / 24),
                       uy + r * math.sin(a1 + span * k / 24)) for k in range(25)])

    def snap(p):
        return (round(p[0] / 0.05) * 0.05, round(p[1] / 0.05) * 0.05)
    ls = [LineString([snap(l[0])] + l[1:-1] + [snap(l[-1])]) for l in lines]
    polys = list(polygonize(unary_union(ls)))
    if not polys:
        raise SystemExit(f"{side}: Edge.Cuts did not close ({len(ls)} objects)")
    return max(polys, key=lambda p: p.area)


DRAWING = ROOT / "hardware" / "CAL1020 V2 Rev0.jpg"
# Reid Print CAL1020 rev 2 (26/04/2024), dimensioned view (bottom-left):
# orthotic 93.10 x 270.00 mm; membrane 70.40 x 219.50 mm, inset 11.75 mm from
# the orthotic bbox side and 25.00 mm from the heel.
ORTH_W, ORTH_L = 93.10, 270.00
MEMB_INSET_X, MEMB_INSET_HEEL = 11.75, 25.00


def orthotic_outline(memb):
    """Trace the red orthotic outline from the drawing raster into the model
    frame. The drawing view has the PCB notch on the opposite side to the
    model, so it is mirrored in x; the 11.75 mm inset therefore lands on the
    model's +x side. Raster scaling: accuracy ~ +/-0.5 mm."""
    Image.MAX_IMAGE_PIXELS = None
    im = Image.open(DRAWING).convert("RGB")
    W, H = im.size
    s = W / 2000
    crop = im.crop((int(20 * s), int(640 * s), int(290 * s), int(1260 * s)))
    a = np.asarray(crop).astype(int)
    red = (a[..., 0] > 170) & (a[..., 1] < 110) & (a[..., 2] < 140)
    ys, xs = np.nonzero(red)
    x0p, x1p, y0p, y1p = xs.min(), xs.max(), ys.min(), ys.max()
    mx0, my0, mx1, my1 = memb.bounds
    ox1 = mx1 + MEMB_INSET_X            # model +x edge of the orthotic bbox
    oy0 = my0 - MEMB_INSET_HEEL         # heel end
    # contour -> polygon: order red pixels by angle around the centroid,
    # binned, taking the outermost pixel in each bin
    cx, cy = xs.mean(), ys.mean()
    ang = np.arctan2(ys - cy, xs - cx)
    rad = np.hypot(xs - cx, ys - cy)
    nb = 720
    b = ((ang + np.pi) / (2 * np.pi) * nb).astype(int) % nb
    pts = []
    for k in range(nb):
        sel = b == k
        if not np.any(sel):
            continue
        i = np.argmax(np.where(sel, rad, -1))
        u = (xs[i] - x0p) / (x1p - x0p)      # 0..1 across, drawing frame
        v = (ys[i] - y0p) / (y1p - y0p)      # 0 = toe (top) .. 1 = heel
        x = ox1 - u * ORTH_W                 # mirrored into model frame
        y = oy0 + (1 - v) * ORTH_L
        pts.append((x, y))
    poly = Polygon(pts).buffer(0)
    return poly, {"px_bbox": [int(x0p), int(y0p), int(x1p), int(y1p)],
                  "px_per_mm": [round((x1p - x0p) / ORTH_W, 3),
                                round((y1p - y0p) / ORTH_L, 3)]}


def register(src, dst):
    """Place src onto dst: 8 orientations, centroid-aligned, best IoU."""
    best = None
    for mir in (False, True):
        s0 = affinity.scale(src, -1 if mir else 1, 1, origin="centroid")
        for rot in (0, 90, 180, 270):
            s1 = affinity.rotate(s0, rot, origin="centroid")
            dx = dst.centroid.x - s1.centroid.x
            dy = dst.centroid.y - s1.centroid.y
            s2 = affinity.translate(s1, dx, dy)
            iou = s2.intersection(dst).area / s2.union(dst).area
            if best is None or iou > best[0]:
                best = (iou, mir, rot, dx, dy, s2)
    return best


def png_mask(name, size=(903, 2877)):
    a = np.asarray(Image.open(ROOT / "artefacts" / f"{name}.png"))
    return a[..., 3] > 128


def register_png(memb, mask):
    """Map PNG pixel frame onto the membrane bbox; test the 4 mirrorings."""
    H, W = mask.shape
    x0, y0, x1, y1 = memb.bounds
    best = None
    ys, xs = np.mgrid[0:H:4, 0:W:4]
    sub = mask[::4, ::4]
    for fx in (False, True):
        for fy in (False, True):
            def to_mm(px, py, fx=fx, fy=fy):
                u = px / (W - 1)
                v = py / (H - 1)
                if fx:
                    u = 1 - u
                # image y grows downward; model y grows upward
                v = v if fy else 1 - v
                return x0 + u * (x1 - x0), y0 + v * (y1 - y0)
            mx, my = to_mm(xs, ys)
            # rasterised membrane membership at sample points
            from shapely import vectorized
            inside = vectorized.contains(memb, mx, my)
            iou = np.count_nonzero(inside & sub) / max(
                np.count_nonzero(inside | sub), 1)
            if best is None or iou > best[0]:
                best = (iou, fx, fy, to_mm)
    return best


def load_sensors(side_letter):
    rows = {}
    import csv
    with open(ROOT / "artefacts" / "all_sensor_coordinates.csv") as fh:
        for r in csv.DictReader(fh):
            lay = r["Layout"]
            kind, s = lay.split("-")
            if s != side_letter:
                continue
            corners = [(float(r[f"{k}_X"]), float(r[f"{k}_Y"]))
                       for k in ("Left", "Top", "Right", "Bottom")]
            if r["Type"] != "Diamond":
                xs_ = [c[0] for c in corners]
                ys_ = [c[1] for c in corners]
                corners = [(min(xs_), min(ys_)), (max(xs_), min(ys_)),
                           (max(xs_), max(ys_)), (min(xs_), max(ys_))]
            rows.setdefault(kind, []).append(
                {"name": r["Sensor"], "corners": corners,
                 "center": (float(r["Center_X"]), float(r["Center_Y"]))})
    return rows


def study(side):
    S = side[0]  # L / R
    parts = stl_parts(MECH / f"SS-483 V1 {side}-STL.stl")
    memb_part = max(parts, key=lambda p: p["size"][0] * p["size"][1])
    pcb_part = min((p for p in parts if abs(p["size"][0] - 21.55) < 0.6
                    and abs(p["size"][1] - 46.55) < 0.6),
                   key=lambda p: abs(p["size"][2] - 0.49))
    memb = footprint(memb_part["tri"])
    pcb_r3 = footprint(pcb_part["tri"]).convex_hull
    batt = [p for p in parts if abs(p["size"][0] - 10.2) < 0.3
            and abs(p["size"][1] - 31.0) < 0.3]
    r4 = r4_outline(side)
    iou_b, mir, rot, dx, dy, r4_on = register(r4, pcb_r3)

    # heel edge of the board = its lowest edge in the model frame (heel is
    # at -y): fit the coil tab below the board's minimum y, centred on the
    # board's lower-edge midpoint
    bx0, by0, bx1, by1 = r4_on.bounds
    low = r4_on.intersection(box(bx0 - 1, by0 - 0.5, bx1 + 1, by0 + 0.5))
    ex = low.centroid.x if not low.is_empty else (bx0 + bx1) / 2
    cur_c = (ex, by0 - CURRENT_COIL_OFFSET)
    tab_c = (ex, by0 - TAB_GAP - COIL_OD / 2)
    tab_r = COIL_OD / 2 + TAB_MARGIN
    tab = unary_union([Point(tab_c).buffer(tab_r, 64),
                       box(tab_c[0] - tab_r, tab_c[1], tab_c[0] + tab_r, by0 + 0.3)])
    board_v3 = unary_union([r4_on, tab]).buffer(0.2).buffer(-0.2)

    sensors = load_sensors(S)
    reg = {}
    sens_mm = {}
    for kind in ("FSR", "CAP", "TMP"):
        name = f"{kind}-{S}" if kind != "TMP" else f"FSR-{S}"
        iou_p, fx, fy, to_mm = register_png(memb, png_mask(name))
        reg[kind] = {"iou": round(iou_p, 4), "mirror_x": fx, "mirror_y": fy}
        sens_mm[kind] = []
        for s_ in sensors.get(kind, []):
            cs = [to_mm(x, y) for x, y in s_["corners"]]
            c = to_mm(*s_["center"])
            sens_mm[kind].append({"name": s_["name"], "center": c,
                                  "poly": cs})

    orth, orth_meta = orthotic_outline(memb)

    def edge_clearance(g):
        return round(orth.exterior.distance(g) if orth.contains(g)
                     else -g.difference(orth).area, 2)

    # what does the proposed tab overlap?
    tab_only = tab.difference(r4_on)
    hits = []
    for kind, lst in sens_mm.items():
        for s_ in lst:
            pg = Polygon(s_["poly"])
            if pg.is_valid and pg.distance(tab_only) < 2.0:
                hits.append(f"{kind} {s_['name']} ({pg.distance(tab_only):.1f} mm)")
    on_membrane = tab_only.intersection(memb).area / tab_only.area

    return {
        "side": side, "memb": memb, "pcb_r3": pcb_r3, "r4": r4_on,
        "batt": [footprint(b["tri"]) for b in batt],
        "cur_coil": Point(cur_c).buffer(COIL_OD / 2, 64),
        "tab": tab_only, "board_v3": board_v3,
        "tab_coil": Point(tab_c).buffer(COIL_OD / 2, 64),
        "magnet": Point(tab_c).buffer(MAGNET_D / 2, 32),
        "sensors": sens_mm, "orth": orth,
        "report": {
            "r4_to_r3_iou": round(iou_b, 4), "r4_mirror": mir, "r4_rot": rot,
            "png_registration": reg,
            "membrane_mm": [round(v, 2) for v in memb.bounds],
            "r4_board_bounds_mm": [round(v, 2) for v in r4_on.bounds],
            "current_coil_centre": [round(v, 2) for v in cur_c],
            "proposed_coil_centre": [round(v, 2) for v in tab_c],
            "proposed_board_mm": [round(board_v3.bounds[2] - board_v3.bounds[0], 2),
                                  round(board_v3.bounds[3] - board_v3.bounds[1], 2)],
            "tab_area_on_membrane_frac": round(on_membrane, 3),
            "sensors_within_2mm_of_tab": hits,
            "orthotic_trace": orth_meta,
            "membrane_inside_orthotic": bool(orth.buffer(1.0).contains(memb)),
            "clearance_to_orthotic_edge_mm": {
                "r4_board": edge_clearance(r4_on),
                "proposed_board_with_tab": edge_clearance(board_v3),
                "current_coil": edge_clearance(Point(cur_c).buffer(COIL_OD / 2, 64)),
            },
        },
    }


def draw(res, ppmm=6):
    m = res["memb"]
    allg = unary_union([m, res["cur_coil"], res["board_v3"], res["orth"]])
    x0, y0, x1, y1 = allg.bounds
    pad = 14
    W = int((x1 - x0) * ppmm) + 2 * pad + 260
    H = int((y1 - y0) * ppmm) + 2 * pad
    img = Image.new("RGB", (W, H), (250, 250, 248))
    dr = ImageDraw.Draw(img)

    def P(x, y):
        return (pad + (x - x0) * ppmm, pad + (y1 - y) * ppmm)

    def poly(g, fill=None, outline=None, width=1):
        geoms = getattr(g, "geoms", [g])
        for gg in geoms:
            if gg.is_empty:
                continue
            pts = [P(*c) for c in gg.exterior.coords]
            dr.polygon(pts, fill=fill, outline=outline, width=width)
            for hole in gg.interiors:
                dr.polygon([P(*c) for c in hole.coords], fill=(250, 250, 248))

    poly(res["orth"], outline=(215, 40, 60), width=2)
    poly(m, fill=(226, 238, 246), outline=(150, 175, 195))
    for kind, col in (("FSR", (70, 110, 200)), ("CAP", (60, 160, 120)),
                      ("TMP", (200, 120, 40))):
        for s_ in res["sensors"].get(kind, []):
            pg = Polygon(s_["poly"])
            if kind == "TMP":
                cx, cy = P(*s_["center"])
                dr.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=col)
            else:
                poly(pg, outline=col, width=2)
    poly(res["cur_coil"], outline=(205, 125, 60), width=2)
    poly(res["board_v3"], fill=(55, 55, 60))
    poly(res["r4"], outline=(200, 200, 200), width=1)
    for b in res["batt"]:
        poly(b, outline=(215, 90, 90), width=2)
        # coin-cell alternatives, centred where the pouch cell sits today
        for dia, col in ((12.1, (150, 90, 200)), (20.0, (90, 160, 210))):
            poly(b.centroid.buffer(dia / 2, 64), outline=col, width=2)
    poly(res["tab_coil"], outline=(240, 160, 70), width=2)
    poly(res["magnet"], fill=(200, 200, 200))

    lx = W - 250
    items = [((215, 40, 60), "orthotic outline (CAL1020 drawing)"),
             ((226, 238, 246), "sensor layer (R3 model, mm)"),
             ((70, 110, 200), "FSR pads"), ((60, 160, 120), "CAP pads"),
             ((200, 120, 40), "temperature sensors"),
             ((55, 55, 60), "R4 board + proposed coil tab"),
             ((240, 160, 70), "proposed etched coil (15 mm)"),
             ((205, 125, 60), "current coil on bridge"),
             ((215, 90, 90), "pouch battery today (stacked on board)"),
             ((150, 90, 200), "alt: VARTA CP1254 coin, 12.1 mm"),
             ((90, 160, 210), "alt: LIR2032 coin, 20 mm")]
    for i, (c, t) in enumerate(items):
        yy = pad + 10 + i * 22
        dr.rectangle([lx, yy, lx + 14, yy + 14], fill=c)
        dr.text((lx + 22, yy), t, fill=(30, 30, 30))
    dr.rectangle([lx, H - 40, lx + 10 * ppmm, H - 34], fill=(30, 30, 30))
    dr.text((lx, H - 30), "10 mm", fill=(30, 30, 30))
    dr.text((lx, pad + 10 + len(items) * 22 + 10),
            f"{res['side']}  (toe up, heel down)", fill=(30, 30, 30))
    return img


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    reports = {}
    for side in sys.argv[1:] or ["LHS", "RHS"]:
        res = study(side)
        draw(res).save(OUT / f"layout_{side}.png")
        reports[side] = res["report"]
        print(side, json.dumps(res["report"], indent=1))
    (OUT / "layout.json").write_text(json.dumps(reports, indent=1))
