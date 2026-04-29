#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root>" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
HEX_PATH="$(sdk_app_dir "${SDK_ROOT}")/Output/Release/Exe/ble_app_aginic_v2.hex"

if [[ ! -f "${HEX_PATH}" ]]; then
  echo "Missing build output: ${HEX_PATH}" >&2
  exit 1
fi

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "nrfjprog not found in PATH" >&2
  exit 1
fi

nrfjprog --family NRF52 --program "${HEX_PATH}" --sectorerase --verify --reset
