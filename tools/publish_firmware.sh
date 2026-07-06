#!/usr/bin/env bash
set -euo pipefail

# publish_firmware.sh
#
# Publishes one or more signed DFU zip packages (built by tools/package_dfu.sh
# into artefacts/out/) to the firmware distribution bucket, and (re)generates
# the manifest.json the Sensor Skins mobile app reads to offer in-app updates.
#
# The manifest is the contract between this repo's release process and the app:
# the app fetches manifest.json, finds the newest release for the connected
# device's side whose protocol it can decode, compares versions, and offers a
# Nordic Secure DFU update. See docs/FIRMWARE_DISTRIBUTION.md for the schema and
# the compatibility-gating rules.
#
# Version handling is DOTTED-first (MAJOR.MINOR.PATCH). The packed uint32
# versionCode (MAJOR<<16 | MINOR<<8 | PATCH) is emitted alongside so the app can
# compare monotonically; it is the same integer the bootloader enforces via
# nrfutil --application-version.
#
# Usage:
#   tools/publish_firmware.sh \
#       --protocol ASCII_V1_TS \
#       --min-app-version 9 \
#       [--channel stable] \
#       [--bucket calceus-dev-firmware] \
#       [--region ap-southeast-2] \
#       [--base-path dfu/] \
#       [--dry-run] \
#       <zip> [<zip> ...]
#
# --dry-run writes artefacts/out/manifest.json locally and skips all S3 uploads,
# so the manifest can be inspected/tested without AWS credentials.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=tools/common.sh
source "${SCRIPT_DIR}/common.sh"

CHANNEL="stable"
BUCKET="calceus-dev-firmware"
REGION="ap-southeast-2"
BASE_PATH="dfu/"
PROTOCOL=""
MIN_APP_VERSION=""
DRY_RUN=0
ZIPS=()

usage() {
  sed -n '3,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --protocol)         PROTOCOL="$2"; shift 2 ;;
    --min-app-version)  MIN_APP_VERSION="$2"; shift 2 ;;
    --channel)          CHANNEL="$2"; shift 2 ;;
    --bucket)           BUCKET="$2"; shift 2 ;;
    --region)           REGION="$2"; shift 2 ;;
    --base-path)        BASE_PATH="$2"; shift 2 ;;
    --dry-run)          DRY_RUN=1; shift ;;
    -h|--help)          usage 0 ;;
    --*)                die "Unknown option: $1" ;;
    *)                  ZIPS+=("$1"); shift ;;
  esac
done

