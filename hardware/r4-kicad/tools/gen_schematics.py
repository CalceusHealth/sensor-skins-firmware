#!/usr/bin/env python3
"""Generate KiCad 7 schematics (LHS + RHS projects) from the R4 copper netlist.

Inputs:
  hardware/r4-kicad/netlist/copper_netlist_{lhs,rhs}.json   (netlist authority)
  artefacts/.../Reid Orthotic v2 R4 BOM.xlsx                (values / MPN / DNF)

Outputs (per side S):
  hardware/r4-kicad/sch/Reid_Orthotic_v2_<S>/
      Reid_Orthotic_v2_<S>.kicad_pro
      Reid_Orthotic_v2_<S>.kicad_sch          (root: 4 hierarchical sheets)
      PWR.kicad_sch MCU.kicad_sch MUX.kicad_sch IO.kicad_sch
      R4_symbols.kicad_sym, sym-lib-table

Connectivity is made with short wire stubs + labels:
  * named nets            -> global labels (cross-sheet safe)
  * power nets            -> power symbols (0V / 3V3 / VBAT / VBAT_F / VSYS)
  * unnamed multi-pin net -> global label "N<id>" if it spans sheets,
                             else local label "N<id>"
  * single-pin nets       -> no-connect flag (named single-pin nets keep a
                             global label instead: IS_LHS, DEC2, SWDIO, SWDCLK)

Stdlib only (xlsx parsed via zipfile+xml). Deterministic UUIDs (uuid5).
"""
import json
import math
import os
import re
import sys
import uuid
import zipfile
import xml.etree.ElementTree as ET

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
NETDIR = os.path.join(REPO, "hardware", "r4-kicad", "netlist")
OUTDIR = os.path.join(REPO, "hardware", "r4-kicad", "sch")
BOM_XLSX = os.path.join(
    REPO, "artefacts", "SSII Orthotics Electronics - Design Verification",
    "Reid Orthotic v2 R4 BOM.xlsx")

NS = uuid.UUID("9b1c2a4e-33dd-4a7e-9a0e-5f6c7d8e9f01")
SCH_VERSION = "20230121"
LIB_VERSION = "20220914"

POWER_NETS = ("0V", "3V3", "VBAT", "VBAT_F", "VSYS")
GND_STYLE = ("0V",)

def U(*parts):
    return str(uuid.uuid5(NS, "|".join(str(p) for p in parts)))

def fnum(v):
    # kicad-style float formatting
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"

# --------------------------------------------------------------------------
# BOM parsing (stdlib xlsx)
# --------------------------------------------------------------------------
def load_bom(path):
    z = zipfile.ZipFile(path)
    ns = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    shared = []
    if "xl/sharedStrings.xml" in z.namelist():
        root = ET.fromstring(z.read("xl/sharedStrings.xml"))
        for si in root.findall("m:si", ns):
            shared.append("".join(t.text or "" for t in si.iter(
                "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t")))
    sheet = ET.fromstring(z.read("xl/worksheets/sheet1.xml"))
    rows = []
    for row in sheet.find("m:sheetData", ns).findall("m:row", ns):
        cells = {}
        for c in row.findall("m:c", ns):
            ref = c.get("r")
            col = re.match(r"[A-Z]+", ref).group(0)
            v = c.find("m:v", ns)
            if v is None:
                continue
            val = v.text
            if c.get("t") == "s":
                val = shared[int(val)]
            cells[col] = val
        rows.append(cells)
    header = {col: name for col, name in rows[0].items()}
    out = []
    for cells in rows[1:]:
        if not cells:
            continue
        out.append({header.get(col, col): val for col, val in cells.items()})
    # per-designator table
    per = {}
    for row in out:
        desigs = [d.strip() for d in (row.get("Designator") or "").split(",")
                  if d.strip()]
        comment = (row.get("Comment") or "").strip()
        mfr = (row.get("Manufacturer") or "").strip()
        dnf = comment == "DNF" or mfr == "DO NOT FIT"
        for d in desigs:
            value = comment if comment != "DNF" else "DNF"
            if dnf and "DNF" not in value:
                value += " DNF"
            per[d] = {
                "value": value,
                "dnf": dnf,
                "mpn": (row.get("MPN") or "").strip(),
                "mfr": mfr,
                "footprint": (row.get("Footprint") or "").strip(),
                "dkpn": (row.get("DK VPN") or "").strip(),
            }
    return per

# --------------------------------------------------------------------------
# Symbol library
# --------------------------------------------------------------------------
# pin: (number, name, etype, side, slot)   side in {L, R}; slot = row index
class Sym:
    def __init__(self, name, ref_prefix, pins, graphics, hide_pin_names=False,
                 hide_pin_numbers=False, power=False, value=None):
        self.name = name
        self.ref_prefix = ref_prefix
        self.pins = pins            # list of dicts
        self.graphics = graphics    # list of sexpr strings (unit _0_1)
        self.hide_pin_names = hide_pin_names
        self.hide_pin_numbers = hide_pin_numbers
        self.power = power
        self.value = value or name

    def pin_map(self):
        return {p["num"]: p for p in self.pins}

