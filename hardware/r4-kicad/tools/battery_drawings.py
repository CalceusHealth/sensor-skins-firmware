#!/usr/bin/env python3
"""v3 concept drawings: etched coil tab + battery in front of / behind the
board, battery connection and service access for the flat sandwich
(bottom layer / electronics layer / top cover).

Geometry comes from layout_study.study() (R3 assembly model frame, mm):
board outline, coil tab, sensor layer and tails, orthotic outline. Test-point
positions come from the reconstructed R4 .kicad_pcb, mapped through the same
board registration. Plans are drawn viewed from above (toe up); service
access is from below.

Outputs: hardware/r4-kicad/v3_concept/{plan_front,plan_behind}_<side>.png,
section.png, battery_connection.png
"""
import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from shapely import affinity
from shapely.geometry import Point, LineString, box, Polygon
from shapely.ops import unary_union, nearest_points

sys.path.insert(0, str(Path(__file__).resolve().parent))
import layout_study as L  # noqa: E402

OUT = L.ROOT / "hardware" / "r4-kicad" / "v3_concept"

POUCH = (31.0, 10.2)          # FLPB301031-HPMW30-30 pack, PCM at one end
PCM_LEN = 3.0                 # PCM folded over the cell top (pack drawing)
COIL_KEEPOUT = 3.0            # battery edge >= 3 mm from coil OD (tuning)
POCKET_MARGIN = 0.5           # die-cut pocket clearance around the pack
ACH = (4.3, 3.0)              # JST ACH 2-pin header body (w along edge, d), 1.4 mm high
HATCH_MARGIN = 2.5

COL = {
    "bg": (250, 250, 248), "orth": (215, 40, 60), "memb": (226, 238, 246),
    "membe": (150, 175, 195), "tail": (236, 215, 150), "taile": (190, 160, 80),
    "board": (55, 55, 60), "comp": (95, 95, 102), "coil": (240, 160, 70),
    "magnet": (200, 200, 200), "batt": (205, 60, 60), "pcm": (120, 30, 30),
    "pocket": (205, 60, 60), "lead": (200, 40, 40), "lead2": (30, 30, 30),
    "conn": (60, 150, 210), "tp": (230, 190, 40), "tpn": (40, 200, 120),
    "hatch": (60, 150, 210), "text": (25, 25, 25), "dim": (90, 90, 90),
}


def font(sz):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if Path(f).exists():
            return ImageFont.truetype(f, sz)
    return ImageFont.load_default()


# ------------------------------------------------------------ geometry
def kicad_points_to_model(side, pts):
    """Map KiCad board coords into the model frame with the exact operations
    layout_study.register() applied to the board outline."""
    r4 = L.r4_outline(side)
    res = L.study_cache[side] if hasattr(L, "study_cache") else None
    pcb_r3 = res["pcb_r3"]
    iou, mir, rot, dx, dy, _ = L.register(r4, pcb_r3)
    c = r4.centroid
    out = []
    for x, y in pts:
        p = Point(x, y)
        p = affinity.scale(p, -1 if mir else 1, 1, origin=c)
        p = affinity.rotate(p, rot, origin=c)
        p = affinity.translate(p, dx, dy)
        out.append((p.x, p.y))
    return out


def test_points(side):
    t = (L.ROOT / "hardware" / "r4-kicad" / "pcb" /
         f"Reid_Orthotic_v2_{side}.kicad_pcb").read_text()
    pts = []
    for m in re.finditer(r'\(footprint "R4:PAD_([^"]+)_\d+" \(layer "[^"]+"\)'
                         r'[^\n]*\n\s*\(at ([-\d.]+) ([-\d.]+)', t):
        name, x, y = m.group(1), float(m.group(2)), float(m.group(3))
        if name.startswith("NET"):
            name = "0V"
        pts.append((name, x, y))
    mapped = kicad_points_to_model(side, [(x, y) for _, x, y in pts])
    return [(n, mx, my) for (n, _, _), (mx, my) in zip(pts, mapped)]


