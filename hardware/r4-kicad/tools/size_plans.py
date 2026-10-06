#!/usr/bin/env python3
"""v3 concept, battery in FRONT of the board, for every insole size (LHS + RHS).

Per-size geometry comes from the Reid Print drawings hardware/CAL10x0*.jpg:
  - "FSR Sensor Layer 2" view: orthotic outline (outer red), sensor-layer
    outline (inner red) and the 19 FSR pads (black squares);
  - "FSR Sensor Layer 1" view: the green PCB position.
Pixels are scaled to mm with each drawing's own orthotic length and width
dimensions (SIZES below, read off the dimensioned RF-shield view).

Board-relative geometry (R4 outline, coil tab, sensor tails, test points) is
the size-S model from layout_study / battery_drawings, i.e. the R3 assembly
model frame. Each size is placed into that frame at its drawn PCB position
(toe-heel), then shifted across the foot so the sensor tails meet the sensor
layer exactly as on the size-S model: the tails are one part, so the
sensor-layer-to-board gap is the same at every size.

Battery: ONE pose relative to the board for every size, so the pack angle,
connector and lead routing are identical across the range. The pose is the
nearest-to-board position at BATT_ANGLE that is legal at every size in
COMMON_SIZES; smaller sizes get the same pose with the violation shown.

Outputs: hardware/r4-kicad/v3_concept/sizes/plan_front_<size>_<side>.png,
         sizes/summary.json, sizes/overview.png
"""
import json
import sys
from pathlib import Path

import contourpy
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as nd
from scipy.signal import fftconvolve
from shapely import affinity
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union

sys.path.insert(0, str(Path(__file__).resolve().parent))
import layout_study as L  # noqa: E402
import battery_drawings as B  # noqa: E402

OUT = B.OUT / "sizes"
HW = L.ROOT / "hardware"

# code, size, file, orthotic L x W, sensor layer L x W (mm), from the drawings
SIZES = [
    ("CAL1000", "XXS", "CAL1000 V2 Rev 1.jpg", 240.00, 81.10, 194.80, 63.20),
    ("CAL1010", "XS", "CAL1010 V2 Rev1.jpg", 257.00, 86.00, 207.00, 66.50),
    ("CAL1020", "S", "CAL1020 V2 Rev0.jpg", 270.00, 93.10, 219.50, 70.40),
    ("CAL1030", "M", "CAL1030 V2 Rev0.jpg", 290.00, 100.00, 231.90, 74.40),
    ("CAL1040", "L", "CAL1040 V2 Rev1.jpg", 305.00, 103.75, 244.90, 77.75),
    ("CAL1050", "XL", "CAL1050 V2 Rev1.jpg", 325.00, 108.00, 254.50, 80.50),
    ("CAL1060", "XXL", "CAL1060 V2 Rev1.jpg", 348.00, 113.00, 270.00, 83.80),
]
K = 2   # raster reduction

# Battery test pads keep R4 positions and numbering (artefacts/pins.png):
# hand-probed with a multimeter, so R4 spacing, not a tight strip.
PAD_NUM = {"0V": "1", "VBAT_F": "2", "VSYS": "3", "3V3": "4"}
VBAT_PAD_CLEAR = 4.0   # new VBAT pad >= this from other pads (probe tip)
BATT_ANGLE = 90        # pack long axis toe-heel, parallel to the board edge
COMMON_SIZES = ("S", "M", "L", "XL", "XXL")   # the shared pose must fit these


# ------------------------------------------------------------ raster
def masks(path):
    Image.MAX_IMAGE_PIXELS = None
    a = np.asarray(Image.open(path).convert("RGB").reduce(K)).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return {"red": (r > 170) & (g < 120) & (b < 150),
            "green": (g > 150) & (g - r > 40) & (b < 200),
            "black": (r < 80) & (g < 80) & (b < 80)}


def outline(mask):
    """Longest boundary of a filled mask as a polygon (pixel coords)."""
    lines = contourpy.contour_generator(z=mask.astype(float)).lines(0.5)
    ln = max(lines, key=len)
    return Polygon(ln).buffer(0)  # contourpy points are (x=col, y=row)