def g_rect(x1, y1, x2, y2, fill="none", width=0.254):
    return (f"(rectangle (start {fnum(x1)} {fnum(y1)}) (end {fnum(x2)} {fnum(y2)}) "
            f"(stroke (width {fnum(width)}) (type default)) (fill (type {fill})))")

def g_poly(pts, fill="none", width=0.254):
    xy = " ".join(f"(xy {fnum(x)} {fnum(y)})" for x, y in pts)
    return (f"(polyline (pts {xy}) (stroke (width {fnum(width)}) (type default)) "
            f"(fill (type {fill})))")

def box_sym(name, ref_prefix, left, right, w2, etypes=None, value=None):
    """left/right: list of (num, pname) or (num, pname, etype)."""
    pins = []
    def add(side, lst):
        for i, item in enumerate(lst):
            if item is None:
                continue
            num, pname = item[0], item[1]
            et = item[2] if len(item) > 2 else "passive"
            pins.append({"num": str(num), "name": pname, "etype": et,
                         "side": side, "slot": i})
    add("L", left)
    add("R", right)
    nL = len(left)
    nR = len(right)
    nmax = max(nL, nR)
    top = 2.54
    bot = -(nmax - 1) * 2.54 - 2.54
    graphics = [g_rect(-w2, top, w2, bot)]
    s = Sym(name, ref_prefix, pins, graphics, value=value)
    s.w2 = w2
    s.pinlen = 3.81
    s.box_top = top
    s.box_bot = bot
    for p in s.pins:
        ly = -p["slot"] * 2.54
        if p["side"] == "L":
            p["at"] = (-(w2 + s.pinlen), ly, 0)
        else:
            p["at"] = (w2 + s.pinlen, ly, 180)
        p["len"] = s.pinlen
    return s

def twopin_sym(name, ref_prefix, graphics, p1name="1", p2name="2",
               reach=3.81, body=2.54, etype="passive"):
    pins = [
        {"num": "1", "name": p1name, "etype": etype, "side": "L", "slot": 0,
         "at": (-reach, 0, 0), "len": reach - body / 2 if body else reach},
        {"num": "2", "name": p2name, "etype": etype, "side": "R", "slot": 0,
         "at": (reach, 0, 180), "len": reach - body / 2 if body else reach},
    ]
    s = Sym(name, ref_prefix, pins, graphics, hide_pin_names=True)
    s.w2 = reach
    s.box_top = 2.0
    s.box_bot = -2.0
    return s

def power_sym(name, gnd=False):
    if gnd:
        graphics = [
            g_poly([(0, 0), (0, -1.27)]),
            g_poly([(-1.27, -1.27), (1.27, -1.27)]),
            g_poly([(-0.762, -1.778), (0.762, -1.778)], width=0.2),
            g_poly([(-0.254, -2.286), (0.254, -2.286)], width=0.2),
        ]
    else:
        graphics = [
            g_poly([(0, 0), (0, 2.54)]),
            g_poly([(-0.762, 1.27), (0, 2.54), (0.762, 1.27)]),
        ]
    pins = [{"num": "1", "name": name, "etype": "power_in", "side": "L",
             "slot": 0, "at": (0, 0, 270 if gnd else 90), "len": 0,
             "hide": True}]
    s = Sym(name, "#PWR", pins, graphics, hide_pin_names=True,
            hide_pin_numbers=True, power=True)
    return s

