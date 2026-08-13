#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <nrf5_sdk_15.3.0_root> [lhs|rhs]" >&2
  exit 1
fi

SDK_ROOT="$(cd "$1" && pwd)"
SIDE="${2:-lhs}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/common.sh"
HEX_PATH="$(sdk_app_dir "${SDK_ROOT}")/Output/Release/Exe/ble_app_aginic_v2.hex"
CONFIG_PATH="$(sdk_app_dir "${SDK_ROOT}")/configure_firmware.h"
KEY_PATH="${REPO_ROOT}/firmware/ble_app_firmware_v2_R6/reid_ble_aginic_v2.06_source/calceus_private.key"
OUT_DIR="${REPO_ROOT}/artefacts/out"

mkdir -p "${OUT_DIR}"

if [[ ! -f "${HEX_PATH}" ]]; then
  echo "Missing build output: ${HEX_PATH}" >&2
  exit 1
fi

require_file "${CONFIG_PATH}"

if [[ ! -f "${KEY_PATH}" ]]; then
  echo "Missing signing key: ${KEY_PATH}" >&2
  exit 1
fi

if ! command -v nrfutil >/dev/null 2>&1; then
  echo "nrfutil not found in PATH" >&2
  exit 1
fi

APP_VERSION="$(extract_app_version_dec "${CONFIG_PATH}")"
APP_VERSION_TAG="$(extract_app_version_tag "${CONFIG_PATH}")"
STREAM_MODE_TAG="$(extract_stream_mode_tag "${CONFIG_PATH}")"
SLEEP_POLICY_TAG="$(extract_sleep_policy_tag "${CONFIG_PATH}")"

if [[ -n "${SLEEP_POLICY_TAG}" ]]; then
  OUT_FILE="${OUT_DIR}/${STREAM_MODE_TAG}_${SLEEP_POLICY_TAG}_${SIDE}_${APP_VERSION_TAG}_sensorskins.zip"
else
  OUT_FILE="${OUT_DIR}/${STREAM_MODE_TAG}_${SIDE}_${APP_VERSION_TAG}_sensorskins.zip"
fi

nrfutil pkg generate \
  --hw-version 52 \
  --application-version "${APP_VERSION}" \
  --application "${HEX_PATH}" \
  --sd-req 0xB7 \
  --app-boot-validation NO_VALIDATION \
  --key-file "${KEY_PATH}" \
  "${OUT_FILE}"

echo "Wrote ${OUT_FILE}"
