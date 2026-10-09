#!/usr/bin/env python3
"""Canonicalize the extracted netlist into per-side (LHS/RHS) netlists.

Sources of truth, strongest first:
  1. firmware gpio.h (hardware-validated): MCU pin -> function, per side
  2. U2 package-pin -> P0.xx port map, read from the schematic PDF text
  3. extracted nets.json (geometry heuristics)
  4. overrides.json: curated fixes for known extraction artifacts

Outputs netlist/netlist_lhs.json and netlist_rhs.json:
  {net: [[ref, pin], ...]}, all nets named (anonymous fragments merged or
  renamed N$x deterministically), plus a validation report.
"""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import pymupdf

HERE = Path(__file__).resolve().parent
PDF = HERE.parents[2] / "artefacts" / "Reid Orthotic v2 R4.pdf"

# ---- gpio.h ground truth (firmware/ble_app_firmware_v2_R7/gpio.h) ----
GPIO = {
    "LHS": {
        "MUX_ON": 31, "FSR_ADC2": 2, "FSR_ADC1": 3, "FSR_ADC0": 4,
        "FSR_S0": 9, "FSR_S1": 8, "FSR_S2": 7,
        "CAP_ADC0": 30, "CAP_ADC1": 28,
        "CAP_S0": 13, "CAP_S1": 14, "CAP_S2": 15,
        "TEMP_S0": 18, "TEMP_S1": 19, "TEMP_S2": 17, "TEMP_S3": 16,
        "TEMP_S4": 20, "TEMP_COM": 29,
        "ADC_VBAT": 5, "VBAT_ON": 6,
        "BAT_PG": 10, "BAT_CHG": 12,
        "MCU_SDA": 26, "MCU_SCL": 27, "IS_LHS": 22, "MCU_RST": 21,
    },
    "RHS": {
        "MUX_ON": 31, "FSR_ADC0": 2, "FSR_ADC1": 3, "FSR_ADC2": 4,
        "FSR_S0": 27, "FSR_S1": 26, "FSR_S2": 25,
        "CAP_ADC0": 30, "CAP_ADC1": 28,
        "CAP_S0": 15, "CAP_S1": 14, "CAP_S2": 13,
        "TEMP_S0": 17, "TEMP_S1": 16, "TEMP_S2": 18, "TEMP_S3": 20,
        "TEMP_S4": 19, "TEMP_COM": 29,
        "ADC_VBAT": 5, "VBAT_ON": 6,
        "BAT_PG": 10, "BAT_CHG": 11,
        "MCU_SDA": 8, "MCU_SCL": 9, "IS_LHS": 22, "MCU_RST": 21,
    },
}
# Note: gpio.h FSR_CH0/1/2 are the ADC inputs (AIN2/1/0 on LHS = P0.04/03/02);
# schematic nets are FSR_ADC0/1/2 — mapped above accordingly.


def u2_port_map():
    """From sheet 3: pair each PIU20<pin> marker with the nearest pin-name
    text (P0.xx/..., DEC1, VDD, ...). Returns {pkg_pin: port_label}."""
    doc = pymupdf.open(PDF)
    page = doc[2]
    words = page.get_text("words")
    marks, names = [], []
    seen = set()
    port_re = re.compile(
        r"^(P0\.\d{1,2}(/[A-Za-z0-9]+)*|DEC[14]|DCC|VDD|VSS|XC[12]|ANT|"
        r"SWDIO|SWDCLK|NFC[12]/P0\.\d{1,2}|P0\.\d{1,2}/NFC[12])$")
    for x0, y0, x1, y1, w, *_ in words:
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        k = (w, round(cx), round(cy))
        if k in seen:
            continue
        seen.add(k)
        m = re.fullmatch(r"PIU20([0-9]+|PAD)", w)
        if m:
            marks.append((m.group(1), cx, cy))
        elif port_re.match(w):
            names.append((w, cx, cy))
    out = {}
    for pin, cx, cy in marks:
        best, bestd = None, 1e9
        for name, nx, ny in names:
            d = (nx - cx) ** 2 + (ny - cy) ** 2
            if d < bestd:
                bestd, best = d, name
        out[pin] = best
    return out


def load_overrides():
    f = HERE.parent / "netlist" / "overrides.json"
    return json.loads(f.read_text()) if f.exists() else {"moves": [], "merges": [], "notes": []}