def build_symbols(u2_table):
    syms = {}

    # ---- passives
    syms["R"] = twopin_sym("R", "R", [g_rect(-2.286, 1.016, 2.286, -1.016)])
    syms["C"] = twopin_sym("C", "C", [
        g_poly([(-0.508, 1.6), (-0.508, -1.6)], width=0.4),
        g_poly([(0.508, 1.6), (0.508, -1.6)], width=0.4)], body=1.016)
    syms["L"] = twopin_sym("L", "L", [
        g_rect(-2.286, 1.016, 2.286, -1.016, fill="outline")])
    syms["FUSE"] = twopin_sym("FUSE", "F", [
        g_rect(-2.286, 1.016, 2.286, -1.016),
        g_poly([(-3.81, 0), (3.81, 0)], width=0.2)])
    # D: pin1 = A (left), pin2 = K (right)  [orientation from net semantics]
    syms["D"] = twopin_sym("D", "D", [
        g_poly([(-1.27, 1.27), (-1.27, -1.27), (1.27, 0), (-1.27, 1.27)]),
        g_poly([(1.27, 1.27), (1.27, -1.27)], width=0.4)],
        p1name="A", p2name="K", body=2.54)
    syms["D_ZENER"] = twopin_sym("D_ZENER", "D", [
        g_poly([(-1.27, 1.27), (-1.27, -1.27), (1.27, 0), (-1.27, 1.27)]),
        g_poly([(1.905, 1.905), (1.27, 1.27), (1.27, -1.27), (0.635, -1.905)],
               width=0.4)],
        p1name="A", p2name="K", body=2.54)
    # X1 32.768k 2-pad crystal
    syms["XTAL_2P"] = twopin_sym("XTAL_2P", "X", [
        g_poly([(-1.016, 1.27), (-1.016, -1.27)], width=0.4),
        g_poly([(1.016, 1.27), (1.016, -1.27)], width=0.4),
        g_rect(-0.508, 1.524, 0.508, -1.524)], body=2.032)
    # ANT: 2-pin chip antenna (pad2 = feed on this board)
    syms["ANT"] = twopin_sym("ANT", "ANT", [
        g_rect(-2.286, 1.016, 2.286, -1.016),
        g_poly([(0, 1.016), (0, 1.9), (0.9, 2.8)]),
        g_poly([(0.3, 2.8), (0.9, 2.8), (0.9, 2.2)])])

    # connectors (pins on left)
    syms["CONN_2P"] = box_sym("CONN_2P", "J",
                              [("1", "1"), ("2", "2")], [], 3.81)
    syms["IO_CONN_20P"] = box_sym(
        "IO_CONN_20P", "J",
        [(str(i), str(i)) for i in range(1, 21)], [], 3.81)
    syms["TC2030"] = box_sym("TC2030", "J", [
        ("1", "VTref"), ("2", "SWDIO"), ("3", "~{RESET}"),
        ("4", "SWDCLK"), ("5", "GND"), ("6", "SWO/NC")], [], 7.62)

    # MOSFETs as labelled boxes; pad mapping ASSUMED 1=G 2=S 3=D (SOT-323)
    syms["NMOS_GSD"] = box_sym("NMOS_GSD", "Q",
                               [("1", "G")], [("3", "D"), ("2", "S")], 5.08,
                               value="NMOS")
    syms["PMOS_GSD"] = box_sym("PMOS_GSD", "Q",
                               [("1", "G")], [("3", "D"), ("2", "S")], 5.08,
                               value="PMOS")

    # LDO TCR3UF33A SOT23-5
    syms["LDO_SOT23_5"] = box_sym("LDO_SOT23_5", "U", [
        ("1", "VIN", "power_in"), ("3", "CT/EN", "input"),
        ("2", "GND", "power_in")], [
        ("5", "VOUT", "power_out"), ("4", "NC", "no_connect")], 7.62)

    # BQ24210 WSON-10 (pin functions per TI SLUSA76B, numbers = our pads)
    syms["BQ24210"] = box_sym("BQ24210", "U", [
        ("1", "VBUS", "power_in"), ("2", "ISET", "passive"),
        ("4", "VTSB", "passive"), ("5", "TS", "passive"),
        ("3", "VSS", "power_in"), ("PAD", "EP(VSS)", "power_in")], [
        ("10", "BAT", "power_out"), ("8", "~{CHG}", "open_collector"),
        ("6", "~{PG}", "open_collector"), ("7", "~{EN}", "input"),
        ("9", "NC", "no_connect")], 8.89)

    # LSM6DSM LGA-14
    syms["LSM6DSM"] = box_sym("LSM6DSM", "U", [
        ("1", "SDO/SA0", "input"), ("2", "SDx", "passive"),
        ("3", "SCx", "passive"), ("12", "CS", "input"),
        ("13", "SCL", "input"), ("14", "SDA", "bidirectional"),
        ("6", "GND", "power_in"), ("7", "GND", "power_in")], [
        ("5", "VDDIO", "power_in"), ("8", "VDD", "power_in"),
        ("4", "INT1", "output"), ("9", "INT2", "output"),
        ("10", "NC", "no_connect"), ("11", "NC", "no_connect")], 8.89)

    # 74LV4051BQ DHVQFN16 + EP
    syms["74LV4051BQ"] = box_sym("74LV4051BQ", "U", [
        ("3", "Z", "passive"), ("11", "S0", "input"), ("10", "S1", "input"),
        ("9", "S2", "input"), ("6", "~{E}", "input"),
        ("16", "VDD", "power_in"), ("7", "VEE", "power_in"),
        ("8", "GND", "power_in"), ("PAD", "EP", "power_in")], [
        ("13", "Y0", "passive"), ("14", "Y1", "passive"),
        ("15", "Y2", "passive"), ("12", "Y3", "passive"),
        ("1", "Y4", "passive"), ("5", "Y5", "passive"),
        ("2", "Y6", "passive"), ("4", "Y7", "passive")], 7.62)

    # XRCGB32M 4-pad crystal: 1/3 = crystal terminals, 2/4 = case GND pads
    syms["XTAL_4P"] = box_sym("XTAL_4P", "X", [
        ("1", "1", "passive"), ("2", "GND", "passive")], [
        ("3", "3", "passive"), ("4", "GND", "passive")], 5.08)

    # nRF52832-QFAA, names + numbers exactly from u2_pin_table
    def u2_et(pkg):
        if pkg.startswith("VDD"):
            return "power_in"
        if pkg.startswith("VSS"):
            return "power_in"
        if pkg == "NC":
            return "no_connect"
        if pkg.startswith("P0.") or pkg in ("SWDIO", "SWDCLK"):
            return "bidirectional"
        return "passive"
    left_order = ["13", "36", "48", "47", "1", "32", "33", "46",
                  "34", "35", "30", "25", "26", "44", "31", "45", "PAD"]
    gpio = [(k, v) for k, v in u2_table.items() if k not in left_order]
    def port_key(item):
        m = re.match(r"P0\.(\d+)", item[1]["pkg_name"])
        return int(m.group(1)) if m else 99
    gpio.sort(key=port_key)
    left = [(k, u2_table[k]["pkg_name"], u2_et(u2_table[k]["pkg_name"]))
            for k in left_order]
    right = [(k, v["pkg_name"], u2_et(v["pkg_name"])) for k, v in gpio]
    syms["NRF52832_QFAA"] = box_sym("NRF52832_QFAA", "U", left, right, 12.7)

    # power symbols
    for pn in POWER_NETS:
        syms["PWR_" + pn] = power_sym(pn, gnd=pn in GND_STYLE)
    return syms

