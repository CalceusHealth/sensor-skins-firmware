#!/usr/bin/env python3
"""Generate editable KiCad 7 .kicad_pcb files for the R4 LHS/RHS boards.

Reuses the copper connectivity engine (copper_netlist.py) end to end: the
same PnP transform, pad ownership/numbering and union-find connectivity that
produced netlist/copper_netlist_<side>.json also nets every emitted pad,
segment, arc, via and zone here (the net ids are asserted identical to the
committed JSON).

Geometry comes verbatim from netlist/inventory_<side>.json (gerber frame,
mm).  Mapping to the KiCad sheet:  kx = x - 240.0,  ky = 300.0 - y  (Y flip:
gerber is Y-up, KiCad Y-down).  Rotations pass through unchanged (both
conventions are visually-CCW).

Run with the scratchpad venv python (numpy for the engine, shapely for the
zone fills).
"""
import json
import math
import sys
import uuid
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import copper_netlist as cne  # noqa: E402

NETDIR = HERE.parent / "netlist"
PCBDIR = HERE.parent / "pcb"

KXOFF = 240.0
KYOFF = 300.0
COPPER = cne.COPPER_LAYERS
VIA_MAX = cne.VIA_MAX_DRILL

GERBER_TO_KICAD = {"F.Silkscreen": "F.SilkS", "B.Silkscreen": "B.SilkS"}


def KX(x):
    return x - KXOFF


def KY(y):
    return KYOFF - y


def fmt(v):
    s = f"{v:.5f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def xy(x, y):
    """gerber point -> kicad '(xy ...)' content."""
    return f"{fmt(KX(x))} {fmt(KY(y))}"


def rotk(theta_deg, x, y):
    """KiCad RotatePoint: positive = CCW on screen (Y-down frame)."""
    t = math.radians(theta_deg)
    c, s = math.cos(t), math.sin(t)
    return (x * c + y * s, -x * s + y * c)


class TS:
    def __init__(self, side):
        self.side, self.n = side, 0

    def __call__(self):
        self.n += 1
        return str(uuid.uuid5(uuid.NAMESPACE_URL, f"r4kicad/{self.side}/{self.n}"))


# ------------------------------------------------------------- flash shapes
def _dedupe(pts):
    out = []
    for p in pts:
        if not out or (abs(p[0] - out[-1][0]) > 1e-6 or abs(p[1] - out[-1][1]) > 1e-6):
            out.append(tuple(p))
    if len(out) > 1 and abs(out[0][0] - out[-1][0]) < 1e-6 and \
            abs(out[0][1] - out[-1][1]) < 1e-6:
        out.pop()
    return out