def outer_edge(board_r4):
    """Free board edge for the connector: the long straight edge facing the
    orthotic edge (model min-x side); the castellated edges face the sensor
    layer and carry the tails."""
    x0, y0, x1, y1 = board_r4.bounds
    return LineString([(x0, y0 + 3), (x0, y1 - 3)])


def place_battery(res, zone):
    """Nearest legal pouch position in `zone`, with the coil keep-out."""
    keep = unary_union([res["memb"], res["tails"], res["board_v3"],
                        res["tab_coil"].buffer(COIL_KEEPOUT)]).buffer(L.BATT_GAP)
    allowed = res["orth"].buffer(-L.EDGE_MARGIN)
    bx0, by0, bx1, by1 = res["r4"].bounds
    from shapely.prepared import prep
    A, K = prep(allowed), prep(keep)
    best = None
    ys = (np.arange(by1, by1 + 40, 0.5) if zone == "front"
          else np.arange(by0 - 60, by0, 0.5))
    x0, _, x1, _ = allowed.bounds
    for ang in range(0, 180, 15):
        for cy in ys:
            for cx in np.arange(x0, x1, 0.5):
                g = L.cell_geom("rect", POUCH, cx, cy, ang)
                if not A.contains(g) or K.intersects(g):
                    continue
                d = g.distance(res["board_v3"])
                if best is None or d < best[0]:
                    best = (d, g, cx, cy, ang)
    d, g, cx, cy, ang = best
    # PCM end = the short end nearest the board (shortest leads, and the
    # rigid PCM sits at the least-flexed end of the pack)
    a = math.radians(ang)
    ux, uy = math.cos(a), math.sin(a)
    e1 = Point(cx + ux * POUCH[0] / 2, cy + uy * POUCH[0] / 2)
    e2 = Point(cx - ux * POUCH[0] / 2, cy - uy * POUCH[0] / 2)
    near = e1 if e1.distance(res["board_v3"]) < e2.distance(res["board_v3"]) else e2
    sgn = 1 if near is e1 else -1
    pcm_c = (cx + sgn * ux * (POUCH[0] / 2 - PCM_LEN / 2),
             cy + sgn * uy * (POUCH[0] / 2 - PCM_LEN / 2))
    pcm = affinity.rotate(box(pcm_c[0] - PCM_LEN / 2, pcm_c[1] - POUCH[1] / 2,
                              pcm_c[0] + PCM_LEN / 2, pcm_c[1] + POUCH[1] / 2),
                          ang, origin=pcm_c)
    return {"geom": g, "gap": d, "angle": ang, "centre": (cx, cy),
            "pcm": pcm, "lead_exit": (near.x, near.y)}


def place_connector(res, batt):
    """JST ACH header just inside the free outer edge, at the legal position
    (fully on the R4 component area) nearest the battery's lead exit."""
    r4 = res["r4"]
    x0, y0, x1, y1 = r4.bounds
    hw, d = ACH[0] / 2, ACH[1]
    best = None
    for cy in np.arange(y0 + hw + 0.5, y1 - hw - 0.5, 0.25):
        for inset in np.arange(0.3, 4.0, 0.1):
            body = box(x0 + inset, cy - hw, x0 + inset + d, cy + hw)
            if r4.buffer(-0.25).contains(body):
                break
        else:
            continue
        dist = Point(batt["lead_exit"]).distance(Point(x0, cy))
        if best is None or dist < best[0]:
            best = (dist, body, (body.bounds[0], cy))
    return {"geom": best[1], "mouth": best[2]}


SERVICE_TP = ["VBAT", "VBAT_F", "VSYS", "3V3", "0V", "SWDIO", "SWDCLK", "RST"]


