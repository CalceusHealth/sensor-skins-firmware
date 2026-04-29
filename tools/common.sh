#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "$*" >&2
  exit 1
}

require_dir() {
  local path="$1"
  [[ -d "${path}" ]] || die "Missing directory: ${path}"
}

require_file() {
  local path="$1"
  [[ -f "${path}" ]] || die "Missing file: ${path}"
}

find_embuild() {
  if command -v emBuild >/dev/null 2>&1; then
    command -v emBuild
    return
  fi

  local base
  local candidate
  for base in /opt/SEGGER "${HOME}/bin" "${HOME}/SEGGER" "${HOME}"; do
    [[ -d "${base}" ]] || continue
    while IFS= read -r candidate; do
      if [[ -x "${candidate}" ]]; then
        printf '%s\n' "${candidate}"
        return
      fi
    done < <(find "${base}" -path '*/bin/emBuild' -type f 2>/dev/null | sort)
  done

  die "Could not find emBuild. Install SEGGER Embedded Studio and ensure emBuild is in PATH."
}

sdk_app_dir() {
  local sdk_root="$1"
  printf '%s\n' "${sdk_root}/examples/ble_peripheral/reid_ble_aginic_v2/pca10040/s112/ses"
}

sdk_bootloader_dir() {
  local sdk_root="$1"
  printf '%s\n' "${sdk_root}/examples/dfu/secure_bootloader_calceus"
}

set_staged_side() {
  local configure_file="$1"
  local side="$2"

  case "${side}" in
    lhs)
      sed -i \
        -e 's@^//\(#define REID_LHS\)@\1@' \
        -e 's@^#define REID_RHS@//#define REID_RHS@' \
        "${configure_file}"
      ;;
    rhs)
      sed -i \
        -e 's@^#define REID_LHS@//#define REID_LHS@' \
        -e 's@^//\(#define REID_RHS\)@\1@' \
        "${configure_file}"
      ;;
    *)
      die "Invalid side '${side}'. Use lhs or rhs."
      ;;
  esac
}

set_staged_stream_mode() {
  local configure_file="$1"
  local mode="$2"

  case "${mode}" in
    stream)
      sed -i \
        -e 's@^//\([[:space:]]*#define[[:space:]]\+SEND_EVERY_MEAS_OVER_BLE\)@\1@' \
        -e 's@^//\([[:space:]]*#define[[:space:]]\+SEND_BLE_MEAS_AS_ASCII\)@\1@' \
        "${configure_file}"
      ;;
    nostream)
      sed -i \
        -e 's@^[[:space:]]*#define[[:space:]]\+SEND_EVERY_MEAS_OVER_BLE@//	#define SEND_EVERY_MEAS_OVER_BLE@' \
        -e 's@^[[:space:]]*#define[[:space:]]\+SEND_BLE_MEAS_AS_ASCII@//	#define SEND_BLE_MEAS_AS_ASCII@' \
        "${configure_file}"
      ;;
    *)
      die "Invalid stream mode '${mode}'. Use stream or nostream."
      ;;
  esac
}

extract_app_version_dec() {
  local configure_file="$1"
  local version_hex

  version_hex="$(sed -n 's/^#define DEVICE_FW_VERSION[[:space:]]*0x\([0-9A-Fa-f]\+\)$/\1/p' "${configure_file}" | head -n 1)"
  [[ -n "${version_hex}" ]] || die "Could not parse DEVICE_FW_VERSION from ${configure_file}"

  printf '%d\n' "$((16#${version_hex}))"
}

extract_app_version_tag() {
  local configure_file="$1"
  local version_hex

  version_hex="$(sed -n 's/^#define DEVICE_FW_VERSION[[:space:]]*0x\([0-9A-Fa-f]\+\)$/\1/p' "${configure_file}" | head -n 1)"
  [[ -n "${version_hex}" ]] || die "Could not parse DEVICE_FW_VERSION from ${configure_file}"

  printf 'v%s\n' "${version_hex}"
}

extract_stream_mode_tag() {
  local configure_file="$1"

  if sed -n 's/^[[:space:]]*#define[[:space:]]\+SEND_EVERY_MEAS_OVER_BLE[[:space:]]*$/stream/p' "${configure_file}" | grep -q '^stream$'; then
    printf 'stream\n'
  else
    printf 'nostream\n'
  fi
}

extract_sleep_policy_tag() {
  local configure_file="$1"

  if sed -n 's/^[[:space:]]*#define[[:space:]]\+ENABLE_SLEEP_SMART_IDLE[[:space:]]*$/sleep/p' "${configure_file}" | grep -q '^sleep$'; then
    printf 'sleep\n'
  else
    printf '\n'
  fi
}
