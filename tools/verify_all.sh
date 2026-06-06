#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT_DIR/tools/verify_fast.sh"
"$ROOT_DIR/tools/verify_firmware_parallel.sh"