def place_strip(res, conn, pitch=1.7):
    """Proposed v3 service strip: all test points in a 2 x 4 grid right
    beside the connector, so one small hatch covers both."""
    cx = conn["geom"].bounds[2] + 1.6
    inner = res["r4"].buffer(-0.9)
    base_y = conn["mouth"][1]
    # slide along the edge (toward the board centre) until every pad sits on
    # the board, clear of the castellated edges
    mid = (res["r4"].bounds[1] + res["r4"].bounds[3]) / 2
    step = -0.25 if base_y > mid else 0.25
    for k_ in range(200):
        cy = base_y + k_ * step
        pts = [(name, cx + (k % 2) * pitch, cy + (k // 2 - 1.5) * pitch)
               for k, name in enumerate(SERVICE_TP)]
        if all(inner.contains(Point(x, y)) for _, x, y in pts):
            return pts
    return pts


def lead_path(res, batt, conn):
    """AWG30 pair from the PCM end to the connector, routed outside the
    board's outer edge and outside the coil keep-out, with a meander
    service loop before the connector."""
    sx, sy = batt["lead_exit"]
    mx, my = conn["mouth"]
    obst = unary_union([res["board_v3"], res["tab_coil"].buffer(COIL_KEEPOUT)])
    lane_x = obst.bounds[0] - 1.2           # run just outside board + coil keep-out
    up = 1 if my > sy else -1
    loop_y = my - up * 4.0
    pts = [(sx, sy), (lane_x, sy + up * 1.5)]
    # meander (service loop) on the lane
    for k in range(3):
        pts.append((lane_x - 1.4, loop_y - up * (2.4 - k * 0.8) + up * 0.4))
        pts.append((lane_x, loop_y - up * (2.0 - k * 0.8) + up * 0.4))
    pts += [(lane_x, my), (mx - 0.3, my)]
    return LineString(pts)


# ------------------------------------------------------------ drawing
class Canvas:
    def __init__(self, region, ppmm, mirror, legend_w=420, title=""):
        self.x0, self.y0, self.x1, self.y1 = region
        self.ppmm, self.mirror, self.pad = ppmm, mirror, 30
        self.W = int((self.x1 - self.x0) * ppmm) + 2 * self.pad + legend_w
        self.H = int((self.y1 - self.y0) * ppmm) + 2 * self.pad + 60
        self.img = Image.new("RGB", (self.W, self.H), COL["bg"])
        self.dr = ImageDraw.Draw(self.img)
        self.f, self.fs, self.fb = font(15), font(12), font(20)
        self.legend_x = self.W - legend_w + 10
        self.dr.text((self.pad, 8), title, fill=COL["text"], font=self.fb)

    def P(self, x, y):
        px = (self.x1 - x) if self.mirror else (x - self.x0)
        return (self.pad + px * self.ppmm, self.pad + 30 + (self.y1 - y) * self.ppmm)

    def poly(self, g, fill=None, outline=None, width=1):
        for gg in getattr(g, "geoms", [g]):
            if gg.is_empty or gg.geom_type != "Polygon":
                continue
            self.dr.polygon([self.P(*c) for c in gg.exterior.coords],
                            fill=fill, outline=outline, width=width)
            for h in gg.interiors:
                self.dr.polygon([self.P(*c) for c in h.coords], fill=COL["bg"])

    def line(self, ls, fill, width=2):
        self.dr.line([self.P(*c) for c in ls.coords], fill=fill, width=width)

    def dashed(self, g, fill, width=2, dash=1.2):
        for gg in getattr(g, "geoms", [g]):
            ring = LineString(gg.exterior.coords)
            n = int(ring.length / dash)
            for i in range(0, n, 2):
                a = ring.interpolate(i * dash)
                b = ring.interpolate(min((i + 1) * dash, ring.length))
                self.dr.line([self.P(a.x, a.y), self.P(b.x, b.y)], fill=fill, width=width)

    def label(self, x, y, text, col=None, dx=8, dy=-8, f=None):
        px, py = self.P(x, y)
        self.dr.text((px + dx, py + dy), text, fill=col or COL["text"], font=f or self.fs)

    def legend(self, items, notes, y=50):
        for i, (c, t, kind) in enumerate(items):
            yy = y + i * 24
            if kind == "fill":
                self.dr.rectangle([self.legend_x, yy, self.legend_x + 16, yy + 16], fill=c)
            elif kind == "dash":
                for k in range(0, 16, 6):
                    self.dr.line([self.legend_x + k, yy + 8, self.legend_x + k + 3, yy + 8], fill=c, width=3)
            else:
                self.dr.rectangle([self.legend_x, yy, self.legend_x + 16, yy + 16], outline=c, width=3)
            self.dr.text((self.legend_x + 24, yy), t, fill=COL["text"], font=self.f)
        yy = y + len(items) * 24 + 16
        for n in notes:
            self.dr.text((self.legend_x, yy), n, fill=COL["text"], font=self.fs)
            yy += 18

    def scalebar(self):
        x, y = self.legend_x, self.H - 40
        self.dr.rectangle([x, y, x + 10 * self.ppmm, y + 6], fill=COL["text"])
        self.dr.text((x, y + 10), "10 mm", fill=COL["text"], font=self.fs)


def coil_spiral(c, r_in=2.6, r_out=7.4, turns=12, n=900):
    pts = []
    for i in range(n + 1):
        t = i / n
        r = r_in + (r_out - r_in) * t
        a = 2 * math.pi * turns * t
        pts.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a)))
    return LineString(pts)


