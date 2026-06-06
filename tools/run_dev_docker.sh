#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-meshcore-tdeck-plus-dev:latest}"
CONTAINER_NAME="${CONTAINER_NAME:-meshcore-tdeck-plus-dev}"
SSH_PORT="${SSH_PORT:-2231}"
HTTP_PORT="${HTTP_PORT:-8092}"
NOVNC_PORT="${NOVNC_PORT:-8094}"
HARDWARE_BRIDGE_PORT="${HARDWARE_BRIDGE_PORT:-8093}"
HOST_WORKSPACE="${HOST_WORKSPACE:-$(cd "${PLAN_DIR}/.." && pwd)}"
HOST_CACHE_ROOT="${HOST_CACHE_ROOT:-${HOME}/.cache/meshcore-tdeck-plus}"
HOST_TDECK_SERIAL="${HOST_TDECK_SERIAL:-/dev/tdeck-plus}"
CONTAINER_TDECK_SERIAL="${CONTAINER_TDECK_SERIAL:-/dev/tdeck-plus}"
CONTAINER_WORKSPACE="${CONTAINER_WORKSPACE:-/workspace/tdeck-plus}"
DOCKER="${DOCKER:-docker}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAN_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

${DOCKER} build -t "${IMAGE_NAME}" -f "${PLAN_DIR}/docker/Dockerfile" "${PLAN_DIR}"

${DOCKER} run --rm \
    -v "${HOST_CACHE_ROOT}:/cache" \
    "${IMAGE_NAME}" bash -lc 'mkdir -p /cache/platformio /cache/ccache /cache/pip /cache/cmake && chown -R developer:developer /cache'

DEVICE_ARGS=()
if [[ -e "${HOST_TDECK_SERIAL}" ]]; then
    DEVICE_ARGS+=(--device "${HOST_TDECK_SERIAL}:${CONTAINER_TDECK_SERIAL}")
fi
GROUP_ARGS=()
if getent group dialout >/dev/null 2>&1; then
    GROUP_ARGS+=(--group-add "$(getent group dialout | cut -d: -f3)")
fi

if ${DOCKER} ps -a --format '{{.Names}}' | grep -Fxq "${CONTAINER_NAME}"; then
    ${DOCKER} rm -f "${CONTAINER_NAME}" >/dev/null
fi

${DOCKER} run -d \
    --name "${CONTAINER_NAME}" \
    --restart unless-stopped \
    -p "${SSH_PORT}:22" \
    -p "${HTTP_PORT}:8080" \
    -p "${NOVNC_PORT}:6080" \
    -p "${HARDWARE_BRIDGE_PORT}:8081" \
    -v "${HOST_WORKSPACE}:${CONTAINER_WORKSPACE}" \
    -v "${HOST_CACHE_ROOT}/platformio:/home/${DEV_SSH_USER:-developer}/.platformio" \
    -v "${HOST_CACHE_ROOT}/ccache:/home/${DEV_SSH_USER:-developer}/.ccache" \
    -v "${HOST_CACHE_ROOT}/pip:/home/${DEV_SSH_USER:-developer}/.cache/pip" \
    -v "${HOST_CACHE_ROOT}/cmake:/home/${DEV_SSH_USER:-developer}/.cache/cmake" \
    "${DEVICE_ARGS[@]}" \
    "${GROUP_ARGS[@]}" \
    -e "MESHCORE_TDECK_SERIAL=${CONTAINER_TDECK_SERIAL}" \
    -e "PLATFORMIO_CORE_DIR=/home/${DEV_SSH_USER:-developer}/.platformio" \
    -e "CCACHE_DIR=/home/${DEV_SSH_USER:-developer}/.ccache" \
    -e "PATH=/usr/lib/ccache:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    -w "${CONTAINER_WORKSPACE}/meshcore_tdeck_plus_lvgl_plan" \
    "${IMAGE_NAME}" >/dev/null

${DOCKER} exec "${CONTAINER_NAME}" bash -lc 'g++ --version | head -1 && cmake --version | head -1 && ccache --version | head -1 && pio --version'
${DOCKER} exec "${CONTAINER_NAME}" bash -lc 'if [[ -n "${MESHCORE_TDECK_SERIAL:-}" && -e "${MESHCORE_TDECK_SERIAL}" ]]; then chmod 666 "${MESHCORE_TDECK_SERIAL}" || true; fi'
${DOCKER} exec -d "${CONTAINER_NAME}" bash -lc "STATIC_PORT=8080 HARDWARE_BRIDGE_PORT=8081 ${CONTAINER_WORKSPACE}/meshcore_tdeck_plus_lvgl_plan/tools/start_simulators.sh"
if ls /dev/video* >/dev/null 2>&1; then
    pkill -f "start_host_camera_capture.sh" >/dev/null 2>&1 || true
    ROOT="${PLAN_DIR}" nohup "${PLAN_DIR}/tools/start_host_camera_capture.sh" >/tmp/meshcore-tdeck-camera-supervisor.log 2>&1 &
fi

cat <<EOF
Container ${CONTAINER_NAME} is running.
SSH: ssh -p ${SSH_PORT} ${DEV_SSH_USER:-developer}@${DEV_HOSTNAME:-localhost}
Simulator HTTPS: https://${DEV_HOSTNAME:-localhost}:${HTTP_PORT}/screens.html
Web flasher: https://${DEV_HOSTNAME:-localhost}:${HTTP_PORT}/flasher.html
Hardware bridge: https://${DEV_HOSTNAME:-localhost}:${HARDWARE_BRIDGE_PORT}/api/status
LVGL noVNC: http://${DEV_HOSTNAME:-localhost}:${NOVNC_PORT}/vnc.html?autoconnect=true&resize=scale
Workspace: ${CONTAINER_WORKSPACE}
Persistent cache: ${HOST_CACHE_ROOT}
EOF
