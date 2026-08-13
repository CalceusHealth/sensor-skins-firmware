#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [Release]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
CONFIG="${2:-Release}"

"${SCRIPT_DIR}/stage_sdk_sources.sh" "${SDK_ROOT}" lhs >/dev/null

PROJECT_DIR="$(sdk_bootloader_dir "${SDK_ROOT}")/pca10040_ble/ses"
PROJECT_FILE="${PROJECT_DIR}/secure_bootloader_ble_s132_calceus_Release.emProject"
EMBUILD="$(find_embuild)"

require_file "${PROJECT_FILE}"

"${EMBUILD}" -config "${CONFIG}" "${PROJECT_FILE}"

echo
echo "Bootloader build complete."
echo "HEX: ${PROJECT_DIR}/Output/${CONFIG}/Exe/secure_bootloader_ble_s132_calceus.hex"