def plan(side, res, variant):
    batt = place_battery(res, variant)
    conn = place_connector(res, batt)
    leads = lead_path(res, batt, conn)
    pocket = batt["geom"].buffer(POCKET_MARGIN, join_style=2)
    strip = place_strip(res, conn)
    strip_g = unary_union([Point(x, y).buffer(0.9) for _, x, y in strip])
    hatch = unary_union([pocket, conn["geom"], strip_g, leads.buffer(1.0)]
                        ).convex_hull.buffer(HATCH_MARGIN)
    tps = test_points(side)

    region = unary_union([hatch, res["board_v3"], res["tab_coil"]]).buffer(14).bounds
    mirror = L.DORSAL_MIRROR[side]
    cv = Canvas(region, 11, mirror,
                title=f"v3 concept ({side}, size S): etched coil tab + battery "
                      f"{'in FRONT of' if variant == 'front' else 'BEHIND'} the board"
                      f" - viewed from above, toe up")
    clip = box(*region)
    cv.poly(res["orth"].intersection(clip), outline=COL["orth"], width=2)
    cv.poly(res["memb"].intersection(clip), fill=COL["memb"], outline=COL["membe"])
    for k in ("FSR", "CAP"):
        for s_ in res["sensors"][k]:
            pg = Polygon(s_["poly"])
            if pg.intersects(clip):
                cv.poly(pg, outline=(70, 110, 200) if k == "FSR" else (60, 160, 120), width=2)
    cv.poly(res["tails"].intersection(clip), fill=COL["tail"], outline=COL["taile"])
    cv.dashed(hatch, COL["hatch"], width=2)
    cv.poly(res["board_v3"], fill=COL["board"])
    cv.dashed(res["r4"], COL["comp"], width=1, dash=0.8)
    cv.line(coil_spiral(res["tab_coil"].centroid.coords[0]), COL["coil"], width=2)
    cv.poly(res["magnet"], fill=COL["magnet"])
    cv.dashed(res["tab_coil"].buffer(COIL_KEEPOUT), COL["coil"], width=1, dash=0.8)
    cv.dashed(pocket, COL["pocket"], width=1, dash=0.6)
    cv.poly(batt["geom"], fill=(245, 205, 205), outline=COL["batt"], width=3)
    cv.poly(batt["pcm"], fill=COL["pcm"])
    cv.poly(conn["geom"], fill=COL["conn"])
    off = leads.parallel_offset(0.35, "left")
    cv.line(leads, COL["lead"], width=3)
    cv.line(off if off.geom_type == "LineString" else leads, COL["lead2"], width=3)
    for name, x, y in tps:
        px, py = cv.P(x, y)
        cv.dr.ellipse([px - 5, py - 5, px + 5, py + 5], outline=COL["tp"], width=2)
    for name, x, y in strip:
        px, py = cv.P(x, y)
        cv.dr.ellipse([px - 7, py - 7, px + 7, py + 7],
                      fill=COL["tpn"] if name == "VBAT" else COL["tp"], outline=COL["text"])
    # callouts in the right-hand margin of the drawing area, with leaders
    callx = cv.P(region[2] if not mirror else region[0], 0)[0] - 150
    calls = [
        (res["tab_coil"].centroid.coords[0], "etched coil Ø15 + magnet"),
        (batt["pcm"].centroid.coords[0], "PCM end (leads exit here)"),
        (batt["geom"].centroid.coords[0], "pack 31 x 10.2 x 3.2"),
        (conn["geom"].centroid.coords[0], "JST ACH header"),
        ((strip[0][1], strip[0][2]), "service strip (8 test points)"),
    ]
    for (x, y), text in calls:
        px, py = cv.P(x, y)
        tx = min(max(px + 70, cv.pad), callx)
        cv.dr.line([px, py, tx - 4, py], fill=COL["dim"], width=1)
        cv.dr.text((tx, py - 8), text, fill=COL["text"], font=cv.fs)

    lx = cv.legend_x
    cv.legend([
        (COL["orth"], "orthotic outline", "box"),
        (COL["memb"], "sensor layer", "fill"),
        (COL["tail"], "sensor tails", "fill"),
        (COL["board"], "v3 board with coil tab", "fill"),
        (COL["comp"], "R4 component area", "dash"),
        (COL["coil"], f"coil; dashed = {COIL_KEEPOUT:.0f} mm battery keep-out", "box"),
        (COL["batt"], "battery pack (FLPB301031-HPMW30-30)", "box"),
        (COL["pcm"], "PCM end of pack", "fill"),
        (COL["pocket"], "die-cut pocket, +0.5 mm", "dash"),
        (COL["conn"], "JST ACH 2-pin header, 1.4 mm high", "fill"),
        (COL["lead"], "AWG30 leads + service loop", "box"),
        (COL["tp"], "service strip test points (proposed)", "fill"),
        (COL["tpn"], "new VBAT point (pack side of F1)", "fill"),
        (COL["tp"], "R4 test point positions today", "box"),
        (COL["hatch"], "service hatch in bottom layer", "dash"),
    ], [
        f"Battery: {batt['angle']} deg, {batt['gap']:.1f} mm from board,",
        f"  >= {COIL_KEEPOUT:.0f} mm from coil, >= 3 mm inside orthotic,",
        "  >= 1 mm clear of sensor layer and tails.",
        "Connector on the free outer edge (castellated",
        "  edges carry the sensor tails).",
        "Service face (connector, test points) faces the",
        "  bottom layer; foot-side cover stays continuous.",
        f"Leads: {leads.length:.0f} mm routed outside board and coil",
        f"  keep-out, incl. service loop (pack leads 30 +/- 2 mm).",
        "Test points regrouped beside the connector",
        "  (today they are spread over the board), so one",
        "  hatch covers battery, connector and test points.",
    ])
    cv.scalebar()
    return cv.img, {"variant": variant, "battery_angle": batt["angle"],
                    "battery_gap_mm": round(batt["gap"], 1),
                    "battery_centre": [round(v, 1) for v in batt["centre"]],
                    "lead_length_mm": round(leads.length, 1),
                    "leads_fit_30mm_pack": bool(leads.length <= 28.0),
                    "hatch_mm": [round(hatch.bounds[2] - hatch.bounds[0], 1),
                                 round(hatch.bounds[3] - hatch.bounds[1], 1)]}


