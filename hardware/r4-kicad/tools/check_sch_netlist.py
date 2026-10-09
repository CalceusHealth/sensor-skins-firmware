#!/usr/bin/env python3
"""Netlist-equivalence check: KiCad 7 .kicad_sch vs copper_netlist_<side>.json.

Independently re-derives the schematic netlist from the generated files:
  * parses s-expressions,
  * reads each sheet's embedded lib_symbols to get symbol pin offsets,
  * places pins through the instance transform (rotation supported),
  * builds a connectivity graph: wire endpoint joins, point-on-wire-segment
    joins, local labels (sheet scope), global labels + power-symbol pins
    (project scope), no-connects,
and diffs the resulting {(ref, pad)} partition against the copper netlist.

Refs present only in the schematic (J5 debug connector, #PWR symbols) are
excluded from the partition compare and reported informationally.

Exit code 0 iff both sides have zero diffs.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
NETDIR = os.path.join(REPO, "hardware", "r4-kicad", "netlist")
SCHDIR = os.path.join(REPO, "hardware", "r4-kicad", "sch")
SHEETS = ["PWR", "MCU", "MUX", "IO"]

# ---------------------------------------------------------------- s-expr
def tokenize(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c in "()":
            yield c
            i += 1
        elif c == '"':
            j = i + 1
            buf = []
            while text[j] != '"':
                if text[j] == "\\":
                    buf.append(text[j + 1])
                    j += 2
                else:
                    buf.append(text[j])
                    j += 1
            yield ("str", "".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            yield ("atom", text[i:j])
            i = j

def parse_sexpr(text):
    stack = [[]]
    for tok in tokenize(text):
        if tok == "(":
            stack.append([])
        elif tok == ")":
            done = stack.pop()
            stack[-1].append(done)
        else:
            kind, val = tok
            stack[-1].append(val if kind == "str" else val)
    return stack[0][0]

def children(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]

def child(node, tag):
    cs = children(node, tag)
    return cs[0] if cs else None

def atoms(node):
    return [c for c in node[1:] if not isinstance(c, list)]

# ---------------------------------------------------------------- union-find
class UF:
    def __init__(self):
        self.p = {}

    def find(self, a):
        p = self.p.setdefault(a, a)
        while p != self.p[p]:
            self.p[p] = self.p[self.p[p]]
            p = self.p[p]
        self.p[a] = p
        return p

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb

def key(x, y):
    return (round(x * 100), round(y * 100))

def rot(x, y, ang):
    ang = ang % 360
    if ang == 0:
        return x, y
    if ang == 90:
        return -y, x
    if ang == 180:
        return -x, -y
    if ang == 270:
        return y, -x
    raise ValueError("non-orthogonal angle " + str(ang))

# ---------------------------------------------------------------- sheet parse
def lib_pins(libsym_node):
    """Return (pins, is_power): pins = [(number, x, y)] from all sub-units."""
    pins = []
    power = bool(children(libsym_node, "power"))
    def walk(node):
        for sub in children(node, "symbol"):
            walk(sub)
        for pin in children(node, "pin"):
            at = child(pin, "at")
            num = child(pin, "number")[1]
            pins.append((num, float(at[1]), float(at[2])))
    walk(libsym_node)
    return pins, power

def load_sheet(path):
    with open(path) as f:
        root = parse_sexpr(f.read())
    assert root[0] == "kicad_sch"
    libs = {}
    libnode = child(root, "lib_symbols")
    if libnode:
        for s in children(libnode, "symbol"):
            libs[s[1]] = lib_pins(s)
    items = {"pins": [], "wires": [], "glabels": [], "llabels": [],
             "ncs": [], "junctions": [], "powers": [], "refs": set()}
    for sym in children(root, "symbol"):
        if not isinstance(sym[1], list):
            continue  # lib def? (placed symbols have (lib_id ...) child)
    for sym in children(root, "symbol"):
        lid = child(sym, "lib_id")
        if lid is None:
            continue
        libname = lid[1]
        at = child(sym, "at")
        sx, sy, sang = float(at[1]), float(at[2]), float(at[3])
        if child(sym, "mirror"):
            raise ValueError("mirror not supported by checker")
        ref = None
        value = None
        for prop in children(sym, "property"):
            if prop[1] == "Reference":
                ref = prop[2]
            elif prop[1] == "Value":
                value = prop[2]
        pins, power = libs[libname]
        if power:
            # single hidden pin at local origin-ish
            for num, lx, ly in pins:
                rx, ry = rot(lx, ly, sang)
                items["powers"].append((value, sx + rx, sy - ry))
        else:
            items["refs"].add(ref)
            for num, lx, ly in pins:
                rx, ry = rot(lx, ly, sang)
                items["pins"].append((ref, num, sx + rx, sy - ry))
    for w in children(root, "wire"):
        pts = child(w, "pts")
        xy = children(pts, "xy")
        items["wires"].append((float(xy[0][1]), float(xy[0][2]),
                               float(xy[1][1]), float(xy[1][2])))
    for lab in children(root, "global_label"):
        at = child(lab, "at")
        items["glabels"].append((lab[1], float(at[1]), float(at[2])))
    for lab in children(root, "label"):
        at = child(lab, "at")
        items["llabels"].append((lab[1], float(at[1]), float(at[2])))
    for nc in children(root, "no_connect"):
        at = child(nc, "at")
        items["ncs"].append((float(at[1]), float(at[2])))
    for j in children(root, "junction"):
        at = child(j, "at")
        items["junctions"].append((float(at[1]), float(at[2])))
    return items

def on_segment(px, py, x1, y1, x2, y2, tol=0.005):
    if min(x1, x2) - tol <= px <= max(x1, x2) + tol and \
       min(y1, y2) - tol <= py <= max(y1, y2) + tol:
        cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)
        seg = max(abs(x2 - x1), abs(y2 - y1), 1e-9)
        return abs(cross) / seg <= tol
    return False

def extract_netlist(project_dir, project):
    uf = UF()
    pin_nodes = []     # ((ref,pad), node)
    all_refs = set()
    for sheet in SHEETS:
        path = os.path.join(project_dir, f"{sheet}.kicad_sch")
        items = load_sheet(path)
        all_refs |= items["refs"]
        P = lambda x, y: ("pt", sheet, key(x, y))
        attach = []
        for x1, y1, x2, y2 in items["wires"]:
            uf.union(P(x1, y1), P(x2, y2))
            attach.append((x1, y1))
            attach.append((x2, y2))
        for ref, num, x, y in items["pins"]:
            pin_nodes.append(((ref, num), P(x, y)))
            attach.append((x, y))
        for text, x, y in items["glabels"]:
            uf.union(P(x, y), ("net", "G", text))
            attach.append((x, y))
        for text, x, y in items["llabels"]:
            uf.union(P(x, y), ("net", sheet, text))
            attach.append((x, y))
        for val, x, y in items["powers"]:
            uf.union(P(x, y), ("net", "G", val))
            attach.append((x, y))
        for x, y in items["junctions"]:
            attach.append((x, y))
        # point-on-segment joins
        for px, py in attach:
            for x1, y1, x2, y2 in items["wires"]:
                if on_segment(px, py, x1, y1, x2, y2):
                    uf.union(P(px, py), P(x1, y1))
    groups = {}
    for rp, node in pin_nodes:
        groups.setdefault(uf.find(node), set()).add(rp)
    return groups, all_refs

# ---------------------------------------------------------------- compare
def check_side(side):
    with open(os.path.join(NETDIR, f"copper_netlist_{side.lower()}.json")) as f:
        data = json.load(f)
    copper_nets = [n for n in data["nets"] if n["pads"]]
    copper_pads = set()
    for n in copper_nets:
        for r, p in n["pads"]:
            copper_pads.add((r, str(p)))
    copper_refs = {r for r, p in copper_pads}

    project = f"Reid_Orthotic_v2_{side}"
    pdir = os.path.join(SCHDIR, project)
    groups, sch_refs = extract_netlist(pdir, project)

    # pad -> schematic group id
    pad_group = {}
    for gid, pads in groups.items():
        for rp in pads:
            pad_group[rp] = gid
    # restrict each schematic group to copper refs
    restricted = {}
    for gid, pads in groups.items():
        restricted[gid] = {rp for rp in pads if rp[0] in copper_refs}

    diffs = []
    missing_pads = sorted(rp for rp in copper_pads if rp not in pad_group)
    for rp in missing_pads:
        diffs.append(f"MISSING PIN in schematic: {rp[0]}.{rp[1]}")

    sch_only = sorted(rp for gid, pads in restricted.items() for rp in pads
                      if rp not in copper_pads)
    for rp in sch_only:
        diffs.append(f"EXTRA PIN in schematic (ref known to copper): "
                     f"{rp[0]}.{rp[1]}")

    for n in copper_nets:
        name = n.get("name") or f"N{n['id']}"
        padset = {(r, str(p)) for r, p in n["pads"]}
        gids = {pad_group.get(rp) for rp in padset if rp in pad_group}
        if len(gids) > 1:
            parts = []
            for gid in gids:
                parts.append("{" + ", ".join(
                    f"{r}.{p}" for r, p in sorted(padset & restricted[gid]))
                    + "}")
            diffs.append(f"NET SPLIT {name} (id {n['id']}): " + " | ".join(parts))
            continue
        gid = next(iter(gids), None)
        if gid is None:
            continue  # all pads missing, already reported
        extra = restricted[gid] - padset
        if extra:
            diffs.append(
                f"NET MERGE into {name} (id {n['id']}): schematic also "
                "connects " + ", ".join(f"{r}.{p}" for r, p in sorted(extra)))

    info_refs = sorted(sch_refs - copper_refs)
    print(f"== {side} ==")
    print(f"  copper: {len(copper_nets)} nets / {len(copper_pads)} pads; "
          f"schematic: {len(groups)} net groups, {len(sch_refs)} components")
    if info_refs:
        print(f"  schematic-only refs (excluded from compare): "
              f"{', '.join(info_refs)}")
    if diffs:
        print(f"  DIFFS: {len(diffs)}")
        for d in diffs:
            print("   - " + d)
    else:
        print("  ZERO DIFFS: schematic netlist == copper netlist")
    return len(diffs)

def main():
    sides = sys.argv[1:] or ["LHS", "RHS"]
    total = sum(check_side(s.upper()) for s in sides)
    sys.exit(0 if total == 0 else 1)

if __name__ == "__main__":
    main()
