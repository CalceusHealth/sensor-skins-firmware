# Firmware Build Matrix

## Purpose

This file is the local source of truth for firmware variants built from the `tim`
branch.

Use it to track:

- **what changed in each firmware version** (see Version history / changelog)
- **which version is currently built and deployed to the orthotics**
- **what features are next to test, then next to implement** (see Experimental tracks)
- which firmware features are in a build
- which DFU zip packages exist on disk
- which side (`lhs` / `rhs`) was built
- which transport mode was used
- which sample rate was used
- which local git commit should be used to recreate or roll back

## Variant dimensions

### Fixed defaults for the current line

- Sleep policy: `sleep` is the default and should remain enabled for all active builds.
- Device protection: code protection, hard-fault recovery, and error-tolerant behaviour are enabled by default.
- Device identity: Nordic chip `DEVICEID` is the canonical firmware device ID.
- Battery query: `QB` returns averaged `mV` plus fresh `RAW`.

### Variant axes that still matter

- Side: `lhs` or `rhs`
- Streaming: `stream` or `nostream`
- Flash summary: `ENABLE_FLASH_SUMMARY` OFF (default, `stream`/`nostream`) or ON (`summary` variant only). The legacy 5-min summary-to-flash write uses raw `NRF_NVMC`, which stalls the SoftDevice radio and drops the BLE link every ~343s while connected (`flash_write_record` cadence = `NEW_SUMMARY_EVERY_N` measurements ≈ 342.86s). Gated OFF from v00020027 so streaming/diagnostic builds no longer disconnect. The `summary` variant (for `;QR`/`;QM` record retrieval) re-enables it and should move to `nrf_fstorage_sd` so it can persist without dropping the link.
- Stream protocol: `ASCII_V1_TS` or `BINARY_V2`
- Sample rate: `8Hz`, `10Hz`, `12Hz`, `15Hz`, etc.
- FSR gain: currently `GAIN1_2` for the active high-force test line

## Branch model

- **`main`** — Alex's original/imported firmware baselines. Not used for builds.
- **`tim`** — the stable line we build and flash for testing. Keep experimental
  (IMU / sample-rate / binary-stream) work **off** this branch.
- **`experimental`** — IMU bring-up and sample-rate / `BINARY_V2` POC work.
  Branched from `tim`; the IMU commit (`f6d7ca9`, `v00020025`) was reverted off
  `tim` and lives here.

## Current status

