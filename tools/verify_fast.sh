#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)}"
LVGL_BUILD_DIR="${LVGL_BUILD_DIR:-build-lvgl}"

cd "$ROOT_DIR/simulator"
make run
if [[ -f "$LVGL_BUILD_DIR/CMakeCache.txt" ]] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$' "$LVGL_BUILD_DIR/CMakeCache.txt"; then
  rm -rf "$LVGL_BUILD_DIR"
fi
cmake -S . -B "$LVGL_BUILD_DIR" -G Ninja
cmake --build "$LVGL_BUILD_DIR" --target navigation_test app_controller_test lvgl_ui_test meshcore_tdeck_sim --parallel "$JOBS"
"$LVGL_BUILD_DIR/navigation_test"
"$LVGL_BUILD_DIR/app_controller_test"
"$LVGL_BUILD_DIR/lvgl_ui_test"
