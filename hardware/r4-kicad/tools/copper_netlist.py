#!/usr/bin/env python3
"""Copper connectivity engine: derive the pad-level netlist from the R4 gerbers.

Inputs
  netlist/inventory_<side>.json  (from gerber_inventory.py; flashes carry bw/bh
                                  bounding boxes, regions carry draw polarity)
  artefacts/.../Reid Orthotic v2 R4 <SIDE> PnP.csv

Model
  * The gerber copper layers are drawn as: dark pour polygons, then CLEAR void
    polygons (clearances/antipads, thermal reliefs), then tracks/arcs/flashes
    (all dark).  Verified for all 8 copper layers of both sides.  So:
      copper = (pours - voids) U tracks U arcs U flashes
  * Tracks/arcs/flashes connect geometrically (gap <= TOL).  Flashes are
    modelled as axis-aligned rectangles (bw x bh) or discs; lines as capsules;
    arcs as sampled polylines of capsules.
  * Pour copper is rasterised per layer at RASTER mm/px (polarity-aware, in
    draw order) and connected-component labelled; an object joins a pour label
    when any of its sample points (interior + just-outside-perimeter ring, to
    catch thermal spokes) lands on that label.
  * Plated drill holes connect all copper layers at that point: a hole joins
    any object/pour it lands on (disc test, + pour ring samples).  Non-plated
    holes connect nothing.
  * PnP -> gerber transform is solved per side (translation + optional mirror,
    plus footprint-local rotation/flip convention), anchored on U2's exposed
    pad and verified/refined against every component.
  * Pad numbers come from footprint-local models; for the perimeter IC
    packages (QFN48 / DHVQFN16 / WSON-10 / LGA-14) the pin-1 position &
    direction are NOT assumed: all cyclic offsets (CCW and CW) are tested
    against electrical anchors and the unique satisfying numbering is kept.

Run with the scratchpad venv python (stdlib + nothing else required; the
inventory JSONs already contain everything).
"""
import csv
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
NETDIR = HERE.parent / "netlist"
ART = HERE.parents[2] / "artefacts" / "SSII Orthotics Electronics - Design Verification"

TOL = 0.01          # copper touch tolerance, mm
RASTER = 0.02       # pour raster, mm/px
PAD_MATCH = 0.15    # model-pad <-> flash match tolerance, mm
VIA_MAX_DRILL = 0.45  # plated holes up to this dia are vias, bigger are pads

COPPER_LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]

# ---------------------------------------------------------------- footprints
# Local pad models (footprint frame, rotation 0, mm), measured from the LHS
# gerbers.  2/3/5-pad parts get fixed numbering conventions; perimeter ICs get
# a CCW candidate order whose cyclic offset is solved from anchors.
TWO_PAD = {
    "0402_RES_HD": 0.45, "0402_CAP_HD": 0.45, "0402_HD": 0.45,
    "0603_RES_HD": 0.7, "0603_CAP_HD": 0.7, "0603_HD": 0.7, "0603_FUSE": 0.7,
    "0805_CAP": 0.95, "SOD-323": 1.15, "CRYSTAL_NDK_NX1610SA": 0.6,
}
VERT_TWO_PAD = {  # two pads on the Y axis; pad 1 = lower (-Y) at rot 0
    "RFANT3216120A5T": 1.55, "RF_CONN": 1.0, "BAT_CONN": 1.0,
}
FIXED_MODELS = {
    "SOT23-5": [("1", -0.95, -1.4), ("2", 0.0, -1.4), ("3", 0.95, -1.4),
                ("4", 0.95, 1.4), ("5", -0.95, 1.4)],
    "SOT323": [("1", -0.65, -0.9), ("2", 0.65, -0.9), ("3", 0.0, 0.9)],
    # second SOT-323 pad variant used by Q1/Q2 (square 0.7mm pads, origin
    # offset): same 1=bottom-left, 2=bottom-right, 3=top convention assumed
    "SOT323_SQ": [("1", -0.65, -0.575), ("2", 0.65, -0.575), ("3", 0.0, 1.225)],
    "XTAL_2016": [("1", -0.675, -0.5), ("2", 0.675, -0.5),
                  ("3", 0.675, 0.5), ("4", -0.675, 0.5)],
}
FP_VARIANTS = {"SOT323": ["SOT323", "SOT323_SQ"]}
ASYM_FPS = {"SOT23-5", "SOT323"}  # footprints whose pads break 180deg symmetry


def qfn_ring(half, pitch, n_side):
    """CCW perimeter starting at top of left column (IPC pin 1 position)."""
    k = n_side // 2
    offs = [(i - (n_side - 1) / 2) * pitch for i in range(n_side)]
    pads = []
    pads += [(-half, y) for y in sorted(offs, reverse=True)]   # left, top->bot
    pads += [(x, -half) for x in sorted(offs)]                 # bottom, l->r
    pads += [(half, y) for y in sorted(offs)]                  # right, bot->top
    pads += [(x, half) for x in sorted(offs, reverse=True)]    # top, r->l
    return pads


PERIMETER_MODELS = {
    # fp: (ccw pad list starting at conventional pin1, EP local pos or None)
    "NRF52832-QFAA": (qfn_ring(2.9, 0.4, 12), (0.0, 0.0)),
    "74LV4051BQ": ([(-1.75, -0.25),
                    (-1.25, -1.25), (-0.75, -1.25), (-0.25, -1.25),
                    (0.25, -1.25), (0.75, -1.25), (1.25, -1.25),
                    (1.75, -0.25), (1.75, 0.25),
                    (1.25, 1.25), (0.75, 1.25), (0.25, 1.25),
                    (-0.25, 1.25), (-0.75, 1.25), (-1.25, 1.25),
                    (-1.75, 0.25)], (0.0, 0.0)),
    "BQ24210": ([(-1.0, 1.0), (-1.0, 0.5), (-1.0, 0.0), (-1.0, -0.5),
                 (-1.0, -1.0), (1.0, -1.0), (1.0, -0.5), (1.0, 0.0),
                 (1.0, 0.5), (1.0, 1.0)], (0.0, 0.0)),
    "LSM6DSM": ([(-1.15, 0.75), (-1.15, 0.25), (-1.15, -0.25), (-1.15, -0.75),
                 (-0.5, -0.9), (0.0, -0.9), (0.5, -0.9),
                 (1.15, -0.75), (1.15, -0.25), (1.15, 0.25), (1.15, 0.75),
                 (0.5, 0.9), (0.0, 0.9), (-0.5, 0.9)], None),
}

# nRF52832-QFAA QFN48 pin names (Nordic PS v1.x, Table "QFN48 pin assignments")
NRF_PINS = {
    1: "DEC1", 2: "P0.00/XL1", 3: "P0.01/XL2", 4: "P0.02/AIN0",
    5: "P0.03/AIN1", 6: "P0.04/AIN2", 7: "P0.05/AIN3", 8: "P0.06",
    9: "P0.07", 10: "P0.08", 11: "P0.09/NFC1", 12: "P0.10/NFC2", 13: "VDD",
    14: "P0.11", 15: "P0.12", 16: "P0.13", 17: "P0.14", 18: "P0.15",
    19: "P0.16", 20: "P0.17", 21: "P0.18", 22: "P0.19", 23: "P0.20",
    24: "P0.21/nRESET", 25: "SWDCLK", 26: "SWDIO", 27: "P0.22", 28: "P0.23",
    29: "P0.24", 30: "ANT", 31: "VSS", 32: "DEC2", 33: "DEC3", 34: "XC1",
    35: "XC2", 36: "VDD", 37: "P0.25", 38: "P0.26", 39: "P0.27",
    40: "P0.28/AIN4", 41: "P0.29/AIN5", 42: "P0.30/AIN6", 43: "P0.31/AIN7",
    44: "NC", 45: "VSS", 46: "DEC4", 47: "DCC", 48: "VDD",
}
PORT_TO_PKG = {}
for _pin, _nm in NRF_PINS.items():
    if _nm.startswith("P0."):
        PORT_TO_PKG[int(_nm[3:5])] = _pin
PORT_TO_PKG[21] = 24  # P0.21/nRESET