def canonicalize(side, nets, port_map, overrides):
    # index: (ref,pin) -> netname
    where = {}
    nets = {k: {tuple(p) for p in v} for k, v in nets.items()}
    for n, pins in nets.items():
        for p in pins:
            where[p] = n

    def move(pin, target):
        src = where.get(pin)
        if src == target:
            return
        if src is not None:
            nets[src].discard(pin)
        nets.setdefault(target, set()).add(pin)
        where[pin] = target

    def merge(src, dst):
        if src not in nets or src == dst:
            return
        for p in list(nets[src]):
            move(p, dst)
        nets.pop(src, None)

    # 1. overrides first (fix extraction artifacts before renames)
    for pin, target in overrides.get("moves", []):
        move(tuple(pin), target)
    for src, dst in overrides.get("merges", []):
        merge(src, dst)

    # 2. gpio.h: U2 package pin whose port is P0.<n> gets the function net
    port_to_pkg = {}
    for pkg, port in port_map.items():
        m = re.match(r"(?:NFC[12]/)?P0\.(\d{1,2})", port or "")
        if m:
            port_to_pkg[int(m.group(1))] = pkg
    report = {"side": side, "gpio_applied": [], "gpio_conflicts": [], "warnings": []}
    for func, p0 in GPIO[side].items():
        pkg = port_to_pkg.get(p0)
        if pkg is None:
            report["warnings"].append(f"{func}: P0.{p0:02d} not in port map")
            continue
        pin = ("U2", pkg)
        src = where.get(pin)
        if src is None:
            report["warnings"].append(f"{func}: U2.{pkg} absent from extraction")
            nets.setdefault(func, set()).add(pin)
            where[pin] = func
            continue
        if src == func:
            continue
        # rename/merge the net containing this MCU pin into the function name
        if src.startswith("N$") or src not in GPIO[side]:
            merge(src, func)
            report["gpio_applied"].append(f"{func}: U2.{pkg} ({src} -> {func})")
        else:
            report["gpio_conflicts"].append(f"{func}: U2.{pkg} currently in {src}")

    # SWD pins by port name
    for port, func in (("SWDIO", "SWDIO"), ("SWDCLK", "SWDCLK")):
        pkg = next((k for k, v in port_map.items() if v == port), None)
        if pkg:
            src = where.get(("U2", pkg))
            if src and src != func:
                merge(src, func)

    # 3. drop empty nets
    nets = {k: v for k, v in nets.items() if v}

    # 4. validation
    pins_per_ref = defaultdict(set)
    for n, pins in nets.items():
        for ref, pin in pins:
            pins_per_ref[ref].add(pin)
    expected2 = [r for r in pins_per_ref if r[0] in "RCL" and r not in ("L1",)]
    for r in sorted(pins_per_ref):
        if r[0] in "RC" and len(pins_per_ref[r]) != 2:
            report["warnings"].append(f"{r}: {sorted(pins_per_ref[r])} pins (expect 2)")
    for n, pins in nets.items():
        refs = defaultdict(list)
        for ref, pin in pins:
            refs[ref].append(pin)
        for ref, ps in refs.items():
            if len(ps) > 1 and ref[0] in "RCDFLQX" and ref != "L1":
                report["warnings"].append(f"net {n}: {ref} has {sorted(ps)} — internal short?")
    report["totals"] = {
        "nets": len(nets),
        "anonymous": sum(1 for k in nets if k.startswith("N$")),
        "pin_connections": sum(len(v) for v in nets.values()),
    }
    return {k: sorted([list(p) for p in v]) for k, v in nets.items()}, report


if __name__ == "__main__":
    nets_file = HERE.parent / "netlist" / "nets.json"
    raw = json.loads(nets_file.read_text())["nets"]
    port_map = u2_port_map()
    (HERE.parent / "netlist" / "u2_port_map.json").write_text(
        json.dumps(port_map, indent=1, sort_keys=True))
    overrides = load_overrides()
    for side in ("LHS", "RHS"):
        nets, report = canonicalize(side, raw, port_map, overrides)
        (HERE.parent / "netlist" / f"netlist_{side.lower()}.json").write_text(
            json.dumps(nets, indent=1, sort_keys=True))
        (HERE.parent / "netlist" / f"report_{side.lower()}.json").write_text(
            json.dumps(report, indent=1))
        print(f"== {side}: {report['totals']}")
        for w in report["gpio_conflicts"]:
            print("  CONFLICT:", w)
        for w in report["warnings"][:20]:
            print("  warn:", w)
