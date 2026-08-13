#!/usr/bin/env bash
set -euo pipefail

# SEN-154: one-shot OTA migration package for fielded S112 units.
# Bundles SoftDevice S132 6.1.1 + rebuilt bootloader + application in a single
# DFU zip. sd-req lists both 0xB8 (S112 6.1.1, pre-migration) and 0xB7
# (S132 6.1.1) so an interrupted/partial migration can be retried with the
# same package. After migration, use package_dfu.sh (sd-req 0xB7) as usual.

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [lhs|rhs] [bootloader_version]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
SIDE="${2:-lhs}"
# Must be strictly greater than the bootloader_version in the fielded units'
# DFU settings page (downgrade prevention). Verify before field rollout.
BL_VERSION="${3:-2}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/common.sh"

SOFTDEVICE_HEX="${SDK_ROOT}/components/softdevice/s132/hex/s132_nrf52_6.1.1_softdevice.hex"
BOOT_HEX="$(sdk_bootloader_dir "${SDK_ROOT}")/pca10040_ble/ses/Output/Release/Exe/secure_bootloader_ble_s132_calceus.hex"
HEX_PATH="$(sdk_app_dir "${SDK_ROOT}")/Output/Release/Exe/ble_app_aginic_v2.hex"
CONFIG_PATH="$(sdk_app_dir "${SDK_ROOT}")/configure_firmware.h"
KEY_PATH="${REPO_ROOT}/firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key"
OUT_DIR="${REPO_ROOT}/artefacts/out"

mkdir -p "${OUT_DIR}"

require_file "${SOFTDEVICE_HEX}"
require_file "${BOOT_HEX}"
require_file "${HEX_PATH}"
require_file "${CONFIG_PATH}"
require_file "${KEY_PATH}"

if ! command -v nrfutil >/dev/null 2>&1; then
  echo "nrfutil not found in PATH" >&2
  exit 1
fi

APP_VERSION="$(extract_app_version_dec "${CONFIG_PATH}")"
APP_VERSION_TAG="$(extract_app_version_tag "${CONFIG_PATH}")"
STREAM_MODE_TAG="$(extract_stream_mode_tag "${CONFIG_PATH}")"
SLEEP_POLICY_TAG="$(extract_sleep_policy_tag "${CONFIG_PATH}")"

if [[ -n "${SLEEP_POLICY_TAG}" ]]; then
  OUT_FILE="${OUT_DIR}/migrate_s132_${STREAM_MODE_TAG}_${SLEEP_POLICY_TAG}_${SIDE}_${APP_VERSION_TAG}_sensorskins.zip"
else
  OUT_FILE="${OUT_DIR}/migrate_s132_${STREAM_MODE_TAG}_${SIDE}_${APP_VERSION_TAG}_sensorskins.zip"
fi

nrfutil pkg generate \
  --hw-version 52 \
  --softdevice "${SOFTDEVICE_HEX}" \
  --sd-req 0xB8,0xB7 \
  --sd-id 0xB7 \
  --bootloader "${BOOT_HEX}" \
  --bootloader-version "${BL_VERSION}" \
  --application "${HEX_PATH}" \
  --application-version "${APP_VERSION}" \
  --app-boot-validation NO_VALIDATION \
  --sd-boot-validation NO_VALIDATION \
  --key-file "${KEY_PATH}" \
  "${OUT_FILE}"

echo "Wrote ${OUT_FILE}"
echo "NOTE: bootloader-version=${BL_VERSION} must exceed the fielded bootloader's version."