# firmware/ble_app_firmware_v2_R7/gpio.h ground truth (P0.xx per function)
GPIO = {
    "LHS": {
        "MUX_ON": 31, "FSR_CH0": 4, "FSR_CH1": 3, "FSR_CH2": 2,
        "FSR_S0": 9, "FSR_S1": 8, "FSR_S2": 7,
        "CAP_CH0": 30, "CAP_CH1": 28, "CAP_S0": 13, "CAP_S1": 14, "CAP_S2": 15,
        "TEMP_S0": 18, "TEMP_S1": 19, "TEMP_S2": 17, "TEMP_S3": 16,
        "TEMP_S4": 20, "TEMP_COM": 29, "VBAT": 5, "VBAT_ON": 6,
        "BQ_PG": 10, "BQ_CHG": 12, "SDA": 26, "SCL": 27,
        "IS_LHS": 22, "RESET": 21,
    },
    "RHS": {
        "MUX_ON": 31, "FSR_CH0": 2, "FSR_CH1": 3, "FSR_CH2": 4,
        "FSR_S0": 27, "FSR_S1": 26, "FSR_S2": 25,
        "CAP_CH0": 30, "CAP_CH1": 28, "CAP_S0": 15, "CAP_S1": 14, "CAP_S2": 13,
        "TEMP_S0": 17, "TEMP_S1": 16, "TEMP_S2": 18, "TEMP_S3": 20,
        "TEMP_S4": 19, "TEMP_COM": 29, "VBAT": 5, "VBAT_ON": 6,
        "BQ_PG": 10, "BQ_CHG": 11, "SDA": 8, "SCL": 9,
        "IS_LHS": 22, "RESET": 21,
    },
}

# 74LV4051BQ (DHVQFN16) terminal functions, NXP datasheet
MUX_FN = {1: "Y4", 2: "Y6", 3: "Z", 4: "Y7", 5: "Y5", 6: "nE", 7: "VEE",
          8: "GND", 9: "S2", 10: "S1", 11: "S0", 12: "Y3", 13: "Y0",
          14: "Y1", 15: "Y2", 16: "VCC"}
# BQ24210 (DQC/WSON-10) pin functions, TI SLUSA76B
BQ_FN = {1: "VBUS", 2: "ISET", 3: "VSS", 4: "VTSB", 5: "TS", 6: "PG",
         7: "EN", 8: "CHG", 9: "VDPM", 10: "BAT"}
# LSM6DSM (LGA-14) pin functions, ST datasheet
LSM_FN = {1: "SDO/SA0", 2: "SDX", 3: "SCX", 4: "INT1", 5: "VDDIO", 6: "GND",
          7: "GND", 8: "VDD", 9: "INT2", 10: "NC", 11: "NC", 12: "CS",
          13: "SCL", 14: "SDA"}


# ------------------------------------------------------------------ geometry
def rot(theta_deg, x, y):
    t = math.radians(theta_deg)
    c, s = math.cos(t), math.sin(t)
    return (c * x - s * y, s * x + c * y)


def seg_pt_dist(x1, y1, x2, y2, px, py):
    dx, dy = x2 - x1, y2 - y1
    L2 = dx * dx + dy * dy
    if L2 == 0:
        return math.hypot(px - x1, py - y1)
    t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / L2))
    return math.hypot(px - x1 - t * dx, py - y1 - t * dy)


def seg_seg_dist(a, b):
    (x1, y1, x2, y2), (x3, y3, x4, y4) = a, b
    d1x, d1y, d2x, d2y = x2 - x1, y2 - y1, x4 - x3, y4 - y3
    den = d1x * d2y - d1y * d2x
    if den != 0:
        t = ((x3 - x1) * d2y - (y3 - y1) * d2x) / den
        u = ((x3 - x1) * d1y - (y3 - y1) * d1x) / den
        if 0 <= t <= 1 and 0 <= u <= 1:
            return 0.0
    return min(seg_pt_dist(x1, y1, x2, y2, x3, y3),
               seg_pt_dist(x1, y1, x2, y2, x4, y4),
               seg_pt_dist(x3, y3, x4, y4, x1, y1),
               seg_pt_dist(x3, y3, x4, y4, x2, y2))


def rect_pt_dist(cx, cy, hw, hh, px, py):
    dx = max(abs(px - cx) - hw, 0.0)
    dy = max(abs(py - cy) - hh, 0.0)
    return math.hypot(dx, dy)


def rect_rect_dist(a, b):
    (cx1, cy1, hw1, hh1), (cx2, cy2, hw2, hh2) = a, b
    dx = max(abs(cx1 - cx2) - (hw1 + hw2), 0.0)
    dy = max(abs(cy1 - cy2) - (hh1 + hh2), 0.0)
    return math.hypot(dx, dy)


def rect_seg_dist(r, s):
    cx, cy, hw, hh = r
    x1, y1, x2, y2 = s
    # inside?
    for (px, py) in ((x1, y1), (x2, y2)):
        if abs(px - cx) <= hw and abs(py - cy) <= hh:
            return 0.0
    corners = [(cx - hw, cy - hh), (cx + hw, cy - hh),
               (cx + hw, cy + hh), (cx - hw, cy + hh)]
    best = min(seg_seg_dist(s, (corners[i][0], corners[i][1],
                                corners[(i + 1) % 4][0], corners[(i + 1) % 4][1]))
               for i in range(4))
    return best


def pt_in_poly(pts, px, py):
    inside = False
    n = len(pts)
    for i in range(n):
        xa, ya = pts[i]
        xb, yb = pts[(i + 1) % n]
        if (ya <= py < yb) or (yb <= py < ya):
            if px < xa + (py - ya) * (xb - xa) / (yb - ya):
                inside = not inside
    return inside


def poly_edges(pts):
    n = len(pts)
    return [(pts[i][0], pts[i][1], pts[(i + 1) % n][0], pts[(i + 1) % n][1])
            for i in range(n)]


def poly_poly_dist(a, b):
    if any(pt_in_poly(b, x, y) for (x, y) in a) or \
       any(pt_in_poly(a, x, y) for (x, y) in b):
        return 0.0
    return min(seg_seg_dist(ea[:4] if len(ea) > 4 else ea, eb)
               for ea in poly_edges(a) for eb in poly_edges(b))


