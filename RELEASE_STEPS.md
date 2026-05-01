# Release Steps

This is the shortest path to produce Sensor Skins DFU zip packages from the
`tim` branch source.

## Prerequisites

- SEGGER Embedded Studio installed with `emBuild` available
- Nordic nRF5 SDK `15.3.0`
- `nrfutil` installed
- The signing key present at:
  `firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key`

## SDK path used in examples

```bash
SDK=~/sdk/nRF5_SDK_15.3.0_59ac345
```

## Build and package `stream_sleep`

### LHS

```bash
./tools/build_app.sh "$SDK" lhs Release stream
./tools/package_dfu.sh "$SDK" lhs
```

### RHS

```bash
./tools/build_app.sh "$SDK" rhs Release stream
./tools/package_dfu.sh "$SDK" rhs
```

Expected output goes to:

```text
artefacts/out/
```

Typical filenames:

```text
stream_sleep_lhs_v00020023_sensorskins.zip
stream_sleep_rhs_v00020023_sensorskins.zip
```

## Build and package `nostream_sleep`

### LHS

```bash
./tools/build_app.sh "$SDK" lhs Release nostream
./tools/package_dfu.sh "$SDK" lhs
```

### RHS

```bash
./tools/build_app.sh "$SDK" rhs Release nostream
./tools/package_dfu.sh "$SDK" rhs
```

Typical filenames:

```text
nostream_sleep_lhs_v00020023_sensorskins.zip
nostream_sleep_rhs_v00020023_sensorskins.zip
```

## Notes

- `build_app.sh` stages the repo `R7` source into the SDK tree before building.
- The side (`lhs` / `rhs`) is applied automatically.
- The stream mode (`stream` / `nostream`) is applied automatically.
- The `_sleep_` part of the filename appears because `ENABLE_SLEEP_SMART_IDLE`
  is enabled in the staged `configure_firmware.h`.
- The current repo default protocol is timestamped ASCII (`ASCII_V1_TS`).
- Binary stream work is being developed separately and should not replace the
  ASCII line until the app decoder is ready.
- The `v00020023` part comes from `DEVICE_FW_VERSION`.
- Before testing or releasing a new build, update
  `artefacts/firmware_build_matrix.md` and make a local git commit so the exact
  source state can be recovered.
