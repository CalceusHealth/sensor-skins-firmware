#!/usr/bin/env python3
"""Charging-link study: calibrate coupling k to Alex's bench data, then sweep
the puck drive, rectifier, alignment and RX coil options. Uses charging_sim.py."""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import charging_sim as C

ALEX = dict(top=57, counts=6)      # R2/R3 firmware: PWM_TOP 57, power 25 -> 6 counts
R4 = dict(top=52, counts=7)        # R4 firmware: PWM_TOP 52, power 30 -> 7 counts
out = {}

# 1. calibrate k: 470 R -> 3.178 V (large + large coils)
lo, hi = 0.10, 0.15
for _ in range(7):
    mid = (lo + hi) / 2
    v = C.run(k=mid, load="resistor", rload=470, vsys0=3.0, **ALEX)["vsys_V"]
    lo, hi = (mid, hi) if v < 3.178 else (lo, mid)
K = round((lo + hi) / 2, 4)
out["calibration"] = {
    "k": K,
    "470R_V (Alex 3.178)": C.run(k=K, load="resistor", rload=470, vsys0=3.0, **ALEX)["vsys_V"],
    "1k_V (Alex 4.9)": C.run(k=K, load="resistor", rload=1000, vsys0=4.0, **ALEX)["vsys_V"],
    "battery 3.7V (Alex ~5.5 mA, 130 mA USB)": C.run(k=K, vbat=3.7, **ALEX),
}
print("k =", K, json.dumps(out["calibration"], indent=1))

base = dict(k=K, vbat=3.8)
out["today_R4"] = C.run(**base, **R4)
out["today_R3"] = C.run(**base, **ALEX)
print("R4 today:", out["today_R4"])

# 2. drive on-time at each firmware frequency
out["on_time"] = {}
for top in (52, 57):
    for counts in (4, 6, 7, 9, 11, 14, 18, 22):
        r = C.run(**base, top=top, counts=counts)
        out["on_time"][f"top{top}_c{counts}"] = r
        print(f"top {top} ({r['f_kHz']} kHz) on {r['on_pct']:5.1f}%: charge {r['i_charge_mA']:6.2f} mA  USB {r['i_usb_mA']:6.1f} mA  eff {r['efficiency_pct']}%  TX {r['tx_coil_rms_A']} A")

# 3. frequency sweep at ~13 % on-time
out["frequency"] = {}
for top in range(46, 66, 2):
    counts = max(1, round(0.132 * (top + 1)))
    r = C.run(**base, top=top, counts=counts)
    out["frequency"][f"top{top}"] = r
    print(f"f {r['f_kHz']:6.1f} kHz on {r['on_pct']:4.1f}%: charge {r['i_charge_mA']:6.2f} mA  USB {r['i_usb_mA']:6.1f} mA  eff {r['efficiency_pct']}%")

# 4. rectifier
out["rectifier"] = {rect: C.run(**base, **R4, rect=rect) for rect in ("half", "bridge", "doubler")}
for k_, r in out["rectifier"].items():
    print("rectifier", k_, r["i_charge_mA"], "mA", r["efficiency_pct"], "%")

# 5. alignment / gap (k)
out["coupling"] = {f"k{kk}": C.run(**dict(base, k=kk), **R4) for kk in (0.06, 0.08, 0.11, 0.14, 0.18, 0.25)}
for k_, r in out["coupling"].items():
    print(k_, "charge", r["i_charge_mA"], "mA  USB", r["i_usb_mA"], "mA")

# 6. RX coil: etched PCB coil estimates vs TDK, retuned so L*C is constant
LC = 27.1e-6 * 33e-9
out["rx_coil"] = {}
for name, L, R in (("TDK wound 27.1uH 0.5R (today)", 27.1e-6, 0.5),
                   ("etched 2-layer ~12uH ~3R (est.)", 12e-6, 3.0),
                   ("etched 4-layer ~25uH ~5R (est.)", 25e-6, 5.0),
                   ("etched 4-layer ~25uH ~2.5R (2 oz, est.)", 25e-6, 2.5)):
    r = C.run(**base, **R4, lrx=L, rrx=R, crx=LC / L)
    out["rx_coil"][name] = r
    print(name, "charge", r["i_charge_mA"], "mA")

(C.OUT / "charging_sim.json").write_text(json.dumps(out, indent=1))