def flash_shape(f, warn):
    """Reconstruct the pad shape of a gerber flash.

    Returns (shape, w, h, angle_deg_visual_ccw, rratio).  shape is one of
    circle|rect|oval|roundrect.  Sizes are in the shape's own (rotated)
    frame.  Falls back to the bounding-box rect (and records a warning)."""
    ap = f.get("ap") or {}
    t = ap.get("type")
    bw, bh = f.get("bw"), f.get("bh")
    if t == "CircleAperture":
        d = ap.get("diameter") or bw
        return ("circle", d, d, 0.0, 0.0)
    if t == "RectangleAperture":
        return ("rect", ap["w"], ap["h"], 0.0, 0.0)
    if t == "ObroundAperture":
        return ("oval", ap["w"], ap["h"], 0.0, 0.0)
    prims = f.get("prims") or []
    circs = [p for p in prims if p["t"] == "circ"]
    polys = [_dedupe(p["pts"]) for p in prims if p["t"] == "poly"]
    try:
        if not circs and len(polys) == 1 and len(polys[0]) == 4:
            pts = polys[0]
            e1 = (pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
            e2 = (pts[2][0] - pts[1][0], pts[2][1] - pts[1][1])
            w, h = math.hypot(*e1), math.hypot(*e2)
            ang = math.degrees(math.atan2(e1[1], e1[0])) % 180.0
            if ang >= 90.0:  # canonicalise: keep w along the stored angle
                ang -= 90.0
                w, h = h, w
            return ("rect", w, h, ang, 0.0)
        if len(circs) == 2 and len(polys) == 1:
            r = circs[0]["r"]
            dx = circs[1]["x"] - circs[0]["x"]
            dy = circs[1]["y"] - circs[0]["y"]
            L = math.hypot(dx, dy)
            ang = math.degrees(math.atan2(dy, dx)) % 180.0
            return ("oval", L + 2 * r, 2 * r, ang, 0.0)
        if len(circs) == 4 and len(polys) == 2:
            r = circs[0]["r"]
            # orientation from the longest edge of the first poly
            best = (0.0, 0.0)
            for pts in polys:
                n = len(pts)
                for i in range(n):
                    a, b = pts[i], pts[(i + 1) % n]
                    L = math.hypot(b[0] - a[0], b[1] - a[1])
                    if L > best[0]:
                        best = (L, math.degrees(math.atan2(b[1] - a[1],
                                                           b[0] - a[0])))
            ang = best[1] % 90.0
            ca, sa = math.cos(math.radians(-ang)), math.sin(math.radians(-ang))
            us = [c["x"] * ca - c["y"] * sa for c in circs]
            vs = [c["x"] * sa + c["y"] * ca for c in circs]
            w = (max(us) - min(us)) + 2 * r
            h = (max(vs) - min(vs)) + 2 * r
            rr = min(0.5, r / min(w, h))
            return ("roundrect", w, h, ang, rr)
    except Exception as e:  # pragma: no cover
        warn.append(f"flash shape decode error at ({f['x']},{f['y']}): {e}")
    warn.append(f"flash at ({f['x']},{f['y']}) ap={t}: bbox-rect fallback")
    return ("rect", bw or 0.1, bh or 0.1, 0.0, 0.0)


def shape_bbox(shape, w, h, ang):
    """Axis-aligned bbox (width,height) of the shape rotated by ang."""
    c, s = abs(math.cos(math.radians(ang))), abs(math.sin(math.radians(ang)))
    if shape == "circle":
        return (w, w)
    if shape == "rect":
        return (w * c + h * s, w * s + h * c)
    if shape == "oval":
        r = h / 2.0
        return ((w - h) * c + 2 * r, (w - h) * s + 2 * r)
    if shape == "roundrect":
        # inner rect (w-2r, h-2r) rotated, +r all round
        # rratio-independent: use min dim * stored ratio implicitly via caller
        return (w * c + h * s, w * s + h * c)  # close enough upper bound
    raise ValueError(shape)


# ---------------------------------------------------------------- outline
def extract_outline(ec_layer, warn):
    """Isolate the closed loop(s) of the physical outline from Edge.Cuts.

    Endpoints are clustered with a 0.05 mm tolerance (the as-drawn outline
    has sub-0.02 mm joins); components smaller than 10 mm (caption text
    strokes) are dropped; a component with exactly two open ends closer
    than 1.5 mm is closed with a synthetic bridge line (the LHS/RHS GM4
    outline has one such drawing gap on the right edge).

    Returns (edges, loops, dropped): edges is a list of ('line'|'arc', obj)
    to emit (synthetic bridges included), loops ordered point lists (gerber
    frame, arcs sampled)."""
    items = [("line", ln) for ln in ec_layer["lines"]] + \
            [("arc", a) for a in ec_layer["arcs"]]

    # --- endpoint clustering (0.05 mm)
    TOLC = 0.05
    cells = {}
    centers = []

    def cluster(x, y):
        ci = int(x / TOLC)
        cj = int(y / TOLC)
        for gi in (ci - 1, ci, ci + 1):
            for gj in (cj - 1, cj, cj + 1):
                for idx in cells.get((gi, gj), ()):
                    cx, cy = centers[idx]
                    if math.hypot(cx - x, cy - y) <= TOLC:
                        return idx
        idx = len(centers)
        centers.append((x, y))
        cells.setdefault((ci, cj), []).append(idx)
        return idx

    ends = []
    for t, o in items:
        ends.append((cluster(o["x1"], o["y1"]), cluster(o["x2"], o["y2"])))

    parent = {}

    def find(a):
        parent.setdefault(a, a)
        r = a
        while parent[r] != r:
            r = parent[r]
        while parent[a] != r:
            parent[a], a = r, parent[a]
        return r

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for a, b in ends:
        union(a, b)
    comps = defaultdict(list)
    for i, (a, b) in enumerate(ends):
        comps[find(a)].append(i)

    kept, loops, dropped = [], [], 0
    for root, idxs in comps.items():
        xs = [c for i in idxs for c in (items[i][1]["x1"], items[i][1]["x2"])]
        ys = [c for i in idxs for c in (items[i][1]["y1"], items[i][1]["y2"])]
        if (max(xs) - min(xs) <= 10) and (max(ys) - min(ys) <= 10):
            dropped += len(idxs)
            continue
        deg = defaultdict(int)
        for i in idxs:
            deg[ends[i][0]] += 1
            deg[ends[i][1]] += 1
        odd = [n for n, v in deg.items() if v % 2]
        comp_items = [items[i] for i in idxs]
        comp_ends = [ends[i] for i in idxs]
        if odd:
            if len(odd) == 2:
                (x1, y1), (x2, y2) = centers[odd[0]], centers[odd[1]]
                gap = math.hypot(x2 - x1, y2 - y1)
                if gap <= 1.5:
                    warn.append(f"outline: {gap:.3f} mm drawing gap bridged "
                                f"at ({x1:.3f},{y1:.3f})-({x2:.3f},{y2:.3f})")
                    br = {"x1": x1, "y1": y1, "x2": x2, "y2": y2,
                          "ap": {"diameter": 0.1}}
                    comp_items.append(("line", br))
                    comp_ends.append((odd[0], odd[1]))
                else:
                    warn.append(f"outline: open component ({gap:.3f} mm gap "
                                "> 1.5) left unclosed")
            else:
                warn.append(f"outline: component with {len(odd)} open ends "
                            "kept unclosed")
        kept.extend(comp_items)
        # order into a polygon (sample arcs)
        adj = defaultdict(list)
        for i, (a, b) in enumerate(comp_ends):
            adj[a].append((i, b))
            adj[b].append((i, a))
        used = set()
        start = comp_ends[0][0]
        node = start
        poly = []
        while True:
            nxt = None
            for i, other in adj[node]:
                if i not in used:
                    nxt = (i, other)
                    break
            if nxt is None:
                break
            used.add(nxt[0])
            t, o = comp_items[nxt[0]]
            if t == "line":
                pts = [(o["x1"], o["y1"]), (o["x2"], o["y2"])]
            else:
                pts = cne.arc_polyline(o)
            # orient: start of pts should sit at the current node
            cx, cy = centers[node]
            d0 = math.hypot(pts[0][0] - cx, pts[0][1] - cy)
            d1 = math.hypot(pts[-1][0] - cx, pts[-1][1] - cy)
            if d1 < d0:
                pts = pts[::-1]
            poly.extend(pts[:-1])
            node = nxt[1]
            if node == start:
                break
        if len(poly) >= 3:
            loops.append(poly)
    return kept, loops, dropped


# ------------------------------------------------------------ zone filling
def fracture(geom):
    """Shapely (Multi)Polygon -> list of hole-free polygons (tiny slit cuts)."""
    from shapely.geometry import box
    out, todo = [], [geom]
    guard = 0
    while todo:
        g = todo.pop()
        guard += 1
        if guard > 10000:
            raise RuntimeError("fracture did not terminate")
        if g.is_empty:
            continue
        if g.geom_type == "MultiPolygon" or g.geom_type == "GeometryCollection":
            todo.extend(gg for gg in g.geoms if gg.geom_type == "Polygon")
            continue
        if g.geom_type != "Polygon":
            continue
        if not g.interiors:
            out.append(g)
            continue
        ring = min(g.interiors, key=lambda r: min(c[0] for c in r.coords))
        px, py = min(ring.coords, key=lambda c: c[0])[:2]
        eps = 5e-4
        cut = box(g.bounds[0] - 1.0, py - eps, px + eps, py + eps)
        todo.append(g.difference(cut))
    return out


def build_zones(board, layer, regions, name_by_id, net_of_root, warn):
    """Returns list of dicts: outline pts, layer, net, filled polygons."""
    from shapely.geometry import Polygon

    labels, x0, y0, nx, ny = board.pour_labels.get(layer, (None, 0, 0, 0, 0))

    def label_at(px, py):
        if labels is None:
            return 0
        i = int((px - x0) / cne.RASTER)
        j = int((py - y0) / cne.RASTER)
        if 0 <= i < nx and 0 <= j < ny:
            return int(labels[j, i])
        return 0

    acc = []  # [region_idx, shapely geom]
    for ri, reg in enumerate(regions):
        pts = _dedupe(reg["points"])
        if len(pts) < 3:
            continue
        poly = Polygon(pts)
        if not poly.is_valid:
            poly = poly.buffer(0)
        if reg.get("dark", True):
            acc.append([ri, poly, pts])
        else:
            for ent in acc:
                ent[1] = ent[1].difference(poly)
    zones = []
    for ri, geom, pts in acc:
        # net: label sampled inside the remaining copper
        net_id = 0
        lab = 0
        if not geom.is_empty:
            rp = geom.representative_point()
            lab = label_at(rp.x, rp.y)
        if lab:
            net_id = net_of_root(("P", layer, lab))
        if net_id == 0 and not geom.is_empty:
            warn.append(f"zone {layer}#{ri}: no net (isolated pour)")
        fills = []
        for poly in fracture(geom):
            ext = [(round(x, 5), round(y, 5)) for x, y in poly.exterior.coords]
            if len(ext) > 1 and ext[0] == ext[-1]:
                ext.pop()
            if len(ext) >= 3:
                fills.append(ext)
        zones.append({"layer": layer, "net": net_id,
                      "name": name_by_id.get(net_id, ""),
                      "outline": pts, "fills": fills})
    return zones


# --------------------------------------------------------------- generator
def run_engine(side):
    b = cne.Board(side)
    b.solve_transform()
    b.assign_pads()
    b.build_graph()
    b.collect_nets()
    b.resolve_perimeter()
    b.finalize()
    b.name_and_validate()
    return b


def generate(side):
    b = run_engine(side)
    inv = b.inv
    layers = inv["layers"]
    ts = TS(side)
    warn = []
    report = {"side": side, "warnings": warn}

    # ---- nets: assert identity with the committed netlist json
    committed = json.loads((NETDIR / f"copper_netlist_{side.lower()}.json").read_text())
    by_id = {n["id"]: n for n in committed["nets"]}
    assert len(by_id) == len(b.net_ids)
    for root, nid in b.net_ids.items():
        jn = by_id[nid]
        assert sorted(set(map(tuple, b.netgroups[root]["pads"]))) == \
            sorted(map(tuple, jn["pads"])), f"net {nid} pads diverge"
        assert (b.names.get(root) or None) == (jn["name"] or None), \
            f"net {nid} name diverges"
    name_by_id = {nid: (by_id[nid]["name"] or f"NET{nid}") for nid in by_id}
    name_by_id[0] = ""

    def net_of_root(node):
        return b.net_ids.get(b.uf.find(node), 0)

    def net_clause(nid):
        return f'(net {nid} "{name_by_id[nid]}")' if nid else "(net 0 \"\")"

    comp_layer = b.comp_layer
    holes_p = b.holes_p
    small_holes = [h for h in holes_p if h["dia"] <= VIA_MAX]
    big_holes = [h for h in holes_p if h["dia"] > VIA_MAX]

    def hole_near(f, holes, tol=0.08):
        for h in holes:
            if abs(f["x"] - h["x"]) < tol and abs(f["y"] - h["y"]) < tol:
                return h
        return None

    # ---- mask / paste pairing (empirical margins + per-pad presence)
    def flash_index(layer):
        return layers.get(layer, {}).get("flashes", [])

    def nearest_flash(coll, x, y, tol=0.1):
        best, bd = None, tol
        for f in coll:
            d = math.hypot(f["x"] - x, f["y"] - y)
            if d < bd:
                best, bd = f, d
        return best

    mask_deltas, paste_deltas = [], []

    def mask_paste_for(f, cu_layer, w, h):
        """Pair the pad flash with its mask/paste flash; margins are compared
        in the pad's own (rotated) shape frame, not the bounding box."""
        mk = nearest_flash(flash_index(cu_layer.replace("Cu", "Mask")), f["x"], f["y"])
        ps = nearest_flash(flash_index(cu_layer.replace("Cu", "Paste")), f["x"], f["y"])
        md = pd = None
        sink = []
        if mk:
            _s, mw, mh, _a, _r = flash_shape(mk, sink)
            md = round(min(mw - w, mh - h) / 2, 4)
            mask_deltas.append(md)
        if ps:
            _s, pw, ph, _a, _r = flash_shape(ps, sink)
            pd = round(min(pw - w, ph - h) / 2, 4)
            paste_deltas.append(pd)
        return (mk is not None, ps is not None, md, pd)

    # ---- pad s-expression builder
    def pad_sexpr(num, f, nid, fp_pos_g, fp_rot, cu_layer, indent="    "):
        """f = flash dict on cu_layer; returns (pad line, is_thru_hole)."""
        shape, w, h, ang, rr = flash_shape(f, warn)
        gx, gy = f["x"], f["y"]
        kgx, kgy = KX(gx), KY(gy)
        kfx, kfy = KX(fp_pos_g[0]), KY(fp_pos_g[1])
        lx, ly = rotk(-fp_rot, kgx - kfx, kgy - kfy)
        hole = hole_near(f, big_holes, tol=0.1)
        has_mask, has_paste, md, pd = mask_paste_for(f, cu_layer, w, h)
        margins = ""
        if has_mask and md is not None and abs(md - 0.05) > 0.02:
            margins += f" (solder_mask_margin {fmt(md)})"
        if has_paste and pd is not None and abs(pd) > 0.02:
            margins += f" (solder_paste_margin {fmt(pd)})"
        side_pfx = "F" if cu_layer == "F.Cu" else "B"
        ang_out = ang % 360.0
        at = f"(at {fmt(lx)} {fmt(ly)}" + \
            (f" {fmt(ang_out)})" if abs(ang_out) > 1e-9 else ")")
        kshape = {"circle": "circle", "rect": "rect", "oval": "oval",
                  "roundrect": "roundrect"}[shape]
        opts = f" (roundrect_rratio {fmt(rr)})" if shape == "roundrect" else ""
        if hole:
            lay = '(layers "*.Cu" "*.Mask")'
            drill = f" (drill {fmt(hole['dia'])}"
            dx, dy = KX(hole["x"]) - kgx, KY(hole["y"]) - kgy
            if math.hypot(dx, dy) > 0.005:
                ox, oy = rotk(-ang_out, dx, dy)
                drill += f" (offset {fmt(ox)} {fmt(oy)})"
            drill += ")"
            kind = "thru_hole"
            extra = " (remove_unused_layers)"
        else:
            ls = [f'"{side_pfx}.Cu"']
            if has_paste:
                ls.append(f'"{side_pfx}.Paste"')
            if has_mask:
                ls.append(f'"{side_pfx}.Mask"')
            lay = f'(layers {" ".join(ls)})'
            drill = ""
            kind = "smd"
            extra = ""
        line = (f'{indent}(pad "{num}" {kind} {kshape} {at} '
                f'(size {fmt(w)} {fmt(h)}){drill} {lay}{opts}{margins}{extra} '
                f'{net_clause(nid)} (tstamp {ts()}))')
        return line, bool(hole)

    # ---- footprints for PnP components
    out_fp = []
    counts = defaultdict(int)
    pads_by_ref = defaultdict(list)
    for i, (ref, pin) in b.pads.items():
        pads_by_ref[ref].append((pin, i))
    comp_flashes = layers[comp_layer]["flashes"]
    fp_layer = comp_layer  # footprints live on the component side
    silk_layer = "F.SilkS" if fp_layer == "F.Cu" else "B.SilkS"
    fab_layer = "F.Fab" if fp_layer == "F.Cu" else "B.Fab"

    for c in b.pnp:
        ref = c["ref"]
        cx, cy = b.comp_pos(c)
        theta = (b.rsign * c["rot"] + b.roff) % 360.0
        plist = sorted(pads_by_ref.get(ref, []),
                       key=lambda t: (len(t[0]), t[0]))
        any_hole = False
        plines = []
        for pin, fi in plist:
            line, hol = pad_sexpr(pin, comp_flashes[fi], net_of_root(("F", comp_layer, fi)),
                                  (cx, cy), theta, comp_layer)
            plines.append(line)
            any_hole |= hol
        attr = "through_hole" if any_hole else "smd"
        mirr = " (justify mirror)" if fp_layer == "B.Cu" else ""
        out_fp.append("\n".join([
            f'  (footprint "R4:{c["fp"]}" (layer "{fp_layer}") (tstamp {ts()})',
            f'    (at {fmt(KX(cx))} {fmt(KY(cy))} {fmt(theta)})',
            f'    (attr {attr})',
            f'    (fp_text reference "{ref}" (at 0 0) (layer "{silk_layer}") hide',
            f'      (effects (font (size 0.3 0.3) (thickness 0.05)){mirr}) (tstamp {ts()}))',
            f'    (fp_text value "{c["comment"]}" (at 0 0.6) (layer "{fab_layer}") hide',
            f'      (effects (font (size 0.3 0.3) (thickness 0.05)){mirr}) (tstamp {ts()}))',
        ] + plines + ["  )"]))
        counts["footprints"] += 1
        counts["pads"] += len(plines)
        if not plist:
            warn.append(f"{ref} ({c['fp']}): footprint emitted with no pads")

    # ---- board-level (unowned) pads and NPTH
    accounted = {}  # (layer, idx) -> category, for validation bookkeeping
    for i in range(len(comp_flashes)):
        if b.is_via_flash.get(i):
            accounted[(comp_layer, i)] = "via_ring"
        elif i in b.pads:
            accounted[(comp_layer, i)] = "component_pad"
    pad_n = 0
    for L in COPPER:
        for i, f in enumerate(layers.get(L, {}).get("flashes", [])):
            if (L, i) in accounted:
                continue
            if hole_near(f, small_holes):
                accounted[(L, i)] = "via_ring"
                continue
            bigh = hole_near(f, big_holes, tol=0.1)
            if bigh and L != comp_layer:
                accounted[(L, i)] = "thru_ring_other_layer"
                continue
            # standalone pad footprint
            pad_n += 1
            nid = net_of_root(("F", L, i))
            nm = name_by_id.get(nid) or f"N{nid}"
            ref = f"PAD{pad_n}"
            theta = 0.0
            line, hol = pad_sexpr("1", f, nid, (f["x"], f["y"]), theta, L)
            if L in ("In1.Cu", "In2.Cu"):
                warn.append(f"unowned flash on {L} at ({f['x']},{f['y']}) "
                            "emitted as SMD pad on inner layer")
            lay_fp = "F.Cu" if L in ("F.Cu", "In1.Cu", "In2.Cu") else "B.Cu"
            silk = "F.SilkS" if lay_fp == "F.Cu" else "B.SilkS"
            out_fp.append("\n".join([
                f'  (footprint "R4:PAD_{nm}_{pad_n}" (layer "{lay_fp}") (tstamp {ts()})',
                f'    (at {fmt(KX(f["x"]))} {fmt(KY(f["y"]))})',
                '    (attr smd exclude_from_pos_files exclude_from_bom)',
                f'    (fp_text reference "{ref}" (at 0 0) (layer "{silk}") hide',
                '      (effects (font (size 0.3 0.3) (thickness 0.05))) '
                f'(tstamp {ts()}))',
                f'    (fp_text value "{nm}" (at 0 0.6) (layer "F.Fab") hide',
                '      (effects (font (size 0.3 0.3) (thickness 0.05))) '
                f'(tstamp {ts()}))',
                line,
                "  )"]))
            accounted[(L, i)] = "board_pad"
            counts["footprints"] += 1
            counts["pads"] += 1
            counts["board_pad_footprints"] += 1
    # non-plated holes
    for k, h in enumerate(layers.get("drill_nonplated", {}).get("holes", []), 1):
        out_fp.append("\n".join([
            f'  (footprint "R4:NPTH_{fmt(h["dia"])}mm" (layer "F.Cu") (tstamp {ts()})',
            f'    (at {fmt(KX(h["x"]))} {fmt(KY(h["y"]))})',
            '    (attr through_hole exclude_from_pos_files exclude_from_bom)',
            f'    (fp_text reference "NP{k}" (at 0 0) (layer "F.SilkS") hide',
            f'      (effects (font (size 0.3 0.3) (thickness 0.05))) (tstamp {ts()}))',
            f'    (fp_text value "NPTH" (at 0 0.6) (layer "F.Fab") hide',
            f'      (effects (font (size 0.3 0.3) (thickness 0.05))) (tstamp {ts()}))',
            f'    (pad "" np_thru_hole circle (at 0 0) (size {fmt(h["dia"])} '
            f'{fmt(h["dia"])}) (drill {fmt(h["dia"])}) (layers "F.Mask" "B.Mask") '
            f'(tstamp {ts()}))',
            "  )"]))
        counts["footprints"] += 1
        counts["npth"] += 1

    # ---- outline (for graphics filtering) BEFORE tracks (caption counting)
    ec = layers["Edge.Cuts"]
    edge_items, loops, edge_dropped = extract_outline(ec, warn)
    report["outline"] = {
        "loops": len(loops),
        "edge_objects_kept": len(edge_items),
        "edge_objects_dropped_captions": edge_dropped,
    }
    from shapely.geometry import Polygon, Point
    from shapely.prepared import prep
    outline_polys = [Polygon(lp).buffer(0) for lp in loops]
    outline_polys.sort(key=lambda p: p.area, reverse=True)
    if outline_polys:
        bb = outline_polys[0].bounds
        report["outline"]["bbox_mm"] = [round(bb[2] - bb[0], 3),
                                        round(bb[3] - bb[1], 3)]
    board_shape = outline_polys[0] if outline_polys else None
    board_prep = prep(board_shape.buffer(0.3)) if board_shape else None

    def inside(x, y):
        return board_prep is not None and board_prep.contains(Point(x, y))

    # ---- tracks & arcs
    out_tracks = []
    net0 = defaultdict(int)
    captions = defaultdict(int)
    for L in COPPER:
        lay = layers.get(L)
        if not lay:
            continue
        for i, ln in enumerate(lay["lines"]):
            w = (ln["ap"] or {}).get("diameter", 0.1)
            nid = net_of_root(("L", L, i))
            if nid == 0:
                net0["segments"] += 1
            mx, my = (ln["x1"] + ln["x2"]) / 2, (ln["y1"] + ln["y2"]) / 2
            if not inside(mx, my):
                captions[L] += 1
            out_tracks.append(
                f'  (segment (start {xy(ln["x1"], ln["y1"])}) '
                f'(end {xy(ln["x2"], ln["y2"])}) (width {fmt(w)}) '
                f'(layer "{L}") (net {nid}) (tstamp {ts()}))')
            counts["segments"] += 1
        for i, a in enumerate(lay["arcs"]):
            w = (a["ap"] or {}).get("diameter", 0.1)
            nid = net_of_root(("A", L, i))
            if nid == 0:
                net0["arcs"] += 1
            pts = cne.arc_polyline(a)
            mid = pts[len(pts) // 2]
            out_tracks.append(
                f'  (arc (start {xy(a["x1"], a["y1"])}) (mid {xy(mid[0], mid[1])}) '
                f'(end {xy(a["x2"], a["y2"])}) (width {fmt(w)}) '
                f'(layer "{L}") (net {nid}) (tstamp {ts()}))')
            counts["arcs"] += 1
    report["copper_objects_outside_outline_captions"] = dict(captions)

    # ---- vias
    out_vias = []
    all_cu_flashes = [(L, f) for L in COPPER
                      for f in layers.get(L, {}).get("flashes", [])]
    for hi, h in enumerate(holes_p):
        if h["dia"] > VIA_MAX:
            continue
        ring = 0.0
        for L, f in all_cu_flashes:
            if abs(f["x"] - h["x"]) < 0.08 and abs(f["y"] - h["y"]) < 0.08:
                ring = max(ring, f.get("bw") or 0)
        if ring == 0.0:
            ring = h["dia"] + 0.2
            warn.append(f"via at ({h['x']},{h['y']}): no ring flash, "
                        f"size defaulted to {ring}")
        nid = net_of_root(("V", hi))
        if nid == 0:
            net0["vias"] += 1
        out_vias.append(
            f'  (via (at {xy(h["x"], h["y"])}) (size {fmt(ring)}) '
            f'(drill {fmt(h["dia"])}) (layers "F.Cu" "B.Cu") '
            f'(remove_unused_layers) (net {nid}) (tstamp {ts()}))')
        counts["vias"] += 1

    # ---- zones
    out_zones = []
    for L in COPPER:
        lay = layers.get(L)
        if not lay:
            continue
        for z in build_zones(b, L, lay["regions"], name_by_id, net_of_root, warn):
            if z["net"] == 0:
                net0["zones"] += 1
            body = [
                f'  (zone (net {z["net"]}) (net_name "{z["name"]}") '
                f'(layer "{z["layer"]}") (tstamp {ts()}) (hatch edge 0.5)',
                '    (connect_pads (clearance 0.2))',
                '    (min_thickness 0.1) (filled_areas_thickness no)',
                '    (fill yes (thermal_gap 0.2) (thermal_bridge_width 0.3))',
                '    (polygon (pts',
            ]
            body += [f'      (xy {xy(x, y)})' for x, y in z["outline"]]
            body.append('    ))')
            for fill in z["fills"]:
                body.append(f'    (filled_polygon (layer "{z["layer"]}") (pts')
                body += [f'      (xy {xy(x, y)})' for x, y in fill]
                body.append('    ))')
            body.append('  )')
            out_zones.append("\n".join(body))
            counts["zones"] += 1
            counts["zone_fill_islands"] += len(z["fills"])

    # ---- graphics: edge, silk, mask/paste drawn lines
    out_gfx = []
    for t, o in edge_items:
        w = (o["ap"] or {}).get("diameter", 0.1)
        if t == "line":
            out_gfx.append(
                f'  (gr_line (start {xy(o["x1"], o["y1"])}) '
                f'(end {xy(o["x2"], o["y2"])}) '
                f'(stroke (width {fmt(w)}) (type solid)) (layer "Edge.Cuts") '
                f'(tstamp {ts()}))')
        else:
            pts = cne.arc_polyline(o)
            mid = pts[len(pts) // 2]
            out_gfx.append(
                f'  (gr_arc (start {xy(o["x1"], o["y1"])}) '
                f'(mid {xy(mid[0], mid[1])}) (end {xy(o["x2"], o["y2"])}) '
                f'(stroke (width {fmt(w)}) (type solid)) (layer "Edge.Cuts") '
                f'(tstamp {ts()}))')
        counts["edge_objects"] += 1

    gfx_dropped = defaultdict(int)
    for src, dst in (("F.Silkscreen", "F.SilkS"), ("B.Silkscreen", "B.SilkS"),
                     ("F.Mask", "F.Mask"), ("B.Mask", "B.Mask"),
                     ("F.Paste", "F.Paste"), ("B.Paste", "B.Paste")):
        lay = layers.get(src)
        if not lay:
            continue
        for ln in lay["lines"]:
            mx, my = (ln["x1"] + ln["x2"]) / 2, (ln["y1"] + ln["y2"]) / 2
            if not inside(mx, my):
                gfx_dropped[src] += 1
                continue
            w = (ln["ap"] or {}).get("diameter", 0.1)
            out_gfx.append(
                f'  (gr_line (start {xy(ln["x1"], ln["y1"])}) '
                f'(end {xy(ln["x2"], ln["y2"])}) '
                f'(stroke (width {fmt(w)}) (type solid)) (layer "{dst}") '
                f'(tstamp {ts()}))')
            counts[f"gfx_{dst}"] += 1
        for a in lay["arcs"]:
            mx, my = (a["x1"] + a["x2"]) / 2, (a["y1"] + a["y2"]) / 2
            if not inside(mx, my):
                gfx_dropped[src] += 1
                continue
            w = (a["ap"] or {}).get("diameter", 0.1)
            pts = cne.arc_polyline(a)
            mid = pts[len(pts) // 2]
            out_gfx.append(
                f'  (gr_arc (start {xy(a["x1"], a["y1"])}) '
                f'(mid {xy(mid[0], mid[1])}) (end {xy(a["x2"], a["y2"])}) '
                f'(stroke (width {fmt(w)}) (type solid)) (layer "{dst}") '
                f'(tstamp {ts()}))')
            counts[f"gfx_{dst}"] += 1
        # unmatched mask/paste flashes inside the board (pad margins cover
        # the matched ones)
        if "Mask" in src or "Paste" in src:
            cu = "F.Cu" if src.startswith("F") else "B.Cu"
            cu_flashes = layers.get(cu, {}).get("flashes", [])
            npth = layers.get("drill_nonplated", {}).get("holes", [])
            for f in lay["flashes"]:
                if nearest_flash(cu_flashes, f["x"], f["y"], tol=0.2) is not None:
                    continue
                if any(abs(f["x"] - h["x"]) < 0.2 and abs(f["y"] - h["y"]) < 0.2
                       for h in npth):
                    continue  # NPTH mask relief handled by the NPTH pad
                if not inside(f["x"], f["y"]):
                    gfx_dropped[src] += 1
                    continue
                shape, w, h, ang, rr = flash_shape(f, warn)
                if shape == "circle":
                    out_gfx.append(
                        f'  (gr_circle (center {xy(f["x"], f["y"])}) '
                        f'(end {xy(f["x"] + w / 2, f["y"])}) '
                        f'(stroke (width 0) (type solid)) (fill solid) '
                        f'(layer "{dst}") (tstamp {ts()}))')
                else:
                    ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
                    cxy = []
                    for sxx, syy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        ux, uy = sxx * w / 2, syy * h / 2
                        cxy.append((f["x"] + ux * ca - uy * sa,
                                    f["y"] + ux * sa + uy * ca))
                    pts_s = " ".join(f"(xy {xy(px, py)})" for px, py in cxy)
                    out_gfx.append(
                        f'  (gr_poly (pts {pts_s}) (stroke (width 0) '
                        f'(type solid)) (fill solid) (layer "{dst}") '
                        f'(tstamp {ts()}))')
                counts[f"gfx_{dst}_flash"] += 1
    report["graphics_dropped_outside_outline"] = dict(gfx_dropped)

    # ---- margins report
    def stats(v):
        if not v:
            return None
        v = sorted(v)
        return {"n": len(v), "median": v[len(v) // 2],
                "min": v[0], "max": v[-1]}
    report["mask_margin_per_side_mm"] = stats(mask_deltas)
    report["paste_margin_per_side_mm"] = stats(paste_deltas)
    report["net0"] = dict(net0)
    report["counts"] = dict(counts)
    report["nets"] = len(by_id)

    # ---- assemble file
    nets_sect = ['  (net 0 "")'] + [
        f'  (net {nid} "{name_by_id[nid]}")' for nid in sorted(by_id)]
    header = f"""(kicad_pcb (version 20221018) (generator r4_gerber_reconstruction)

  (general
    (thickness 0.4)
  )

  (paper "A4")
  (title_block
    (title "Reid Orthotic v2 R4 {side}")
    (date "2024-09-05")
    (rev "R4")
    (company "Calceus Health")
    (comment 1 "As-built reconstruction from the R4 production gerbers")
    (comment 2 "Generated by hardware/r4-kicad/tools/generate_kicad_pcb.py")
    (comment 3 "Nets from copper_netlist_{side.lower()}.json (copper connectivity engine)")
  )

  (layers
    (0 "F.Cu" signal)
    (1 "In1.Cu" signal)
    (2 "In2.Cu" signal)
    (31 "B.Cu" signal)
    (34 "B.Paste" user)
    (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings")
    (41 "Cmts.User" user "User.Comments")
    (44 "Edge.Cuts" user)
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.CrtYd" user "F.Courtyard")
    (48 "B.Fab" user)
    (49 "F.Fab" user)
  )

  (setup
    (stackup
      (layer "F.SilkS" (type "Top Silk Screen") (color "White"))
      (layer "F.Paste" (type "Top Solder Paste"))
      (layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))
      (layer "F.Cu" (type "copper") (thickness 0.035))
      (layer "dielectric 1" (type "prepreg") (thickness 0.0999) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "In1.Cu" (type "copper") (thickness 0.0152))
      (layer "dielectric 2" (type "core") (thickness 0.0998) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "In2.Cu" (type "copper") (thickness 0.0152))
      (layer "dielectric 3" (type "prepreg") (thickness 0.0999) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "B.Cu" (type "copper") (thickness 0.035))
      (layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))
      (layer "B.Paste" (type "Bottom Solder Paste"))
      (layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
      (copper_finish "ENIG")
      (dielectric_constraints no)
    )
    (pad_to_mask_clearance 0.05)
    (pad_to_paste_clearance 0)
    (aux_axis_origin 0 0)
    (grid_origin 0 0)
  )

"""
    doc = (header + "\n".join(nets_sect) + "\n\n" +
           "\n".join(out_fp) + "\n\n" +
           "\n".join(out_gfx) + "\n\n" +
           "\n".join(out_tracks) + "\n\n" +
           "\n".join(out_vias) + "\n\n" +
           "\n".join(out_zones) + "\n)\n")
    PCBDIR.mkdir(exist_ok=True)
    dest = PCBDIR / f"Reid_Orthotic_v2_{side}.kicad_pcb"
    dest.write_text(doc)
    report["file"] = str(dest)
    report["file_kb"] = round(len(doc) / 1024, 1)

    # minimal project file: Default net class, 0.1 mm clearance
    pro = {
        "board": {"design_settings": {"rules": {
            "min_clearance": 0.1, "min_track_width": 0.1,
            "min_via_diameter": 0.4, "min_via_hole": 0.2}}},
        "net_settings": {"classes": [{
            "name": "Default", "clearance": 0.1, "track_width": 0.15,
            "via_diameter": 0.4, "via_drill": 0.2, "bus_width": 12,
            "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
            "diff_pair_width": 0.2, "line_style": 0,
            "microvia_diameter": 0.3, "microvia_drill": 0.1,
            "pcb_color": "rgba(0, 0, 0, 0.000)",
            "schematic_color": "rgba(0, 0, 0, 0.000)", "wire_width": 6}],
            "meta": {"version": 3}},
        "meta": {"filename": dest.with_suffix(".kicad_pro").name, "version": 1},
    }
    dest.with_suffix(".kicad_pro").write_text(json.dumps(pro, indent=2) + "\n")

    return b, report, accounted


# -------------------------------------------------------------- validation
def sexpr_parse(text):
    import re
    toks = re.findall(r'"(?:[^"\\]|\\.)*"|[()]|[^\s()"]+', text)
    pos = 0

    def parse():
        nonlocal pos
        assert toks[pos] == "("
        pos += 1
        out = []
        while toks[pos] != ")":
            if toks[pos] == "(":
                out.append(parse())
            else:
                t = toks[pos]
                if t.startswith('"'):
                    t = t[1:-1]
                out.append(t)
                pos += 1
        pos += 1
        return out
    return parse()


def validate(side, board, accounted):
    dest = PCBDIR / f"Reid_Orthotic_v2_{side}.kicad_pcb"
    doc = sexpr_parse(dest.read_text())
    rep = {"side": side}
    inv = board.inv
    layers = inv["layers"]

    nets = {}
    counts = defaultdict(int)
    pads_global = []   # (layer_set, x, y, bbox_w, bbox_h)
    segs = defaultdict(list)
    for node in doc:
        if not isinstance(node, list):
            continue
        tag = node[0]
        if tag == "net":
            nets[int(node[1])] = node[2] if len(node) > 2 else ""
        elif tag == "footprint":
            counts["footprints"] += 1
            at = next(n for n in node if isinstance(n, list) and n[0] == "at")
            fx, fy = float(at[1]), float(at[2])
            frot = float(at[3]) if len(at) > 3 else 0.0
            for p in node:
                if not (isinstance(p, list) and p[0] == "pad"):
                    continue
                counts["pads"] += 1
                pat = next(n for n in p if isinstance(n, list) and n[0] == "at")
                lx, ly = float(pat[1]), float(pat[2])
                pang = float(pat[3]) if len(pat) > 3 else 0.0
                size = next(n for n in p if isinstance(n, list) and n[0] == "size")
                w, h = float(size[1]), float(size[2])
                lay = next(n for n in p if isinstance(n, list) and n[0] == "layers")
                netn = next((n for n in p if isinstance(n, list) and n[0] == "net"),
                            None)
                nid = int(netn[1]) if netn else 0
                assert nid in nets or nid == 0, f"pad net {nid} undeclared"
                gx, gy = rotk(frot, lx, ly)
                shape = p[3]  # (pad "n" <kind> <shape> ...)
                rr = 0.0
                rrn = next((n for n in p if isinstance(n, list) and
                            n[0] == "roundrect_rratio"), None)
                if rrn:
                    rr = float(rrn[1])
                bw, bh = pad_bbox(shape, w, h, pang, rr)
                pads_global.append((set(lay[1:]), fx + gx, fy + gy, bw, bh,
                                    p[1], nid))
        elif tag == "segment":
            counts["segments"] += 1
            st = next(n for n in node if isinstance(n, list) and n[0] == "start")
            en = next(n for n in node if isinstance(n, list) and n[0] == "end")
            wd = next(n for n in node if isinstance(n, list) and n[0] == "width")
            ly = next(n for n in node if isinstance(n, list) and n[0] == "layer")
            nid = int(next(n for n in node if isinstance(n, list) and
                           n[0] == "net")[1])
            assert nid in nets
            segs[ly[1]].append((float(st[1]), float(st[2]), float(en[1]),
                                float(en[2]), float(wd[1]), nid))
        elif tag == "arc":
            counts["track_arcs"] += 1
        elif tag == "via":
            counts["vias"] += 1
            nid = int(next(n for n in node if isinstance(n, list) and
                           n[0] == "net")[1])
            assert nid in nets
        elif tag == "zone":
            counts["zones"] += 1
            counts["zone_fills"] += sum(1 for n in node if isinstance(n, list)
                                        and n[0] == "filled_polygon")
            nid = int(next(n for n in node if isinstance(n, list) and
                           n[0] == "net")[1])
            assert nid in nets
        elif tag in ("gr_line", "gr_arc", "gr_circle", "gr_poly"):
            counts["graphics"] += 1
    rep["counts"] = dict(counts)
    rep["nets_declared"] = len(nets)

    # geometry round-trip on F.Cu
    #  pads: every non-via F.Cu flash must be matched by a pad covering F.Cu
    fcu_pads = [p for p in pads_global
                if "F.Cu" in p[0] or "*.Cu" in p[0]]
    flashes = layers["F.Cu"]["flashes"]
    small = [h for h in board.holes_p if h["dia"] <= VIA_MAX]
    maxdev = 0.0
    sizedev = 0.0
    dropped = []
    for i, f in enumerate(flashes):
        if any(abs(f["x"] - h["x"]) < 0.08 and abs(f["y"] - h["y"]) < 0.08
               for h in small):
            continue  # via ring -> represented by a via
        kx, ky = KX(f["x"]), KY(f["y"])
        best, bd = None, 0.3
        for p in fcu_pads:
            d = math.hypot(p[1] - kx, p[2] - ky)
            if d < bd:
                best, bd = p, d
        if best is None:
            dropped.append(("flash", f["x"], f["y"]))
            continue
        maxdev = max(maxdev, bd)
        if f.get("bw"):
            sizedev = max(sizedev,
                          abs(best[3] - f["bw"]), abs(best[4] - f["bh"]))
    #  segments
    inv_lines = layers["F.Cu"]["lines"]
    got = segs["F.Cu"]
    segdev = 0.0
    for ln in inv_lines:
        a = (KX(ln["x1"]), KY(ln["y1"]))
        bpt = (KX(ln["x2"]), KY(ln["y2"]))
        w = (ln["ap"] or {}).get("diameter", 0.1)
        best = None
        for s in got:
            d1 = max(math.hypot(s[0] - a[0], s[1] - a[1]),
                     math.hypot(s[2] - bpt[0], s[3] - bpt[1]))
            d2 = max(math.hypot(s[2] - a[0], s[3] - a[1]),
                     math.hypot(s[0] - bpt[0], s[1] - bpt[1]))
            d = min(d1, d2) + abs(s[4] - w)
            if best is None or d < best:
                best = d
        if best is None:
            dropped.append(("line", ln["x1"], ln["y1"]))
        else:
            segdev = max(segdev, best)
    rep["roundtrip"] = {
        "F.Cu_pad_center_max_dev_mm": round(maxdev, 6),
        "F.Cu_pad_bbox_max_dev_mm": round(sizedev, 6),
        "F.Cu_segment_max_dev_mm": round(segdev, 6),
        "n_F.Cu_segments_file_vs_inventory": [len(got), len(inv_lines)],
        "dropped_objects": dropped,
    }
    return rep


def pad_bbox(shape, w, h, ang, rr):
    c = abs(math.cos(math.radians(ang)))
    s = abs(math.sin(math.radians(ang)))
    if shape == "circle":
        return (w, w)
    if shape == "oval":
        r = min(w, h) / 2.0
        L = max(w, h)
        dx, dy = (L - 2 * r), 0.0
        if h > w:
            dx, dy = 0.0, (L - 2 * r)
        ex = abs(dx * c) + abs(dy * s)
        ey = abs(dx * s) + abs(dy * c)
        return (ex + 2 * r, ey + 2 * r)
    if shape == "roundrect":
        r = rr * min(w, h)
        iw, ih = w - 2 * r, h - 2 * r
        return (iw * c + ih * s + 2 * r, iw * s + ih * c + 2 * r)
    return (w * c + h * s, w * s + h * c)


def main():
    sides = sys.argv[1:] or ["LHS", "RHS"]
    full = {}
    for side in sides:
        b, report, accounted = generate(side)
        vrep = validate(side, b, accounted)
        report["validation"] = vrep
        full[side] = report
        print(json.dumps(report, indent=1, default=str))
    (PCBDIR / "generation_report.json").write_text(
        json.dumps(full, indent=1, default=str))


if __name__ == "__main__":
    main()