def section():
    """Schematic cross-section through board + battery (vertical x4)."""
    sx, sz = 9, 36   # px per mm horizontally / vertically
    W, H = 1500, 860
    img = Image.new("RGB", (W, H), COL["bg"])
    dr = ImageDraw.Draw(img)
    f, fs, fb = font(15), font(12), font(20)
    dr.text((20, 10), "Section through board and battery - flat sandwich "
            "(vertical scale x4, foot on top)", fill=COL["text"], font=fb)
    X0, base = 80, 560

    def rect(x, z, w, t, fill, outline=None):
        dr.rectangle([X0 + x * sx, base - (z + t) * sz, X0 + (x + w) * sx, base - z * sz],
                     fill=fill, outline=outline or COL["text"])

    t_bot, t_e, t_top = 2.0, 3.4, 1.5
    span = 120
    # bottom layer with hatch
    rect(0, 0, 30, t_bot, (200, 200, 195))
    rect(30.5, 0, 71, t_bot, (170, 200, 230))
    rect(102, 0, span - 102, t_bot, (200, 200, 195))
    # electronics layer
    rect(0, t_bot, span, t_e, (232, 232, 225))
    # board (service face down) + components
    zb = t_bot + t_e - 0.4                                   # board flush under the top cover
    rect(34, zb, 30, 0.4, COL["board"])
    for x in (36, 41, 47, 52, 58):
        rect(x, zb - 1.35, 3, 1.35, COL["comp"])
    rect(60, zb - 1.4, 4.3, 1.4, COL["conn"])
    # pocket + pack
    rect(68, t_bot + 0.1, 32, 3.3, COL["bg"], outline=COL["pocket"])
    rect(68.5, t_bot + 0.1, 31, 3.2, (245, 205, 205), outline=COL["batt"])
    rect(96.5, t_bot + 0.1, 3, 3.2, COL["pcm"])
    # coil tab region (left of board)
    rect(14, zb, 18, 0.4, COL["board"])                    # coil tab (same board), coil on its top copper
    rect(14, zb - 0.3, 18, 0.3, (120, 120, 120))           # ferrite UNDER the coil
    rect(8.0, t_bot + t_e - 1.0, 5, 1.0, COL["magnet"])     # magnet discs outside the coil,

    # top cover
    rect(0, t_bot + t_e, span, t_top, (210, 225, 210))
    # sensor layer
    rect(0, t_bot + t_e - 0.6, 7.4, 0.6, COL["memb"])
    rect(104, t_bot + t_e - 0.6, span - 104, 0.6, COL["memb"])

    def note(x, z, text, tx, tz):
        px, pz = X0 + x * sx, base - z * sz
        qx, qz = X0 + tx * sx, base - tz * sz
        dr.line([px, pz, qx, qz], fill=COL["dim"], width=1)
        dr.text((qx + 4, qz - 8), text, fill=COL["text"], font=fs)

    note(60, zb - 0.7, "JST ACH header, 1.4 mm", 58, -1.2)
    note(48, zb - 0.7, "components + test points face DOWN (service face)", 32, -2.0)
    note(56, zb + 0.2, "board 0.4 mm at the TOP of the bay: coil as close to the puck as possible", 44, t_bot + t_e + t_top + 1.6)
    note(84, t_bot + 1.6, "pack 3.2 mm in die-cut pocket, low-tack tape", 86, t_bot + t_e + t_top + 0.8)
    note(98, t_bot + 1.6, "PCM end", 104, -1.2)
    note(10, t_bot + t_e - 0.5, "3 x Ø5 x 1 magnets outside the coil, just under the top cover", 2, -1.2)
    note(22, zb - 0.15, "etched coil on the tab, ferrite 0.2-0.3 UNDER it", 2, t_bot + t_e + t_top + 1.6)
    note(66, 0.5, "service hatch spans board + pack: lid on re-closable adhesive", 62, -2.8)
    note(110, t_bot + t_e + t_top / 2, "top cover 1-2 mm, continuous; the charging puck sits on top", 88, t_bot + t_e + t_top + 2.4)
    note(4, t_bot + t_e - 0.3, "sensor layer", 0, t_bot + t_e + t_top + 0.8)

    # thickness dims
    for z0, z1, txt in ((0, t_bot, "bottom (TBD)"), (t_bot, t_bot + t_e, "electronics bay = cell + ~0.2"),
                        (t_bot + t_e, t_bot + t_e + t_top, "top cover 1-2 mm")):
        x = X0 + span * sx + 30
        dr.line([x, base - z0 * sz, x, base - z1 * sz], fill=COL["dim"], width=2)
        dr.line([x - 6, base - z0 * sz, x + 6, base - z0 * sz], fill=COL["dim"])
        dr.line([x - 6, base - z1 * sz, x + 6, base - z1 * sz], fill=COL["dim"])
        dr.text((x + 10, base - (z0 + z1) / 2 * sz - 8), txt, fill=COL["text"], font=fs)
    yy = 720
    for t in ["Bay thickness is set by the cell alone: 3.4 mm with today's pack, ~2.2 mm with a 2 mm cell.",
              "Tallest board items (100 uF 0805, SOT-23, ACH header) stay under ~1.5 mm, below the cell.",
              "Nothing is stacked on the cell, so components can no longer press into it.",
              "Bottom layer thickness is a placeholder until the flat-insole build-up is known; magnets ~0.6 mm clear of the sensor layer in plan."]:
        dr.text((20, yy), t, fill=COL["text"], font=f)
        yy += 24
    return img


