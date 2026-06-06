#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
if [[ "${FAST_VERIFY:-1}" == "1" ]]; then
  tools/verify_fast.sh
fi
exec python3 tools/hil_tdeck_loop.py "$@"
