#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if [[ $# -lt 1 || $# -gt 4 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [lhs|rhs] [Release|Common] [stream|nostream]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
SIDE="${2:-lhs}"
CONFIG="${3:-Release}"
STREAM_MODE="${4:-stream}"

"${SCRIPT_DIR}/stage_sdk_sources.sh" "${SDK_ROOT}" "${SIDE}" "${STREAM_MODE}"

PROJECT_DIR="$(sdk_app_dir "${SDK_ROOT}")"
PROJECT_FILE="${PROJECT_DIR}/ble_app_aginic_v2.emProject"
EMBUILD="$(find_embuild)"

require_file "${PROJECT_FILE}"

"${EMBUILD}" -config "${CONFIG}" "${PROJECT_FILE}"

echo
echo "Application build complete."
echo "HEX: ${PROJECT_DIR}/Output/${CONFIG}/Exe/ble_app_aginic_v2.hex"