def sym_lib_sexpr(sym, libname="R4"):
    out = []
    flags = ""
    out.append(f'  (symbol "{libname}:{sym.name}"')
    if sym.power:
        out.append("    (power)")
    if sym.hide_pin_numbers:
        out.append("    (pin_numbers hide)")
    off = "0" if sym.power else "0.762"
    hide = " hide" if sym.hide_pin_names else ""
    out.append(f"    (pin_names (offset {off}){hide})")
    out.append("    (in_bom yes) (on_board yes)")
    refhide = " hide" if sym.power else ""
    out.append(f'    (property "Reference" "{sym.ref_prefix}" (at 0 {fnum((getattr(sym, "box_top", 2.54)) + 1.27)} 0) '
               f'(effects (font (size 1.27 1.27)){refhide}))')
    out.append(f'    (property "Value" "{sym.value}" (at 0 {fnum((getattr(sym, "box_bot", -2.54)) - 1.27)} 0) '
               f'(effects (font (size 1.27 1.27))))')
    out.append('    (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))')
    out.append('    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))')
    out.append(f'    (symbol "{sym.name}_0_1"')
    for g in sym.graphics:
        out.append("      " + g)
    out.append("    )")
    out.append(f'    (symbol "{sym.name}_1_1"')
    for p in sym.pins:
        x, y, ang = p["at"]
        ph = " hide" if p.get("hide") else ""
        pname = p["name"]
        out.append(
            f'      (pin {p["etype"]} line (at {fnum(x)} {fnum(y)} {ang}) '
            f'(length {fnum(p.get("len", 2.54))}){ph} '
            f'(name "{pname}" (effects (font (size 1.016 1.016)))) '
            f'(number "{p["num"]}" (effects (font (size 1.016 1.016)))))')
    out.append("    )")
    out.append("  )")
    return "\n".join(out)

# --------------------------------------------------------------------------
# Component -> symbol/value assignment
# --------------------------------------------------------------------------
def component_table(refs, bom):
    table = {}
    for ref in refs:
        if ref == "U2":
            s = "NRF52832_QFAA"
        elif ref in ("U3", "U4", "U5", "U6", "U7"):
            s = "74LV4051BQ"
        elif ref == "U8":
            s = "BQ24210"
        elif ref == "U9":
            s = "LDO_SOT23_5"
        elif ref == "U1":
            s = "LSM6DSM"
        elif ref == "X1":
            s = "XTAL_2P"
        elif ref == "X2":
            s = "XTAL_4P"
        elif ref == "ANT1":
            s = "ANT"
        elif ref in ("J1", "J2"):
            s = "IO_CONN_20P"
        elif ref in ("J3", "J4"):
            s = "CONN_2P"
        elif ref == "F1":
            s = "FUSE"
        elif ref == "D1":
            s = "D"
        elif ref == "D2":
            s = "D_ZENER"
        elif ref == "Q1":
            s = "NMOS_GSD"
        elif ref in ("Q2", "Q3"):
            s = "PMOS_GSD"
        elif ref.startswith("L"):
            s = "L"
        elif ref.startswith("C"):
            s = "C"
        elif ref.startswith("R"):
            s = "R"
        else:
            raise ValueError("no symbol for " + ref)
        b = bom.get(ref)
        if b is None:
            raise ValueError("no BOM row for " + ref)
        table[ref] = {"sym": s, **b}
    # J5 is schematic-only (TC2030 debug pads; not in BOM / copper json)
    table["J5"] = {"sym": "TC2030", "value": "TC2030-CTX (debug pads)",
                   "dnf": True, "mpn": "", "mfr": "Tag-Connect",
                   "footprint": "TC2030", "dkpn": ""}
    return table

