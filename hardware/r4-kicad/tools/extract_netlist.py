#!/usr/bin/env python3
"""Extract the R4 netlist from the CircuitStudio schematic PDF (v4).

Model: every pin has ONE connection endpoint — the end of its black lead
segment that faces away from its symbol (ties broken by navy-wire touch).
Nets = navy-wire groups; pins join a net when their connection endpoint
touches it; two pins join directly when their endpoints coincide or one
endpoint lies on the other pin's lead. Same-ref internal shorts are
impossible by construction.

Markers: hidden text 'PI<ref>0<pin>' (pin), 'NL<name>' (label, '0'='_'),
visible words VSYS/VBAT/3V3/0V (power). Wires navy (0,0,.502), leads black,
symbol art other colors (ignored).
"""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import pymupdf

PDF = Path(__file__).resolve().parents[3] / "artefacts" / "Reid Orthotic v2 R4.pdf"
TOL = 1.2
POWER_NAMES = {"VSYS", "VBAT", "3V3", "0V"}

BOM_REFS = [
    "ANT1", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11",
    "C12", "C13", "C15", "C16", "C18", "C19", "C20", "C21", "C22", "C23",
    "C24", "C25", "C26", "C27", "C28", "C34", "C35", "C37", "C38", "C39",
    "C40", "C42", "C43", "D1", "D2", "F1", "J1", "J2", "J3", "J4", "J5",
    "L1", "L2", "L3", "Q1", "Q2", "Q3",
    *[f"R{i}" for i in list(range(1, 52)) + [54, 55, 56, 57, 58, 59, 60, 61, 62]],
    "U1", "U2", "U3", "U4", "U5", "U6", "U7", "U8", "U9", "X1", "X2",
    *[f"TP{i}" for i in range(1, 8)],
]
REFS_BY_LEN = sorted(BOM_REFS, key=len, reverse=True)
REF_ALT = "|".join(re.escape(r) for r in REFS_BY_LEN)
MARK_START = re.compile(r"PI(?:%s)0" % REF_ALT)


class UF:
    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        r = a
        while self.p[r] != r:
            r = self.p[r]
        while self.p[a] != r:
            self.p[a], a = r, self.p[a]
        return r

    def union(self, a, b):
        self.p[self.find(a)] = self.find(b)


def snap(x, y):
    return (round(x / TOL), round(y / TOL))


def split_markers(token):
    """Token may concatenate several PI markers; split at marker starts.
    Returns (ref, pin, frac) where frac is the sub-marker's center as a
    fraction of the token length (for proportional bbox slicing)."""
    starts = [m.start() for m in MARK_START.finditer(token)]
    if not starts:
        return []
    starts.append(len(token))
    out = []
    for i in range(len(starts) - 1):
        s, e = starts[i], starts[i + 1]
        chunk = token[s:e]
        body = chunk[2:]
        for ref in REFS_BY_LEN:
            if body.startswith(ref + "0"):
                pin = body[len(ref) + 1:]
                if pin:
                    out.append((ref, pin, (s + e) / 2 / len(token)))
                break
    return out


def seg_dist2(px, py, s):
    x1, y1, x2, y2 = s
    dx, dy = x2 - x1, y2 - y1
    L2 = dx * dx + dy * dy
    if L2 == 0:
        qx, qy = x1, y1
    else:
        t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / L2))
        qx, qy = x1 + t * dx, y1 + t * dy
    return (qx - px) ** 2 + (qy - py) ** 2


def decode_label(raw, visible):
    if raw in visible:
        return raw
    n = raw.count("0")
    if n and n <= 6:
        cands = set()
        for mask in range(1 << n):
            s, k = [], 0
            for ch in raw:
                if ch == "0":
                    s.append("_" if mask >> k & 1 else "0")
                    k += 1
                else:
                    s.append(ch)
            cands.add("".join(s))
        vis = cands & visible
        if len(vis) == 1:
            return vis.pop()
    out = list(raw)
    for i, ch in enumerate(out):
        if ch == "0" and 0 < i < len(out) - 1 and out[i - 1].isalpha() and raw[i + 1].isalpha():
            out[i] = "_"
    return "".join(out)