[[ -n "${PROTOCOL}" ]]        || die "Missing required --protocol (e.g. ASCII_V1_TS, BINARY_V2, BINARY_V3)"
[[ -n "${MIN_APP_VERSION}" ]] || die "Missing required --min-app-version (app versionCode gate, e.g. 9)"
[[ "${MIN_APP_VERSION}" =~ ^[0-9]+$ ]] || die "--min-app-version must be an integer (Android versionCode)"
[[ ${#ZIPS[@]} -gt 0 ]]       || die "No DFU zip files given. Pass one or more paths (usually artefacts/out/*.zip)."

# Normalise the base path to have exactly one trailing slash and no leading one.
BASE_PATH="${BASE_PATH#/}"
BASE_PATH="${BASE_PATH%/}/"
[[ "${BASE_PATH}" == "/" ]] && BASE_PATH=""

OUT_DIR="${REPO_ROOT}/artefacts/out"
MANIFEST_PATH="${OUT_DIR}/manifest.json"
mkdir -p "${OUT_DIR}"

if [[ "${DRY_RUN}" -eq 0 ]] && ! command -v aws >/dev/null 2>&1; then
  die "aws CLI not found in PATH. Install it, or pass --dry-run to only write the manifest locally."
fi

# json_escape <string> — minimal JSON string escaping for the controlled values
# we emit (filenames, hashes, enum-like tags). Escapes backslash and quote.
json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  printf '%s' "${s}"
}

# parse_dotted_version <raw> -> "major minor patch"
# Accepts dotted "2.0.24" or legacy packed hex "00020024" / "0x00020024".
parse_dotted_version() {
  local raw="$1" major minor patch hex
  if [[ "${raw}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    printf '%s %s %s' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
    return 0
  fi
  hex="${raw#0x}"
  if [[ "${hex}" =~ ^[0-9A-Fa-f]{8}$ ]]; then
    local val=$(( 16#${hex} ))
    major=$(( (val >> 16) & 0xFF ))
    minor=$(( (val >> 8) & 0xFF ))
    patch=$(( val & 0xFF ))
    printf '%s %s %s' "${major}" "${minor}" "${patch}"
    return 0
  fi
  return 1
}

# Parse a DFU zip filename into its metadata dimensions.
# Expected: <stream|nostream>[_sleep]_<lhs|rhs>_v<version>_sensorskins.zip
# Sets globals: F_STREAM F_SLEEP F_SIDE F_MAJOR F_MINOR F_PATCH
parse_zip_filename() {
  local base="$1" rest stream sleep side ver
  base="${base%.zip}"
  [[ "${base}" =~ _sensorskins$ ]] || die "Unexpected filename (missing _sensorskins suffix): $1"
  rest="${base%_sensorskins}"

  if   [[ "${rest}" =~ ^(stream|nostream)_sleep_(lhs|rhs)_v(.+)$ ]]; then
    stream="${BASH_REMATCH[1]}"; sleep="sleep"; side="${BASH_REMATCH[2]}"; ver="${BASH_REMATCH[3]}"
  elif [[ "${rest}" =~ ^(stream|nostream)_(lhs|rhs)_v(.+)$ ]]; then
    stream="${BASH_REMATCH[1]}"; sleep="";      side="${BASH_REMATCH[2]}"; ver="${BASH_REMATCH[3]}"
  else
    die "Cannot parse metadata from filename: $1 (expected <stream|nostream>[_sleep]_<lhs|rhs>_v<version>_sensorskins.zip)"
  fi

  local comps
  comps="$(parse_dotted_version "${ver}")" || die "Cannot parse version '${ver}' from filename: $1"
  read -r F_MAJOR F_MINOR F_PATCH <<<"${comps}"
  F_STREAM="${stream}"
  F_SLEEP="${sleep}"
  F_SIDE="${side}"
}

RELEASE_ENTRIES=()
UPLOAD_SRC=()
UPLOAD_KEY=()

for zip in "${ZIPS[@]}"; do
  require_file "${zip}"
  base="$(basename "${zip}")"
  parse_zip_filename "${base}"

  version_code=$(( (F_MAJOR << 16) | (F_MINOR << 8) | F_PATCH ))
  dotted="${F_MAJOR}.${F_MINOR}.${F_PATCH}"
  size="$(stat -c%s "${zip}" 2>/dev/null || stat -f%z "${zip}")"
  sha256="$(sha256sum "${zip}" | awk '{print $1}')"
  key="${BASE_PATH}${base}"

  entry=$(cat <<JSON
    {
      "side": "$(json_escape "${F_SIDE}")",
      "version": "$(json_escape "${dotted}")",
      "versionCode": ${version_code},
      "protocol": "$(json_escape "${PROTOCOL}")",
      "streamMode": "$(json_escape "${F_STREAM}")",
      "sleepPolicy": "$(json_escape "${F_SLEEP}")",
      "minAppVersionCode": ${MIN_APP_VERSION},
      "file": "$(json_escape "${key}")",
      "size": ${size},
      "sha256": "$(json_escape "${sha256}")"
    }
JSON
)
  RELEASE_ENTRIES+=("${entry}")
  UPLOAD_SRC+=("${zip}")
  UPLOAD_KEY+=("${key}")
  echo "Prepared ${F_SIDE} v${dotted} (${PROTOCOL}, ${F_STREAM}${F_SLEEP:+/${F_SLEEP}}) -> ${key} [${size} bytes]"
done

# generatedAt is filled from the build host clock. Kept out of the hashed
# artefacts so it does not affect DFU signatures.
GENERATED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

joined=""
for i in "${!RELEASE_ENTRIES[@]}"; do
  joined+="${RELEASE_ENTRIES[$i]}"
  if [[ "$i" -lt $(( ${#RELEASE_ENTRIES[@]} - 1 )) ]]; then
    joined+=","$'\n'
  fi
done

cat >"${MANIFEST_PATH}" <<JSON
{
  "schemaVersion": 1,
  "channel": "$(json_escape "${CHANNEL}")",
  "generatedAt": "${GENERATED_AT}",
  "bucket": "$(json_escape "${BUCKET}")",
  "basePath": "$(json_escape "${BASE_PATH}")",
  "releases": [
${joined}
  ]
}
JSON

echo "Wrote manifest: ${MANIFEST_PATH}"

if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run: skipped S3 upload. Review ${MANIFEST_PATH}."
  exit 0
fi

echo "Uploading ${#UPLOAD_SRC[@]} package(s) + manifest to s3://${BUCKET}/ (region ${REGION})..."
for i in "${!UPLOAD_SRC[@]}"; do
  aws s3 cp "${UPLOAD_SRC[$i]}" "s3://${BUCKET}/${UPLOAD_KEY[$i]}" \
    --region "${REGION}" \
    --content-type "application/zip" \
    --cache-control "public, max-age=31536000, immutable"
done

# Manifest is mutable and must never be cached stale — the app polls it to learn
# about new releases. No-cache so a freshly published version is seen promptly.
aws s3 cp "${MANIFEST_PATH}" "s3://${BUCKET}/manifest.json" \
  --region "${REGION}" \
  --content-type "application/json" \
  --cache-control "no-cache, max-age=0"

echo "Publish complete. Manifest live at s3://${BUCKET}/manifest.json"