class Shape:
    """kind: 'disc'(cx,cy,r) | 'rect'(cx,cy,hw,hh) | 'cap'(x1,y1,x2,y2,hw)
    | 'poly'(list of (x,y))"""
    __slots__ = ("kind", "p", "bbox")

    def __init__(self, kind, p):
        self.kind, self.p = kind, p
        if kind == "disc":
            cx, cy, r = p
            self.bbox = (cx - r, cy - r, cx + r, cy + r)
        elif kind == "rect":
            cx, cy, hw, hh = p
            self.bbox = (cx - hw, cy - hh, cx + hw, cy + hh)
        elif kind == "poly":
            xs = [q[0] for q in p]
            ys = [q[1] for q in p]
            self.bbox = (min(xs), min(ys), max(xs), max(ys))
        else:
            x1, y1, x2, y2, hw = p
            self.bbox = (min(x1, x2) - hw, min(y1, y2) - hw,
                         max(x1, x2) + hw, max(y1, y2) + hw)

    def as_poly(self):
        if self.kind == "poly":
            return self.p
        cx, cy, hw, hh = self.p
        return [(cx - hw, cy - hh), (cx + hw, cy - hh),
                (cx + hw, cy + hh), (cx - hw, cy + hh)]

    def dist(self, o):
        a, b = self, o
        if a.kind > b.kind:  # cap < disc < poly < rect alphabetically
            a, b = b, a
        ka, kb = a.kind, b.kind
        if ka == "disc" and kb == "disc":
            (x1, y1, r1), (x2, y2, r2) = a.p, b.p
            return max(math.hypot(x1 - x2, y1 - y2) - r1 - r2, 0.0)
        if ka == "disc" and kb == "rect":
            x, y, r = a.p
            return max(rect_pt_dist(*b.p, x, y) - r, 0.0)
        if ka == "cap" and kb == "disc":
            x1, y1, x2, y2, hw = a.p
            x, y, r = b.p
            return max(seg_pt_dist(x1, y1, x2, y2, x, y) - hw - r, 0.0)
        if ka == "cap" and kb == "rect":
            x1, y1, x2, y2, hw = a.p
            return max(rect_seg_dist(b.p, (x1, y1, x2, y2)) - hw, 0.0)
        if ka == "cap" and kb == "cap":
            return max(seg_seg_dist(a.p[:4], b.p[:4]) - a.p[4] - b.p[4], 0.0)
        if kb == "rect" and ka == "rect":
            return rect_rect_dist(a.p, b.p)
        # polygon cases
        if ka == "poly" and kb == "rect":
            return poly_poly_dist(a.p, b.as_poly())
        if ka == "poly" and kb == "poly":
            return poly_poly_dist(a.p, b.p)
        if ka == "disc" and kb == "poly":
            x, y, r = a.p
            if pt_in_poly(b.p, x, y):
                return 0.0
            d = min(seg_pt_dist(*e, x, y) for e in poly_edges(b.p))
            return max(d - r, 0.0)
        if ka == "cap" and kb == "poly":
            x1, y1, x2, y2, hw = a.p
            if pt_in_poly(b.p, x1, y1) or pt_in_poly(b.p, x2, y2):
                return 0.0
            d = min(seg_seg_dist((x1, y1, x2, y2), e) for e in poly_edges(b.p))
            return max(d - hw, 0.0)
        raise ValueError((ka, kb))

    def samples(self, step=0.1):
        """Interior + perimeter(+outward) sample points, for pour tests."""
        pts = []
        if self.kind == "poly":
            poly = self.p
            cx = sum(q[0] for q in poly) / len(poly)
            cy = sum(q[1] for q in poly) / len(poly)
            pts.append((cx, cy))
            for (x1, y1, x2, y2) in poly_edges(poly):
                L = math.hypot(x2 - x1, y2 - y1)
                n = max(1, int(L / step))
                nxv, nyv = ((y2 - y1) / L, -(x2 - x1) / L) if L else (0, 0)
                for i in range(n + 1):
                    f = i / n
                    px, py = x1 + (x2 - x1) * f, y1 + (y2 - y1) * f
                    pts.append((px, py))
                    for off in (0.02, 0.04, -0.05):
                        pts.append((px + nxv * off, py + nyv * off))
                        pts.append((px - nxv * off, py - nyv * off))
            bx0, by0, bx1, by1 = self.bbox
            gx = max(1, int((bx1 - bx0) / step))
            gy = max(1, int((by1 - by0) / step))
            for i in range(gx + 1):
                for j in range(gy + 1):
                    px = bx0 + (bx1 - bx0) * i / gx
                    py = by0 + (by1 - by0) * j / gy
                    if pt_in_poly(poly, px, py):
                        pts.append((px, py))
            return pts
        if self.kind == "disc":
            cx, cy, r = self.p
            pts.append((cx, cy))
            for rr in (r * 0.6, r, r + 0.02, r + 0.04):
                n = max(8, int(2 * math.pi * max(rr, 0.05) / step))
                for i in range(n):
                    a = 2 * math.pi * i / n
                    pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        elif self.kind == "rect":
            cx, cy, hw, hh = self.p
            nx = max(2, int(2 * hw / step) + 1)
            ny = max(2, int(2 * hh / step) + 1)
            for i in range(nx + 1):
                for j in range(ny + 1):
                    pts.append((cx - hw + 2 * hw * i / nx,
                                cy - hh + 2 * hh * j / ny))
            for grow in (0.02, 0.04):
                w, h = hw + grow, hh + grow
                n = max(4, int((w + h) / step))
                for i in range(n + 1):
                    f = i / n
                    pts += [(cx - w + 2 * w * f, cy - h), (cx - w + 2 * w * f, cy + h),
                            (cx - w, cy - h + 2 * h * f), (cx + w, cy - h + 2 * h * f)]
        else:
            x1, y1, x2, y2, hw = self.p
            L = math.hypot(x2 - x1, y2 - y1)
            n = max(1, int(L / step))
            if L > 0:
                ux, uy = (x2 - x1) / L, (y2 - y1) / L
            else:
                ux, uy = 1.0, 0.0
            nxv, nyv = -uy, ux
            for i in range(n + 1):
                f = i / n
                px, py = x1 + (x2 - x1) * f, y1 + (y2 - y1) * f
                pts.append((px, py))
                for off in (hw * 0.7, -hw * 0.7):
                    pts.append((px + nxv * off, py + nyv * off))
        return pts


def arc_polyline(a):
    """Sample an inventory arc into points (max chord sagitta ~5um)."""
    x1, y1, x2, y2 = a["x1"], a["y1"], a["x2"], a["y2"]
    cx, cy = x1 + a["cx"], y1 + a["cy"]
    r = math.hypot(x1 - cx, y1 - cy)
    a1 = math.atan2(y1 - cy, x1 - cx)
    a2 = math.atan2(y2 - cy, x2 - cx)
    cw = a.get("clockwise", False)
    if cw:
        while a2 > a1:
            a2 -= 2 * math.pi
    else:
        while a2 < a1:
            a2 += 2 * math.pi
    sweep = a2 - a1
    if abs(sweep) < 1e-9:
        sweep = -2 * math.pi if cw else 2 * math.pi
    n = max(2, int(abs(sweep) * r / max(2 * math.sqrt(2 * r * 0.005), 0.05)))
    n = min(n, 64)
    return [(cx + r * math.cos(a1 + sweep * i / n),
             cy + r * math.sin(a1 + sweep * i / n)) for i in range(n + 1)]


class UF:
    def __init__(self):
        self.p = {}

    def find(self, a):
        p = self.p
        p.setdefault(a, a)
        root = a
        while p[root] != root:
            root = p[root]
        while p[a] != root:
            p[a], a = root, p[a]
        return root

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


# --------------------------------------------------------------- pour raster
def rasterize_pours(regions, x0, y0, nx, ny):
    """Paint dark/clear region polygons in order; return labelled int array
    (0 = no pour copper) via numpy-less? -> uses numpy."""
    import numpy as np
    mask = np.zeros((ny, nx), dtype=bool)
    for reg in regions:
        pts = reg["points"]
        if len(pts) < 3:
            continue
        dark = reg.get("dark", True)
        ys = [p[1] for p in pts]
        j0 = max(0, int((min(ys) - y0) / RASTER) - 1)
        j1 = min(ny - 1, int((max(ys) - y0) / RASTER) + 1)
        n = len(pts)
        for j in range(j0, j1 + 1):
            yy = y0 + (j + 0.5) * RASTER
            xs = []
            for i in range(n):
                xa, ya = pts[i]
                xb, yb = pts[(i + 1) % n]
                if ya <= yy < yb:        # upward crossing
                    xs.append((xa + (yy - ya) * (xb - xa) / (yb - ya), 1))
                elif yb <= yy < ya:      # downward crossing
                    xs.append((xa + (yy - ya) * (xb - xa) / (yb - ya), -1))
            if not xs:
                continue
            # NONZERO winding fill (not even-odd): Altium pre-fractured pour
            # outlines self-overlap at island-separation channels; CAM tools
            # and gerbonara/cairo fill them with the nonzero rule, and
            # even-odd would bridge separate plane islands (seen on In1.Cu:
            # the MUX_VCC island channel at (294.6..295.9, 246.9..247.5)).
            xs.sort()
            wind = 0
            for k in range(len(xs) - 1):
                wind += xs[k][1]
                if wind != 0:
                    i0 = max(0, int(math.ceil((xs[k][0] - x0) / RASTER - 0.5)))
                    i1 = min(nx - 1,
                             int(math.floor((xs[k + 1][0] - x0) / RASTER - 0.5)))
                    if i1 >= i0:
                        mask[j, i0:i1 + 1] = dark
    # connected-component label (8-conn) via row-span union-find
    labels = np.zeros((ny, nx), dtype=np.int32)
    uf = UF()
    nextlab = 1
    prev_spans = []
    for j in range(ny):
        row = mask[j]
        idx = np.flatnonzero(np.diff(np.concatenate(([0], row.view(np.int8), [0]))))
        spans = []
        for k in range(0, len(idx), 2):
            a, b = int(idx[k]), int(idx[k + 1]) - 1
            lab = nextlab
            nextlab += 1
            uf.find(lab)
            for (pa, pb, plab) in prev_spans:
                if pa <= b + 1 and pb >= a - 1:  # 8-connectivity
                    uf.union(lab, plab)
            spans.append((a, b, lab))
            labels[j, a:b + 1] = lab
        prev_spans = spans
    # flatten labels
    if nextlab > 1:
        remap = np.zeros(nextlab, dtype=np.int32)
        for l in range(1, nextlab):
            remap[l] = uf.find(l)
        labels = remap[labels]
    return labels


