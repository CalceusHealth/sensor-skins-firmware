#!/usr/bin/env bash
#
# Publish signed DFU packages + a distribution manifest to the firmware bucket,
# so the mobile app's in-app updater can offer them.
#
# Also performs the one-time infrastructure setup (bucket, CORS, IAM) when it is
# missing, because all three are required before the app can read anything and
# the failure mode when any is absent is opaque: S3 error responses carry no
# CORS headers, so the WebView reports a bare "Failed to fetch" rather than
# NoSuchBucket or AccessDenied.
#
# Install to: sensor-skins-firmware/tools/publish_firmware.sh
#
# Usage:
#   AWS_PROFILE=calceus-dev-admin tools/publish_firmware.sh [--setup] [--dry-run] [zip ...]
#
#   --setup    create/repair bucket + CORS + IAM role policy, then publish
#   --dry-run  print the manifest that would be written; upload nothing
#
# With no zip arguments, publishes the newest stream_sleep_{lhs,rhs} package
# found in artefacts/out/.
#
# Requires: aws CLI, python3 (no jq).
#
set -euo pipefail

BUCKET="${FIRMWARE_BUCKET:-calceus-dev-firmware}"
REGION="${AWS_REGION:-ap-southeast-2}"
CHANNEL="${FIRMWARE_CHANNEL:-dev}"
BASE_PATH="dfu"
# Cognito Identity Pool authenticated role that the app assumes.
AUTH_ROLE="${COGNITO_AUTH_ROLE:-vibe-mobile-role}"
# Wire format these packages speak. This is the compatibility GATE: the app only
# offers a release whose `protocol` is in firmwareConfig.supportedProtocols, so a
# wrong value here either hides a good build or — far worse — flashes a build the
# app can't decode and silently breaks recording. There is deliberately NO default:
# it must be passed explicitly (--protocol / FIRMWARE_PROTOCOL) and must match the
# real wire format of the zips (see artefacts/firmware_build_matrix.md — dotted
# v2.0.43+ builds are BINARY_V2, older packed-hex builds are ASCII_V1_TS) AND the
# exact tag string the app checks in firmwareConfig.supportedProtocols.
PROTOCOL="${FIRMWARE_PROTOCOL:-}"
# Android versionCode floor required to run these builds.
MIN_APP_VERSION_CODE="${MIN_APP_VERSION_CODE:-11}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${FIRMWARE_OUT_DIR:-$ROOT_DIR/artefacts/out}"

SETUP=0
DRY_RUN=0
ZIPS=()
i=0
args=("$@")
while (( i < ${#args[@]} )); do
  case "${args[i]}" in
    --setup)    SETUP=1 ;;
    --dry-run)  DRY_RUN=1 ;;
    --protocol) i=$((i+1)); PROTOCOL="${args[i]:-}" ;;
    *)          ZIPS+=("${args[i]}") ;;
  esac
  i=$((i+1))
done

die() { echo "error: $*" >&2; exit 1; }

command -v aws     >/dev/null || die "aws CLI not found"
command -v python3 >/dev/null || die "python3 not found"

# ─────────────────────────── one-time setup ───────────────────────────

setup_infra() {
  echo "==> Ensuring bucket s3://$BUCKET exists in $REGION"
  if aws s3api head-bucket --bucket "$BUCKET" 2>/dev/null; then
    echo "    already exists"
  else
    aws s3api create-bucket \
      --bucket "$BUCKET" \
      --region "$REGION" \
      --create-bucket-configuration "LocationConstraint=$REGION" >/dev/null
    echo "    created"
  fi

  # Firmware is not secret, but it is not public either: reads go through the
  # Cognito authenticated role. Keep the public-access block on.
  aws s3api put-public-access-block --bucket "$BUCKET" \
    --public-access-block-configuration \
      "BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true" >/dev/null

  aws s3api put-bucket-versioning --bucket "$BUCKET" \
    --versioning-configuration Status=Enabled >/dev/null

  # CORS is mandatory, not optional. The app reads the manifest with the AWS SDK
  # from inside a Capacitor WebView, so every request is a browser request and is
  # subject to CORS. Origins mirror the raw data bucket's config.
  echo "==> Applying CORS"
  aws s3api put-bucket-cors --bucket "$BUCKET" --cors-configuration '{
    "CORSRules": [
      {
        "AllowedHeaders": ["*"],
        "AllowedMethods": ["GET", "HEAD"],
        "AllowedOrigins": [
          "https://localhost",
          "capacitor://localhost",
          "http://localhost:5173"
        ],
        "ExposeHeaders": ["ETag", "x-amz-request-id", "x-amz-id-2"],
        "MaxAgeSeconds": 3000
      }
    ]
  }' >/dev/null

  echo "==> Granting s3:GetObject on $BUCKET to role $AUTH_ROLE"
  aws iam put-role-policy \
    --role-name "$AUTH_ROLE" \
    --policy-name "sensor-skins-firmware-read" \
    --policy-document "{
      \"Version\": \"2012-10-17\",
      \"Statement\": [
        {
          \"Effect\": \"Allow\",
          \"Action\": [\"s3:GetObject\"],
          \"Resource\": \"arn:aws:s3:::$BUCKET/*\"
        },
        {
          \"Effect\": \"Allow\",
          \"Action\": [\"s3:ListBucket\"],
          \"Resource\": \"arn:aws:s3:::$BUCKET\"
        }
      ]
    }" >/dev/null
  echo "    policy sensor-skins-firmware-read applied"
}

