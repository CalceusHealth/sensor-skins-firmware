#!/usr/bin/env python3
"""Circuit simulation of the puck -> insole wireless charging link (ngspice).

Puck (Reid Orthotic Charger R2 schematic + firmware):
  USB 5 V -> D3 (PMEG6010CEJ) -> C6+C8 (200 uF) -> Q1 PMOS high-side switch
  (DMG2305UX, gate straight from the ATtiny pin) -> coil node; parallel tank
  TX coil (TDK WR151580-48F2-G) || C7 33 nF C0G to 0 V. Low-side FETs DNF.
  Firmware: TCA0 split mode, 10 MHz clock, period = PWM_TOP+1 counts,
  switch on for coil_set_power(p) >> 2 counts.
Insole (R4 PWR sheet):
  RX coil (same TDK part) || C37 33 nF -> D1 CUS08F30 half-wave -> VSYS,
  D2 MM3Z5V1 5.1 V clamp, C40+C42; BQ24210 modelled as a linear charger
  (current limit ISET, small dropout) into the battery.

Unknown and fitted: coupling k (gap/alignment). Coil AC resistance is taken
from the datasheet (Rs <= 0.5 ohm @ 100 kHz). Averages are taken over the
last part of a long transient, once the tanks and VSYS have settled.

Requires libngspice (installed with KiCad) and PySpice in the .venv.
"""
import json
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
LIB = HERE.parent / ".venv" / "ngspice" / "libngspice.so"
OUT = HERE.parent / "v3_concept"

from PySpice.Spice.NgSpice.Shared import NgSpiceShared  # noqa: E402
NgSpiceShared.LIBRARY_PATH = str(LIB)
import logging  # noqa: E402
logging.getLogger("PySpice").setLevel(logging.CRITICAL)
_ng = None


def ng():
    global _ng
    if _ng is None:
        _ng = NgSpiceShared.new_instance()
    return _ng


CLK = 10e6
BASE = dict(
    vusb=5.0, rusb=0.15,
    ltx=27.1e-6, rtx=0.5, ctx=33e-9,
    lrx=27.1e-6, rrx=0.5, crx=33e-9,
    k=0.3,
    top=52, counts=7,                 # R4 firmware: PWM_TOP 52, power 30 >> 2 = 7
    load="battery", rload=470.0,
    vbat=3.8, iset=0.077, vdo=0.05, rpass=1.0,
    rect="half",                      # half | bridge | doubler
    cvsys=4.7e-6, vsys0=4.0,
    t_end=2.0e-3, t_avg=0.6e-3,
)


