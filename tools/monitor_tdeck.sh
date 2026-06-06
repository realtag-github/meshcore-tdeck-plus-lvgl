#!/usr/bin/env bash
set -euo pipefail

PORT_ARG=()
if [[ "${1:-}" != "" ]]; then
  PORT_ARG=(--port "$1")
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR/firmware"

pio device monitor --baud 115200 "${PORT_ARG[@]}"
