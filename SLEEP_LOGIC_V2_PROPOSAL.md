# Sleep Logic v2 Proposal — Motion-Anchored Sleep + Session/Reconnect Hardening

Status: PROPOSAL (2026-07-05). Successor to `SLEEP_LOGIC_MATRIX.md` and to the
v00020021 spec (`artefacts/device_sleep_firmware_spec.md`). Grounded in the
`tim`-branch audit of v2.0.59 (see "Defects this design fixes" below).

## 1. Context this design is built on

### Battery (Routejade FLPB301031, `artefacts/Routejade-FLPB301031-HPMW30-30.pdf`)

| Parameter | Value |
|---|---|
| Nominal capacity | 76 mAh (min 70 mAh, 0.2C to 3.0 V) |
| Nominal / full-charge voltage | 3.8 V / 4.35 V (high-voltage LiPo chemistry) |
| Discharge cut-off | 3.0 V |
| Max discharge | 2C = 140 mA |
| Impedance | < 400 mΩ (new) |
| Cold capacity | 59% at −20 °C, 86% at −10 °C |

Consequences:
- The firmware protection floor (3250 mV averaged) sits safely above the 3.0 V
  hard cut-off. Keep it.
- 76 mAh means **every 100 µA of quiescent drain costs ~32 days of shelf life**.
  The IMU left running (~0.4–0.6 mA) flattens a full cell in under a week and a
  half-charged one in 2–3 days — consistent with units found dead in storage.
- Usage is worn-all-day / charge-overnight, so the active budget target is
  ≥ 16 h streaming (≈ 4 mA average ceiling) and sleep must be ≤ tens of µA.

### Physical sensor layout (`artefacts/all_sensor_coordinates.csv`, CAP/FSR PNGs)

Y axis runs toe (top) → heel (bottom):

| Region | FSRs | CAPs | TMPs |
|---|---|---|---|
| Toes (5 diamonds; FSR1 = hallux … FSR5 = 5th toe) | 1–5 | CAP1 (hallux) | TMP1 |
| Forefoot / metatarsal heads (2 columns) | 6–13 | CAP2, CAP3, CAP4, CAP5 | TMP2–4 |
| Midfoot / arch | 14, 15 | — | — |
| Proximal heel | 16, 17 | — | — |
| Heel centre (stacked pair) | 18, 19 | CAP6 | TMP5 |

L/R are mirror images; firmware channel indices are identical per side (the
mux wiring differs, already handled in `measure.c`). Each CAPn has s/n (south/
north) electrodes = the 12 firmware channels.

Design use: a worn foot always loads heel (FSR16–19) *or* forefoot (6–13);
a foot *donning* a shoe cannot avoid motion. So worn-detection does not need
fragile absolute thresholds — motion is the primary signal, FSR/CAP deltas
are the confirmer, exactly as the v00020021 spec intended.

### Mobile app behaviour (`sensor-skins-mobile`)

- **Auto-reconnect is aggressive and permanent**: 5 s background poll for any
  saved device whenever the app runs and BT is on; reconnects on app launch and
  on BT off→on. A connected device is held connected indefinitely.
- **Idle monitoring streams all day**: after Stop, the app restarts streaming
  with an empty callback just for packets/FPS/battery display, at whatever rate
  was last set via ;CF (1–100 Hz presets).
- **Session flag lifecycle**: `;CX 1` sent once at record start (JS-owned link,
  before native handoff); `;CX 0` sent at stop after JS ownership is restored.
  Leak paths (all real):
  - `setSessionActive()` **silently skips any side not connected at that
    moment** — a foot that dropped mid-session never gets `;CX 0`;
  - write errors are swallowed (best-effort);
  - the stop flow can throw before reaching `;CX 0` (finalize/stitch steps);
  - app crash / force-kill / phone dies mid-session sends nothing;
  - `;CX` writes are blocked while the native owner holds the link.
- Native BLE owner (Android foreground service / iOS CoreBluetooth) holds the
  GATT link during recording; JS reconciles at stop.

### IMU (LSM6DSM)