def feet_layer2(m):
    """Two feet in the Layer 2 view: (orthotic mask, sensor-layer mask, pads)."""
    H, W = m["red"].shape
    x0, x1, y1 = int(.24 * W), int(.50 * W), int(.5 * H)
    red = np.zeros_like(m["red"])
    red[:y1, x0:x1] = m["red"][:y1, x0:x1]
    lab, _ = nd.label(nd.binary_dilation(red, iterations=2))
    objs = nd.find_objects(lab)
    big = sorted([(i + 1, o) for i, o in enumerate(objs)
                  if o[0].stop - o[0].start > 0.25 * H],
                 key=lambda t: (t[1][1].stop - t[1][1].start) * (t[1][0].stop - t[1][0].start),
                 reverse=True)
    outers = sorted(big[:2], key=lambda t: t[1][1].start)
    feet = []
    for li, o in outers:
        orth = nd.binary_erosion(nd.binary_fill_holes(lab == li), iterations=2)
        inner = [t for t in big[2:] if t[1][1].start > o[1].start and t[1][1].stop < o[1].stop]
        lj = max(inner, key=lambda t: t[1][0].stop - t[1][0].start)[0]
        memb = nd.binary_erosion(nd.binary_fill_holes(lab == lj), iterations=2)
        bl, n = nd.label(m["black"] & memb)
        pads = []
        for k, sl in enumerate(nd.find_objects(bl)):
            ys, xs = np.nonzero(bl[sl] == k + 1)
            if len(xs) < 800:
                continue
            pts = [(x + sl[1].start, y + sl[0].start) for x, y in zip(xs[::7], ys[::7])]
            pads.append(Polygon(pts).convex_hull if len(pts) > 3 else None)
        feet.append({"orth": orth, "memb": memb, "pads": [p for p in pads if p],
                     "bbox": (o[1].start, o[0].start, o[1].stop, o[0].stop)})
    return feet


def pcb_layer1(m, foot_masks):
    """Drawn PCB (green fill) for each foot, found in the Layer 1 view and
    shifted into the Layer 2 view by cross-correlating the red outlines."""
    H, W = m["red"].shape
    xw, y1 = int(.25 * W), int(.5 * H)
    g = nd.binary_opening(m["green"][:y1, :xw], iterations=3)  # drop Pedar outlines
    lab, _ = nd.label(g)
    blobs = []
    for k, sl in enumerate(nd.find_objects(lab)):
        comp = lab[sl] == k + 1
        area = comp.sum()
        if area > 8000 and area / comp.size > 0.5:
            blobs.append((sl[1].start, k + 1))
    blobs.sort()
    r = 4
    red1 = m["red"][:y1, :xw][::r, ::r].astype(float)
    out = []
    for (_, li), foot in zip(blobs, foot_masks):
        bx0, by0, bx1, by1 = foot["bbox"]
        pad = 40
        tpl = m["red"][max(by0 - pad, 0):by1 + pad, bx0 - pad:bx1 + pad][::r, ::r].astype(float)
        cc = fftconvolve(red1, tpl[::-1, ::-1], mode="valid")
        py, px = np.unravel_index(np.argmax(cc), cc.shape)
        dx = (bx0 - pad) - px * r
        dy = (max(by0 - pad, 0)) - py * r
        ys, xs = np.nonzero(lab == li)
        pts = np.c_[xs + dx, ys + dy][::5]
        out.append({"poly": Polygon(pts).convex_hull, "shift": (int(dx), int(dy))})
    return out


def to_mm(geom, sx, sy, mirror):
    g = affinity.scale(geom, 1 / sx, -1 / sy, origin=(0, 0))
    return affinity.scale(g, -1, 1, origin=(0, 0)) if mirror else g


def extract(code, size, fname, OL, OW, ML, MW):
    m = masks(HW / fname)
    feet = feet_layer2(m)
    pcbs = pcb_layer1(m, feet)
    res = []
    for f, p in zip(feet, pcbs):
        bx0, by0, bx1, by1 = f["bbox"]
        sx = (bx1 - bx0 - 4) / OW          # minus the dilation
        sy = (by1 - by0 - 4) / OL
        orth, memb = outline(f["orth"]), outline(f["memb"])
        mb = memb.bounds
        res.append({"orth": orth, "memb": memb, "pads": f["pads"], "pcb": p["poly"],
                    "sx": sx, "sy": sy,
                    "check_memb_mm": [round((mb[2] - mb[0]) / sx, 2),
                                      round((mb[3] - mb[1]) / sy, 2)],
                    "n_pads": len(f["pads"]), "l1_shift_px": p["shift"]})
    return res


