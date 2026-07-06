# Firmware Distribution (in-app DFU)

This document describes how signed DFU packages built from the `tim` line are
distributed to the **Sensor Skins mobile app** so users can update device
firmware over Bluetooth from within the app, instead of using nRF Connect +
Nordic DFU by hand.

## Overview

```
 SES / emBuild ──► ble_app_aginic_v2.hex
        │
        ▼
 tools/package_dfu.sh  ──►  signed DFU zip (nrfutil, calceus_private.key)
        │                    artefacts/out/<...>_v<MAJOR.MINOR.PATCH>_sensorskins.zip
        ▼
 tools/publish_firmware.sh  ──►  s3://calceus-dev-firmware/dfu/<zip>
                            └──►  s3://calceus-dev-firmware/manifest.json
        │
        ▼
 Mobile app  ──► reads manifest.json (Cognito-authenticated GetObject)
             ──► compares to the connected device's reported version
             ──► downloads the signed zip
             ──► sends the ;R NUS command (reboot into Secure DFU bootloader)
             ──► runs Nordic Secure DFU against service 0xFE59
```

Nothing about the firmware or the signed package changes. The bootloader still
enforces a signed, monotonically-versioned image. The app is just a second,
more convenient DFU client alongside the desktop nRF tools.

## Device-side facts the app relies on

- **SoC / SoftDevice:** nRF52832 / S112 6.1.1 (`--sd-req 0xB8`, `--hw-version 52`).
- **Bootloader:** Nordic **Secure DFU** (`firmware/secure_bootloader_calceus/`),
  ECDSA P-256 signed, downgrade-prevention on. Advertises the standard Secure
  DFU Service **`0xFE59`** while in DFU mode.
- **DFU entry:** the app has no buttonless DFU characteristic (`BLE_DFU_ENABLED 0`).
  Instead the NUS command **`;R`** (`MSG_COMMAND_REBOOT`) calls `system_reboot()`,
  which sets `GPREGRET = BOOTLOADER_DFU_START (0xB1)` and resets into the
  bootloader. The mobile app sends `;R`, then runs Secure DFU. No firmware change
  is required to support app-driven updates.
- **Version reporting:** the `;QI` (`MSG_QUERY_SYSINFO`) reply contains
  `VER=%u.%u.%u` — **dotted** `MAJOR.MINOR.PATCH`. This is the canonical version
  format. (Older field units may report a packed 8-hex-digit form such as
  `00020024`; the app maps that to dotted defensively.)
- **Per-side builds:** firmware is compiled per foot (`REID_LHS` / `REID_RHS`,
  device names `REIDLHS` / `REIDRHS`), so every release is **two** signed zips.

## Versioning

Source of truth is `configure_firmware.h`:

```c
#define DEVICE_FW_VERSION_MAJOR   2
#define DEVICE_FW_VERSION_MINOR   0
#define DEVICE_FW_VERSION_PATCH   59
```

- **Display / manifest version:** dotted `2.0.59`.
- **versionCode (packed uint32):** `(MAJOR << 16) | (MINOR << 8) | PATCH`. This is
  the integer passed to `nrfutil --application-version`, the same value the
  bootloader uses for its monotonic downgrade check, and the value the app
  compares. Bump `PATCH` (or higher) on every release you intend to publish;
  the bootloader rejects a lower `--application-version` and (by config) accepts
  an equal one.

## Manifest schema (`manifest.json`)

Single JSON object at the bucket root. `publish_firmware.sh` regenerates it from
the exact set of zips passed on the command line.

```jsonc
{
  "schemaVersion": 1,
  "channel": "stable",
  "generatedAt": "2026-07-06T22:01:17Z",   // build-host clock, informational
  "bucket": "calceus-dev-firmware",
  "basePath": "dfu/",
  "releases": [
    {
      "side": "lhs",                 // "lhs" | "rhs" — app maps left->lhs, right->rhs
      "version": "2.0.59",           // dotted, canonical
      "versionCode": 131131,         // (2<<16)|(0<<8)|59
      "protocol": "ASCII_V1_TS",     // stream wire format this build speaks
      "streamMode": "stream",        // "stream" | "nostream"
      "sleepPolicy": "sleep",        // "" | "sleep"
      "minAppVersionCode": 9,        // app Android versionCode gate
      "file": "dfu/stream_sleep_lhs_v2.0.59_sensorskins.zip",
      "size": 91966,
      "sha256": "…"                  // app verifies the download before flashing
    }
    // … the matching rhs entry, and any other published sides/builds …
  ]
}
```

### Compatibility gating (why the app won't brick recording)

The newest firmware line can change the **stream wire format** (e.g. `BINARY_V2`
v2 → v3 adds IMU rows). If the app can't decode a build's protocol, offering it
would silently break recording. Two fields let the app refuse an incompatible
build:

- **`protocol`** — the app only offers a release whose `protocol` is in the set
  of formats its current build can decode.
- **`minAppVersionCode`** — a hard floor; the app ignores releases that require a
  newer app than the one installed.

So you can publish a `BINARY_V3` build with `minAppVersionCode` set to the app
version that ships the v3 decoder; older apps simply won't offer it.

## Publishing

Prerequisites: built + signed zips in `artefacts/out/` (see `RELEASE_STEPS.md`),
and the AWS CLI authenticated with write access to `calceus-dev-firmware`.

```bash
# Dry run first — writes artefacts/out/manifest.json locally, no upload:
tools/publish_firmware.sh \
  --protocol ASCII_V1_TS \
  --min-app-version 9 \
  --dry-run \
  artefacts/out/stream_sleep_lhs_v2.0.59_sensorskins.zip \
  artefacts/out/stream_sleep_rhs_v2.0.59_sensorskins.zip

# Then publish for real (uploads the zips + manifest.json):
tools/publish_firmware.sh \
  --protocol ASCII_V1_TS \
  --min-app-version 9 \
  artefacts/out/stream_sleep_lhs_v2.0.59_sensorskins.zip \
  artefacts/out/stream_sleep_rhs_v2.0.59_sensorskins.zip
```

Always publish **both** sides together so lhs and rhs stay in lockstep. The
manifest is fully regenerated from the arguments each run, so pass every release
you want live in that single invocation.

## Required AWS setup (one-time, by an account admin)

The app fetches firmware with **temporary Cognito Identity Pool credentials**
(the same mechanism it already uses for the data bucket). The Identity Pool's
**authenticated** IAM role must be allowed to read this bucket:

```json
{
  "Sid": "SensorSkinsFirmwareRead",
  "Effect": "Allow",
  "Action": ["s3:GetObject"],
  "Resource": "arn:aws:s3:::calceus-dev-firmware/*"
}
```

If the manifest is also served publicly (e.g. behind CloudFront), the app can
read it anonymously and this policy only needs to cover the `dfu/*` objects — but
the default design reads everything via authenticated GetObject, so the single
statement above is sufficient.

Bucket notes:
- Region `ap-southeast-2` (same as the app's other resources).
- `dfu/*` objects are immutable (`Cache-Control: immutable`); `manifest.json` is
  `no-cache` so new releases are picked up promptly.
- The signing key (`calceus_private.key`) is **never** uploaded and is not in the
  repo. It must match the bootloader's committed `public_key.c`, and it cannot be
  rotated without reflashing every field device.
