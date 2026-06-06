#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan}"
STATIC_PORT="${STATIC_PORT:-8080}"
STATIC_HTTPS="${STATIC_HTTPS:-1}"
STATIC_CERT_DIR="${STATIC_CERT_DIR:-/tmp/meshcore-static-cert-developer}"
STATIC_WEBROOT="${STATIC_WEBROOT:-/tmp/meshcore-8092-web}"
HARDWARE_BRIDGE_PORT="${HARDWARE_BRIDGE_PORT:-8081}"

cd "${ROOT}/simulator"

make run >/tmp/meshcore-static-sim-build.log 2>&1

pkill -f "python3 -m http.server ${STATIC_PORT} -d ${ROOT}/simulator/build" >/dev/null 2>&1 || true
pkill -f "serve_static_https.py --port ${STATIC_PORT}" >/dev/null 2>&1 || true
pkill -f "websockify.*${STATIC_PORT}.*localhost:5900" >/dev/null 2>&1 || true
pkill -f "hardware_bridge.py --port ${HARDWARE_BRIDGE_PORT}" >/dev/null 2>&1 || true

mkdir -p "${STATIC_CERT_DIR}"
if [[ ! -f "${STATIC_CERT_DIR}/cert.pem" || ! -f "${STATIC_CERT_DIR}/key.pem" ]]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
        -keyout "${STATIC_CERT_DIR}/key.pem" \
        -out "${STATIC_CERT_DIR}/cert.pem" \
        -subj "/CN=dev-host.local" \
        -addext "subjectAltName=DNS:dev-host.local,DNS:localhost,IP:127.0.0.1" \
        >/tmp/meshcore-static-cert.log 2>&1
fi
chmod 644 "${STATIC_CERT_DIR}/key.pem" || true
cp "${STATIC_CERT_DIR}/cert.pem" "${ROOT}/simulator/build/dev-host-local-cert.pem"

rm -rf "${STATIC_WEBROOT}"
mkdir -p "${STATIC_WEBROOT}"
cp -a /usr/share/novnc/. "${STATIC_WEBROOT}/"
cp -a "${ROOT}/simulator/build/." "${STATIC_WEBROOT}/"
ln -sf "${ROOT}/simulator/build/camera.jpg" "${STATIC_WEBROOT}/camera.jpg"

"${ROOT}/tools/start_lvgl_vnc.sh"

if [[ -e "${MESHCORE_TDECK_SERIAL:-/dev/tdeck-plus}" ]]; then
    chmod 666 "${MESHCORE_TDECK_SERIAL:-/dev/tdeck-plus}" >/dev/null 2>&1 || true
fi

if [[ "${STATIC_HTTPS}" == "1" ]]; then
    websockify --web="${STATIC_WEBROOT}" --ssl-only \
        --cert="${STATIC_CERT_DIR}/cert.pem" \
        --key="${STATIC_CERT_DIR}/key.pem" \
        "${STATIC_PORT}" "localhost:${VNC_PORT:-5900}" \
        >/tmp/meshcore-static-sim-https.log 2>&1 &
    STATIC_SCHEME=https
    python3 "${ROOT}/tools/hardware_bridge.py" \
        --port "${HARDWARE_BRIDGE_PORT}" \
        --cert "${STATIC_CERT_DIR}/cert.pem" \
        --key "${STATIC_CERT_DIR}/key.pem" \
        >/tmp/meshcore-hardware-bridge.log 2>&1 &
else
    python3 -m http.server "${STATIC_PORT}" -d "${STATIC_WEBROOT}" >/tmp/meshcore-static-sim-http.log 2>&1 &
    STATIC_SCHEME=http
fi

cat <<EOF
Simulators started.
Static simulator: ${STATIC_SCHEME} on container port ${STATIC_PORT}
Hardware bridge: ${STATIC_SCHEME} on container port ${HARDWARE_BRIDGE_PORT}
LVGL noVNC container port: ${NOVNC_PORT:-6080}
Logs:
  /tmp/meshcore-static-sim-build.log
  /tmp/meshcore-static-sim-http.log
  /tmp/meshcore-static-sim-https.log
  /tmp/meshcore-hardware-bridge.log
  /tmp/meshcore-lvgl-sim.log
  /tmp/meshcore-xvfb.log
  /tmp/meshcore-x11vnc.log
  /tmp/meshcore-novnc.log
EOF