def extract_page(page, visible):
    wires, leads = [], []
    for d in page.get_drawings():
        col = tuple(round(c, 3) for c in (d["color"] or ()))
        if col == (0.0, 0.0, 0.502):
            dst = wires
        elif col == (0.0, 0.0, 0.0):
            dst = leads
        else:
            continue
        for it in d["items"]:
            if it[0] == "l":
                dst.append((it[1].x, it[1].y, it[2].x, it[2].y))

    pin_marks, labels, powers = [], [], []
    seen = set()
    for x0, y0, x1, y1, w, *_ in page.get_text("words"):
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        key = (w, round(cx), round(cy))
        if key in seen:
            continue
        seen.add(key)
        if "PI" in w:
            for ref, pin, frac in split_markers(w):
                mx = x0 + frac * (x1 - x0)
                my = y0 + frac * (y1 - y0)
                pin_marks.append(((ref, pin), mx, my))
        if w.startswith("NL") and "PI" not in w and len(w) > 2:
            labels.append((w[2:], cx, cy))
        elif w in POWER_NAMES:
            powers.append((w, cx, cy))

    # dedupe pin marks (each appears ~2x); keep one position per (ref,pin)
    bypin = {}
    for pp, cx, cy in pin_marks:
        bypin.setdefault(pp, []).append((cx, cy))

    # symbol centroid per ref (mean of its pin-marker positions)
    centroid = {}
    for (ref, _), poss in bypin.items():
        centroid.setdefault(ref, []).extend(poss)
    centroid = {r: (sum(p[0] for p in ps) / len(ps), sum(p[1] for p in ps) / len(ps))
                for r, ps in centroid.items()}

    # navy wire net groups
    uf = UF()
    for (x1, y1, x2, y2) in wires:
        uf.union(snap(x1, y1), snap(x2, y2))
    weps = [(snap(x1, y1), x1, y1) for x1, y1, _, _ in wires] + \
           [(snap(x2, y2), x2, y2) for _, _, x2, y2 in wires]
    for s in wires:
        a = snap(s[0], s[1])
        for key, px, py in weps:
            if seg_dist2(px, py, s) <= TOL * TOL:
                uf.union(key, a)

    def touching_wire_group(px, py):
        for s in wires:
            if seg_dist2(px, py, s) <= TOL * TOL:
                return uf.find(snap(s[0], s[1]))
        return None

    # pin -> lead: 1:1 greedy assignment per ref so duplicated/merged marker
    # positions can never give two pins of one component the same lead
    diags = {"pins_unattached": [], "labels_unattached": []}
    byref = defaultdict(list)
    for pp in bypin:
        byref[pp[0]].append(pp)
    lead_of = {}
    for ref, pps in byref.items():
        cands = []   # (dist, pp, lead_idx)
        for pp in pps:
            for cx, cy in bypin[pp]:
                for li, s in enumerate(leads):
                    d = seg_dist2(cx, cy, s)
                    if d < 16.0:
                        cands.append((d, pp, li))
        cands.sort(key=lambda t: t[0])
        used_pins, used_leads = set(), set()
        for d, pp, li in cands:
            if pp in used_pins or li in used_leads:
                continue
            used_pins.add(pp)
            used_leads.add(li)
            lead_of[pp] = leads[li]

    pininfo = {}   # (ref,pin) -> {ep, lead, wiregroup}
    for pp, poss in bypin.items():
        best = lead_of.get(pp)
        if best is None:
            # no lead: try wire directly at the marker
            g = None
            for cx, cy in poss:
                g = touching_wire_group(cx, cy)
                if g:
                    break
            if g is None:
                diags["pins_unattached"].append(f"{pp[0]}.{pp[1]}")
                continue
            pininfo[pp] = {"ep": poss[0], "lead": None, "wg": g}
            continue
        e1, e2 = (best[0], best[1]), (best[2], best[3])
        g1 = touching_wire_group(*e1)
        g2 = touching_wire_group(*e2)
        if g1 is not None and g2 is None:
            ep, wg = e1, g1
        elif g2 is not None and g1 is None:
            ep, wg = e2, g2
        else:
            cx, cy = centroid.get(pp[0], poss[0])
            d1 = (e1[0] - cx) ** 2 + (e1[1] - cy) ** 2
            d2 = (e2[0] - cx) ** 2 + (e2[1] - cy) ** 2
            ep = e1 if d1 >= d2 else e2
            wg = g1 if ep == e1 else g2
        pininfo[pp] = {"ep": ep, "lead": best, "wg": wg}

    # group nodes: wire group ids ∪ synthetic ids for wireless junctions
    net_uf = UF()
    for pp, info in pininfo.items():
        node = ("pin",) + pp
        net_uf.find(node)
        if info["wg"] is not None:
            net_uf.union(node, ("w", info["wg"]))
    # pin-to-pin: endpoints coincide or endpoint on other pin's lead
    pins = list(pininfo.items())
    for i, (pa, ia) in enumerate(pins):
        for pb, ib in pins[i + 1:]:
            if pa[0] == pb[0]:
                continue
            ea, eb = ia["ep"], ib["ep"]
            if (ea[0] - eb[0]) ** 2 + (ea[1] - eb[1]) ** 2 <= TOL * TOL:
                net_uf.union(("pin",) + pa, ("pin",) + pb)
                continue
            if ib["lead"] and seg_dist2(ea[0], ea[1], ib["lead"]) <= TOL * TOL:
                net_uf.union(("pin",) + pa, ("pin",) + pb)
            elif ia["lead"] and seg_dist2(eb[0], eb[1], ia["lead"]) <= TOL * TOL:
                net_uf.union(("pin",) + pa, ("pin",) + pb)

    groups = defaultdict(lambda: {"pins": set(), "names": set()})
    for pp, info in pininfo.items():
        groups[net_uf.find(("pin",) + pp)]["pins"].add(pp)

    def attach_name(name, cx, cy, maxd):
        best, bestd, bestkind = None, maxd * maxd, None
        for s in wires:
            d = seg_dist2(cx, cy, s)
            if d < bestd:
                bestd, best, bestkind = d, ("w", uf.find(snap(s[0], s[1]))), "w"
        for pp, info in pininfo.items():
            if info["lead"]:
                d = seg_dist2(cx, cy, info["lead"])
                if d < bestd:
                    bestd, best, bestkind = d, ("pin",) + pp, "p"
        if best is None:
            return False
        groups[net_uf.find(best)]["names"].add(name)
        return True

    seen_lab = set()
    for name, cx, cy in labels:
        dec = decode_label(name, visible)
        k = (dec, round(cx / 20), round(cy / 20))
        if k in seen_lab:
            continue
        seen_lab.add(k)
        if not attach_name(dec, cx, cy, 14.0):
            diags["labels_unattached"].append(dec)
    for name, cx, cy in powers:
        attach_name(name, cx, cy, 12.0)

    return groups, diags