SHEET_OF = {}
def build_sheet_assignment():
    pwr = ["U8", "U9", "J3", "J4", "F1", "D1", "D2",
           "C37", "C38", "C40", "C42", "C34", "C35", "C43", "C39",
           "R56", "R57", "R58", "R59", "R60", "R61", "R62", "R20"]
    mcu = ["U2", "X1", "C4", "C5", "X2", "C11", "C12",
           "C6", "C13", "C9", "L1", "L2", "C1x",  # C1x placeholder removed
           "C7", "C8", "C10", "J5", "ANT1", "L3", "C15", "C16",
           "R39", "R40", "R45", "C3",
           "R41", "Q1", "R42", "Q2", "R43", "R44", "C2"]
    mcu.remove("C1x")
    mux = ["U3", "U4", "U5", "U6", "U7",
           "R47", "R48", "R49", "R50", "R51",
           "C19", "C20", "C21", "C22", "C23",
           "Q3", "R46", "R54", "R55", "C18",
           "C24", "C25", "C26", "C27", "C28"]
    io = ["J1", "J2", "U1", "C1"] + \
         [f"R{i}" for i in list(range(1, 20)) + list(range(21, 39))]
    order = {"PWR": pwr, "MCU": mcu, "MUX": mux, "IO": io}
    for sheet, refs in order.items():
        for r in refs:
            SHEET_OF[r] = sheet
    return order

# --------------------------------------------------------------------------
# Netlist helpers
# --------------------------------------------------------------------------
def load_side(side):
    with open(os.path.join(NETDIR, f"copper_netlist_{side.lower()}.json")) as f:
        return json.load(f)

def net_info(data):
    """Return pad->net map and net descriptors."""
    pad_net = {}
    nets = {}
    for n in data["nets"]:
        if not n["pads"]:
            continue
        nid = n["id"]
        name = n.get("name")
        pads = [(r, str(p)) for r, p in n["pads"]]
        nets[nid] = {"id": nid, "name": name, "pads": pads}
        for rp in pads:
            pad_net[rp] = nid
    return pad_net, nets

def classify_nets(nets):
    """Attach label plan to each net."""
    for n in nets.values():
        sheets = {SHEET_OF[r] for r, p in n["pads"]}
        n["sheets"] = sheets
        name = n["name"]
        if name in POWER_NETS:
            n["kind"] = "power"
            n["label"] = name
        elif name:
            n["kind"] = "global"
            n["label"] = name
        elif len(n["pads"]) <= 1:
            n["kind"] = "nc"
            n["label"] = None
        elif len(sheets) > 1:
            n["kind"] = "global"
            n["label"] = f"N{n['id']}"
        else:
            n["kind"] = "local"
            n["label"] = f"N{n['id']}"

