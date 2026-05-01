# Firmware Build Matrix

## Purpose

This file is the local source of truth for firmware variants built from the `tim`
branch.

Use it to track:

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

## Current repo defaults

- Firmware source: `firmware/ble_app_firmware_v2_R7`
- Current version macro: `0x00020023`
- Current default stream protocol in repo: `ASCII_V1_TS`
- Current default sample rate in repo: `8Hz` (`MAIN_LOOP_TIME_MS=125`)
- Current default FSR gain in repo: `NRF_SAADC_GAIN1_2`

## Active tracked builds

These are the builds that should be considered current and reproducible.

| status | version | side | stream | sleep | protocol | sample_rate | fsr_gain | `QI` `STREAM=` | package | sha256 | git_commit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| active | `v00020023` | `lhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_lhs_v00020023_sensorskins.zip` | `d633722a5db1790413ec474675e194ba0e89292bb559c59f11c1e001508b5a6b` | `317f680` |
| active | `v00020023` | `rhs` | `stream` | `sleep` | `ASCII_V1_TS` | `8Hz` | `GAIN1_2` | `ASCII_V1_TS` | `artefacts/out/stream_sleep_rhs_v00020023_sensorskins.zip` | `cc947176bcc2e2c562797e57ea7a718c4849f659a63bd394352fcb99c8d710fd` | `317f680` |

## Experimental tracked variants

These variants are in source or compile-tested, but are not yet packaged for
field use.

| status | version | side | stream | sleep | protocol | sample_rate | fsr_gain | package | notes |
|---|---|---|---|---|---|---|---|---|---|
| compile-tested only | `v00020023` | `lhs/rhs` | `stream` | `sleep` | `BINARY_V2` | `8Hz` | `GAIN1_2` | none yet | Binary stream scaffold builds cleanly but is not yet packaged or app-integrated |

## Existing zip inventory on disk

These packages currently exist in `artefacts/out/`:

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

## Next planned variants

These should remain separate from the stable ASCII line until app support is
ready:

| planned_version | protocol | sample_rate | notes |
|---|---|---|---|
| next binary test | `BINARY_V2` | `8Hz` | First end-to-end app decode validation |
| next binary rate test | `BINARY_V2` | `10Hz` | First rate increase after decode validation |
| later binary rate test | `BINARY_V2` | `12-15Hz` | Only after row integrity and battery checks |
