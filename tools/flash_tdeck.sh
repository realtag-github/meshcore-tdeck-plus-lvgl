#!/usr/bin/env bash
set -euo pipefail

ENVIRONMENT="${1:-tdeck-plus-915}"
PORT_ARG=()

if [[ "${2:-}" != "" ]]; then
  PORT_ARG=(--upload-port "$2")
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR/firmware"

pio run -e "$ENVIRONMENT" -t upload "${PORT_ARG[@]}"
