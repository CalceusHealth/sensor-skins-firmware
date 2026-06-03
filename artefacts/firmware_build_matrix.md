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
- **Built, awaiting test: `v00020026` (stream / `ASCII_V1_TS`)** — battery moving-average reseed-on-recharge fix on top of the deployed `v00020024` line (IMU kept off `tim`). `tim` HEAD; LHS/RHS stream packages built (`dfb7ac0`). Next to flash + test.
- **`experimental` HEAD: `v00020025`** — IMU bring-up (`f6d7ca9`), reverted off `tim`, not yet built or flashed. Tracked under Experimental tracks → IMU test.

Repo defaults (unchanged across the current line):

- Firmware source: `firmware/ble_app_firmware_v2_R7`
- Default stream protocol: `ASCII_V1_TS`
- Default sample rate: `8Hz` (`MAIN_LOOP_TIME_MS=125`)
- Default FSR gain: `NRF_SAADC_GAIN1_2`

## Version history / changelog

Newest first. "flashed" = on the orthotics now; "built" = packaged but not flashed; "source only" = committed but not yet built.

| version | state | changes | commit |
|---|---|---|---|
| `v00020026` | **built, awaiting test (stream)** | Battery: reseed the vbat moving average when a fresh reading jumps >150 mV above the average (recharge), so reported voltage/% snaps to the real level instead of washing stale low samples out over minutes. Built on the `v00020024` line; no IMU. | `dfb7ac0` (`tim`) |
| `v00020025` | experimental branch — not built | Wake the LSM6DSM IMU: `i2c_init()`+`lsm6dsm_init()` in `main_init()`, `lsm6dsm_whoami()` + WHO_AM_I check, boot + per-loop RTT logging, new `;QD` BLE/serial query returning `WHO/AX/AY/AZ/GX/GY/GZ/T`. Reverted off `tim` (`6de6441`); lives on `experimental`. Build is currently blocked by a 7-arg `NRF_LOG_INFO` (`LOG_INTERNAL_7`) at `main.c` that must be split into two ≤6-arg calls. | `f6d7ca9` |
| `v00020024` | **flashed to orthotics (stream)** | Watchdog timer to recover from frozen sleep states; BLE "lifeline" slow advertising during sleep states. | `45a3f6e` |
| `v00020023` | superseded | ASCII_V1_TS line with `sleep`, `GAIN1_2`, `8Hz`; binary-stream scaffold added (compile-tested, not packaged). | `317f680` / `064bbd6` |
| `≤ v00020022` | historical | Earlier `stream`/`nostream` packages; see zip inventory and `git log`. | — |

## Active tracked builds

These are the builds that should be considered current and reproducible.

| status | version | side | stream | sleep | protocol | sample_rate | fsr_gain | `QI` `STREAM=` | package | sha256 | git_commit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| built — awaiting test | `v00020026` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v00020026_sensorskins.zip` | `9ddf67d03c56ae3f2e210248b8017caaf1cc84da2716463422103f34eb8ddce3` | `dfb7ac0` |
| built — awaiting test | `v00020026` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v00020026_sensorskins.zip` | `44eac1513a7632db8489881d1d1e5050b5acbb31b3f74ee37ce468da43a3d992` | `dfb7ac0` |
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