# ------------------------------------------------------------------- loading
def load_pnp(side):
    comps = []
    with open(ART / f"Reid Orthotic v2 R4 {side} PnP.csv") as f:
        for row in csv.DictReader(f):
            if not row.get("Designator"):
                continue
            comps.append({
                "ref": row["Designator"], "fp": row["Footprint"],
                "x": float(row["Mid X"].replace("mm", "")),
                "y": float(row["Mid Y"].replace("mm", "")),
                "rot": float(row["Rotation"]) % 360.0,
                "layer": row["Layer"].strip(), "comment": row["Comment"],
            })
    return comps


def local_model(fp):
    """[(pin, lx, ly), ...] or None; perimeter ICs return CCW base order."""
    if fp in TWO_PAD:
        d = TWO_PAD[fp]
        return [("1", -d, 0.0), ("2", d, 0.0)]
    if fp in VERT_TWO_PAD:
        d = VERT_TWO_PAD[fp]
        return [("1", 0.0, -d), ("2", 0.0, d)]
    if fp in FIXED_MODELS:
        return FIXED_MODELS[fp]
    if fp in PERIMETER_MODELS:
        ring, ep = PERIMETER_MODELS[fp]
        out = [(str(i + 1), x, y) for i, (x, y) in enumerate(ring)]
        if ep is not None:
            out.append(("PAD", ep[0], ep[1]))
        return out
    return None  # IO_CONN handled specially


