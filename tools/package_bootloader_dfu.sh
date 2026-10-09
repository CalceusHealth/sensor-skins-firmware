#!/usr/bin/env bash
set -euo pipefail

# SEN-182: bootloader-only DFU zip for units already on S132 (post SEN-154).
# nrfutil will not combine a bootloader with an application in one package
# ("use two .zip packages instead"), so a bootloader roll-out on S132 units is
# either this zip followed by the matching app zip (package_dfu.sh), or the
# single-step SD+BL+app package from package_migration_dfu.sh, whose sd-req
# (0xB8,0xB7) is also accepted by S132 units. The bootloader hex is side
# independent, so one zip serves LHS and RHS.

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [bootloader_version]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
# Must be strictly greater than the bootloader_version in the fielded units'
# DFU settings page (downgrade prevention). 2 shipped with the S132 migration.
BL_VERSION="${2:-3}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/common.sh"

BOOT_HEX="$(sdk_bootloader_dir "${SDK_ROOT}")/pca10040_ble/ses/Output/Release/Exe/secure_bootloader_ble_s132_calceus.hex"
KEY_PATH="${REPO_ROOT}/firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key"
OUT_DIR="${REPO_ROOT}/artefacts/out"
OUT_FILE="${OUT_DIR}/bootloader_v${BL_VERSION}_s132_sensorskins.zip"

mkdir -p "${OUT_DIR}"
require_file "${BOOT_HEX}"
require_file "${KEY_PATH}"

if ! command -v nrfutil >/dev/null 2>&1; then
  echo "nrfutil not found in PATH" >&2
  exit 1
fi

rm -f "${OUT_FILE}"
nrfutil pkg generate \
  --hw-version 52 \
  --sd-req 0xB7 \
  --bootloader "${BOOT_HEX}" \
  --bootloader-version "${BL_VERSION}" \
  --key-file "${KEY_PATH}" \
  "${OUT_FILE}"

# The nrfutil wrapper can report success on a rejected option set; insist on output.
require_file "${OUT_FILE}"
echo "Wrote ${OUT_FILE}"
echo "NOTE: bootloader-version=${BL_VERSION} must exceed the fielded bootloader's version."