- Currently 208 Hz accel+gyro from boot, **never powered down** —
  `lsm6dsm_deinit()` is uncalled and its power-down write is commented out.
- Has a hardware wake-on-motion engine: accel-only low-power mode (~5 µA at
  52 Hz), WAKE_UP_THS/WAKE_UP_DUR, readable via WAKE_UP_SRC or routable to INT1.
- **INT1 wiring confirmed from the R4 schematic (CAL1020): LHS = P0.25,
  RHS = P0.12.** The stale `gpio.h` comment saying RHS 11 is off by one —
  P0.11 is BAT_CHG on RHS (the schematic carries both sides' net labels; both
  firmware pin tables otherwise verify 100% against it). INT2 leaves the IMU
  (net `6DOF_INT2`) but does not land on an MCU pin — single-interrupt design.
  A GPIOTE PORT-event wake on INT1 is therefore available on both sides;
  polling WAKE_UP_SRC over I2C at the sleep check is the zero-risk fallback.

### Hardware facts verified from the R4 schematic (CAL1020, one PCB both sides)

- Side detect: `nIS_RHS` strap on P0.22 (grounded on RHS, open on LHS) —
  matches `PIN_IS_LHS`/`gpio_init()`.
- **Sensor power is properly gated**: MUX_VCC feeds all five 74LV4051 muxes
  and the 10k FSR dividers through PMOS Q3, controlled by MUX_ON (active low).
  Firmware parks MUX_ON high after each frame, so sensors draw nothing between
  measurements or in sleep. Once the IMU is managed, nothing on this board
  prevents a ~10 µA sleep floor (3V3 LDO is a TCR3UF33A, sub-µA quiescent).
- **Vbat divider is 100k:100k (÷2), not the 21:10 (×3.1) the firmware
  assumes** — see "Vbat scale" below.
- Charger is a BQ24210 with ISET R56 = 5.1 kΩ → ~80 mA charge current. The
  FLPB301031 cell's spec is 35 mA standard / **70 mA maximum** — we are
  charging above the cell's rated max, a plausible contributor to the
  high-impedance aged-cell failures seen in the field. Hardware fix is one
  resistor (10 kΩ → 40 mA); overnight charging makes the slower charge free.
- PG̅/CHG̅ are open-drain active-low with external 100k pull-ups
  (PG to VBAT via a 100k series into P0.10) — firmware polarity is correct.

### Vbat scale: the ×3.1 constant is a timing artifact on R4 hardware

`adc_vbat_raw_to_mv()` scales the pin voltage by 31/10 (a 21k:10k divider that
matches some earlier build). The R4 board divider is 100k:100k with a 100 nF
filter cap: the correct ratio is ×2.0, and the RC time constant seen by the
ADC node is (100k‖100k)·100n = **5 ms — exactly the settle delay the firmware
waits** (`adc.c` `nrf_delay_ms(5)`). Sampling at ≈1τ means the pin reads
~63% of VBAT/2, and ×3.1 ≈ 2/(1−e⁻¹) ≈ 3.16 almost exactly un-does that.
So the reported mV is ~2% low and *looks* right — but only by coincidence of
timing. This also finally explains the "fresh read ~1.5× high" mystery
(`battery.c` comment, QB RAW ≈ 4620): a more-settled read approaches the true
VBAT/2, i.e. 1/0.632 ≈ 1.58× the 1τ-sampled normal read — and 4620 raw ⇒
~4.06 V, a sane battery voltage.

Consequences: absolute accuracy rides on C2's ±10–20% tolerance and on
code-path timing (BLE interrupts stretching the delay) — worst case a few
hundred mV of per-unit error against a 200 mV protection hysteresis.

Fix (cheap): settle ≥5τ (25–30 ms) before sampling, change the constant to
×2.0, verify against a multimeter on a few units. Cost: ~20 µA through the
divider for 25 ms every 5 s — negligible. The 3250/3450 thresholds should
transfer as-is (current readings are ≈2% low), but re-check during the bench
calibration.