- **Flashed to orthotics: `v00020024` (stream / `ASCII_V1_TS`)** — the streaming build is what's currently on the devices. Adds watchdog recovery + BLE sleep "lifeline" advertising (commit `45a3f6e`). The matching `nostream` v00020024 build is also packaged but is not the flashed variant.
- **Latest build: `v2.0.57` (stream / `BINARY_V2`) — SEN-59 round 3, the real fix, TESTED.** The 87.5 Hz ceiling was a **deterministic 1-in-8 skipped sample** (`main.c` `battery_update_divider` `continue`-ing past `measure_sensors()`), not BLE. Fixed. **Walk-test (sessions `876668b7`/`d0f3e651`) confirms: the every-7th-row 20 ms pattern is GONE and a foot hits true ~100 Hz (LEFT 99.5/99.9 Hz, 100% at 10 ms, 0 gaps).** BUT removing the skip doubled the real per-foot BLE load and exposed the genuine ceiling: **the Pixel 7a can't serve two `CONNECTION_PRIORITY_HIGH` links — it starves the second-connected foot** (RIGHT collapsed to ~57–62 Hz with 400–750 big 100–190 ms gaps; LEFT connects ~3 ms first and wins the airtime; no reconnects; RIGHT was the *clean* foot on v2.0.55, so not a bad unit). **Divider = firmware SEN-59 done; simultaneous dual-foot 100 Hz is a new central-side/mobile problem** (dual-connection scheduling; relates to SEN-66/SEN-69). Firmware idea to try: relax PPCP to ~15 ms so two links interleave.
- **Prior build: `v2.0.56` (stream / `BINARY_V2`)** — SEN-59 round 2: **deep HVN TX queue** (`hvn_tx_queue_size=4`) so several notifications buffer + drain per connection event, plus the SoftDevice RAM bump it needs (`RAM_START` 0x20002500→0x20003500 in the `.emProject`). Round 1 (v2.0.55) walk-test showed no gain and proved the depth-1 queue (1 notification/interval) is the real cap — see below. Keeps r1's conn interval + non-blocking TX. Built stream LHS+RHS; **awaiting a walk-test**. ⚠️ New runtime risk: if a foot won't advertise/connect after flashing, the SD RAM provisioning was short — reflash v2.0.53 and report (recoverable).
- **Round 1 result: `v2.0.55` — no improvement.** Walk-test session `6d6b366c` (2026-07-02): RIGHT 87.5 Hz / 14.3% 20 ms-gaps (≈ the 86 Hz baseline), LEFT *regressed* to 77.5 Hz / 22.4% with new 40–50 ms multi-slot gaps. Conn-interval tightening evidently not granted / not the bottleneck; non-blocking TX at depth-1 queue drops bursts on the foot the phone starves (two feet share one central's schedule). This localised the cap to the HVN queue depth → round 2.
- **Prior build: `v2.0.54` (nostream / `BINARY_V2`)** — SEN-54 IMU (LSM6DSM) bring-up on top of `v2.0.53`. Wires `i2c_init()`+`lsm6dsm_init()` into `main_init()` (previously the I2C bus was never brought up outside bus recovery), adds `lsm6dsm_whoami()` WHO_AM_I (expect 0x6A), and a new `;QU` query returning WHO_AM_I + fresh raw temp/accel/gyro. Also adds a re-entrancy guard to `msg_process_packet()` since `;QU` is the first query handler to touch I2C. Built **nostream only** (LHS+RHS, `c80a3fc`) — that's the test vehicle: on a stream build the `;RU` reply is buried in the continuous binary frames. This is the same functionality the reverted `experimental` `v00020025` attempted (via `;QD`), reimplemented cleanly on `tim` and avoiding that build's 7-arg-log blocker. Stream integration stays in SEN-68. **Hardware-validated on both feet (2026-07-02, SEN-54 done):** `WAI_OK=1`/`0x6A`, at-rest |accel| ≈ 2057 LSB (1.00 g) with gyro ~0, gyro tracks real rotation (hundreds of °/s) under motion, temp ~24 °C.
- **Prior build: `v2.0.53` (stream + nostream / `BINARY_V2`)** — SEN-58 cap-section optimisation: split the 8 cap-mux settling busy-loops onto a dedicated `CAP_SETTLING` (2000 cycles, from `MEAS_SETTLING`=10000). **Measured CAPUS 5.4 ms → 4.26 ms, MEASUS 8.8 → 8.40 ms** on RHS. Walk-test validated: all 6 `CAP*_friction_mag` peaks held/exceeded the `v2.0.50` baseline, raw cap full dynamic range (no `v2.0.51` collapse). At 100 Hz the device generates true 10 ms frames (measure 8.40 ms fits with ~1.6 ms headroom); the binding constraint is now **BLE transport** (~14% frame loss → 86 Hz effective), not `measure_sensors()`. Built on `93f88f1` (`tim`). Next: BLE-loss tuning (conn interval / rows-per-frame / latency) for solid 100 Hz.
- **SEN-58 progression (built, BINARY_V2): `v2.0.47`–`v2.0.52`** — step 2 (EasyDMA single bank scan, `v2.0.49`) and the `;QI CAPUS` cap-timing harness (`v2.0.50`) drove MEASUS 38.7 → 8.8 ms across `v2.0.46`→`v2.0.50`. `v2.0.51`/`v2.0.52` (cap convert reduction, persistent cap channels) **failed and were reverted** — neither moved cap time and `v2.0.51` collapsed cap signal; lesson captured in `v2.0.53` (settling was the real cost). `v2.0.50` is the validated fallback below `v2.0.53`.
- **Superseded: `v2.0.45` (BINARY_V2)** — `;CF` + `;QI LOOPMS/MEASUS` (baseline harness, MEASUS 38.7 ms). Rolled into `v2.0.46`.
- **Superseded: `v2.0.43`/`v2.0.44` (BINARY_V2)** — first binary build (SEN-53) and the `;CF` command (SEN-57); rolled into `v2.0.45`.
- **Deployed for field test: `v2.0.42` (ASCII_V1_TS)** — SEN-48 deterministic vbat acquisition (in-sequence read, decoupled ~5 s cadence, `VBAT_AVERAGE_N` 10→6); kills the v2.0.40 `_fresh`-fallback no-op that froze the average under ADC contention. Flashed to the two field units now being drained in-shoe to validate the 3250 mV protection. ASCII; folded into `f7b9ee6` (rebuild needs `STREAM_PROTOCOL_BINARY_V2` disabled).
- **`v2.0.41` (ASCII_V1_TS)** — monotonic charge-curve clamp in `battery_pack_charge()` (reported % holds/climbs across a charge session instead of cratering on plug-in). Reporting-only. ASCII; folded into `f7b9ee6`.
- **Superseded: `v2.0.40` (stream + nostream / `ASCII_V1_TS`)** — busy-ADC battery-average fix (the `_fresh` fallback, later found to be a no-op — see SEN-48); first `MAJOR.MINOR.PATCH` version reporting. `44e3ace`. Superseded by `v2.0.42`.
- **Superseded: `v00020026` (stream)** — battery moving-average reseed-on-recharge fix on top of the deployed `v00020024` line (`dfb7ac0`). Rolled into the `v2.0.40` build.
- **`experimental` HEAD: `v00020025`** — IMU bring-up (`f6d7ca9`), reverted off `tim`, not yet built or flashed. Tracked under Experimental tracks → IMU test.

Repo defaults (unchanged across the current line):

- Firmware source: `firmware/ble_app_firmware_v2_R7`
- Default stream protocol: **`BINARY_V2`** (as of `v2.0.43` / `f7b9ee6`; was `ASCII_V1_TS` through `v2.0.42`)
- Default sample rate: `8Hz` (`MAIN_LOOP_TIME_MS=125`) — to be raised under SEN-53 once the ceiling is characterised
- Default FSR gain: `NRF_SAADC_GAIN1_2`

## Version history / changelog

Newest first. "flashed" = on the orthotics now; "built" = packaged but not flashed; "source only" = committed but not yet built.

| version | state | changes | commit |
|---|---|---|---|
| `v2.0.57` | **built — latest (stream, BINARY_V2) — awaiting walk-test** | SEN-59 round 3 — **the real fix.** Walk-test data (sessions `49dbd950` v2.0.55, `813daad9` v2.0.56) proved the steady ~14% "loss" is a **deterministic 1-in-8 skipped sample**, not BLE: 20 ms device-clock gaps spaced *exactly* every 7th row (754/754) = 7 rows/80 ms = **87.5 Hz** — the ceiling on every foot in every build. Cause: `main.c` `battery_update_divider` did cheap battery housekeeping every 8th loop then `continue`d, skipping `measure_sensors()`. Fix: fall through and measure that slot too (keep `continue` only on the sleep path). The original "BLE transport frame loss" premise was a **misdiagnosis**. Rounds 1–2 retained (they cut the *secondary* bursty contention gaps — v2.0.56 clean foot: 1 big gap vs 213 on v2.0.55). Expect ~100 Hz. | `73dc710` (`tim`) |
| `v2.0.56` | tested — deep queue helped bursts, ceiling remained; superseded by `v2.0.57` | SEN-59 round 2: deep HVN TX queue (`hvn_tx_queue_size=4`) + RAM_START bump. Walk-test `813daad9`: the bursty multi-slot gaps **swapped feet** (LEFT clean/1 big gap, RIGHT 68) confirming central-side contention, and the deep queue cut the worst-foot bursts vs v2.0.55 — but the steady ~14%/87.5 Hz ceiling persisted (that was the divider, see `v2.0.57`). RAM change booted & streamed fine on hardware. | SEN-59 round 2: deep HVN TX queue. Round-1 walk-test (session `6d6b366c`) showed **no gain** — RIGHT 87.5 Hz/14% (= baseline), LEFT *worse* 77.5 Hz/22% with new 40–50 ms gaps — proving the cap is the **depth-1 HVN queue (1 notification/connection event)**, worsened by two feet sharing one phone. `sd_ble_cfg_set(BLE_CONN_CFG_GATTS, hvn_tx_queue_size=4)` so several notifications buffer + drain per event; `RAM_START` 0x20002500→0x20003500 in the `.emProject` for the extra SoftDevice RAM. Keeps r1's conn interval + non-blocking TX (now backed by the deep queue → buffers instead of drops). ⚠️ If a foot fails to advertise/connect after flashing, it's the SD RAM — reflash v2.0.53 and report. | `769999c` (`tim`) |
| `v2.0.55` | tested — **no improvement**, superseded by `v2.0.56` | SEN-59 round 1: conn interval 10–20→7.5–15 ms, slave latency 1→0, non-blocking stream TX. Walk-test (session `6d6b366c`, 2026-07-02): RIGHT 87.5 Hz/14.3% (unchanged from the 86 Hz baseline), LEFT 77.5 Hz/22.4% (regressed — non-blocking + depth-1 queue drops bursts). Conn-interval request evidently not granted / not the cap. Diagnosis → depth-1 HVN queue is the real limit (see `v2.0.56`). | SEN-59 round 1: BLE transport tuning for solid 100 Hz. Tightened connection interval 10–20 ms → **7.5–15 ms** and slave latency 1 → **0** (`ble_reid.c`); added a **non-blocking stream TX** (`ble_reid_tx_stream`) so a momentarily-full SoftDevice queue no longer busy-waits and stalls the 10 ms measurement loop (that stall is the mechanism behind the ~14% "lost" rows — link-layer retransmits mean frames aren't truly lost on-air; the loop was overrunning). Bulk query/record responses keep the blocking `ble_reid_tx`. Stream format unchanged (still v2). **Next:** walk-test vs the v2.0.53 86 Hz baseline; if still short of <2% loss, round 2 = deep HVN TX queue + multi-notification-per-event (needs a SoftDevice RAM bump). | `8a42057` (`tim`) |
| `v2.0.54` | built (nostream, BINARY_V2); IMU bring-up | SEN-54: IMU (LSM6DSM) bring-up. `main_init()` now calls `i2c_init()` + `lsm6dsm_init()` (`i2c_init` was otherwise only reached via i2c.c bus recovery, so the bus was dead). Added `lsm6dsm_whoami()` (WHO_AM_I reg 0x0F, expect 0x6A). New `;QU` query → fresh `lsm6dsm_update()` then `;RU WHOAMI=0x..,WAI_OK=..,T=..,AX/AY/AZ,GX/GY/GZ`. Re-entrancy guard added to `msg_process_packet()` (`;QU` is the first handler to touch I2C, whose busy-wait pumps `system_sleep()` → would otherwise re-enter and double-fire). Kept the boot WHO_AM_I `NRF_LOG_INFO` to 2 args, avoiding the `v00020025` 7-arg `LOG_INTERNAL_7` build blocker. Stream integration deferred to SEN-68. Test on **nostream** so the `;RU` reply isn't buried in the binary stream. | `c80a3fc` (`tim`) |
| `v2.0.53` | built (stream + nostream, BINARY_V2); source folded into `v2.0.54`. Still the current **stream** artifact — v2.0.54 was built nostream-only for IMU bring-up. | SEN-58 cap-section: split the 8 cap-mux `system_delay_cycles(MEAS_SETTLING)` busy-loops onto a dedicated `CAP_SETTLING`=2000 (FSR keeps `MEAS_SETTLING`=10000). **CAPUS 5.4 → 4.26 ms, MEASUS 8.8 → 8.40 ms** on RHS. Walk-test: all 6 `CAP*_friction_mag` peaks held/exceeded `v2.0.50`, raw cap full range (no `v2.0.51` collapse). 100 Hz now BLE-transport-bound, not measurement-bound. No wire/format change. | `93f88f1` (`tim`) |
| `v2.0.51`/`v2.0.52` | reverted (not built for release) | Cap convert reduction (3→2) and persistent cap channels — both **failed**: neither reduced CAPUS, and `v2.0.51` collapsed CAP1S ~25%. Reverted via `git checkout`; ruled converts/channel-reconfig/PAN-74 out as the cap cost. | — |
| `v2.0.50` | built — validated fallback (BINARY_V2) | `;QI` adds `,CAPUS=<us>` — cap-section duration (DWT-timed in `measure_sensors()`); the harness that localised the cap cost. Desktop prints it. MEASUS ~8.8 ms, CAPUS ~5.4 ms. | `7fcb4cd` (`tim`) |
| `v2.0.49` | superseded by `v2.0.53` | SEN-58 step 2: single EasyDMA bank scan (`adc_banks_begin`/`scan`/`end`) reads the 3 FSR banks in one scan instead of per-bank. **MEASUS 10.9 → ~8.8 ms** (validated signal-faithful: 8 Hz vs 100 Hz FSR+CAP peaks track). | `6c4b6d1` (`tim`) |
| `v2.0.47`/`v2.0.48` | superseded by `v2.0.53` | SEN-58: removed the SAADC bank throwaway read; time-based temp cadence (`TEMP_SAMPLE_PERIOD_MS`, gates the ~90 ms LMT01 blocking read off elapsed time not frame count, so it doesn't stall the loop at high rates). | `cdaf485` (`tim`) |
| `v2.0.46` | superseded by `v2.0.53` | SEN-58 step 1: SAADC inited once (main_init); removed per-frame `adc_init()`/`adc_deinit()` + 3 pre-init no-op "clear" reads from `measure_sensors()`. **MEASUS 38.7 ms → 10.9 ms** measured on RHS; CF-30 median frame 40→35 ms. No wire/format change. | `c596faf` (`tim`) |
| `v2.0.45` | superseded by `v2.0.46` | `;QI` now reports `,LOOPMS=<ms>,MEASUS=<us>` — active loop period (confirms `;CF`) + last `measure_sensors()` cost (the measurement floor), readable without RTT; `measure_last_us` stored each frame. Desktop parses/prints them. | `35467f4` (`tim`) |
| `v2.0.44` | superseded by `v2.0.45` | SEN-57: runtime `;CF <hz>` command — `main_loop_period_ms` becomes a live variable (default `MAIN_LOOP_TIME_MS`), clamped 1..200 Hz, wired into the RX parser + handler. Foundation for the in-app rate sweep / user "modes". Folded into `35467f4`. | `35467f4` (`tim`) |
| `v2.0.43` | superseded by `v2.0.45` | SEN-53: enable `STREAM_PROTOCOL_BINARY_V2` (ASCII_V1 off) as the default stream protocol for the >8 Hz migration. Add DWT µs profiling (`system_cycle_counter_init`/`system_cycles` in system.c) + a periodic `NRF_LOG_INFO("measure_sensors: %u us")` (every 40 frames) to find the per-frame measurement ceiling. Frame format unchanged (16 B header + 74 B rows, `measure.h`). Desktop decoder added in `sensor_gui/controllers/ble_controller.py`. | `f7b9ee6` (`tim`) |
| `v2.0.42` | **deployed for field test (ASCII_V1_TS)** | SEN-48: deterministic vbat acquisition. vbat read in-sequence in `measure_sensors()` (SAADC guaranteed idle) on a ~5 s time gate (`VBAT_SAMPLE_PERIOD_MS`), decoupled from frame rate; `battery_update()` split into `battery_submit_raw()` (averaging) + read-and-submit for sleep/QB; removed the awake per-8-frame ADC read in main.c; `VBAT_AVERAGE_N` 10→6 (~30 s window, trips the 3250 mV floor within ~1 min). Removes the v2.0.40 `_fresh`-fallback no-op that froze the average under ADC contention. ASCII; source folded into `f7b9ee6` (rebuild needs `STREAM_PROTOCOL_BINARY_V2` off). | `f7b9ee6` (`tim`) |
| `v2.0.41` | built (ASCII_V1_TS) | Monotonic charge-curve clamp in `battery_pack_charge()`: while charging, reported % is seeded from the last resting value and only holds/climbs, so plugging in no longer craters the number (dual-curve seam). Reporting-only — sleep matrix keys off averaged mV. ASCII; folded into `f7b9ee6`. | `f7b9ee6` (`tim`) |
| `v2.0.40` | superseded by `v2.0.42` | Battery: `battery_update()` no longer becomes a no-op when `adc_read_vbat_raw()` returns 0 on a busy SAADC — it falls back to a forced fresh conversion, so the moving average always lands a real sample and the reported level snaps correctly after a recharge. (The >150 mV reseed alone was being bypassed because it lives inside the dropped-sample validity guard.) Also switches version reporting to `MAJOR.MINOR.PATCH`: `VER=2.0.40` (was hex), explicit `DEVICE_FW_VERSION_MAJOR/MINOR/PATCH` defines composing the same packed uint32 (`0x00020028`), and `tools/common.sh` derives `vX.Y.Z` filenames + the packed `--application-version` (131112). **Wire-format change**: the app `VER=` parser must accept `X.Y.Z` (SEN-47). Folds in the `v00020027` flash-summary gating; no IMU. | `44e3ace` (`tim`) |
| `v00020027` | source only — built as part of `v2.0.40` | Gate the legacy 5-min summary-to-flash write behind new `ENABLE_FLASH_SUMMARY` (default OFF). `flash_write_record()` writes via raw `NRF_NVMC`, which stalls the SoftDevice radio and drops the BLE link every ~343s while connected (diagnosed from S3 logs: deterministic on both feet, ≈342.86s = `NEW_SUMMARY_EVERY_N` cadence). `stream`/`nostream` builds now compile it out → no more periodic disconnect. The `summary` build re-enables it (raw-NVMC for now; should become `nrf_fstorage_sd`). On the `v00020026` line; no IMU. | `32dcabf` |
| `v00020026` | **built, awaiting test (stream)** | Battery: reseed the vbat moving average when a fresh reading jumps >150 mV above the average (recharge), so reported voltage/% snaps to the real level instead of washing stale low samples out over minutes. Built on the `v00020024` line; no IMU. | `dfb7ac0` (`tim`) |
| `v00020025` | experimental branch — not built | Wake the LSM6DSM IMU: `i2c_init()`+`lsm6dsm_init()` in `main_init()`, `lsm6dsm_whoami()` + WHO_AM_I check, boot + per-loop RTT logging, new `;QD` BLE/serial query returning `WHO/AX/AY/AZ/GX/GY/GZ/T`. Reverted off `tim` (`6de6441`); lives on `experimental`. Build is currently blocked by a 7-arg `NRF_LOG_INFO` (`LOG_INTERNAL_7`) at `main.c` that must be split into two ≤6-arg calls. | `f6d7ca9` |
| `v00020024` | **flashed to orthotics (stream)** | Watchdog timer to recover from frozen sleep states; BLE "lifeline" slow advertising during sleep states. | `45a3f6e` |
| `v00020023` | superseded | ASCII_V1_TS line with `sleep`, `GAIN1_2`, `8Hz`; binary-stream scaffold added (compile-tested, not packaged). | `317f680` / `064bbd6` |
| `≤ v00020022` | historical | Earlier `stream`/`nostream` packages; see zip inventory and `git log`. | — |

## Active tracked builds

These are the builds that should be considered current and reproducible.

| status | version | side | stream | sleep | protocol | sample_rate | fsr_gain | `QI` `STREAM=` | package | sha256 | git_commit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| tested ✅ divider fixed (single-foot true 100 Hz) | `v2.0.57` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.57_sensorskins.zip` | `4a001f2638f44cf043b8bec9f7dcdc3d4a6dea6aa08ae3bd27d37079ac06704f` | `73dc710` |
| tested ✅ divider fixed (single-foot true 100 Hz) | `v2.0.57` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.57_sensorskins.zip` | `f10ef836889dae5bca9b5b4317404d5c125a66a09e03197542d2de920d3e9029` | `73dc710` |
| tested — ceiling remained, superseded | `v2.0.56` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.56_sensorskins.zip` | `dd05a041641130e8931ff86dbd56919d68839511db5673942d7f645392f92b8d` | `769999c` |
| tested — ceiling remained, superseded | `v2.0.56` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.56_sensorskins.zip` | `6b5a66e389c298edfe69f999851048b141abbbed3d1c7de8229d2f69bd58bb87` | `769999c` |
| tested — no gain, superseded | `v2.0.55` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.55_sensorskins.zip` | `d425892b22a4e0d4038e54e53f54d38f8316b5349e4f67ea2dc0c0d4446f1c40` | `8a42057` |
| tested — no gain, superseded | `v2.0.55` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.55_sensorskins.zip` | `50d7fa579cb450126657903fe672f0b70217ae562d1dda89f95fdb25d10fe170` | `8a42057` |
| hardware-validated (IMU bring-up) | `v2.0.54` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.54_sensorskins.zip` | `ddda25b3122989d972987618cb7f3f35dc739005e7bd836039b95b0e89dc2698` | `c80a3fc` |
| hardware-validated (IMU bring-up) | `v2.0.54` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.54_sensorskins.zip` | `e7772c565e8efba5e91cf63530a98e02560db6da84e2064dffb5b337200ade28` | `c80a3fc` |
| built | `v2.0.53` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.53_sensorskins.zip` | `6b352474f88631c7bcff88fd482b49277947f3060d25d40caad6120a4bb2c1c8` | `93f88f1` |
| built — latest | `v2.0.53` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.53_sensorskins.zip` | `e6b4515cff83cf7feb6f109ebec623aa578a685831b7ef7c594f7ccd6405d98b` | `93f88f1` |
| built — latest | `v2.0.53` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.53_sensorskins.zip` | `8947c0102c4a30a18829249f1975ad71fe69d9040952424085b9e31142896747` | `93f88f1` |
| built — latest | `v2.0.53` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.53_sensorskins.zip` | `5066eb8b097d03b44c910b76eb1ab61ea6ec93f4c563ef4d845652704a09c679` | `93f88f1` |
| validated fallback | `v2.0.50` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.50_sensorskins.zip` | `0aa6cd5f45efa277a20f31a1ede736f1c45e9fde3cbfc357a9c4650e7e41f52b` | `7fcb4cd` |
| validated fallback | `v2.0.50` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.50_sensorskins.zip` | `dc1a1cc598b8c00acbb7357a4280681001997f6b79eb2c55108196dc530ebb42` | `7fcb4cd` |
| validated fallback | `v2.0.50` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.50_sensorskins.zip` | `64a81035bdf246cbe0fd8fb4cf12c8c797d544121826cb1efdd6cff19f55bb77` | `7fcb4cd` |
| validated fallback | `v2.0.50` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.50_sensorskins.zip` | `6656d56b735060ad9c4b9d73ee2c6fcf8cf6246a4de22eb26500cae74a12e5f0` | `7fcb4cd` |
| superseded by `v2.0.50` | `v2.0.49` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.49_sensorskins.zip` | `7837e0b3c8bc44c76b40c4a4b919a271b5b8ec3761adcf051ec79b9d414901de` | `6c4b6d1` |
| superseded by `v2.0.50` | `v2.0.49` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `100Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.49_sensorskins.zip` | `2f82c2389dfebb6345ef611e7d489ff189fa3a84cba79a04341093a7dcabe86d` | `6c4b6d1` |
| superseded by `v2.0.49` | `v2.0.48` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.48_sensorskins.zip` | `3783adae2130dfd29e2acaf7b371f561e075e0509eeded28beebb003f0d584f6` | `cdaf485` |
| superseded by `v2.0.49` | `v2.0.47` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.47_sensorskins.zip` | `5f99fff4eb27ec2a2452d1991a4349695da83937b72311fb70c23ab90ed1afb2` | `cdaf485` |
| superseded by `v2.0.53` | `v2.0.46` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.46_sensorskins.zip` | `4389b356c7384151114186bd503e71138a0d2b84d8601fcf56f7c21aad5b6327` | `c596faf` |
| superseded by `v2.0.53` | `v2.0.46` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.46_sensorskins.zip` | `26a3c2499705f23f003fe3550bf4098d2b043f4bcd41437dd03512e4ec74f227` | `c596faf` |
| superseded by `v2.0.53` | `v2.0.46` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.46_sensorskins.zip` | `433038bc86de6fec3bc8c68303684b03a3425547498fc970d993a8847c82b86c` | `c596faf` |
| superseded by `v2.0.53` | `v2.0.46` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.46_sensorskins.zip` | `8d485e4c747a4083d1f593768e9b5e5a02c0fcc9871183dfa18c90f6cc487327` | `c596faf` |
| superseded by `v2.0.46` | `v2.0.45` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.45_sensorskins.zip` | `47d4f4958ebce078ae2314d4625007510bcaf41adcb0816dd120e2f01d43a545` | `35467f4` |
| superseded by `v2.0.46` | `v2.0.45` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.45_sensorskins.zip` | `a1ea802750abc99e1e8b538ea6ca834330b36e73a823d867cf73a435f98b31cb` | `35467f4` |
| superseded by `v2.0.46` | `v2.0.45` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.45_sensorskins.zip` | `1a5b12223f43dcbc1a605840633f85d6225267c811d07816adf7912a5a90900e` | `35467f4` |
| superseded by `v2.0.46` | `v2.0.45` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.45_sensorskins.zip` | `c19a7809e3a4e9b82e2e63ce3efe20224b1d0d3371f50ee9fd4b5aabcad0b93f` | `35467f4` |
| superseded by `v2.0.45` | `v2.0.43` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.43_sensorskins.zip` | `b36f174c8ba554ca8bc3c0eb6a88b4e949bbae35c2360e47ba47cc136cabce6b` | `f7b9ee6` |
| superseded by `v2.0.45` | `v2.0.43` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.43_sensorskins.zip` | `b5b6f176b21d1a1028dd680457423c848c600256fd745f459ebc3183e2756018` | `f7b9ee6` |
| superseded by `v2.0.45` | `v2.0.43` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.43_sensorskins.zip` | `0d4ef315675dca3b84470e6a3ba15577b260d9a5ffc4d7a47d057758e192771c` | `f7b9ee6` |
| superseded by `v2.0.45` | `v2.0.43` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.43_sensorskins.zip` | `15933d88fd2b9fe05916587530ac1a2658890dc4c6111ee6684a1c164eb4194c` | `f7b9ee6` |
| deployed — field test | `v2.0.42` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v2.0.42_sensorskins.zip` | `01abf9ee823ef9bfe8a1d71d26368045818325e7b340b9579548559e0fb64075` | `f7b9ee6` † |
| deployed — field test | `v2.0.42` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v2.0.42_sensorskins.zip` | `7764cad8ef04bdca4286f3ff2ea494b654677b6c11d8e448a23ea67c2fa72704` | `f7b9ee6` † |
| deployed — field test | `v2.0.42` | `lhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.42_sensorskins.zip` | `79c882bddabf2b7c4dd4e7a9722990825aa8c535ee6db0ffaa3203db129e8742` | `f7b9ee6` † |
| deployed — field test | `v2.0.42` | `rhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.42_sensorskins.zip` | `7339f475259bb5bf17597e247c866089ce3b3aaf305f43c4962c37c6b41f1ae6` | `f7b9ee6` † |
| built | `v2.0.41` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v2.0.41_sensorskins.zip` | `a6adb24f0cb016f06ea20c00eede5857dfce89a98f7e1e74648a90582c289686` | `f7b9ee6` † |
| built | `v2.0.41` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v2.0.41_sensorskins.zip` | `1b24c9ab4793c6d359cb62d57a645dc9716ebb7268ec0a8925ab4c613ac46535` | `f7b9ee6` † |
| built | `v2.0.41` | `lhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.41_sensorskins.zip` | `321fffd2b362b582f14c3c44d05c208d5c08abe757dbe493f5be5ef992d0c899` | `f7b9ee6` † |
| built | `v2.0.41` | `rhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.41_sensorskins.zip` | `22789436dfaf38f44a33c793669fed2e663867585bc873d850ecc64b7326195c` | `f7b9ee6` † |
| superseded by `v2.0.42` | `v2.0.40` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v2.0.40_sensorskins.zip` | `94bc8a30af3ad45450c2a9d503bc73d27c0fc812807df4ab7972c461ad7ec8f7` | `44e3ace` |
| superseded by `v2.0.42` | `v2.0.40` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v2.0.40_sensorskins.zip` | `e7a2f0db59ded1d0b9e84e9c9a5a221e36f010d9ed45046d68d55d5b4b5c0464` | `44e3ace` |
| superseded by `v2.0.42` | `v2.0.40` | `lhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.40_sensorskins.zip` | `01a635ab6f29e001387335892934778c55114344f3272a793d6310ae69354a36` | `44e3ace` |
| superseded by `v2.0.42` | `v2.0.40` | `rhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.40_sensorskins.zip` | `a052f11359383bee1eef0b230fd47ae3d2b3a34a5371ba003a0cc220fa7befcd` | `44e3ace` |

`*` `8Hz` is the default; from `v2.0.44` the rate is runtime-settable via `;CF <hz>` (SEN-57), 1–200 Hz, measurement-bound ceiling per SEN-58.

† `v2.0.41`/`v2.0.42` are ASCII intermediate builds whose source is folded into `f7b9ee6` (which builds `BINARY_V2` by default). To reproduce them, build `f7b9ee6` with `STREAM_PROTOCOL_BINARY_V2` disabled and `STREAM_PROTOCOL_ASCII_V1` enabled, at the matching `DEVICE_FW_VERSION_PATCH`. Only `v2.0.43` is reproducible from `f7b9ee6` as-is.
| superseded by `v2.0.40` | `v00020026` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v00020026_sensorskins.zip` | `9ddf67d03c56ae3f2e210248b8017caaf1cc84da2716463422103f34eb8ddce3` | `dfb7ac0` |
| superseded by `v2.0.40` | `v00020026` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v00020026_sensorskins.zip` | `44eac1513a7632db8489881d1d1e5050b5acbb31b3f74ee37ce468da43a3d992` | `dfb7ac0` |
| **flashed** | `v00020024` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v00020024_sensorskins.zip` | `dd2f1eff77211eaf8c67f62867c0d169c9792486fbfbb52e746cbcd9b4f6c8fa` | `45a3f6e` |
| **flashed** | `v00020024` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v00020024_sensorskins.zip` | `2cd45c6e6fa086017c88ab486401b8cf32283ceaf3c30b63fdad5d1f69d4148a` | `45a3f6e` |
| built (not flashed) | `v00020024` | `lhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v00020024_sensorskins.zip` | `888da4c2aa627f4cb5505014a106b874e0faaa2beb99b72dc5353b384c50a9d2` | `45a3f6e` |
| built (not flashed) | `v00020024` | `rhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v00020024_sensorskins.zip` | `0c5f70d65780354a37c3bd70a27f0a652e6ff61dc1aa15557c6ee4db12f9ce9e` | `45a3f6e` |
| superseded | `v00020023` | `lhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v00020023_sensorskins.zip` | `84599d5f3d052bd445268a82b2e04b7f44183c63efe1bae35ece6b66985030b7` | `a867666` |
| superseded | `v00020023` | `rhs` | `nostream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v00020023_sensorskins.zip` | `ec3cfa26b1ac0f7aeda7e664d5d9da97f6a077c93a6a28c955a286880ce55cdc` | `a867666` |
| superseded | `v00020023` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v00020023_sensorskins.zip` | `d633722a5db1790413ec474675e194ba0e89292bb559c59f11c1e001508b5a6b` | `317f680` |
| superseded | `v00020023` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v00020023_sensorskins.zip` | `cc947176bcc2e2c562797e57ea7a718c4849f659a63bd394352fcb99c8d710fd` | `317f680` |

## Experimental tracks (in progress)

Two experimental tracks run alongside the stable deployed line. Neither is
flashed to orthotics yet.

### 1. IMU test — `v2.0.54` (on `tim`, SEN-54) — **reimplemented, built**

- **State:** reimplemented directly on `tim` (`c80a3fc`, `v2.0.54`); **nostream LHS+RHS built** (rows in Active tracked builds). Supersedes the reverted `experimental` `v00020025` attempt below.
- **Query:** `;QU` (not the old `;QD`) → `;RU WHOAMI=0x..,WAI_OK=..,T=..,AX/AY/AZ,GX/GY/GZ`. Boot WHO_AM_I also logs over RTT under `ENABLE_DEBUG`.
- **Old blocker avoided:** the `v00020025` build failed on a 7-arg `NRF_LOG_INFO` (`LOG_INTERNAL_7` undefined); this build keeps the log to 2 args and compiled clean.
- **Next:** flash nostream to a pair, connect, `;QU` → confirm `WAI_OK=1` / `WHOAMI=0x6A` on both feet, then sanity-check accel (gravity, ~2048 LSB/g at 16g) tracks tilt and gyro spikes on rotation.
- **On success:** IMU is already on `tim`; then proceed to SEN-68 (stream the IMU into the `BINARY_V2` frame). Superseded historical attempt:

#### 1a. (historical) `v00020025` (on `experimental`)

- **State:** source written on `experimental` (`f6d7ca9`); reverted off `tim` (`6de6441`); never built. Superseded by `v2.0.54` above.
- **Blocker:** build failed on a 7-arg `NRF_LOG_INFO` ("IMU acc … temp", `LOG_INTERNAL_7` undefined) in `main.c`.

### 2. Binary stream test — `BINARY_V2`

- **State:** scaffold compile-tested on `v00020023`; not packaged or app-integrated.
- **Next:** build + first end-to-end app decode validation at `8Hz`.
- **Then:** raise sample rate `10Hz` → `12–15Hz`, only after row-integrity and battery checks.
- **Later:** combined binary + IMU — append `int16 imu_acc[3] + int16 imu_gyro[3]` to `reid_ble_stream_row_v2_t` (74 → 86 B/row), add flag `REID_STREAM_BINARY_V2_FLAG_IMU 0x04` (drops max rows/frame 3 → 2 at MTU 247); needs host decoder support.

## Existing zip inventory on disk

These packages currently exist in `artefacts/out/`:

- `nostream_sleep_{lhs,rhs}_v2.0.54_sensorskins.zip` (BINARY_V2; SEN-54 IMU bring-up + `;QU` — latest; nostream only)
- `stream_sleep_{lhs,rhs}_v2.0.53_sensorskins.zip` + `nostream_*` (BINARY_V2; SEN-58 cap-section, current stream artifact)
- `stream_sleep_{lhs,rhs}_v2.0.46_sensorskins.zip` + `nostream_*` (BINARY_V2; SEN-58 step 1, MEASUS ~10.9ms)
- `stream_sleep_{lhs,rhs}_v2.0.45_sensorskins.zip` + `nostream_*` (BINARY_V2; `;CF`+`;QI` instrumentation)
- `stream_sleep_{lhs,rhs}_v2.0.44_sensorskins.zip` + `nostream_*` (BINARY_V2; `;CF` command, folded into 35467f4)
- `stream_sleep_lhs_v2.0.43_sensorskins.zip` (BINARY_V2)
- `stream_sleep_rhs_v2.0.43_sensorskins.zip` (BINARY_V2)
- `nostream_sleep_lhs_v2.0.43_sensorskins.zip` (BINARY_V2)
- `nostream_sleep_rhs_v2.0.43_sensorskins.zip` (BINARY_V2)
- `stream_sleep_lhs_v2.0.42_sensorskins.zip` / `_rhs_` (ASCII; deployed for field test)
- `nostream_sleep_lhs_v2.0.42_sensorskins.zip` / `_rhs_` (ASCII)
- `stream_sleep_lhs_v2.0.41_sensorskins.zip` / `_rhs_` (ASCII) + `nostream_*_v2.0.41`
- `stream_sleep_lhs_v2.0.39_sensorskins.zip` / `_rhs_` (ASCII)
- `stream_sleep_lhs_v2.0.40_sensorskins.zip`
- `stream_sleep_rhs_v2.0.40_sensorskins.zip`
- `nostream_sleep_lhs_v2.0.40_sensorskins.zip`
- `nostream_sleep_rhs_v2.0.40_sensorskins.zip`
- `stream_sleep_lhs_v00020026_sensorskins.zip`
- `stream_sleep_rhs_v00020026_sensorskins.zip`
- `nostream_sleep_lhs_v00020024_sensorskins.zip`
- `nostream_sleep_rhs_v00020024_sensorskins.zip`
- `stream_sleep_lhs_v00020024_sensorskins.zip`
- `stream_sleep_rhs_v00020024_sensorskins.zip`
- `nostream_sleep_lhs_v00020023_sensorskins.zip`
- `nostream_sleep_rhs_v00020023_sensorskins.zip`
- `nostream_lhs_v00020019_sensorskins.zip`
- `nostream_lhs_v00020020_sensorskins.zip`
- `nostream_rhs_v00020019_sensorskins.zip`
- `nostream_rhs_v00020020_sensorskins.zip`
- `nostream_sleep_lhs_v00020021_sensorskins.zip`
- `nostream_sleep_rhs_v00020021_sensorskins.zip`
- `reid_ble_aginic_v2_r7_lhs_dfu.zip`
- `reid_ble_aginic_v2_r7_lhs_v00020018_nostream_dfu.zip`
- `reid_ble_aginic_v2_r7_lhs_v00020019_streaming_dfu.zip`
- `reid_ble_aginic_v2_r7_rhs_dfu.zip`
- `reid_ble_aginic_v2_r7_rhs_v00020018_nostream_dfu.zip`
- `reid_ble_aginic_v2_r7_rhs_v00020019_streaming_dfu.zip`
- `stream_lhs_v00020020_sensorskins.zip`
- `stream_rhs_v00020020_sensorskins.zip`
- `stream_sleep_lhs_v00020021_sensorskins.zip`
- `stream_sleep_lhs_v00020022_sensorskins.zip`
- `stream_sleep_lhs_v00020023_sensorskins.zip`
- `stream_sleep_rhs_v00020021_sensorskins.zip`
- `stream_sleep_rhs_v00020022_sensorskins.zip`
- `stream_sleep_rhs_v00020023_sensorskins.zip`

## Rollback guidance

If you need to roll back:

1. Use `git log --oneline tim` to find the local commit associated with the last known-good firmware source state.
2. Use this matrix to identify the matching package filename and protocol/sample-rate assumptions.
3. If needed, rebuild from that commit using `./tools/build_app.sh` and `./tools/package_dfu.sh`.

## Release workflow from now on

For every meaningful firmware change:

1. Make the firmware source change.
2. Commit it locally before building packages.
3. Build the required `lhs` / `rhs` packages.
4. Record the new rows in this file:
   - version
   - side
   - stream mode
   - protocol
   - sample rate
   - FSR gain
   - package filename
   - sha256
   - git commit
5. Only then hand the package to app testing or device DFU.
