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
- **Latest build: `v2.0.46` (stream + nostream / `BINARY_V2`)** — SEN-58 step 1: SAADC inited once at startup, no per-frame `adc_init()`/`adc_deinit()`. **Measured MEASUS 38.7 ms → 10.9 ms (~3.5×)** — the per-frame teardown was the dominant cost. CF-30 median frame 40→35 ms (no longer measurement-floored). Built on `c596faf` (`tim`). Next: SEN-58 step 2 (single EasyDMA bank scan) + step 5 (non-blocking temp).
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
| `v2.0.46` | **built — latest (stream + nostream, BINARY_V2)** | SEN-58 step 1: SAADC inited once (main_init); removed per-frame `adc_init()`/`adc_deinit()` + 3 pre-init no-op "clear" reads from `measure_sensors()`. **MEASUS 38.7 ms → 10.9 ms** measured on RHS; CF-30 median frame 40→35 ms. No wire/format change. | `c596faf` (`tim`) |
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
| built — latest | `v2.0.46` | `lhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_lhs_v2.0.46_sensorskins.zip` | `4389b356c7384151114186bd503e71138a0d2b84d8601fcf56f7c21aad5b6327` | `c596faf` |
| built — latest | `v2.0.46` | `rhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | `BINARY_V2` | `artefacts/out/stream_sleep_rhs_v2.0.46_sensorskins.zip` | `26a3c2499705f23f003fe3550bf4098d2b043f4bcd41437dd03512e4ec74f227` | `c596faf` |
| built — latest | `v2.0.46` | `lhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_lhs_v2.0.46_sensorskins.zip` | `433038bc86de6fec3bc8c68303684b03a3425547498fc970d993a8847c82b86c` | `c596faf` |
| built — latest | `v2.0.46` | `rhs` | `nostream` | `sleep` | `BINARY_V2` | `8Hz`* | `GAIN1_2` | none | `artefacts/out/nostream_sleep_rhs_v2.0.46_sensorskins.zip` | `8d485e4c747a4083d1f593768e9b5e5a02c0fcc9871183dfa18c90f6cc487327` | `c596faf` |
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

### 1. IMU test — `v00020025` (on `experimental`)

- **State:** source written on `experimental` (`f6d7ca9`); reverted off `tim` (`6de6441`); not yet built.
- **Blocker:** build fails on a 7-arg `NRF_LOG_INFO` ("IMU acc … temp", `LOG_INTERNAL_7` undefined) in `main.c`. `NRF_LOG_INFO` supports ≤6 varargs — split into two calls before building.
- **Next:** fix the log call, build `nostream` LHS/RHS, flash, confirm `WHO_AM_I = 0x6A` over RTT and via the `;QD` query, then sanity-check the accel/gyro/temp sample.
- **On success:** record the built packages as a row in Active tracked builds and merge the IMU back into `tim`.

### 2. Binary stream test — `BINARY_V2`

- **State:** scaffold compile-tested on `v00020023`; not packaged or app-integrated.
- **Next:** build + first end-to-end app decode validation at `8Hz`.
- **Then:** raise sample rate `10Hz` → `12–15Hz`, only after row-integrity and battery checks.
- **Later:** combined binary + IMU — append `int16 imu_acc[3] + int16 imu_gyro[3]` to `reid_ble_stream_row_v2_t` (74 → 86 B/row), add flag `REID_STREAM_BINARY_V2_FLAG_IMU 0x04` (drops max rows/frame 3 → 2 at MTU 247); needs host decoder support.

## Existing zip inventory on disk

These packages currently exist in `artefacts/out/`:

- `stream_sleep_{lhs,rhs}_v2.0.46_sensorskins.zip` + `nostream_*` (BINARY_V2; SEN-58 step 1, MEASUS ~10.9ms — latest)
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