### Deep-discharge: the drain defects destroy cells, not just charge

There is **no functional barrier between this cell and 0 V** (verified
2026-07-05): the FLPB301031 datasheet documents a bare cell with no PCM (its
safety section abuse-tests naked cells; the 5 V/3 C overcharge runs to
completion); the board's UVLO provision is explicitly unfitted (R58 DNF, per
the schematic note); MCU brownout is disabled in firmware (`system.c:75`);
`system_shutdown()` is compiled out; and "protection sleep" keeps drawing
~0.5 mA (IMU) while the 3450 mV recovery wake re-burns rest recovery. Below
the ~3.0 V knee that drain takes the cell to <1.5 V within days → copper
dissolution → permanent internal micro-shorts. The resulting field signature:
0.00 V off charge, runs and shows state 'C' indefinitely on the puck
(BQ24210 precharge powers the board; the shorted cell never reaches CV), dead
the moment it leaves the puck — matching at least one known lab unit
(REIDLHS E1:2F:FB:FA:78:EB). This happens to **any unit left off charge for
a few weeks via D1 alone**; D2 only accelerates it. Fixes #1/D1 and D9 are
therefore hardware-preservation fixes, not just battery-life fixes.
(When diagnosing such a unit: measure at the cell tabs — a broken tab weld
mimics the connector-level symptom but shows normal voltage at the tabs.)

## 2. Defects this design fixes (tim-branch audit, v2.0.59)

