#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root>" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
SOFTDEVICE_HEX="${SDK_ROOT}/components/softdevice/s132/hex/s132_nrf52_6.1.1_softdevice.hex"
BOOT_HEX="$(sdk_bootloader_dir "${SDK_ROOT}")/pca10040_ble/ses/Output/Release/Exe/secure_bootloader_ble_s132_calceus.hex"
APP_HEX="$(sdk_app_dir "${SDK_ROOT}")/Output/Release/Exe/ble_app_aginic_v2.hex"

require_file "${SOFTDEVICE_HEX}"
require_file "${BOOT_HEX}"
require_file "${APP_HEX}"

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "nrfjprog not found in PATH" >&2
  exit 1
fi

nrfjprog --family NRF52 --program "${SOFTDEVICE_HEX}" --sectorerase --verify
nrfjprog --family NRF52 --program "${BOOT_HEX}" --sectorerase --verify
nrfjprog --family NRF52 --program "${APP_HEX}" --sectorerase --verify --reset
