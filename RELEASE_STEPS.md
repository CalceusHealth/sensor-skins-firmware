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

## Publish to the mobile app (in-app DFU)

Once the signed zips are in `artefacts/out/`, publish them so the Sensor Skins
app can offer the update over the air. This uploads the zips and (re)generates
`manifest.json` in the `calceus-dev-firmware` bucket. See
`docs/FIRMWARE_DISTRIBUTION.md` for the manifest schema, compatibility gating,
and the one-time IAM setup.

```bash
# Inspect the manifest locally first (no AWS needed):
./tools/publish_firmware.sh \
  --protocol ASCII_V1_TS \
  --min-app-version 9 \
  --dry-run \
  artefacts/out/stream_sleep_lhs_v2.0.59_sensorskins.zip \
  artefacts/out/stream_sleep_rhs_v2.0.59_sensorskins.zip

# Publish for real (requires AWS CLI with write access to the bucket):
./tools/publish_firmware.sh \
  --protocol ASCII_V1_TS \
  --min-app-version 9 \
  artefacts/out/stream_sleep_lhs_v2.0.59_sensorskins.zip \
  artefacts/out/stream_sleep_rhs_v2.0.59_sensorskins.zip
```

- Always publish `lhs` and `rhs` together, in one invocation (the manifest is
  regenerated from exactly the files you pass).
- `--protocol` must match the build's stream wire format so the app only offers
  builds it can decode. `--min-app-version` is the app `versionCode` floor.