# ------------------------------------------------------------ the main class
class Board:
    def __init__(self, side):
        self.side = side
        self.inv = json.load(open(NETDIR / f"inventory_{side.lower()}.json"))
        self.pnp = load_pnp(side)
        self.diag = {"side": side}
        layers = set(c["layer"] for c in self.pnp)
        self.comp_layer = "F.Cu" if layers == {"T"} else "B.Cu"
        self.diag["component_layer"] = self.comp_layer

        self.holes_p = self.inv["layers"]["drill_plated"]["holes"]
        self.holes_np = self.inv["layers"]["drill_nonplated"]["holes"]

    # ---------------- transform
    def solve_transform(self):
        flashes = self.inv["layers"][self.comp_layer]["flashes"]
        u2 = next(c for c in self.pnp if c["fp"] == "NRF52832-QFAA")
        ep = max(flashes, key=lambda f: f.get("bw", 0) * f.get("bh", 0))
        assert abs(ep.get("bw", 0) - 4.6) < 0.2, f"U2 EP not found: {ep}"
        fxy = [(f["x"], f["y"]) for f in flashes]

        def nearest(px, py):
            return min(math.hypot(x - px, y - py) for (x, y) in fxy)

        best = None
        for mir in ("none", "x", "y", "xy"):
            def M(x, y, mir=mir):
                return (-x if mir in ("x", "xy") else x,
                        -y if mir in ("y", "xy") else y)
            for rsign in (1, -1):
                for lflip in (False, True):
                    for roff in (0.0, 180.0):
                        mx, my = M(u2["x"], u2["y"])
                        t = (ep["x"] - mx, ep["y"] - my)
                        score = npts = ascore = anpts = 0
                        for c in self.pnp:
                            fps = FP_VARIANTS.get(c["fp"], [c["fp"]])
                            variants = [local_model(f) for f in fps]
                            variants = [m for m in variants if m]
                            if not variants:
                                continue
                            cx, cy = M(c["x"], c["y"])
                            vbest = None
                            for mdl in variants:
                                s = 0
                                for (_pin, lx, ly) in mdl:
                                    if lflip:
                                        lx = -lx
                                    px, py = rot(rsign * c["rot"] + roff, lx, ly)
                                    if nearest(cx + t[0] + px, cy + t[1] + py) < 0.12:
                                        s += 1
                                if vbest is None or s > vbest[0]:
                                    vbest = (s, len(mdl))
                            score += vbest[0]
                            npts += vbest[1]
                            if c["fp"] in ASYM_FPS:
                                ascore += vbest[0]
                                anpts += vbest[1]
                        frac = score / max(npts, 1)
                        afrac = ascore / max(anpts, 1)
                        key = (round(afrac, 3), round(frac, 3))
                        if best is None or key > best[0]:
                            best = (key, frac, afrac, mir, rsign, lflip, roff, t)
        _k, frac, afrac, mir, rsign, lflip, roff, t = best
        self.mir, self.rsign, self.lflip, self.roff, self.t = \
            mir, rsign, lflip, roff, t
        self.diag["transform_fit"] = {"all_pads_frac": round(frac, 4),
                                      "asym_pads_frac": round(afrac, 4)}
        assert frac > 0.97 and afrac > 0.97, f"transform fit poor: {best}"
        self.transform = {
            "pnp_mirror": mir, "local_rot_sign": rsign,
            "local_x_flip": lflip, "rot_offset_deg": roff,
            "dx": round(t[0], 4), "dy": round(t[1], 4),
            "note": "gerber = mirror(pnp) + t; "
                    "pad = that + R(sign*rot + rot_offset)(flip(local))",
        }

    def M(self, x, y):
        return (-x if self.mir in ("x", "xy") else x,
                -y if self.mir in ("y", "xy") else y)

    def comp_pos(self, c):
        mx, my = self.M(c["x"], c["y"])
        return (mx + self.t[0], my + self.t[1])

    def pad_pos(self, c, lx, ly):
        if self.lflip:
            lx = -lx
        px, py = rot(self.rsign * c["rot"] + self.roff, lx, ly)
        cx, cy = self.comp_pos(c)
        return (cx + px, cy + py)

    # ---------------- pad assignment
    def assign_pads(self):
        flashes = self.inv["layers"][self.comp_layer]["flashes"]
        # vias on the component layer are not pads
        via_pts = [(h["x"], h["y"]) for h in self.holes_p
                   if h["dia"] <= VIA_MAX_DRILL]
        self.is_via_flash = {}
        for i, f in enumerate(flashes):
            self.is_via_flash[i] = any(
                abs(f["x"] - x) < 0.08 and abs(f["y"] - y) < 0.08
                for (x, y) in via_pts)

        self.pads = {}          # flash idx -> (ref, pin or None)
        residuals = {}
        missing = []
        variant_used = {}
        for c in self.pnp:
            cands = []
            for fp in FP_VARIANTS.get(c["fp"], [c["fp"]]):
                mdl = local_model(fp)
                if mdl is None:
                    continue
                if c["layer"] == "B":
                    # bottom-mounted: the package is seen x-mirrored in the
                    # (top-view) gerber frame, so pin k sits at (-lx, ly)
                    mdl = [(pin, -lx, ly) for (pin, lx, ly) in mdl]
                matched, miss, res = {}, [], []
                for (pin, lx, ly) in mdl:
                    px, py = self.pad_pos(c, lx, ly)
                    bi, bd = None, PAD_MATCH
                    for i, f in enumerate(flashes):
                        if self.is_via_flash[i] and pin != "PAD":
                            continue
                        d = math.hypot(f["x"] - px, f["y"] - py)
                        if d < bd:
                            bi, bd = i, d
                    if bi is None:
                        miss.append((c["ref"], pin))
                    else:
                        matched[bi] = (c["ref"], pin)
                        res.append(bd)
                cands.append((len(miss), fp, matched, miss, res))
            if not cands:
                continue
            cands.sort(key=lambda t: t[0])
            nmiss, fp, matched, miss, res = cands[0]
            if fp != c["fp"]:
                variant_used[c["ref"]] = fp
            self.pads.update(matched)
            missing.extend(miss)
            if res:
                residuals[c["ref"]] = round(max(res), 4)
        if variant_used:
            self.diag["footprint_variant_used"] = variant_used
        # IO_CONN: the 0.7mm castellation/wire holes, nearest connector wins
        ios = [c for c in self.pnp if c["fp"] == "IO_CONN"]
        if ios:
            big_holes = [h for h in self.holes_p if h["dia"] > VIA_MAX_DRILL]
            groups = defaultdict(list)
            for h in big_holes:
                dists = [(math.hypot(h["x"] - self.comp_pos(c)[0],
                                     h["y"] - self.comp_pos(c)[1]), c["ref"])
                         for c in ios]
                dists.sort()
                groups[dists[0][1]].append(h)
            for ref, hs in groups.items():
                c = next(c for c in ios if c["ref"] == ref)
                cx, cy = self.comp_pos(c)
                # order along dominant axis of the hole line
                xs = [h["x"] for h in hs]
                ys = [h["y"] for h in hs]
                vx, vy = max(xs) - min(xs), max(ys) - min(ys)
                L = math.hypot(vx, vy) or 1.0
                hs.sort(key=lambda h: (h["x"] * vx + h["y"] * vy) / L)
                for n, h in enumerate(hs, 1):
                    bi = min(range(len(flashes)),
                             key=lambda i: math.hypot(flashes[i]["x"] - h["x"],
                                                      flashes[i]["y"] - h["y"]))
                    if math.hypot(flashes[bi]["x"] - h["x"],
                                  flashes[bi]["y"] - h["y"]) < 0.1:
                        self.pads[bi] = (ref, str(n))
                self.diag.setdefault("io_conn_pads", {})[ref] = len(hs)

        owned = len(self.pads)
        pad_flashes = [i for i in range(len(flashes)) if not self.is_via_flash[i]]
        self.diag["pnp_residual_max_mm"] = max(residuals.values())
        self.diag["pnp_residuals"] = residuals
        self.diag["model_pads_missing_flash"] = missing
        self.diag["flashes_on_comp_layer"] = len(flashes)
        self.diag["via_flashes_on_comp_layer"] = sum(self.is_via_flash.values())
        self.diag["component_pads_assigned"] = owned
        self.diag["board_level_pads"] = len(pad_flashes) - sum(
            1 for i in pad_flashes if i in self.pads)

    # ---------------- connectivity
    def build_graph(self):
        import numpy as np  # noqa: F401  (rasteriser needs it)
        self.uf = UF()
        self.layer_objects = {}   # layer -> list[(node, Shape)]
        self.net_tracks = defaultdict(int)
        allx, ally = [], []
        for L in COPPER_LAYERS:
            lay = self.inv["layers"].get(L)
            if not lay:
                continue
            for f in lay["flashes"]:
                allx.append(f["x"]); ally.append(f["y"])
            for r in lay["regions"]:
                for (x, y) in r["points"]:
                    allx.append(x); ally.append(y)
            for ln in lay["lines"]:
                allx += [ln["x1"], ln["x2"]]; ally += [ln["y1"], ln["y2"]]
        x0, y0 = min(allx) - 0.5, min(ally) - 0.5
        x1, y1 = max(allx) + 0.5, max(ally) + 0.5
        nx = int((x1 - x0) / RASTER) + 1
        ny = int((y1 - y0) / RASTER) + 1
        self.diag["raster"] = [nx, ny]

        self.pour_labels = {}
        for L in COPPER_LAYERS:
            lay = self.inv["layers"].get(L)
            if not lay:
                continue
            objs = []
            for i, f in enumerate(lay["flashes"]):
                node = ("F", L, i)
                ap = f["ap"] or {}
                prims = f.get("prims")
                if prims:
                    for pr in prims:
                        if pr["t"] == "circ":
                            objs.append((node, Shape("disc",
                                                     (pr["x"], pr["y"], pr["r"]))))
                        else:
                            objs.append((node, Shape("poly",
                                                     [tuple(q) for q in pr["pts"]])))
                    continue
                bw, bh = f.get("bw"), f.get("bh")
                if bw is None:
                    bw = bh = ap.get("diameter", 0.1)
                if ap.get("type") == "CircleAperture":
                    objs.append((node, Shape("disc", (f["x"], f["y"], bw / 2))))
                elif ap.get("type") == "ObroundAperture" and abs(bw - bh) > 1e-9:
                    # true obround = core rect + two end discs; a plain rect
                    # overestimates the rounded ends by up to (sqrt(2)-1)*r,
                    # which falsely bridged pads to adjacent pours
                    if bw > bh:
                        r = bh / 2
                        dx = (bw - bh) / 2
                        objs.append((node, Shape("rect", (f["x"], f["y"], dx, r))))
                        for s in (-1, 1):
                            objs.append((node, Shape("disc",
                                                     (f["x"] + s * dx, f["y"], r))))
                    else:
                        r = bw / 2
                        dy = (bh - bw) / 2
                        objs.append((node, Shape("rect", (f["x"], f["y"], r, dy))))
                        for s in (-1, 1):
                            objs.append((node, Shape("disc",
                                                     (f["x"], f["y"] + s * dy, r))))
                else:
                    objs.append((node, Shape("rect", (f["x"], f["y"], bw / 2, bh / 2))))
            for i, ln in enumerate(lay["lines"]):
                hw = (ln["ap"].get("diameter", 0.1) if ln["ap"] else 0.1) / 2
                objs.append((("L", L, i),
                             Shape("cap", (ln["x1"], ln["y1"], ln["x2"], ln["y2"], hw))))
            for i, a in enumerate(lay["arcs"]):
                hw = (a["ap"].get("diameter", 0.1) if a["ap"] else 0.1) / 2
                pts = arc_polyline(a)
                for k in range(len(pts) - 1):
                    objs.append((("A", L, i),
                                 Shape("cap", (pts[k][0], pts[k][1],
                                               pts[k + 1][0], pts[k + 1][1], hw))))
            self.layer_objects[L] = objs

            # geometric same-layer unions via spatial hash
            CELL = 2.0
            grid = defaultdict(list)
            for oi, (node, shp) in enumerate(objs):
                bx0, by0, bx1, by1 = shp.bbox
                for gx in range(int((bx0 - TOL) / CELL), int((bx1 + TOL) / CELL) + 1):
                    for gy in range(int((by0 - TOL) / CELL), int((by1 + TOL) / CELL) + 1):
                        grid[(gx, gy)].append(oi)
            seen = set()
            for cell, members in grid.items():
                for ii in range(len(members)):
                    for jj in range(ii + 1, len(members)):
                        a, b = members[ii], members[jj]
                        if a > b:
                            a, b = b, a
                        if (a, b) in seen:
                            continue
                        seen.add((a, b))
                        na, sa = objs[a]
                        nb, sb = objs[b]
                        if na == nb:
                            self.uf.union(na, nb)
                            continue
                        ba, bb = sa.bbox, sb.bbox
                        if ba[0] > bb[2] + TOL or bb[0] > ba[2] + TOL or \
                           ba[1] > bb[3] + TOL or bb[1] > ba[3] + TOL:
                            continue
                        if sa.dist(sb) <= TOL:
                            self.uf.union(na, nb)

            # pours
            labels = rasterize_pours(lay["regions"], x0, y0, nx, ny)
            self.pour_labels[L] = (labels, x0, y0, nx, ny)
            if labels.max() == 0:
                continue

            def lab_at(px, py):
                i = int((px - x0) / RASTER)
                j = int((py - y0) / RASTER)
                if 0 <= i < nx and 0 <= j < ny:
                    return int(labels[j, i])
                return 0

            for (node, shp) in objs:
                hit = set()
                for (px, py) in shp.samples():
                    l = lab_at(px, py)
                    if l:
                        hit.add(l)
                for l in hit:
                    self.uf.union(node, ("P", L, l))

        # vias join layers
        self.via_nodes = []
        for hi, h in enumerate(self.holes_p):
            node = ("V", hi)
            self.via_nodes.append(node)
            hx, hy, hr = h["x"], h["y"], h["dia"] / 2
            probe = Shape("disc", (hx, hy, hr))
            for L in COPPER_LAYERS:
                for (onode, shp) in self.layer_objects.get(L, []):
                    b = shp.bbox
                    if hx + hr + TOL < b[0] or hx - hr - TOL > b[2] or \
                       hy + hr + TOL < b[1] or hy - hr - TOL > b[3]:
                        continue
                    if probe.dist(shp) <= TOL:
                        self.uf.union(node, onode)
                labels, lx0, ly0, lnx, lny = self.pour_labels.get(L, (None, 0, 0, 0, 0))
                if labels is None or labels.max() == 0:
                    continue
                hits = set()
                for (px, py) in probe.samples(step=0.05):
                    i = int((px - lx0) / RASTER)
                    j = int((py - ly0) / RASTER)
                    if 0 <= i < lnx and 0 <= j < lny:
                        l = int(labels[j, i])
                        if l:
                            hits.add(l)
                for l in hits:
                    self.uf.union(node, ("P", L, l))

    # ---------------- nets
    def collect_nets(self):
        groups = defaultdict(lambda: {"pads": [], "unnumbered": [],
                                      "tracks": 0, "vias": 0, "layers": set()})
        flashes = self.inv["layers"][self.comp_layer]["flashes"]
        for L, objs in self.layer_objects.items():
            seen_tracks = set()
            for (node, _s) in objs:
                root = self.uf.find(node)
                g = groups[root]
                g["layers"].add(L)
                if node[0] in ("L", "A"):
                    if node not in seen_tracks:
                        seen_tracks.add(node)
                        g["tracks"] += 1
                elif node[0] == "F":
                    i = node[2]
                    seen = g.setdefault("_seen", set())
                    if (L, i) in seen:
                        continue
                    seen.add((L, i))
                    if L == self.comp_layer:
                        if i in self.pads:
                            g["pads"].append(self.pads[i])
                        elif not self.is_via_flash.get(i, False):
                            f = flashes[i]
                            g["unnumbered"].append(
                                {"x": f["x"], "y": f["y"], "layer": L,
                                 "owner": None})
                    else:
                        # non-component copper layer: flag solderable pads
                        # (not via pads, not the through-hole wire/castellation
                        # pads already represented on the component layer)
                        f = self.inv["layers"][L]["flashes"][i]
                        on_hole = any(
                            abs(f["x"] - h["x"]) < 0.08 and
                            abs(f["y"] - h["y"]) < 0.08
                            for h in self.holes_p)
                        if not on_hole:
                            g["unnumbered"].append(
                                {"x": f["x"], "y": f["y"], "layer": L,
                                 "owner": None})
        for hi, node in enumerate(self.via_nodes):
            groups[self.uf.find(node)]["vias"] += 1
        # drop groups with nothing of interest (isolated single objects)
        self.netgroups = {k: v for k, v in groups.items()
                          if v["pads"] or v["unnumbered"] or v["vias"] or v["tracks"]}
        # map (ref,pin)->root and root->id
        self.where = {}
        for root, g in self.netgroups.items():
            for (ref, pin) in g["pads"]:
                self.where[(ref, pin)] = root

    def pad_root(self, ref, pin):
        return self.where.get((ref, str(pin)))

    # ---------------- pin-1 / rotation resolution for perimeter ICs
    def resolve_perimeter(self):
        """For each perimeter package, choose the cyclic offset (+direction)
        that satisfies the electrical anchors; renumber self.pads."""
        report = {}
        flashes = self.inv["layers"][self.comp_layer]["flashes"]
        # current numbering: pads hold base-order numbers (CCW from
        # conventional pin1).  Build per-component pin->flash idx.
        comp_pads = defaultdict(dict)
        for i, (ref, pin) in self.pads.items():
            comp_pads[ref][pin] = i
        by_ref = {c["ref"]: c for c in self.pnp}

        def net_of_flash(i):
            return self.uf.find(("F", self.comp_layer, i))

        def net_has(root, *refpins):
            g = self.netgroups.get(root)
            if not g:
                return False
            pads = set(g["pads"])
            return all(any(p[0] == ref and (pin is None or p[1] == str(pin))
                           for p in pads) for (ref, pin) in refpins)

        def net_has_ref(root, ref):
            g = self.netgroups.get(root)
            return bool(g) and any(p[0] == ref for p in g["pads"])

        gnd_root = None
        u2pads = comp_pads.get("U2", {})
        if "PAD" in u2pads:
            gnd_root = net_of_flash(u2pads["PAD"])

        def renumber(ref, n, offset, direction):
            """new_pin = f(base_pin): base order is CCW list 1..n; rotating by
            offset with direction d: pin1 is at base index `offset`."""
            m = comp_pads[ref]
            newmap = {}
            for base_pin, fi in m.items():
                if base_pin == "PAD":
                    newmap[fi] = (ref, "PAD")
                    continue
                b = int(base_pin) - 1
                if direction == "ccw":
                    newp = (b - offset) % n + 1
                else:
                    newp = (offset - b) % n + 1
                newmap[fi] = (ref, str(newp))
            for fi, rp in newmap.items():
                self.pads[fi] = rp
            # rebuild where for this ref
            for (r, p) in list(self.where):
                if r == ref:
                    del self.where[(r, p)]
            for fi, (r, p) in newmap.items():
                self.where[(r, p)] = net_of_flash(fi)
            comp_pads[ref] = {p: fi for fi, (r, p) in newmap.items()}
            # recompute net pad lists from the updated numbering
            for g in self.netgroups.values():
                g["pads"] = []
            for fi2, (r2, p2) in self.pads.items():
                root = net_of_flash(fi2)
                if root in self.netgroups:
                    self.netgroups[root]["pads"].append((r2, p2))

        def pin_net(ref, pin):
            fi = comp_pads[ref].get(str(pin))
            return net_of_flash(fi) if fi is not None else None

        def try_numbering(ref, n, checks):
            """checks(pin->net fn) -> bool; try all offsets x directions."""
            sols = []
            for direction in ("ccw", "cw"):
                for off in range(n):
                    def pn(pin, _o=off, _d=direction):
                        # inverse: which base pin is displayed pin `pin`
                        p = int(pin) - 1
                        if _d == "ccw":
                            b = (p + _o) % n + 1
                        else:
                            b = (_o - p) % n + 1
                        return pin_net(ref, b)
                    if checks(pn):
                        sols.append((off, direction))
            return sols

        # --- U2 (QFN48) ---
        def u2_checks(pn):
            # crystals
            x1 = pn(2) and net_has_ref(pn(2), "X1") and \
                pn(3) and net_has_ref(pn(3), "X1") and pn(2) != pn(3)
            x2 = pn(34) and net_has_ref(pn(34), "X2") and \
                pn(35) and net_has_ref(pn(35), "X2") and pn(34) != pn(35)
            dec1 = pn(1) and net_has_ref(pn(1), "C6")
            # DC/DC: DCC -> L1(15n)+L2(10u) series -> DEC4 (+ its cap).
            # (The brief guessed C8 on DEC4; the copper says the DEC4 cap is
            # C9 and C8 is a VDD 100n -- so anchor on the inductors instead.)
            dec4 = pn(46) and (net_has_ref(pn(46), "L1") or
                               net_has_ref(pn(46), "L2")) and \
                any(r.startswith("C") for (r, _p) in
                    self.netgroups[pn(46)]["pads"])
            dcc = pn(47) and (net_has_ref(pn(47), "L1") or
                              net_has_ref(pn(47), "L2")) and pn(47) != pn(46)
            rst = pn(24) and net_has_ref(pn(24), "R45") and net_has_ref(pn(24), "C3")
            return bool(x1 and x2 and dec1 and dec4 and dcc and rst)

        sols = try_numbering("U2", 48, u2_checks)
        report["U2"] = {"solutions": sols}
        assert len(sols) == 1, f"U2 numbering not unique: {sols}"
        off, d = sols[0]
        renumber("U2", 48, off, d)
        report["U2"].update({"offset": off, "direction": d})
        gnd_root = self.pad_root("U2", "PAD")
        vdd_root = self.pad_root("U2", 13)

        # --- muxes (74LV4051BQ) ---
        mux_refs = [c["ref"] for c in self.pnp if c["fp"] == "74LV4051BQ"]
        mux_sols = {}
        for ref in mux_refs:
            def mux_checks(pn, ref=ref):
                fi = comp_pads[ref].get("PAD")
                ep_root = net_of_flash(fi) if fi is not None else None
                vcc_ok = pn(16) is not None and pn(16) == ep_root
                gnd_ok = all(pn(p) == gnd_root for p in (6, 7, 8))
                return bool(vcc_ok and gnd_ok)
            s = try_numbering(ref, 16, mux_checks)
            mux_sols[ref] = s
        report["74LV4051BQ"] = {r: s for r, s in mux_sols.items()}
        uniq = set(tuple(s) for s in mux_sols.values())
        assert all(len(s) == 1 for s in mux_sols.values()), \
            f"mux numbering not unique: {mux_sols}"
        assert len(uniq) == 1, f"mux instances disagree: {mux_sols}"
        for ref in mux_refs:
            o, d = mux_sols[ref][0]
            renumber(ref, 16, o, d)

        # --- BQ24210 ---
        def bq_checks(pn):
            return bool(pn(2) and net_has_ref(pn(2), "R56") and
                        pn(5) and net_has_ref(pn(5), "R60") and
                        pn(10) and net_has_ref(pn(10), "C43") and
                        pn(1) and net_has_ref(pn(1), "C34"))
        sols = try_numbering("U8", 10, bq_checks)
        report["BQ24210"] = {"solutions": sols}
        assert len(sols) == 1, f"BQ24210 numbering not unique: {sols}"
        o, d = sols[0]
        renumber("U8", 10, o, d)

        # --- LSM6DSM ---
        def lsm_checks(pn):
            # 6/7 GND, 5/8 VDDIO/VDD, 13/14 SCL/SDA to the MCU, and the
            # 180deg-degeneracy breakers: 12 (CS, tied high for I2C) on VDD,
            # 1 (SDO/SA0 address strap) on a rail
            return bool(pn(6) == gnd_root and pn(7) == gnd_root and
                        pn(8) == vdd_root and pn(5) == vdd_root and
                        pn(12) == vdd_root and
                        pn(1) in (gnd_root, vdd_root) and
                        pn(13) is not None and pn(14) is not None and
                        net_has(pn(13), ("U2", None)) and
                        net_has(pn(14), ("U2", None)) and pn(13) != pn(14))
        sols = try_numbering("U1", 14, lsm_checks)
        report["LSM6DSM"] = {"solutions": sols}
        if len(sols) == 1:
            o, d = sols[0]
            renumber("U1", 14, o, d)
        else:
            report["LSM6DSM"]["warning"] = "not unique; base numbering kept"

        # --- X2 (XRCGB32M): crystal terminals must be pins 1/3 on XC1/XC2 ---
        xc1, xc2 = self.pad_root("U2", 34), self.pad_root("U2", 35)
        x2map = {p: net_of_flash(fi) for p, fi in comp_pads["X2"].items()}
        term = {p for p, r in x2map.items() if r in (xc1, xc2)}
        report["X2"] = {"terminal_pads": sorted(term)}
        if term == {"2", "4"}:  # rotate numbering by one so terminals are 1/3
            m = comp_pads["X2"]
            newnum = {"2": "1", "3": "2", "4": "3", "1": "4"}
            for p, fi in list(m.items()):
                self.pads[fi] = ("X2", newnum[p])
            report["X2"]["renumbered"] = True
            for g in self.netgroups.values():
                g["pads"] = []
            for fi2, (r2, p2) in self.pads.items():
                root = net_of_flash(fi2)
                if root in self.netgroups:
                    self.netgroups[root]["pads"].append((r2, p2))
            self.where = {}
            for root, g in self.netgroups.items():
                for rp in g["pads"]:
                    self.where[rp] = root
        self.numbering_report = report

    # ---------------- naming + validation
    def name_and_validate(self):
        names = {}

        def setname(root, name):
            if root is not None and root not in names:
                names[root] = name

        setname(self.pad_root("U2", "PAD"), "0V")
        setname(self.pad_root("U2", 13), "3V3")
        setname(self.pad_root("U8", 1), "VSYS")
        setname(self.pad_root("U8", 10), "VBAT_F")  # fused side (C43, F1)
        # battery side of the fuse: net with F1 + J4
        for root, g in self.netgroups.items():
            refs = {r for (r, p) in g["pads"]}
            if "F1" in refs and "J4" in refs:
                setname(root, "VBAT")
        mux_refs = [c["ref"] for c in self.pnp if c["fp"] == "74LV4051BQ"]
        setname(self.pad_root(mux_refs[0], 16), "MUX_VCC")
        setname(self.pad_root("U2", 24), "MCU_RST")
        setname(self.pad_root("U2", 25), "SWDCLK")
        setname(self.pad_root("U2", 26), "SWDIO")
        for pin, nm in ((2, "XL1"), (3, "XL2"), (34, "XC1"), (35, "XC2"),
                        (30, "ANT_FEED"), (1, "DEC1"), (32, "DEC2"),
                        (33, "DEC3"), (46, "DEC4"), (47, "DCC")):
            setname(self.pad_root("U2", pin), nm)

        gpio = GPIO[self.side]
        anchors = {}

        def net_pads(root):
            g = self.netgroups.get(root)
            return sorted(set(g["pads"])) if g else []

        def has(root, ref, pin=None):
            return any(r == ref and (pin is None or p == str(pin))
                       for (r, p) in net_pads(root))

        def one_hop(root, pred):
            """True if pred holds on a net reached through one 2-pad passive."""
            for (r, p) in net_pads(root):
                if r[0] in "RCLF" and p in ("1", "2"):
                    other = self.pad_root(r, "2" if p == "1" else "1")
                    if other is not None and pred(other):
                        return True, r
            return False, None

        # anchor 1: MCU_RST
        rst = self.pad_root("U2", 24)
        j5_unnum = len(self.netgroups[rst]["unnumbered"]) if rst in self.netgroups else 0
        anchors["MCU_RST"] = {
            "pass": bool(has(rst, "R45") and has(rst, "C3") and j5_unnum >= 1),
            "members": net_pads(rst), "unnumbered_J5_candidates": j5_unnum}

        # anchor 2: BQ24210
        bq = {
            "VBUS->VSYS(C34,C40,C42)": all(has(self.pad_root("U8", 1), r)
                                           for r in ("C34", "C40", "C42")),
            "BAT->VBAT_F(C43,F1)": has(self.pad_root("U8", 10), "C43") and
                                   has(self.pad_root("U8", 10), "F1"),
            "ISET->R56": has(self.pad_root("U8", 2), "R56"),
            "TS->R59+R60": has(self.pad_root("U8", 5), "R60") and
                           (has(self.pad_root("U8", 5), "R59") or
                            has(self.pad_root("U8", 4), "R59")),
        }
        anchors["BQ24210"] = {"pass": all(bq.values()), "checks": bq}

        # anchor 3: muxes
        gnd = self.pad_root("U2", "PAD")
        mux_ok, mux_detail = True, {}
        mux_vcc = self.pad_root(mux_refs[0], 16)
        caps_on_vcc = [r for (r, p) in net_pads(mux_vcc)
                       if r in ("C24", "C25", "C26", "C27", "C28")]
        for ref in mux_refs:
            ok = (self.pad_root(ref, 16) == mux_vcc ==
                  self.pad_root(ref, "PAD")) and all(
                self.pad_root(ref, p) == gnd for p in (6, 7, 8))
            mux_detail[ref] = ok
            mux_ok &= ok
        mux_ok &= len(caps_on_vcc) >= 5
        # MUX_VCC must be a distinct switched rail, not merged into 0V/3V3
        # (guards against pour-bridging extraction bugs passing vacuously)
        distinct = mux_vcc not in (gnd, self.pad_root("U2", 13))
        mux_ok &= distinct
        anchors["74LV4051BQ"] = {"pass": bool(mux_ok), "per_mux": mux_detail,
                                 "MUX_VCC_caps": caps_on_vcc,
                                 "MUX_VCC_distinct_from_rails": distinct}

        # anchor 4: VBAT divider
        adc_vbat = self.pad_root("U2", PORT_TO_PKG[gpio["VBAT"]])
        div_ok = has(adc_vbat, "R43") and has(adc_vbat, "R44")
        anchors["VBAT_divider"] = {"pass": bool(div_ok),
                                   "members": net_pads(adc_vbat)}
        setname(adc_vbat, "ADC_VBAT")
        vbat_on = self.pad_root("U2", PORT_TO_PKG[gpio["VBAT_ON"]])
        anchors["VBAT_ON_switch"] = {
            "members": net_pads(vbat_on),
            "info": "expect R4x to Q1/Q2 gate chain"}

        # gpio.h cross-check
        gpio_rep = {}
        mux_groups = {"FSR": set(), "CAP": set()}
        for fn, port in sorted(gpio.items()):
            pkg = PORT_TO_PKG[port]
            root = self.pad_root("U2", pkg)
            members = net_pads(root)
            entry = {"port": f"P0.{port:02d}", "pkg_pin": pkg,
                     "members": members}
            ok = None
            if fn.startswith(("FSR_S", "CAP_S")):
                want = {"S0": 11, "S1": 10, "S2": 9}[fn[-2:]]
                hits = [r for r in mux_refs if has(root, r, want)]
                wrong = [(r, p) for (r, p) in members
                         if r in mux_refs and p != str(want)]
                ok = bool(hits) and not wrong
                entry["mux_select_pins"] = hits
                mux_groups[fn[:3]].update(hits)
            elif fn.startswith(("FSR_CH", "CAP_CH")) or fn == "TEMP_COM":
                hits = [r for r in mux_refs if has(root, r, 3)]
                entry["mux_Z_pins"] = hits
                ok = bool(hits) if fn != "TEMP_COM" else None
            elif fn in ("SDA", "SCL"):
                want = 14 if fn == "SDA" else 13
                ok = has(root, "U1", want)
                entry["imu_pin"] = want
            elif fn == "BQ_PG":
                ok = has(root, "U8", 6)
                if not ok:  # PG is sensed through a series resistor (R20)
                    ok, via_r = one_hop(root, lambda n: has(n, "U8", 6))
                    entry["via_series"] = via_r
            elif fn == "BQ_CHG":
                ok = has(root, "U8", 8)
                if not ok:
                    ok, via_r = one_hop(root, lambda n: has(n, "U8", 8))
                    entry["via_series"] = via_r
            elif fn == "MUX_ON":
                ok = any(r.startswith("Q") for (r, p) in members)
                if not ok:
                    ok, via_r = one_hop(root, lambda n: any(
                        r.startswith("Q") for (r, p) in net_pads(n)))
                    entry["via_series"] = via_r
            elif fn == "VBAT_ON":
                ok = any(r.startswith("Q") for (r, p) in members)
                if not ok:
                    ok, via_r = one_hop(root, lambda n: any(
                        r.startswith("Q") for (r, p) in net_pads(n)))
                    entry["via_series"] = via_r
            elif fn == "VBAT":
                ok = div_ok
            elif fn == "RESET":
                ok = anchors["MCU_RST"]["pass"]
            elif fn == "IS_LHS":
                gnd_r = self.pad_root("U2", "PAD")
                vdd_r = self.pad_root("U2", 13)
                if root == gnd_r:
                    entry["strap"] = "0V"
                elif root == vdd_r:
                    entry["strap"] = "3V3"
                else:
                    hit0, r0 = one_hop(root, lambda n: n == gnd_r)
                    hit1, r1 = one_hop(root, lambda n: n == vdd_r)
                    entry["strap"] = (f"0V via {r0}" if hit0 else
                                      f"3V3 via {r1}" if hit1 else "floating")
            if ok is not None:
                entry["pass"] = bool(ok)
            gpio_rep[fn] = entry
            if names.get(root) is None:
                setname(root, fn)
        anchors["gpio_h"] = gpio_rep
        anchors["mux_groups"] = {k: sorted(v) for k, v in mux_groups.items()}

        # U2 pin table
        u2_table = {}
        for pin in list(range(1, 49)) + ["PAD"]:
            root = self.pad_root("U2", pin)
            u2_table[str(pin)] = {
                "pkg_name": NRF_PINS.get(pin, "VSS(EP)") if pin != "PAD" else "VSS(EP)",
                "net": names.get(root) if root else None,
                "net_id": self.net_ids.get(root) if root else None,
                "n_net_pads": len(net_pads(root)) if root else 0,
            }
        # optional cross-check against the schematic-PDF extraction
        # (netlist_<side>.json).  Named nets only; 2-pad passives compared at
        # ref level because the PDF extraction's pin-1/2 orientation for
        # passives is unreliable.
        sch_file = NETDIR / f"netlist_{self.side.lower()}.json"
        if sch_file.exists():
            sch = json.loads(sch_file.read_text())
            if "nets" in sch and isinstance(sch["nets"], dict):
                sch = sch["nets"]
            passives = {c["ref"] for c in self.pnp
                        if c["ref"][0] in "RCLF" and
                        len(local_model(FP_VARIANTS.get(c["fp"], [c["fp"]])[0])
                            or []) == 2}

            def key(ref, pin):
                return (ref,) if ref in passives else (ref, str(pin))

            cop_of = defaultdict(set)
            for root, g in self.netgroups.items():
                for (r, p) in g["pads"]:
                    cop_of[key(r, p)].add(root)
            ok = bad = 0
            mismatches = {}
            for net, pins in sch.items():
                if net.startswith("N$") or not isinstance(pins, list):
                    continue
                pin_roots = {tuple(rp): cop_of.get(key(*rp), set())
                             for rp in map(tuple, pins)}
                votes = defaultdict(int)
                for rs in pin_roots.values():
                    for r in rs:
                        votes[r] += 1
                if not votes:
                    continue
                main = max(votes, key=lambda k: votes[k])
                outliers = sorted(rp for rp, rs in pin_roots.items()
                                  if rs and main not in rs)
                ok += sum(1 for rs in pin_roots.values() if main in rs)
                bad += len(outliers)
                if outliers:
                    mismatches[net] = {
                        "copper_net": names.get(main) or self.net_ids[main],
                        "outlier_pins": outliers}
            self.diag["schematic_crosscheck"] = {
                "named_net_pins_agreeing": ok, "outlier_pins": bad,
                "nets_with_outliers": mismatches}

        # board-level (unowned) pads and the J5 / TC2030 cluster
        board_pads = []
        for root, g in self.netgroups.items():
            for u in g["unnumbered"]:
                board_pads.append({**u, "net": names.get(root),
                                   "net_id": self.net_ids[root]})
        self.diag["board_pads"] = board_pads
        self.diag["j5_tc2030_candidates"] = [
            p for p in board_pads if p.get("net") in
            ("MCU_RST", "SWDIO", "SWDCLK")]

        self.names = names
        self.anchors = anchors
        self.u2_table = u2_table

    # ---------------- output
    def finalize(self):
        # stable net ids: by (has name, size desc)
        roots = sorted(self.netgroups,
                       key=lambda r: (-len(self.netgroups[r]["pads"]), str(r)))
        self.net_ids = {r: i + 1 for i, r in enumerate(roots)}

    def output(self):
        nets = []
        for root, g in sorted(self.netgroups.items(),
                              key=lambda kv: self.net_ids[kv[0]]):
            nets.append({
                "id": self.net_ids[root],
                "name": self.names.get(root),
                "pads": sorted(set(g["pads"])),
                "unnumbered_pads": g["unnumbered"],
                "n_tracks": g["tracks"], "n_vias": g["vias"],
                "layers": sorted(g["layers"]),
            })
        out = {
            "side": self.side,
            "transform": self.transform,
            "numbering": self.numbering_report,
            "u2_pin_table": self.u2_table,
            "anchors": self.anchors,
            "nets": nets,
            "diagnostics": self.diag,
        }
        dest = NETDIR / f"copper_netlist_{self.side.lower()}.json"
        dest.write_text(json.dumps(out, indent=1))
        n2 = sum(1 for n in nets if len(n["pads"]) >= 2)
        orphans = sum(1 for n in nets
                      if len(n["pads"]) == 1 and not n["unnumbered_pads"])
        named = sum(1 for n in nets if n["name"])
        print(f"[{self.side}] transform={self.transform}")
        print(f"[{self.side}] nets={len(nets)} (>=2 numbered pads: {n2}, "
              f"named: {named}, single-pad orphans: {orphans})")
        print(f"[{self.side}] pads assigned={self.diag['component_pads_assigned']} "
              f"board-level={self.diag['board_level_pads']} "
              f"vias(comp layer)={self.diag['via_flashes_on_comp_layer']}")
        apass = {k: v.get("pass") for k, v in self.anchors.items()
                 if isinstance(v, dict) and "pass" in v}
        print(f"[{self.side}] anchors: {apass}")
        gp = self.anchors["gpio_h"]
        fails = {k: v for k, v in gp.items() if v.get("pass") is False}
        print(f"[{self.side}] gpio.h checks: "
              f"{sum(1 for v in gp.values() if v.get('pass'))} pass, "
              f"{len(fails)} fail {list(fails) if fails else ''}")
        return out


def run(side):
    b = Board(side)
    b.solve_transform()
    b.assign_pads()
    b.build_graph()
    b.collect_nets()
    b.resolve_perimeter()
    b.finalize()
    b.name_and_validate()
    return b.output()


if __name__ == "__main__":
    sides = sys.argv[1:] or ["LHS", "RHS"]
    for s in sides:
        run(s)
