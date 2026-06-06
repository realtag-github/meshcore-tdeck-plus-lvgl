#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-meshcore-tdeck.local}"
BIN="${2:-dist/tdeck-plus-915-firmware.bin}"

if [[ ! -f "$BIN" ]]; then
  echo "Firmware image not found: $BIN" >&2
  echo "Run tools/package_firmware.sh first." >&2
  exit 1
fi

echo "Uploading $BIN to http://$HOST/update"
curl --fail --show-error --location \
  -F "firmware=@${BIN};filename=$(basename "$BIN")" \
  "http://$HOST/update"
echo