# ------------------------------------------------------------ placement
def keepouts(res):
    """(allowed region, keep-out) for the pack, as battery_drawings.place_battery."""
    keep = unary_union([res["memb"], res["tails"], res["board_v3"],
                        res["tab_coil"].buffer(B.COIL_KEEPOUT)]).buffer(L.BATT_GAP)
    return res["orth"].buffer(-L.EDGE_MARGIN), keep


def front_grid(res, x0=-40, x1=30):
    by1 = res["r4"].bounds[3]
    return [(cx, cy) for cy in np.arange(by1, by1 + 40, 0.5)
            for cx in np.arange(x0, x1, 0.5)]


def common_pose(sres, placed):
    """Nearest-to-board pack centre at BATT_ANGLE that is legal at every
    size in COMMON_SIZES (board/tails/coil are the same model at all sizes,
    so one centre in the model frame is one pose relative to the board)."""
    from shapely.prepared import prep
    envs = []
    for g in placed:
        A, Kp = keepouts({**sres, "orth": g["orth"], "memb": g["memb"]})
        envs.append((prep(A), prep(Kp)))
    best = None
    for cx, cy in front_grid(sres):
        r = L.cell_geom("rect", B.POUCH, cx, cy, BATT_ANGLE)
        if all(A.contains(r) and not Kp.intersects(r) for A, Kp in envs):
            d = r.distance(sres["board_v3"])
            if best is None or d < best[0]:
                best = (d, (float(cx), float(cy)))
    if best is None:
        raise RuntimeError(f"no {BATT_ANGLE} deg pose fits all of {COMMON_SIZES}")
    return best[1]


def battery_at(res, centre):
    """The pack at the shared pose, PCM at the end nearest the board, plus
    any violation of this size's edge / sensor-layer clearances."""
    import math
    cx, cy = centre
    ang = BATT_ANGLE
    g = L.cell_geom("rect", B.POUCH, cx, cy, ang)
    a = math.radians(ang)
    ux, uy = math.cos(a), math.sin(a)
    e1 = Point(cx + ux * B.POUCH[0] / 2, cy + uy * B.POUCH[0] / 2)
    e2 = Point(cx - ux * B.POUCH[0] / 2, cy - uy * B.POUCH[0] / 2)
    near = e1 if e1.distance(res["board_v3"]) < e2.distance(res["board_v3"]) else e2
    sgn = 1 if near is e1 else -1
    pc = (cx + sgn * ux * (B.POUCH[0] / 2 - B.PCM_LEN / 2),
          cy + sgn * uy * (B.POUCH[0] / 2 - B.PCM_LEN / 2))
    pcm = affinity.rotate(box(pc[0] - B.PCM_LEN / 2, pc[1] - B.POUCH[1] / 2,
                              pc[0] + B.PCM_LEN / 2, pc[1] + B.POUCH[1] / 2), ang, origin=pc)
    allowed, keep = keepouts(res)
    viol = g.difference(allowed).union(g.intersection(keep))
    batt = {"geom": g, "gap": g.distance(res["board_v3"]), "angle": ang,
            "centre": centre, "pcm": pcm, "lead_exit": (near.x, near.y)}
    if viol.area > 0.05:
        batt.update(violation=viol, violation_mm2=round(viol.area, 1),
                    max_len_fits=longest_pack(res, allowed, keep))
    return batt


def longest_pack(res, allowed, keep):
    """Longest 10.2 mm-wide pack at BATT_ANGLE that fits anywhere in front
    of the board with full clearances."""
    from shapely.prepared import prep
    A, Kp = prep(allowed), prep(keep)
    grid = front_grid(res)
    for Lp in np.arange(B.POUCH[0] - 1, 10, -1):
        if any(A.contains(r) and not Kp.intersects(r) for cx, cy in grid
               for r in [L.cell_geom("rect", (Lp, B.POUCH[1]), cx, cy, BATT_ANGLE)]):
            return float(Lp)
    return 0.0


