#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [lhs|rhs]" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$1"
SIDE="${2:-lhs}"

"${SCRIPT_DIR}/build_bootloader.sh" "${SDK_ROOT}" Release
"${SCRIPT_DIR}/build_app.sh" "${SDK_ROOT}" "${SIDE}" Release
