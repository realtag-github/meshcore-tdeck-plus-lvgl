#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan}"
DISPLAY_ID="${DISPLAY_ID:-:1}"
VNC_PORT="${VNC_PORT:-5900}"
NOVNC_PORT="${NOVNC_PORT:-6080}"
MESHCORE_SIM_RX_INTERVAL_MS="${MESHCORE_SIM_RX_INTERVAL_MS:-600000}"
LOG_DIR="${LOG_DIR:-/tmp/meshcore-lvgl-${NOVNC_PORT}}"

mkdir -p "${LOG_DIR}" "${LOG_DIR}/cache"

cd "${ROOT}/simulator"

cmake -S . -B build-lvgl
cmake --build build-lvgl

pkill -x meshcore_tdeck_sim >/dev/null 2>&1 || true
pkill -f "build-lvgl/meshcore_tdeck_sim" >/dev/null 2>&1 || true
ps -eo pid=,args= | awk '/build-lvgl\/meshcore_tdeck_sim/ && !/awk/ {print $1}' | xargs -r kill -9 >/dev/null 2>&1 || true
pkill -x x11vnc >/dev/null 2>&1 || true
pkill -x Xvfb >/dev/null 2>&1 || true
pkill -f "/usr/bin/websockify --web=/usr/share/novnc/ ${NOVNC_PORT}" >/dev/null 2>&1 || true
pkill -f "websockify --web=/usr/share/novnc/ ${NOVNC_PORT}" >/dev/null 2>&1 || true
pkill -f "websockify.*${NOVNC_PORT}.*localhost:${VNC_PORT}" >/dev/null 2>&1 || true
pkill -f "python3.*/usr/bin/websockify.*${NOVNC_PORT}" >/dev/null 2>&1 || true
sleep 1

Xvfb "${DISPLAY_ID}" -screen 0 320x240x24 -ac +extension GLX +render -noreset >"${LOG_DIR}/xvfb.log" 2>&1 &
sleep 1

x11vnc -display "${DISPLAY_ID}" -forever -shared -nopw -rfbport "${VNC_PORT}" >"${LOG_DIR}/x11vnc.log" 2>&1 &
websockify --web=/usr/share/novnc/ "${NOVNC_PORT}" "localhost:${VNC_PORT}" >"${LOG_DIR}/novnc.log" 2>&1 &

DISPLAY="${DISPLAY_ID}" \
  XDG_CACHE_HOME="${LOG_DIR}/cache" \
  MESHCORE_SIM_RX_INTERVAL_MS="${MESHCORE_SIM_RX_INTERVAL_MS}" \
  MESHCORE_SIM_INPUT_TRACE="${MESHCORE_SIM_INPUT_TRACE:-}" \
  ./build-lvgl/meshcore_tdeck_sim >"${LOG_DIR}/lvgl-sim.log" 2>&1 &

cat <<EOF
LVGL simulator started.
noVNC container port: ${NOVNC_PORT}
VNC container port: ${VNC_PORT}
Logs:
  ${LOG_DIR}/lvgl-sim.log
  ${LOG_DIR}/xvfb.log
  ${LOG_DIR}/x11vnc.log
  ${LOG_DIR}/novnc.log
EOF