def fit_size_s(sres, foot):
    """For size S: which drawn foot / mirror is this model side, and the
    offset from the drawn PCB centroid to the model frame (best IoU of the
    drawn sensor layer with the R3 model's)."""
    best = None
    target = sres["memb"]
    for fi, f in enumerate(foot):
        for mir in (False, True):
            memb = to_mm(f["memb"], f["sx"], f["sy"], mir)
            pc = to_mm(f["pcb"], f["sx"], f["sy"], mir).centroid
            base = (sres["pcb_r3"].centroid.x - pc.x, sres["pcb_r3"].centroid.y - pc.y)
            for ddx in np.arange(-6, 6.01, 0.5):
                for ddy in np.arange(-6, 6.01, 0.5):
                    g = affinity.translate(memb, base[0] + ddx, base[1] + ddy)
                    iou = g.intersection(target).area / g.union(target).area
                    if best is None or iou > best[0]:
                        best = (iou, fi, mir, (base[0] + ddx - (sres["pcb_r3"].centroid.x - pc.x),
                                               base[1] + ddy - (sres["pcb_r3"].centroid.y - pc.y)))
    return {"iou": round(best[0], 4), "foot": best[1], "mirror": best[2],
            "pcb_offset": [round(v, 2) for v in best[3]]}


def tail_gap(memb, tails):
    """Gap from the tip of the lower (heel-side) sensor tail to the sensor
    layer edge it joins (model x runs board -> sensor layer)."""
    t = min(getattr(tails, "geoms", [tails]), key=lambda p: p.bounds[1])
    x0, _, x1, _ = t.bounds
    xs, ys = np.asarray(t.exterior.coords).T
    tip = ys[xs > x1 - 0.5]
    band = memb.intersection(box(x0, tip.min(), 1e4, tip.max()))
    return band.bounds[0] - x1


def place(f, fit, sres):
    """Drawn foot -> model frame: toe-heel at the drawn PCB position (with
    the size-S offset), across the foot so the tails meet the sensor layer
    as on the size-S model."""
    mir = fit["mirror"]
    pc = to_mm(f["pcb"], f["sx"], f["sy"], mir).centroid
    dx = sres["pcb_r3"].centroid.x - pc.x + fit["pcb_offset"][0]
    dy = sres["pcb_r3"].centroid.y - pc.y + fit["pcb_offset"][1]
    memb = affinity.translate(to_mm(f["memb"], f["sx"], f["sy"], mir), dx, dy)
    dx += tail_gap(sres["memb"], sres["tails"]) - tail_gap(memb, sres["tails"])

    def T(g):
        return affinity.translate(to_mm(g, f["sx"], f["sy"], mir), dx, dy)
    return {"orth": T(f["orth"]), "memb": T(f["memb"]),
            "pads": [T(p) for p in f["pads"]], "pcb_drawn": T(f["pcb"])}


def magnet_arc(c, ang):
    ring = Point(c).buffer(12.6, 96).difference(Point(c).buffer(8.6, 96))
    return ring.intersection(affinity.translate(
        affinity.rotate(box(-30, 0, 30, 30), ang, origin=(0, 0)), *c))


def magnet_c(sres, placed):
    """C-shaped N52 magnet: 180 deg arc, ID 17.2 / OD 25.2, concentric with
    the coil, ONE orientation for every size (the puck's split ring must
    mate the same way): least worst-case overlap with the board, sensor
    layer, tails and orthotic edge over COMMON_SIZES, then most clearance."""
    c = sres["tab_coil"].centroid.coords[0]
    best = None
    for ang in range(0, 360, 5):
        arc = magnet_arc(c, ang)
        bad = max(arc.intersection(unary_union([sres["r4"], g["memb"], sres["tails"]])).area
                  + arc.difference(g["orth"]).area for g in placed)
        key = (round(bad, 1), -min(arc.distance(g["memb"]) for g in placed))
        if best is None or key < best[0]:
            best = (key, ang)
    return best[1]