def connection_detail():
    W, H = 1500, 820
    img = Image.new("RGB", (W, H), COL["bg"])
    dr = ImageDraw.Draw(img)
    f, fs, fb = font(15), font(13), font(20)
    dr.text((20, 10), "Battery connection - robust in use, replaceable without soldering",
            fill=COL["text"], font=fb)

    s = 16  # px per mm
    # pack
    ox, oy = 60, 120
    dr.rectangle([ox, oy, ox + 31 * s - 3 * s, oy + 10.2 * s], fill=(245, 205, 205), outline=COL["batt"], width=3)
    dr.rectangle([ox + 28 * s, oy, ox + 31 * s, oy + 10.2 * s], fill=COL["pcm"])
    dr.text((ox + 6, oy + 4.5 * s), "FLPB301031 cell", fill=COL["text"], font=f)
    dr.text((ox + 27 * s, oy + 10.4 * s + 4), "PCM RJD404HP", fill=COL["text"], font=fs)
    # leads
    lx0 = ox + 31 * s
    for k, col in ((3.5, COL["lead"]), (6.5, COL["lead2"])):
        y = oy + k * s
        dr.line([lx0, y, lx0 + 9 * s, y, lx0 + 11 * s, y + 3 * s, lx0 + 13 * s, y,
                 lx0 + 21 * s, y], fill=col, width=4)
    dr.text((lx0 + 2, oy - 26), "AWG30 leads, ~30 mm, service loop", fill=COL["text"], font=fs)
    # plug + header + board
    px = lx0 + 21 * s
    dr.rectangle([px, oy + 2.4 * s, px + 3 * s, oy + 7.6 * s], fill=(120, 170, 220), outline=COL["text"])
    dr.rectangle([px + 3 * s, oy + 2 * s, px + 6 * s, oy + 8 * s], fill=COL["conn"], outline=COL["text"])
    dr.rectangle([px + 6 * s, oy - 1 * s, px + 25 * s, oy + 11 * s], fill=COL["board"])
    dr.line([px + 1.5 * s, oy + 7.6 * s, px + 1.5 * s, oy + 12.5 * s], fill=COL["dim"])
    dr.text((px - 20, oy + 12.6 * s), "ACH plug (crimped)", fill=COL["text"], font=fs)
    dr.line([px + 4.5 * s, oy + 8 * s, px + 4.5 * s, oy + 14 * s], fill=COL["dim"])
    dr.text((px + 2 * s, oy + 14.1 * s), "ACH header, 1.4 mm", fill=COL["text"], font=fs)
    # test pads on board
    for i, (n, c) in enumerate((("VBAT", COL["tpn"]), ("VBAT_F", COL["tp"]),
                                ("VSYS", COL["tp"]), ("3V3", COL["tp"]), ("0V", COL["tp"]))):
        cx, cy = px + 8 * s + i * 3.3 * s, oy + 3 * s
        dr.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], fill=c, outline=COL["text"])
        dr.text((cx - 16, cy + 12), n, fill=(240, 240, 240), font=font(11))
    for i, n in enumerate(("SWDIO", "SWDCLK", "RST")):
        cx, cy = px + 9 * s + i * 4.2 * s, oy + 8 * s
        dr.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=COL["tp"], outline=COL["text"])
        dr.text((cx - 18, cy + 11), n, fill=(240, 240, 240), font=font(11))
    dr.text((px + 6 * s + 4, oy - 1 * s - 22), "board service face (faces the hatch)",
            fill=COL["text"], font=fs)
    # staking marks
    for xx in (lx0 + 4 * s, lx0 + 17 * s):
        dr.ellipse([xx - 14, oy + 3 * s - 14, xx + 14, oy + 7 * s + 14], outline=(150, 90, 200), width=3)
    dr.text((lx0 - 8 * s, oy + 12.6 * s), "removable staking to the electronics layer (low-tack tape / soft RTV)",
            fill=(120, 70, 170), font=fs)

    y = 430
    rows = [
        ("Prototype (now)", "Master Instruments crimps a JST ACH plug onto the pack's AWG30 leads "
         "(ACH: 1.2 mm pitch, 1.4 mm high, AWG30-28). Front layout: stock 30 mm leads. "
         "Behind layout: ~45 mm leads (custom length)."),
        ("Production", "Custom pack with a flex tail to a Hirose BM28 board-to-flex connector "
         "(0.6 mm stacked, 5 A power contacts) - the phone-battery approach, no wires."),
        ("Robust in use", "Leads leave the PCM end straight, run in a service loop, and are staked to the "
         "electronics layer (not the cell) so foot load and flex never pull on the PCM or the crimp."),
        ("", "Pack sits flat in a die-cut pocket (+0.5 mm), never on components; the sandwich clamps "
         "the mated connector so it cannot back out."),
        ("Replace", "Open the bottom hatch, lift the staking, unplug, peel the pack off low-tack tape, "
         "fit the new pack. No soldering iron near the cell or PCM."),
        ("Test", "VBAT (new, pack side of F1) beside the header plus VBAT_F / VSYS / 3V3 / 0V and SWD, "
         "all on the service face under the hatch."),
        ("", "Dead unit: VBAT ~0 V with the pack plugged in -> unplug and measure the pack leads; "
         "most PCMs release an over-discharge lockout once a charger is applied (confirm for RJD404HP)."),
        ("Option", "On-board BQ29700 protection: add a test point either side of it (cell-side / protected) "
         "so the hatch alone diagnoses lockout vs damage."),
    ]
    for head, body in rows:
        dr.text((20, y), head, fill=COL["text"], font=font(15))
        # wrap body
        words, line, yy = body.split(), "", y
        for w_ in words:
            if dr.textlength(line + " " + w_, font=f) > W - 260:
                dr.text((200, yy), line, fill=COL["text"], font=f)
                line, yy = w_, yy + 20
            else:
                line = (line + " " + w_).strip()
        dr.text((200, yy), line, fill=COL["text"], font=f)
        y = yy + 30
    return img


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    L.study_cache = {}
    import json
    summary = {}
    for side in sys.argv[1:] or ["LHS", "RHS"]:
        res = L.study(side)
        L.study_cache[side] = res
        for variant in ("front", "behind"):
            img, info = plan(side, res, variant)
            img.save(OUT / f"plan_{variant}_{side}.png")
            summary[f"{side}_{variant}"] = info
            print(side, info)
    section().save(OUT / "section.png")
    connection_detail().save(OUT / "battery_connection.png")
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1))