def netlist(p):
    T = (p["top"] + 1) / CLK
    ton = p["counts"] / CLK
    L = [".title charging link",
         f"VUSB usb 0 DC {p['vusb']}",
         f"RUSB usb usbr {p['rusb']}",
         "D3 usbr v5 DSCH3",
         "C6 v5 0 200u IC=4.7",
         "S1 v5 x g 0 SWQ",
         "DB x v5 DBODY",
         f"VG g 0 PULSE(0 1 0 5n 5n {ton:.4e} {T:.6e})",
         f"C7 x 0 {p['ctx']}",
         f"LTX x tx1 {p['ltx']}",
         f"RTX tx1 0 {p['rtx']}",
         "RSNS x 0 20k",
         f"LRX rf rx1 {p['lrx']}",
         f"RRX rx1 rfn {p['rrx']}",
         f"K1 LTX LRX {p['k']}",
         f"C37 rf rfn {p['crx']}"]
    if p["rect"] == "half":
        L += ["VRN rfn 0 0", "D1 rf vsys DSCH1"]
    elif p["rect"] == "bridge":     # floating tank, 4-diode bridge
        L += ["D1 rf vsys DSCH1", "D1B rfn vsys DSCH1",
              "D1C 0 rf DSCH1", "D1D 0 rfn DSCH1", "RFL rfn 0 1e7"]
    elif p["rect"] == "doubler":    # series cap + 2 diodes (Delon-style doubler)
        L += ["VRN rfn 0 0", "CDBL rf rfd 1u", "DDA 0 rfd DSCH1", "D1 rfd vsys DSCH1"]
    L += ["DZ 0 vsys DZEN",
          f"C40 vsys 0 {p['cvsys']} IC={p['vsys0']}"]
    if p["load"] == "resistor":
        L += [f"RL vsys 0 {p['rload']}"]
    else:
        L += [f"BCH vsys 0 I = min({p['iset']}, max(0, (V(vsys)-{p['vbat']}-{p['vdo']})/{p['rpass']}))"]
    L += [".model SWQ SW(Ron=0.06 Roff=1e7 Vt=0.5 Vh=0.05)",
          ".model DBODY D(IS=1e-12 N=1.5 RS=0.05 CJO=200p)",
          ".model DSCH1 D(IS=1e-6 N=1.0 RS=0.15 CJO=60p)",
          ".model DSCH3 D(IS=2e-6 N=1.0 RS=0.05 CJO=100p)",
          ".model DZEN D(IS=1e-14 N=1 BV=5.1 IBV=5m RS=10)",
          ".options method=gear reltol=1e-3 abstol=1e-9 vntol=1e-5 itl4=200",
          f".tran 4n {p['t_end']} 0 8n uic",
          ".end"]
    return "\n".join(L)


def avg(t, y, t0):
    m = t >= t0
    return float(np.trapezoid(y[m], t[m]) / (t[m][-1] - t[m][0]))


def rms(t, y, t0):
    m = t >= t0
    return float(np.sqrt(np.trapezoid(y[m] ** 2, t[m]) / (t[m][-1] - t[m][0])))


def run(**over):
    p = dict(BASE, **over)
    s = ng()
    s.destroy()
    s.load_circuit(netlist(p))
    try:
        s.run()
    except Exception:
        # PySpice treats ngspice 42's status chatter as a failed command;
        # the transient still completes - verified below by the end time.
        pass
    v = s.plot(simulation=None, plot_name=s.last_plot)
    t = np.array(v["time"]._data, dtype=float)
    if t[-1] < 0.999 * p["t_end"]:
        raise RuntimeError(f"transient stopped early at {t[-1]:.3e} s")
    t0 = p["t_end"] - p["t_avg"]
    vsys = np.array(v["vsys"]._data, dtype=float)
    iusb = -np.array(v["vusb#branch"]._data, dtype=float)
    itx = np.array(v["ltx#branch"]._data, dtype=float)
    V = avg(t, vsys, t0)
    Iu = avg(t, iusb, t0)
    if p["load"] == "resistor":
        Pout = avg(t, vsys ** 2, t0) / p["rload"]
        Ich = None
    else:
        ich = np.minimum(p["iset"], np.maximum(0, (vsys - p["vbat"] - p["vdo"]) / p["rpass"]))
        Ich = avg(t, ich, t0)
        Pout = Ich * p["vbat"]
    Pin = Iu * p["vusb"]
    return {"vsys_V": round(V, 3), "i_usb_mA": round(Iu * 1e3, 1),
            "i_charge_mA": None if Ich is None else round(Ich * 1e3, 2),
            "p_out_mW": round(Pout * 1e3, 2), "p_in_mW": round(Pin * 1e3, 1),
            "efficiency_pct": round(100 * Pout / Pin, 1) if Pin > 0 else None,
            "tx_coil_rms_A": round(rms(t, itx, t0), 3),
            "tx_coil_loss_mW": round(rms(t, itx, t0) ** 2 * p["rtx"] * 1e3, 1),
            "f_kHz": round(CLK / (p["top"] + 1) / 1e3, 1),
            "on_pct": round(100 * p["counts"] / (p["top"] + 1), 1)}


if __name__ == "__main__":
    print(json.dumps(run(), indent=1))