| # | Defect | Effect |
|---|---|---|
| D1 | IMU never powered down (`lsm6dsm.c:74` commented out, no caller) | "Sleep" floor ≈ 0.5 mA; flat in days |
| D2 | `session_active` never cleared on disconnect (`messaging.c:426` only writer; `ble_reid.c:323` doesn't touch it) | Forgotten session + link loss = awake at full rate until battery floor, then 3250/3450 sawtooth to deep discharge |
| D3 | Connection alone blocks sleep forever (`main.c:480`) | Any phone with the app near an idle device holds it at up to 100 Hz all day |
| D4 | FSR **level** wake in sleep loop (`main.c:510`, ≥5 of 19 FSRs ≥ 10 counts) | In-shoe-not-worn oscillates 180 s awake / 5 s asleep; contradicts the v00020021 spec's own design principle |
| D5 | Sleep-loop delta wake compares against a stale pre-sleep baseline (`previous_ble_data` never refreshed while asleep) | Slow cap/thermal drift causes spurious 180 s wake bursts |
| D6 | Idle timers accumulate compile-time `MAIN_LOOP_TIME_MS` (125 ms) per loop instead of `main_loop_period_ms` (`main.c:667,674`) | Timeout is 14.4 s wall at 100 Hz, 24 min at 1 Hz |
| D7 | Vbat 150 mV upward reseed has no debounce (`battery.c:87`) | One relaxation spike on a high-impedance cell flushes the average and can clear low-battery protection instantly |
| D8 | 3250 floor measured under load, 3450 wake measured at rest | IR-drop delta on aged cells eats the 200 mV hysteresis → end-of-charge sleep/wake sawtooth |
| D9 | Protection sleep still accepts connections and serves commands | A phone can hold a 7.5–15 ms link against a critically low cell |
| D10 | Vbat scale constant (×3.1) is wrong for the R4 100k:100k divider; readings are only right because the 5 ms settle ≈ 1τ of the divider RC (see §1) | Per-unit absolute error up to a few hundred mV from cap tolerance/timing jitter, against a 200 mV protection window |

## 3. State machine

Five runtime states (extends the v00020021 model with one new state):

```
ACTIVE ──(idle: no session ∧ no motion ∧ no deltas, T_IDLE_CONN)──▶ IDLE_CONNECTED   (connected)
ACTIVE ──(idle: no BLE ∧ no motion ∧ no deltas, T_IDLE)──────────▶ SLEEP             (not connected)
IDLE_CONNECTED ──(motion ∨ deltas ∨ session ∨ command)───────────▶ ACTIVE
IDLE_CONNECTED ──(disconnect)────────────────────────────────────▶ ACTIVE (idle timer keeps running)
SLEEP ──(IMU wake-on-motion ∨ BLE connection)────────────────────▶ ACTIVE
ANY ──(vbat_avg < 3250)──────────────────────────────────────────▶ PROTECTION_SLEEP
PROTECTION_SLEEP ──(vbat_avg ≥ 3450, debounced ×2)───────────────▶ ACTIVE
ANY ──(bench_keepawake)──────────────────────────────────────────▶ BENCH (no auto-sleep)
```

Per-state hardware policy:

| State | Loop | Sensors | IMU | BLE |
|---|---|---|---|---|
| ACTIVE | `main_loop_period_ms` | full frame | 208 Hz A+G | as-is (stream if connected) |
| IDLE_CONNECTED | 500 ms frames internally | full frame at 2 Hz | accel-only LP 52 Hz + WoM, **gyro off** | connection kept; stream at reduced cadence |
| SLEEP | 5 s check | none (see wake table) | accel-only LP + WoM (~5 µA), gyro off | lifeline adv (5 s interval) |
| PROTECTION_SLEEP | 60 s check | none | **fully powered down** | lifeline adv; force-disconnect any connection after 30 s unless charging_confirmed |
| BENCH | `main_loop_period_ms` | full | full | full |

Notes:
- IDLE_CONNECTED is new and is what fixes D3 without fighting the app's
  deliberate always-connected design: the link stays up, the app still gets
  battery/status frames, but the device drops from up to ~5 mA to well under
  0.5 mA. The user's chosen ;CF rate is *not* overwritten — the reduction is an
  internal cadence that ends the moment activity/session/command arrives.
- SLEEP no longer measures FSR/CAP at all (fixes D4/D5 and removes the 5 s
  measurement burst): you physically cannot don or use the insole without
  motion, and a phone connecting wakes it regardless.

## 4. Wake & stay-awake signals

| Signal | Source | Role |
|---|---|---|
| Motion | LSM6DSM WoM (poll WAKE_UP_SRC each sleep check; INT1 later if pin confirmed) | **Primary wake** from SLEEP/IDLE_CONNECTED; also refreshes `last_motion_ms` in ACTIVE |
| Sensor deltas | existing `fsr_delta_score ≥ 2` ∨ `cap_delta_score ≥ 2` (150/80 thresholds) | Stay-awake confirmer in ACTIVE (covers worn-but-motionless edge, e.g. seated feet-still) |
| BLE connection | SoftDevice event | Wakes from SLEEP; in ACTIVE counts as intent only together with session/commands (see D3 fix) |
| BLE command | any valid `;`-command | Refreshes `last_ble_activity_ms` (120 s hold, unchanged) |
| Session latch | `;CX 1` | Stay-awake, **but motion-gated when disconnected** (below) |
| Bench latch | `;CK 1` | Absolute stay-awake |
| Charging | confirmed 3-of-5 | Never wakes; 15 s idle → sleep (unchanged from spec) |

**Session latch, motion-gated (fixes D2):** `session_active` suppresses sleep
only while `(connected) ∨ (time_since_disconnect < SESSION_DISCONNECT_GRACE_MS)
∨ (time_since_motion < MOTION_HOLD_MS) ∨ (worn_static_load)`. A mid-walk BLE
drop keeps recording because the wearer is moving (the flag's original purpose,
per the app comment "so it won't enter smart-idle sleep during brief BLE drops
mid-recording"). Shoes-off + phone-gone stops holding the device awake once
motion stops. The flag itself survives sleep, so a reconnecting app finds
consistent state — but the app-side reconcile (below) is the real cleanup.

`worn_static_load` (seated-wearer guard) — **tuned against 9 labelled sitting
sessions from S3 field data (2026-07-05, see §"Sitting-session validation")**:
**any single FSR channel** with a 5 s-window median ≥ **25 firmware-raw counts**
(≈100 in host/CSV units) refreshes a **300 s hold**, same duration as the
disconnect grace and motion hold. Any-channel, not heel-specific: the field
data shows seated load landing on arch (FSR15), forefoot, or heel depending on
posture — sometimes on only one channel. Evaluated **only while
`session_active` is set**, so it cannot recreate the shipping/in-shoe-not-worn
failure modes of level-based logic (no session flag in those scenarios). This
keeps a long-seated wearer's device awake through an extended phone disconnect,
so streaming resumes with zero wake latency on reconnect. Note the seated case
is already safe whenever the link is up: session + connection pin the device in
ACTIVE at full rate for hours, regardless of motion or deltas.

**Level-based FSR wake is deleted** (D4). Static load never wakes or holds
awake — restoring the v00020021 principle the implementation drifted from.

### Sitting-session validation (S3 field data, 2026-07-05)

Nine labelled `sitting` foot-sessions (7 indoor + car, 11–58 min, 8 Hz and
100 Hz eras, four device pairs) were replayed against the firmware's activity
logic. CSV values are host-domain (FSR ×4 / CAP ×3); all numbers below are
converted to firmware-raw. Scale sanity-checked: one session pegs an FSR at
4044 raw = the known 16176/4 ceiling.

Findings:

1. **Every sitting session would sleep mid-session under the current
   delta-based activity logic** — longest delta-inactive streaks of 227–1007 s
   against the 180 s idle timeout, in all nine. A rate-independent variant
   (compare vs 1 s ago) still sleeps in 8 of 9. Delta activity cannot protect
   seated sessions; the guard is mandatory, not defensive.
2. **Static load, when present, is rock solid**: floor-resting feet show 4–15
   channels ≥25 raw in 100% of 5 s windows for the whole session. But load is
   posture-dependent and per-foot: dangling/crossed feet drop to a single
   channel (e.g. arch FSR15 median 114 raw for 58 min in a car) with
   intermittent dropouts.
3. **Longest genuine gap between guard events (load ≥25 raw ∨ motion)
   across all sessions: 85 s** → the 300 s hold bridges every observed gap with
   >3× margin. (One apparent 477 s gap turned out to be a real 475 s BLE
   dropout — no rows recorded — i.e. exactly the disconnected-mid-session case
   this guard exists for.)
4. **Car sitting is fully covered by motion alone**: vehicle vibration keeps
   the ~62.5 mg wake-on-motion proxy firing (longest still runs 33–105 s).
   Indoor motionless sitting shows genuine 340–390 s IMU-still runs — motion
   alone is NOT sufficient; the load guard carries those stretches.
5. **CAP absolute level does not discriminate worn vs handled**: a
   held-in-hand demo session shows the same cap range (≈3 500–7 300 raw) as
   worn feet. The empty-shoe bench capture (v00020021 open item) is still the
   missing baseline; until then CAP stays out of the worn decision.
6. Residual accepted risk: both feet dangling + wearer truly motionless for
   >5 min while disconnected → that foot's device sleeps. No data is lost that
   wasn't already lost (nothing transmits while disconnected); reconnection
   wakes it in ~5–10 s.

## 5. Timing & battery constants

| Constant | Value | Note |
|---|---|---|
| `IDLE_SLEEP_TIMEOUT_MS` | 180 000 | unchanged; **accumulate wall-clock, not loop counts** (fixes D6) |
| `IDLE_CONNECTED_TIMEOUT_MS` | 120 000 | new: connected, no session, no motion/deltas → IDLE_CONNECTED |
| `SESSION_DISCONNECT_GRACE_MS` | 300 000 | new: session latch honoured this long after link loss even without motion |
| `MOTION_HOLD_MS` | 300 000 | new: how long motion keeps the latch alive (300 s bridges all observed seated gaps ≥3×) |
| `WORN_LOAD_THRESHOLD` / hold | 25 raw / 300 000 | new: any 1 FSR channel, 5 s-window median (session-latched only); field-validated |
| `BLE_ACTIVITY_HOLD_MS` | 120 000 | unchanged |
| `SLEEP_CHECK_EVERY_MS` | 5 000 | unchanged (WAKE_UP_SRC poll + vbat every 12th check) |
| Vbat cadence | 5 s awake / 60 s asleep | protection latency is irrelevant at µA drain |
| `LOW_BATTERY_SLEEP_MIN_MV` / wake | 3250 / 3450 | unchanged, but wake requires **2 consecutive** qualifying averages (D7/D8) |
| Vbat reseed | 150 mV, **2 consecutive** samples | fixes D7 |
| WoM threshold | start 62.5 mg (WAKE_UP_THS=1 @ FS 2g equiv), 1 sample duration | tune on bench; err sensitive — a false wake costs one 180 s awake window (~0.2 mAh) |

Projected budgets (to verify with a power profiler — the SEN-49 audit):

| State | Est. current | 76 mAh lifetime |
|---|---|---|
| ACTIVE 100 Hz streaming | ~4–6 mA (measure) | 13–19 h → survives a worn day |
| IDLE_CONNECTED | < 0.5 mA | weeks |
| SLEEP (v2: IMU WoM, no sensor polls) | ~10–15 µA | ~7 months |
| SLEEP (today, v2.0.59) | ~0.5 mA | ~6 days |

## 6. Disconnect / reconnect logic

Firmware:
1. `BLE_GAP_EVT_DISCONNECTED`: record `last_disconnect_ms`. Do **not** clear
   `session_active` (grace + motion gating handles it); do not reset
   `main_loop_period_ms`.
2. On connect: nothing automatic — wait for app reconcile.
3. Add session/rate visibility to `;QI`: append `SESSION=<0|1>,RATE=<hz>` so
   the app can reconcile instead of assuming.
4. PROTECTION_SLEEP: if a central connects and `charging_confirmed == 0`,
   allow 30 s (enough for the app to read `;QB` and show "battery critical")
   then force-disconnect (fixes D9).

App (sensor-skins-mobile):
1. **Reconcile on every (re)connect** — the single most robust fix, heals every
   `;CX` leak path including crashes and skipped sides: after services resolve,
   if not recording send `;CX 0`; if recording (reconnect mid-session) send
   `;CX 1`. Idempotent, one write.
2. Move `;CX 0` into the stop flow's `finally` (currently unreachable when
   finalize throws), and queue it per-side: a side that is disconnected at stop
   gets `;CX 0` on its next reconnect (covered by reconcile, but log it).
3. When idle (not recording), optionally stop the idle-monitoring stream or
   accept IDLE_CONNECTED's reduced cadence (no app change needed — frames just
   arrive slower; battery frame stays ~1 per emission cycle).