if __name__ == "__main__":
    doc = pymupdf.open(PDF)
    visible = set()
    for pno in range(len(doc)):
        for *_, w, a, b, c in doc[pno].get_text("words"):
            if re.fullmatch(r"[A-Z][A-Z0-9_]{1,15}", w):
                visible.add(w)

    merged = defaultdict(set)
    anon = [0]
    all_diags = {}
    for pno in range(0, 5):
        groups, diags = extract_page(doc[pno], visible)
        all_diags[f"page{pno+1}"] = diags
        for g, d in groups.items():
            if not d["pins"]:
                continue
            names = sorted(d["names"])
            if names:
                tgt = names[0]
                for extra in names[1:]:
                    merged[tgt] |= merged.pop(extra, set())
            else:
                anon[0] += 1
                tgt = f"N${pno+1}_{anon[0]}"
            merged[tgt] |= d["pins"]

    out = {k: sorted([list(p) for p in v]) for k, v in merged.items() if v}
    dest = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("nets.json")
    dest.write_text(json.dumps({"nets": out, "diagnostics": all_diags}, indent=1))
    npins = sum(len(v) for v in out.values())
    nanon = sum(1 for k in out if k.startswith("N$"))
    print(f"nets: {len(out)} ({nanon} anonymous)  pin connections: {npins}")
    for pg, d in all_diags.items():
        if d["pins_unattached"] or d["labels_unattached"]:
            print(pg, "unattached pins:", d["pins_unattached"][:8],
                  "labels:", d["labels_unattached"][:14])