# ───────────────────────── manifest generation ─────────────────────────

# Newest dotted-version stream_sleep package per side.
default_zips() {
  local side
  for side in lhs rhs; do
    ls "$OUT_DIR"/stream_sleep_${side}_v[0-9]*.zip 2>/dev/null | sort -V | tail -1
  done
}

(( SETUP )) && setup_infra

if [[ ${#ZIPS[@]} -eq 0 ]]; then
  mapfile -t ZIPS < <(default_zips)
fi
[[ ${#ZIPS[@]} -gt 0 ]] || die "no DFU packages found in $OUT_DIR"

for zip in "${ZIPS[@]}"; do
  [[ -f "$zip" ]] || die "not found: $zip"
done

# The protocol tag is the safety gate; never let it be implicit.
[[ -n "$PROTOCOL" ]] || die "no --protocol given. This is the compatibility gate and must be explicit.
       Pass the real wire format of these zips (see artefacts/firmware_build_matrix.md):
         dotted v2.0.43+  -> BINARY_V2
         packed-hex / older -> ASCII_V1_TS
       and confirm the SAME string is in the app's firmwareConfig.supportedProtocols,
       or the app will refuse (or worse, mis-accept) the release."

# Guardrail against the exact mistake this tool is meant to prevent: the dotted
# v2.0.43+ builds are BINARY_V2 per the matrix, so tagging one ASCII_V1_TS would
# defeat the gate. Refuse rather than publish a lie.
if [[ "$PROTOCOL" == "ASCII_V1_TS" ]]; then
  for zip in "${ZIPS[@]}"; do
    name="$(basename "$zip")"
    if [[ "$name" =~ _v([0-9]+)\.([0-9]+)\.([0-9]+)_ ]]; then
      code=$(( (BASH_REMATCH[1] << 16) | (BASH_REMATCH[2] << 8) | BASH_REMATCH[3] ))
      (( code >= ((2 << 16) | (0 << 8) | 43) )) \
        && die "$name is a dotted v2.0.43+ build (BINARY_V2 per the matrix) but --protocol is ASCII_V1_TS. Refusing to mislabel."
    fi
  done
fi

MANIFEST_JSON="$(
  BASE_PATH="$BASE_PATH" BUCKET="$BUCKET" CHANNEL="$CHANNEL" \
  PROTOCOL="$PROTOCOL" MIN_APP_VERSION_CODE="$MIN_APP_VERSION_CODE" \
  python3 - "${ZIPS[@]}" <<'PY'
import hashlib, json, os, re, sys, datetime

base_path = os.environ["BASE_PATH"]
releases = []

for path in sys.argv[1:]:
    name = os.path.basename(path)

    m_side = re.search(r"_(lhs|rhs)_", name)
    if not m_side:
        sys.exit(f"error: cannot determine side from '{name}'")
    side = m_side.group(1)

    m_ver = re.search(r"_v(\d+)\.(\d+)\.(\d+)_", name)
    if not m_ver:
        sys.exit(f"error: cannot determine dotted version from '{name}' "
                 "(legacy packed-hex names are not publishable)")
    major, minor, patch = (int(g) for g in m_ver.groups())
    # Must match the app's versionCodeOf(): (MAJOR<<16)|(MINOR<<8)|PATCH
    version_code = (major << 16) | (minor << 8) | patch

    data = open(path, "rb").read()
    releases.append({
        "side": side,
        "version": f"{major}.{minor}.{patch}",
        "versionCode": version_code,
        "protocol": os.environ["PROTOCOL"],
        "streamMode": "nostream" if name.startswith("nostream_") else "stream",
        "sleepPolicy": "sleep" if "_sleep_" in name else "none",
        "minAppVersionCode": int(os.environ["MIN_APP_VERSION_CODE"]),
        "file": f"{base_path}/{name}",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    })

print(json.dumps({
    "schemaVersion": 1,
    "channel": os.environ["CHANNEL"],
    "generatedAt": datetime.datetime.now(datetime.timezone.utc)
                       .strftime("%Y-%m-%dT%H:%M:%SZ"),
    "bucket": os.environ["BUCKET"],
    "basePath": base_path,
    "releases": releases,
}, indent=2))
PY
)"

if (( DRY_RUN )); then
  echo "--- manifest.json (dry run, nothing uploaded) ---"
  echo "$MANIFEST_JSON"
  exit 0
fi

for zip in "${ZIPS[@]}"; do
  echo "==> uploading $(basename "$zip")"
  aws s3 cp "$zip" "s3://$BUCKET/$BASE_PATH/$(basename "$zip")" --only-show-errors
done

tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
echo "$MANIFEST_JSON" >"$tmp"
# Manifest last: it is the index, so it must never point at an object that has
# not finished uploading.
aws s3 cp "$tmp" "s3://$BUCKET/manifest.json" \
  --content-type application/json --cache-control "no-cache" --only-show-errors

echo
echo "Published to s3://$BUCKET/manifest.json"
echo "$MANIFEST_JSON" | python3 -c "
import json,sys
for r in json.load(sys.stdin)['releases']:
    print(f\"  {r['side']} {r['version']}  (versionCode {r['versionCode']})  {r['file']}\")
"