## 7. Firmware change list (implementation order)

1. **IMU power management** (D1): restore `lsm6dsm_deinit()` write; add
   `lsm6dsm_enter_wom()` (accel LP 52 Hz, gyro PD, WAKE_UP_THS/DUR, latched
   status) and `lsm6dsm_exit_wom()` (restore 208 Hz A+G); call in state
   transitions. Verify WHO_AM_I after every mode change (I2C bus recovery
   already exists).
2. Wall-clock idle timers (D6) — trivial, independent, ship first with #1.
3. Session grace + motion gating (D2) in `ble_activity_detected()` /
   new `intent_detected()`.
4. Delete FSR-level wake; replace sleep-loop measurement with WAKE_UP_SRC poll
   (D4, D5).
5. IDLE_CONNECTED state (D3).
6. Vbat debounces (D7, D8) in `battery_submit_raw()` /
   `battery_sleep_protection_required()`.
7. **Vbat ratio-metric fix (D10)**: 25–30 ms settle + ×2.0 constant in
   `adc_vbat_raw_to_mv()`, multimeter verification on ≥3 units before rollout
   (do this in the same release as #6 so thresholds are validated once).
8. Protection-sleep connection policy (D9).
9. `;QI` SESSION=/RATE= fields; app reconcile ships alongside.
10. Once WoM is proven by polling, move to the INT1 GPIOTE PORT-event wake
    (LHS P0.25 / RHS P0.12) and lengthen `SLEEP_CHECK_EVERY_MS` — the CPU then
    only wakes for vbat housekeeping.

## 8. Test matrix (extends the v00020021 seven)

| # | Scenario | Expected |
|---|---|---|
| 1 | On puck, stable charge | asleep ≤ 15 s after idle |
| 2 | On puck, flickering C/K | sleeps after 3-of-5 confirmation; no wake from charge events |
| 3 | Worn, standing/walking | ACTIVE throughout (motion) |
| 4 | Worn, seated, feet still 10 min | stays ACTIVE via FSR/CAP deltas (breathing/micro-shifts); if it sleeps, WoM wakes it on first movement — verify data loss ≤ 5 s |
| 5 | In shoe, not worn, overnight | SLEEP within 5 min, **stays asleep** (was: oscillated) |
| 6 | Bench `;CK 1` | never sleeps |
| 7 | Shipping static preload, weeks | SLEEP, ~10–15 µA |
| 8 | **Forgotten session, shoes off, phone connected** | IDLE_CONNECTED ≤ 5 min (was: 100 Hz forever) |
| 9 | **Forgotten session, shoes off, phone gone** | SLEEP after grace+idle ≤ ~8 min (was: awake to battery floor) |
| 10 | App crash mid-session, reinstall/relaunch | reconcile clears `;CX` on reconnect |
| 11 | Desk + background app auto-reconnect | IDLE_CONNECTED, < 0.5 mA (was: full rate) |
| 12 | Aged cell at 3250 floor | protection sleep; no sawtooth (debounced wake); connection ejected after 30 s |
| 13 | Mid-walk 10 s BLE drop during recording | stays ACTIVE (session + motion), rows buffered per SEN-62 |
| 14 | **Seated 60 min mid-session, connected** | stays ACTIVE at full rate (session + connection; activity signals not consulted) — sustained heel FSR16–19 data keeps streaming |
| 15 | Seated mid-session, phone disconnected 20 min | stays awake via `worn_static_load` guard; streaming resumes instantly on reconnect |

## 9. Open items

- ~~Confirm INT1 pin per side~~ **Done (R4 schematic): LHS P0.25, RHS P0.12.**
  Fix the stale `gpio.h` comments when wiring it up. Start with WAKE_UP_SRC
  polling anyway (no GPIOTE dependency, proven first).
- Bench-tune WAKE_UP_THS with the insole in a carried gym bag (should wake) vs
  a parcel in transit (will wake — acceptable: it re-sleeps in 3 min if nothing
  else happens; consider a longer WAKE_UP_DUR if transit wakes prove costly).
- Power-profile ACTIVE at 8/50/100 Hz and v2 SLEEP (SEN-49) to replace the
  estimates in §5.
- Bench-verify the vbat D10 story before changing constants: scope ADC_VBAT
  during a read (expect ~63% of VBAT/2 at the 5 ms sample point) and compare
  `;QB` RAW (settled fresh read) against multimeter ÷2.
- Decide whether IDLE_CONNECTED's reduced stream cadence needs an app toggle
  (current app tolerates arbitrary frame gaps in idle monitoring).
- Hardware (next board spin / rework): charge current 80 mA exceeds the cell's
  70 mA max (35 mA standard) — ISET 5.1 k → 10 k. Possible link to aged
  high-impedance cells in the field.
- Re-cell triage & checklist (2026-07-05, dead-batch units — all rails 0.00 V
  off charge but run + show 'C' on puck): that signature is also produced by
  an open F1 (200 mA fuse between VBAT rail and J4) or broken tab weld with a
  perfectly healthy cell — measure BOTH sides of F1 off-charge before scrapping
  a unit. For the Master Instruments replacement cells: confirm charge-voltage
  compatibility with the BQ24210's 4.2 V regulation, check rated charge current
  (do the ISET 10 k rework while units are open), ship the IMU power-down fix
  before/with re-celled units (new cells inherit the deep-discharge kill chain
  otherwise), and store spares disconnected at ~3.8 V with periodic top-up
  (the old batch was 3× past the 1-year storage spec).