# --------------------------------------------------------------------------
# Sheet writer
# --------------------------------------------------------------------------
class SheetWriter:
    def __init__(self, side, project, sheet_name, file_uuid, inst_path,
                 syms, paper="A2"):
        self.side = side
        self.project = project
        self.sheet = sheet_name
        self.file_uuid = file_uuid
        self.inst_path = inst_path
        self.syms = syms
        self.paper = paper
        self.body = []
        self.pwr_count = 0
        self.used_syms = set(["PWR_" + p for p in POWER_NETS])

    def wire(self, x1, y1, x2, y2, key):
        self.body.append(
            f"  (wire (pts (xy {fnum(x1)} {fnum(y1)}) (xy {fnum(x2)} {fnum(y2)}))\n"
            f"    (stroke (width 0) (type default))\n"
            f'    (uuid "{U(self.side, self.sheet, "wire", key)}"))')

    def no_connect(self, x, y, key):
        self.body.append(
            f'  (no_connect (at {fnum(x)} {fnum(y)}) '
            f'(uuid "{U(self.side, self.sheet, "nc", key)}"))')

    def glabel(self, text, x, y, direction, key):
        if direction > 0:
            ang, just = 0, "left"
        else:
            ang, just = 180, "right"
        self.body.append(
            f'  (global_label "{text}" (shape passive) '
            f"(at {fnum(x)} {fnum(y)} {ang}) (fields_autoplaced)\n"
            f"    (effects (font (size 1.27 1.27)) (justify {just}))\n"
            f'    (uuid "{U(self.side, self.sheet, "glabel", key)}")\n'
            f'    (property "Intersheet References" "${{INTERSHEET_REFS}}" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) hide)))")

    def llabel(self, text, x, y, direction, key):
        if direction > 0:
            ang, just = 0, "left bottom"
        else:
            ang, just = 180, "right bottom"
        self.body.append(
            f'  (label "{text}" (at {fnum(x)} {fnum(y)} {ang})\n'
            f"    (effects (font (size 1.27 1.27)) (justify {just}))\n"
            f'    (uuid "{U(self.side, self.sheet, "llabel", key)}"))')

    def power(self, net, x, y, direction, key):
        """Place a power symbol attached at (x,y), glyph extending outward."""
        self.pwr_count += 1
        ref = f"#PWR{self.sheet}{self.pwr_count:03d}"
        sym = self.syms["PWR_" + net]
        if net in GND_STYLE:
            ang = 90 if direction > 0 else 270
        else:
            ang = 270 if direction > 0 else 90
        uid = U(self.side, self.sheet, "pwr", key)
        tx = x + direction * 5.2
        just = "left" if direction > 0 else "right"
        self.body.append(
            f'  (symbol (lib_id "R4:{sym.name}") (at {fnum(x)} {fnum(y)} {ang}) (unit 1)\n'
            f"    (in_bom yes) (on_board yes) (dnp no)\n"
            f'    (uuid "{uid}")\n'
            f'    (property "Reference" "{ref}" (at {fnum(tx)} {fnum(y + 2.54)} 0) '
            f"(effects (font (size 1.27 1.27)) hide))\n"
            f'    (property "Value" "{net}" (at {fnum(tx)} {fnum(y)} 0) '
            f"(effects (font (size 1.27 1.27)) (justify {just})))\n"
            f'    (property "Footprint" "" (at {fnum(x)} {fnum(y)} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (property "Datasheet" "" (at {fnum(x)} {fnum(y)} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (pin "1" (uuid "{U(self.side, self.sheet, "pwrpin", key)}"))\n'
            f'    (instances (project "{self.project}" (path "{self.inst_path}"\n'
            f'      (reference "{ref}") (unit 1)))))')

    def place_component(self, ref, comp, cx, cy, pad_net, nets):
        sym = self.syms[comp["sym"]]
        self.used_syms.add(comp["sym"])
        uid = U(self.side, "comp", ref)
        dnp = "yes" if comp["dnf"] else "no"
        in_bom = "no" if comp["dnf"] else "yes"
        props = []
        ref_y = cy - getattr(sym, "box_top", 2.54) - 2.2
        val_y = cy - getattr(sym, "box_bot", -2.54) + 2.2
        props.append(('Reference', ref, cx, ref_y, False))
        props.append(('Value', comp["value"], cx, val_y, False))
        props.append(('Footprint', comp.get("footprint", ""), cx, val_y + 2.0, True))
        props.append(('Datasheet', "", cx, cy, True))
        if comp.get("mpn"):
            props.append(('MPN', comp["mpn"], cx, val_y + 4.0, True))
        if comp.get("mfr") and comp["mfr"] != "DO NOT FIT":
            props.append(('Manufacturer', comp["mfr"], cx, val_y + 6.0, True))
        plines = []
        for name, val, px, py, hide in props:
            h = " hide" if hide else ""
            val = val.replace('"', "'")
            plines.append(
                f'    (property "{name}" "{val}" (at {fnum(px)} {fnum(py)} 0) '
                f"(effects (font (size 1.27 1.27)){h}))")
        pin_lines = []
        for p in sym.pins:
            pin_lines.append(
                f'    (pin "{p["num"]}" (uuid "{U(self.side, ref, "pin", p["num"])}"))')
        self.body.append(
            f'  (symbol (lib_id "R4:{sym.name}") (at {fnum(cx)} {fnum(cy)} 0) (unit 1)\n'
            f"    (in_bom {in_bom}) (on_board yes) (dnp {dnp})\n"
            f'    (uuid "{uid}")\n'
            + "\n".join(plines) + "\n"
            + "\n".join(pin_lines) + "\n"
            f'    (instances (project "{self.project}" (path "{self.inst_path}"\n'
            f'      (reference "{ref}") (unit 1)))))')
        # pin hookup
        for p in sym.pins:
            lx, ly, ang = p["at"]
            px, py = cx + lx, cy - ly
            direction = 1 if ang == 180 else -1   # 180 = pin points left => on right side
            key = f"{ref}.{p['num']}"
            rp = (ref, p["num"])
            nid = pad_net.get(rp)
            if nid is None:
                # schematic-only pins (J5): wire per J5_PLAN
                plan = J5_PLAN.get(p["num"]) if ref == "J5" else None
                if plan is None:
                    self.no_connect(px, py, key)
                    continue
                kind, label = plan
                ex, ey = px + direction * 2.54, py
                self.wire(px, py, ex, ey, key)
                if kind == "power":
                    self.power(label, ex, ey, direction, key)
                else:
                    self.glabel(label, ex, ey, direction, key)
                continue
            net = nets[nid]
            if net["kind"] == "nc":
                if net["name"]:
                    ex, ey = px + direction * 2.54, py
                    self.wire(px, py, ex, ey, key)
                    self.glabel(net["name"], ex, ey, direction, key)
                else:
                    self.no_connect(px, py, key)
                continue
            ex, ey = px + direction * 2.54, py
            self.wire(px, py, ex, ey, key)
            if net["kind"] == "power":
                self.power(net["label"], ex, ey, direction, key)
            elif net["kind"] == "global":
                self.glabel(net["label"], ex, ey, direction, key)
            else:
                self.llabel(net["label"], ex, ey, direction, key)

    def render(self, lib_sexpr, title):
        hdr = (
            f"(kicad_sch (version {SCH_VERSION}) (generator gen_schematics)\n\n"
            f'  (uuid "{self.file_uuid}")\n\n'
            f'  (paper "{self.paper}")\n\n'
            f"  (title_block\n"
            f'    (title "{title}")\n'
            f'    (date "2026-10-01")\n'
            f'    (rev "R4")\n'
            f'    (company "Calceus Health (copper-netlist reconstruction)")\n'
            f"  )\n\n"
            f"  (lib_symbols\n{lib_sexpr}\n  )\n\n")
        return hdr + "\n".join(self.body) + "\n)\n"

# J5 (TC2030) wiring plan: pins 2/3/4 evidenced by B.Cu board pads on
# SWDIO / MCU_RST / SWDCLK; pins 1/5 assumed per standard TC2030 SWD wiring.
J5_PLAN = {
    "1": ("power", "3V3"),
    "2": ("glabel", "SWDIO"),
    "3": ("glabel", "MCU_RST"),
    "4": ("glabel", "SWDCLK"),
    "5": ("power", "0V"),
    "6": None,
}
J5_PLAN = {k: v for k, v in J5_PLAN.items() if v is not None}

# --------------------------------------------------------------------------
# Layout
# --------------------------------------------------------------------------
def text_w(s):
    return 1.1 * len(s)

def cell_size(sym, comp, nets, pad_net, ref):
    """Width/height of placement cell including stubs + label clearance."""
    max_lab = 8
    for p in sym.pins:
        rp = (ref, p["num"])
        nid = pad_net.get(rp)
        lab = ""
        if nid is not None and nets[nid].get("label"):
            lab = nets[nid]["label"]
        max_lab = max(max_lab, text_w(lab) + 2)
    w2 = getattr(sym, "w2", 3.81) + (getattr(sym, "pinlen", 0) or 0)
    w = 2 * (w2 + 2.54 + max_lab) + 6
    h = (getattr(sym, "box_top", 2.54) - getattr(sym, "box_bot", -2.54)) + 11
    return w, h

def snap(v):
    return round(v / 1.27) * 1.27

def layout_sheet(writer, refs, comps, pad_net, nets, width=560.0):
    x0, y0 = 30.0, 35.0
    x, y = x0, y0
    rowh = 0.0
    for ref in refs:
        comp = comps[ref]
        sym = writer.syms[comp["sym"]]
        w, h = cell_size(sym, comp, nets, pad_net, ref)
        if x + w > width and x > x0:
            x = x0
            y += rowh + 4
            rowh = 0.0
        cx = snap(x + w / 2)
        cy = snap(y + 6 - getattr(sym, "box_top", 2.54) + 2.54)
        writer.place_component(ref, comp, cx, cy, pad_net, nets)
        x += w
        rowh = max(rowh, h)

# --------------------------------------------------------------------------
# Project generation
# --------------------------------------------------------------------------
SHEETS = ["PWR", "MCU", "MUX", "IO"]

def gen_project(side, bom):
    data = load_side(side)
    pad_net, nets = net_info(data)
    refs = sorted({r for n in nets.values() for r, p in n["pads"]})
    order = build_sheet_assignment()
    assigned = {r for lst in order.values() for r in lst}
    missing = set(refs) - assigned
    if missing:
        raise RuntimeError(f"{side}: unassigned refs {sorted(missing)}")
    classify_nets(nets)

    comps = component_table(refs, bom)
    syms = build_symbols(data["u2_pin_table"])

    # sanity: symbol pad set covers copper pad set per ref
    per_ref = {}
    for n in nets.values():
        for r, p in n["pads"]:
            per_ref.setdefault(r, set()).add(p)
    for r, pads in per_ref.items():
        spins = {p["num"] for p in syms[comps[r]["sym"]].pins}
        if not pads <= spins:
            raise RuntimeError(f"{side}: {r} copper pads {sorted(pads - spins)} "
                               f"missing from symbol {comps[r]['sym']}")
        if pads != spins:
            raise RuntimeError(f"{side}: {r} symbol pins {sorted(spins - pads)} "
                               f"not present in copper json")

    project = f"Reid_Orthotic_v2_{side}"
    pdir = os.path.join(OUTDIR, project)
    os.makedirs(pdir, exist_ok=True)

    root_uuid = U(side, "root-file")
    sheet_elem = {s: U(side, "sheet-elem", s) for s in SHEETS}
    sheet_file_uuid = {s: U(side, "sheet-file", s) for s in SHEETS}

    # ---- library file + embedded lib text
    all_syms = list(build_symbols(data["u2_pin_table"]).values())
    lib_entries = "\n".join(sym_lib_sexpr(s) for s in all_syms)
    with open(os.path.join(pdir, "R4_symbols.kicad_sym"), "w") as f:
        f.write(f"(kicad_symbol_lib (version {LIB_VERSION}) "
                f"(generator gen_schematics)\n{lib_entries}\n)\n")
    with open(os.path.join(pdir, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n  (lib (name "R4")(type "KiCad")'
                '(uri "${KIPRJMOD}/R4_symbols.kicad_sym")(options "")(descr '
                '"R4 reconstruction symbols"))\n)\n')

    # ---- child sheets
    for s in SHEETS:
        w = SheetWriter(side, project, s, sheet_file_uuid[s],
                        f"/{root_uuid}/{sheet_elem[s]}", syms)
        layout_sheet(w, order[s], comps, pad_net, nets)
        title = f"Reid Orthotic v2 R4 {side} - {s} (copper reconstruction)"
        with open(os.path.join(pdir, f"{s}.kicad_sch"), "w") as f:
            f.write(w.render(lib_entries, title))

    # ---- root sheet
    body = []
    px, py = 25.4, 25.4
    for i, s in enumerate(SHEETS):
        sx = px + (i % 2) * 76.2
        sy = py + (i // 2) * 50.8
        body.append(
            f"  (sheet (at {fnum(sx)} {fnum(sy)}) (size 50.8 25.4) (fields_autoplaced)\n"
            f"    (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0))\n"
            f'    (uuid "{sheet_elem[s]}")\n'
            f'    (property "Sheetname" "{s}" (at {fnum(sx)} {fnum(sy - 0.8)} 0)\n'
            f"      (effects (font (size 1.27 1.27)) (justify left bottom)))\n"
            f'    (property "Sheetfile" "{s}.kicad_sch" (at {fnum(sx)} {fnum(sy + 26.2)} 0)\n'
            f"      (effects (font (size 1.27 1.27)) (justify left top)))\n"
            f'    (instances (project "{project}" (path "/{root_uuid}" '
            f'(page "{i + 2}")))))')
    body.append('  (sheet_instances (path "/" (page "1")))')
    note = ("Reconstructed from R4 gerber copper (copper_netlist_" +
            side.lower() + ".json). Netlist authority = copper, not the "
            "original PDF.")
    root = (
        f"(kicad_sch (version {SCH_VERSION}) (generator gen_schematics)\n\n"
        f'  (uuid "{root_uuid}")\n\n'
        f'  (paper "A4")\n\n'
        f"  (title_block\n"
        f'    (title "Reid Orthotic v2 R4 {side} - root")\n'
        f'    (date "2026-10-01")\n    (rev "R4")\n'
        f'    (company "Calceus Health (copper-netlist reconstruction)")\n'
        f'    (comment 1 "{note}")\n  )\n\n'
        f"  (lib_symbols)\n\n"
        f'  (text "{note}" (at 25.4 110 0)\n'
        f"    (effects (font (size 2 2)) (justify left bottom))\n"
        f'    (uuid "{U(side, "root-note")}"))\n\n'
        + "\n".join(body) + "\n)\n")
    with open(os.path.join(pdir, f"{project}.kicad_sch"), "w") as f:
        f.write(root)

    # ---- project file
    pro = {
        "board": {"design_settings": {}, "layer_presets": [], "viewports": []},
        "boards": [],
        "cvpcb": {"equivalence_files": []},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{project}.kicad_pro", "version": 1},
        "net_settings": {"classes": [], "meta": {"version": 3}},
        "pcbnew": {},
        "schematic": {
            "annotate_start_num": 0,
            "drawing": {"default_line_thickness": 6.0,
                        "default_text_size": 50.0,
                        "label_size_ratio": 0.375},
            "legacy_lib_dir": "", "legacy_lib_list": [],
            "meta": {"version": 1},
            "net_format_name": "", "page_layout_descr_file": "",
            "plot_directory": "", "spice_current_sheet_as_root": False,
            "spice_external_command": 'spice "%I"',
            "spice_model_current_sheet_as_root": True,
            "spice_save_all_currents": False,
            "spice_save_all_voltages": False,
            "subpart_first_id": 65, "subpart_id_separator": 0},
        "sheets": [[root_uuid, ""]] + [[sheet_elem[s], s] for s in SHEETS],
        "text_variables": {},
    }
    with open(os.path.join(pdir, f"{project}.kicad_pro"), "w") as f:
        json.dump(pro, f, indent=2)
    print(f"{side}: wrote {pdir}  ({len(refs)} components, "
          f"{len(nets)} non-empty copper nets)")

def main():
    bom = load_bom(BOM_XLSX)
    for side in ("LHS", "RHS"):
        gen_project(side, bom)

if __name__ == "__main__":
    main()
