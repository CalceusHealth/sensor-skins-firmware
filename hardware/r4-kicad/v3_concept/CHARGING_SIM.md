# Charging-link circuit simulation (puck → insole)

Model: `tools/charging_sim.py`, with the study in `tools/charging_study.py`. It runs on ngspice, using the libngspice that KiCad installed. Results are in `charging_sim.json`.

What the model includes:
- **Puck:** USB 5 V, D3, 200 µF, then the single high-side PMOS (Q1), driven at the firmware's real timing (10 MHz / (PWM_TOP+1), on for `power>>2` counts). The TX coil is in parallel with C7 33 nF.
- **Insole:** the RX coil with C37 33 nF, the D1 half-wave rectifier, the D2 5.1 V clamp, then VSYS. The BQ24210 is modelled as a linear charger into a 3.8 V cell.
- **Coils:** TDK WR151580-48F2-G, 27.1 µH, 0.5 Ω.

## Calibration against Alex's bench data (R2/R3 firmware: 172.4 kHz, 6 counts on)

| Measurement | Alex | Model (k = 0.1115) |
|---|---|---|
| Into 470 Ω | 3.178 V | 3.177 V (fitted) |
| Into 1 kΩ | 4.9 V | 4.74 V |
| Charge current | ~5.5 mA | 6.8 mA |
| USB current | 130 mA | 9.9 mA ✗ |

The USB current does not match. Alex's later build note reads: *"Current consumption is lower than I remembered at '25%' duty cycle"*. So treat the 130 mA as suspect, and measure USB current on a current puck.

Absolute currents are therefore approximate. **Comparisons between frequencies and options are the reliable part.**

## Findings

1. **R4's 188.7 kHz (PWM_TOP 52) only charges when the coupling is tight.** Charge current in mA into a 3.8 V cell, at ~13 % on-time:

   | Frequency (PWM_TOP) | k = 0.08 | 0.11 | 0.15 | 0.20 | 0.25 | 0.30 | 0.40 |
   |---|---|---|---|---|---|---|---|
   | 188.7 kHz (52), R4 | 0 | 0 | 0 | 2.5 | 9.7 | 17.3 | 33.4 |
   | 172.4 kHz (57), R3 | 4.0 | 6.9 | 11.2 | 17.1 | 23.5 | 29.4 | 32.8 |
   | **169.5 kHz (58)** | **5.5** | **8.5** | **12.8** | **18.7** | **24.7** | **29.5** | **29.0** |

   Two tuned coils have a transfer peak that moves with coupling. R4 sits on the peak for very tight coupling and falls off a cliff below it. ~170 kHz is near-best across the whole range. **R5** (`firmware/Reid Orthotic v2 Charger Firmware R5`) is R4 with PWM_TOP 58.

2. **Rectifier:** full-bridge and voltage-doubler versions come out 5–45 % *worse* than today's half-wave, because the extra diode drops cost more than they gain at this power. **Keep D1.**

3. **RX coil:** the etched coils (estimated L and R) lose badly.

   | RX coil | k = 0.11 | k = 0.25 |
   |---|---|---|
   | TDK wound | 8.5 mA | 24.7 mA |
   | Etched 2-layer | 0 mA | 0 mA |
   | Etched 4-layer | 0 mA | 2.7 mA |
   | Etched 4-layer, 2 oz | 1.8 mA | 11.9 mA |

   → Keep a wound coil, bonded to the board tab with its leads soldered to adjacent pads.

4. **On-time:** small gains. At k = 0.25, going from 13.6 % to 22 % on-time raises charge current by ~13 % (24.7 → 28.1 mA) at similar efficiency. A minor lever; watch puck temperature.

## Not modelled

- Magnet and ferrite eddy losses
- Coil resistance above the datasheet value
- MOSFET switching edges
- The puck's MCU and LED current
- The real coupling in today's top-charging setup (unknown; tight coupling through a 1–2 mm cover is likely k ≈ 0.2–0.4)

## A/B test: R4 vs R5 on real hardware

1. Flash one puck with R4 and one with R5. Put a USB power meter on each.
2. Use units starting at the same battery level (~50 %). Measure the charge rate as battery-voltage rise over 30 min, or with a meter in series with the cell. Repeat at three placements:
   - puck centred
   - puck offset 3 mm
   - puck over the thickest top cover in use
3. Record the USB current and puck temperature.
4. If R5 is equal or better at every placement, ship R5.