def plan_size(side, size, code, sres, geo, pose, mag_ang):
    res = dict(sres)
    res["orth"], res["memb"] = geo["orth"], geo["memb"]
    batt = battery_at(res, pose)
    fits = "violation" not in batt
    board_out = res["board_v3"].difference(res["orth"]).area
    conn = B.place_connector(res, batt)
    leads = B.lead_path(res, batt, conn)
    pocket = batt["geom"].buffer(B.POCKET_MARGIN, join_style=2)

    tps = B.test_points(side)
    # pads 1-4 at their R4 positions; the second 0V pad is not a test point
    batt_pads, seen = [], set()
    for n, x, y in tps:
        if n in PAD_NUM and n not in seen:
            batt_pads.append((PAD_NUM[n], n, x, y))
            seen.add(n)
    swd = [(n, x, y) for n, x, y in tps if n in ("SWDIO", "SWDCLK", "MCU_RST")]
    # new VBAT pad (pack side of F1): beside the header on the board,
    # >= VBAT_PAD_CLEAR from every other pad
    others = [Point(x, y) for _, _, x, y in batt_pads] + [Point(x, y) for _, x, y in swd]
    inner = res["r4"].buffer(-1.2)
    cx0, cy0, cx1, cy1 = conn["geom"].bounds
    vb = None
    for d in np.arange(2.0, 12.0, 0.25):
        for p in (Point(cx1 + d, (cy0 + cy1) / 2), Point((cx0 + cx1) / 2, cy0 - d),
                  Point(cx1 + d, cy0 - d)):
            if inner.contains(p) and p.distance(conn["geom"]) > 1.0 and \
                    all(p.distance(o) >= VBAT_PAD_CLEAR for o in others):
                vb = p
                break
        if vb:
            break
    pad_pts = [Point(x, y) for _, _, x, y in batt_pads] + ([vb] if vb else [])
    hatch = unary_union([pocket, conn["geom"], leads.buffer(1.0)]
                        + [p.buffer(1.5) for p in pad_pts]).convex_hull.buffer(B.HATCH_MARGIN)
    min_pad_gap = min(a.distance(b) for i, a in enumerate(pad_pts) for b in pad_pts[i + 1:])

    region = unary_union([hatch, res["board_v3"], res["tab_coil"].buffer(13)]).buffer(10)
    ob = res["orth"].bounds
    region = (min(region.bounds[0], ob[0] - 3), region.bounds[1],
              max(region.bounds[2], ob[2] + 3), region.bounds[3])
    mirror = L.DORSAL_MIRROR[side]
    cv = B.Canvas(region, 9, mirror, legend_w=470,
                  title=f"v3 concept - size {size} ({code}), {side}: battery in FRONT of "
                        f"the board - viewed from above, toe up")
    clip = box(*region)
    C = B.COL
    cv.poly(res["memb"].intersection(clip), fill=C["memb"], outline=C["membe"])
    for p in geo["pads"]:
        if p.intersects(clip):
            cv.poly(p.intersection(clip), outline=(70, 110, 200), width=2)
    cv.poly(res["tails"].intersection(clip), fill=C["tail"], outline=C["taile"])
    cv.poly(res["orth"].intersection(clip), outline=C["orth"], width=3)
    cv.dashed(hatch, C["hatch"], width=2)
    cv.poly(res["board_v3"], fill=C["board"])
    cv.dashed(res["r4"], C["comp"], width=1, dash=0.8)
    cc = res["tab_coil"].centroid.coords[0]
    mag = magnet_arc(cc, mag_ang)
    mag_bad = round(mag.intersection(unary_union([res["r4"], res["memb"], res["tails"]])).area
                    + mag.difference(res["orth"]).area, 1)
    cv.poly(mag, fill=C["magnet"], outline=(120, 120, 120))
    cv.line(B.coil_spiral(cc), C["coil"], width=2)
    cv.dashed(res["tab_coil"].buffer(B.COIL_KEEPOUT), C["coil"], width=1, dash=0.8)
    cv.dashed(pocket, C["pocket"], width=1, dash=0.6)
    cv.poly(batt["geom"], fill=(245, 205, 205), outline=C["batt"], width=3)
    cv.poly(batt["pcm"], fill=C["pcm"])
    if not fits:
        cv.poly(batt["violation"], fill=(255, 120, 0))
    cv.poly(conn["geom"], fill=C["conn"])
    B.draw_leads(cv, leads)
    for num, name, x, y in batt_pads:
        px, py = cv.P(x, y)
        cv.dr.ellipse([px - 6, py - 6, px + 6, py + 6], fill=C["tp"], outline=C["text"])
        cv.dr.rectangle([px - 22, py - 9, px - 10, py + 9], fill=(255, 255, 255), outline=C["text"])
        cv.dr.text((px - 20, py - 9), num, fill=C["text"], font=cv.f)
    if vb:
        px, py = cv.P(vb.x, vb.y)
        cv.dr.ellipse([px - 6, py - 6, px + 6, py + 6], fill=C["tpn"], outline=C["text"])
    for name, x, y in swd:
        px, py = cv.P(x, y)
        cv.dr.ellipse([px - 4, py - 4, px + 4, py + 4], outline=C["tp"], width=2)

    callx = cv.P(region[2] if not mirror else region[0], 0)[0] - 10
    calls = [(cc, "coil Ø15 on tab + C magnet"),
             (batt["pcm"].centroid.coords[0], "PCM end (leads exit)"),
             (batt["geom"].centroid.coords[0], "pack 31 x 10.2 x 3.2"),
             (conn["geom"].centroid.coords[0], "JST ACH header")]
    if vb:
        calls.append(((vb.x, vb.y), "new VBAT pad"))
    calls.append(((batt_pads[0][2], batt_pads[0][3]), "test pads 1-4 (R4 positions)"))
    for (x, y), text in calls:
        px, py = cv.P(x, y)
        tw = cv.dr.textlength(text, font=cv.fs)
        tx = callx - tw
        cv.dr.line([px, py, tx - 4, py], fill=C["dim"], width=1)
        cv.dr.text((tx, py - 8), text, fill=C["text"], font=cv.fs)

    edge = res["orth"].exterior.distance(batt["geom"])
    memb_gap = batt["geom"].distance(unary_union([res["memb"], res["tails"]]))
    cv.legend([
        (C["orth"], f"orthotic outline ({code})", "box"),
        (C["memb"], f"sensor layer ({code} outline)", "fill"),
        ((70, 110, 200), "FSR pads", "box"),
        (C["tail"], "sensor tails", "fill"),
        (C["board"], "v3 board with coil tab", "fill"),
        (C["comp"], "R4 component area", "dash"),
        (C["coil"], f"coil; dashed = {B.COIL_KEEPOUT:.0f} mm battery keep-out", "box"),
        (C["magnet"], "C magnet 180 deg, ID 17.2 / OD 25.2", "fill"),
        (C["batt"], "battery pack (FLPB301031-HPMW30-30)", "box"),
        (C["pcm"], "PCM end of pack", "fill"),
        (C["pocket"], "die-cut pocket, +0.5 mm", "dash"),
        (C["conn"], "JST ACH 2-pin header, 1.4 mm high", "fill"),
        (C["lead"], "AWG30 lead, + (red) / - (black), service loop", "box"),
        (C["tp"], "battery test pads 1-4 (as R4, pins.png)", "fill"),
        (C["tpn"], "new VBAT pad (pack side of F1)", "fill"),
        (C["tp"], "SWD pads (as R4)", "box"),
        (C["hatch"], "service hatch in bottom layer", "dash"),
    ], [
        f"Battery: {batt['angle']} deg, {batt['gap']:.1f} mm from board - same pose,",
        "  connector and leads at every size (set by size S).",
        f"  {edge:.1f} mm inside orthotic edge (min 3),",
        f"  {memb_gap:.1f} mm clear of sensor layer/tails (min 1),",
        f"  >= {B.COIL_KEEPOUT:.0f} mm from coil.",
        f"Leads: {leads.length:.0f} mm incl. service loop (pack 30 +/- 2).",
        "Pads 1 = 0V, 2 = VBAT_F, 3 = VSYS, 4 = 3V3 (copper",
        "  mapping; confirm 2/3 with an off-puck reading).",
        f"Pads stay hand-probeable: closest pair {min_pad_gap:.1f} mm.",
        "Service face (pads, header) faces the bottom hatch.",
        "",
        "Board, coil tab and tails: size-S model (R3), at this",
        "  size's drawn PCB position toe-heel; across the foot",
        "  the tails meet the sensor layer as at size S.",
        "Outlines/pads traced from the Reid drawing, +/-0.5 mm.",
    ])
    if not fits:
        marginal = batt["violation_mm2"] < 2.0
        bx, by = cv.legend_x, cv.H - 190
        cv.dr.rectangle([bx - 6, by, cv.W - 12, by + 120], fill=(255, 235, 220),
                        outline=(200, 40, 40), width=3)
        lines = [("MARGINAL: today's pack only fits by relaxing" if marginal
                  else "TODAY'S PACK DOES NOT FIT IN FRONT"),
                 ("  the clearance rules" if marginal else "  OF THE BOARD AT THIS SIZE"),
                 "Shared pose shown; orange =",
                 f"  {batt['violation_mm2']:.1f} mm2 beyond the 3 mm edge / 1 mm",
                 "  sensor-layer clearances. No angle fits here; longest",
                 f"  10.2 mm-wide pack that fits at {BATT_ANGLE} deg: {batt['max_len_fits']:.0f} mm."]
        for i, t in enumerate(lines):
            cv.dr.text((bx + 4, by + 6 + i * 18), t, fill=(180, 20, 20),
                       font=cv.f if i < 2 else cv.fs)
    if board_out > 0.05:
        bx, by = cv.legend_x, cv.H - 330
        cv.dr.rectangle([bx - 6, by, cv.W - 12, by + 66], fill=(255, 235, 220),
                        outline=(200, 40, 40), width=3)
        for i, t in enumerate(["BOARD CROSSES THE ORTHOTIC EDGE",
                               f"  {board_out:.1f} mm2 outside with the tails at full",
                               "  length; this size needs shorter tails."]):
            cv.dr.text((bx + 4, by + 6 + i * 18), t, fill=(180, 20, 20),
                       font=cv.f if i == 0 else cv.fs)
    cv.scalebar()
    info = {"fits": fits, "battery_angle": batt["angle"],
            "battery_gap_to_board_mm": round(batt["gap"], 1),
            "battery_to_orthotic_edge_mm": round(edge, 1),
            "battery_to_sensor_layer_mm": round(memb_gap, 1),
            "lead_length_mm": round(leads.length, 1),
            "leads_fit_30mm_pack": bool(leads.length <= 28.0),
            "min_test_pad_gap_mm": round(min_pad_gap, 1),
            "new_vbat_pad_placed": vb is not None,
            "c_magnet_overlap_mm2": mag_bad,
            "hatch_mm": [round(hatch.bounds[2] - hatch.bounds[0], 1),
                         round(hatch.bounds[3] - hatch.bounds[1], 1)],
            **({} if fits else {"violation_mm2": batt["violation_mm2"],
                                 "longest_pack_that_fits_mm": batt["max_len_fits"]}),
            "tail_tip_to_sensor_layer_mm": round(tail_gap(res["memb"], res["tails"]), 2),
            "board_outside_orthotic_mm2": round(board_out, 1),
            "board_to_orthotic_edge_mm": round(res["orth"].exterior.distance(res["board_v3"]), 2)}
    return cv.img, info


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    L.study_cache = {}
    sres = {}
    for side in ("LHS", "RHS"):
        sres[side] = L.study(side)
        L.study_cache[side] = sres[side]
    geo = {s[1]: extract(*s) for s in SIZES}
    fits = {side: fit_size_s(sres[side], geo["S"]) for side in sres}
    for v in fits.values():
        v["pcb_offset"] = [float(x) for x in v["pcb_offset"]]
    placed = {side: {size: place(geo[size][fits[side]["foot"]], fits[side], sres[side])
                     for _, size, *_ in SIZES} for side in sres}
    pose = {side: common_pose(sres[side], [placed[side][s] for s in COMMON_SIZES])
            for side in sres}
    mag_ang = {side: magnet_c(sres[side], [placed[side][s] for s in COMMON_SIZES])
               for side in sres}
    summary = {"fit_size_S": fits, "magnet_angle": mag_ang, "battery_pose": {
        side: {"angle": BATT_ANGLE, "centre_model_mm": [round(v, 2) for v in pose[side]],
               "fits_sizes": list(COMMON_SIZES)} for side in sres}, "sizes": {}}
    for code, size, *_ in SIZES:
        for side in ("LHS", "RHS"):
            f = geo[size][fits[side]["foot"]]
            img, info = plan_size(side, size, code, sres[side], placed[side][size], pose[side],
                                   mag_ang[side])
            info.update({"drawing_px_per_mm": [round(f["sx"], 3), round(f["sy"], 3)],
                         "sensor_layer_traced_mm": f["check_memb_mm"],
                         "fsr_pads_found": f["n_pads"]})
            if img:
                img.save(OUT / f"plan_front_{size}_{side}.png")
            summary["sizes"][f"{size}_{side}"] = info
            print(size, side, info)
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1))
    print(fits)


if __name__ == "__main__":
    main()
