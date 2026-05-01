#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/common.sh"

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [lhs|rhs] [stream|nostream]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
SIDE="${2:-lhs}"
STREAM_MODE="${3:-stream}"

require_dir "${SDK_ROOT}/components"
require_dir "${REPO_ROOT}/firmware/ble_app_firmware_v2_R7"
require_dir "${REPO_ROOT}/firmware/secure_bootloader_calceus"

APP_SRC="${REPO_ROOT}/firmware/ble_app_firmware_v2_R7/"
APP_DST="$(sdk_app_dir "${SDK_ROOT}")"
BOOT_SRC="${REPO_ROOT}/firmware/secure_bootloader_calceus/"
BOOT_DST="$(sdk_bootloader_dir "${SDK_ROOT}")"
RETARGET_FILE="${SDK_ROOT}/components/libraries/uart/retarget.c"

mkdir -p "${APP_DST}" "${BOOT_DST}"

rsync -a --delete "${APP_SRC}" "${APP_DST}/"
rsync -a --delete "${BOOT_SRC}" "${BOOT_DST}/"

set_staged_side "${APP_DST}/configure_firmware.h" "${SIDE}"
set_staged_stream_mode "${APP_DST}/configure_firmware.h" "${STREAM_MODE}"

python3 - <<'PY' "${RETARGET_FILE}"
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
marker = '#include "nrf_error.h"\n'
patch = (
    '#include "nrf_error.h"\n'
    '#if defined(__GNUC__) && defined(__SES_ARM)\n'
    '#include "__SEGGER_RTL.h"\n'
    '#ifndef __printf_tag_ptr\n'
    'typedef void * __printf_tag_ptr;\n'
    '#endif\n'
    '#endif\n'
)
if patch not in text:
    if marker not in text:
        raise SystemExit(f"Could not patch {path}")
    text = text.replace(marker, patch, 1)
    path.write_text(text)
PY

echo "Staged application sources to ${APP_DST}"
echo "Staged bootloader sources to ${BOOT_DST}"
echo "Configured staged application for side: ${SIDE}"
echo "Configured staged application for stream mode: ${STREAM_MODE}"
