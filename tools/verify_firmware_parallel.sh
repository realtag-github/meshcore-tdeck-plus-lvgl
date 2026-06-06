#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS_PER_ENV="${JOBS_PER_ENV:-8}"
LOG_DIR="${LOG_DIR:-/tmp/meshcore-tdeck-build-logs}"
BUILD_ENVS="${BUILD_ENVS:-tdeck-plus-915}"
read -r -a ENVS <<< "$BUILD_ENVS"

mkdir -p "$LOG_DIR"
cd "$ROOT_DIR/firmware"

pids=()
for env_name in "${ENVS[@]}"; do
  (
    set -euo pipefail
    pio run -j "$JOBS_PER_ENV" -e "$env_name"
  ) >"$LOG_DIR/$env_name.log" 2>&1 &
  pids+=("$!")
done

failed=0
for i in "${!ENVS[@]}"; do
  env_name="${ENVS[$i]}"
  pid="${pids[$i]}"
  if wait "$pid"; then
    grep -E "$env_name[[:space:]]+SUCCESS" "$LOG_DIR/$env_name.log" || tail -n 25 "$LOG_DIR/$env_name.log"
  else
    failed=1
    echo "=== $env_name failed ===" >&2
    tail -n 120 "$LOG_DIR/$env_name.log" >&2
  fi
done

if [[ "$failed" != "0" ]]; then
  exit 1
fi

if command -v ccache >/dev/null 2>&1; then
  ccache --show-stats | sed -n '1,20p'
fi
