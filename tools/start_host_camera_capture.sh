#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-${PWD%/*}/meshcore_tdeck_plus_lvgl_plan}"
CAMERA_DEVICE="${CAMERA_DEVICE:-}"
CAMERA_SIZE="${CAMERA_SIZE:-640x480}"
CAMERA_FRAMERATE="${CAMERA_FRAMERATE:-15}"
CAMERA_FRAMES="${CAMERA_FRAMES:-12}"
CAMERA_INTERVAL="${CAMERA_INTERVAL:-0.45}"
CAMERA_FILTER="${CAMERA_FILTER:-eq=contrast=1.15:saturation=1.08}"
CAMERA_OUTPUT="${CAMERA_OUTPUT:-${ROOT}/simulator/build/camera.jpg}"
CAMERA_TMP="${CAMERA_TMP:-/tmp/meshcore-tdeck-camera-frame-${USER:-developer}.tmp.jpg}"
CAMERA_LOG="${CAMERA_LOG:-/tmp/meshcore-tdeck-camera.log}"
CAMERA_AUTO_EXPOSURE="${CAMERA_AUTO_EXPOSURE:-1}"
CAMERA_EXPOSURE="${CAMERA_EXPOSURE:-20}"
CAMERA_BRIGHTNESS="${CAMERA_BRIGHTNESS:-30}"
CAMERA_CONTRAST="${CAMERA_CONTRAST:-5}"
CAMERA_SATURATION="${CAMERA_SATURATION:-100}"
CAMERA_BACKLIGHT="${CAMERA_BACKLIGHT:-0}"

if [[ -z "${CAMERA_DEVICE}" ]]; then
    for candidate in \
        /dev/v4l/by-id/*-video-index0 \
        /dev/video0 \
        /dev/video1; do
        if [[ -e "${candidate}" ]]; then
            CAMERA_DEVICE="${candidate}"
            break
        fi
    done
fi

if [[ -z "${CAMERA_DEVICE}" || ! -e "${CAMERA_DEVICE}" ]]; then
    echo "camera capture: no /dev/video device found" >&2
    exit 1
fi

mkdir -p "$(dirname "${CAMERA_OUTPUT}")"
rm -f "${CAMERA_TMP}" 2>/dev/null || sudo -n rm -f "${CAMERA_TMP}" 2>/dev/null || true

if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl -d "${CAMERA_DEVICE}" \
        -c "auto_exposure=${CAMERA_AUTO_EXPOSURE}" \
        -c "exposure_time_absolute=${CAMERA_EXPOSURE}" \
        -c "brightness=${CAMERA_BRIGHTNESS}" \
        -c "contrast=${CAMERA_CONTRAST}" \
        -c "saturation=${CAMERA_SATURATION}" \
        -c "backlight_compensation=${CAMERA_BACKLIGHT}" \
        >>"${CAMERA_LOG}" 2>&1 || true
fi

if [[ -r "${CAMERA_DEVICE}" && -w "${CAMERA_DEVICE}" ]]; then
    FFMPEG=(ffmpeg)
else
    FFMPEG=(sudo -n ffmpeg)
fi

while true; do
    FILTER_ARGS=()
    if [[ -n "${CAMERA_FILTER}" ]]; then
        FILTER_ARGS=(-vf "${CAMERA_FILTER}")
    fi
    if timeout 8 "${FFMPEG[@]}" -hide_banner -loglevel error -y \
        -f video4linux2 -input_format mjpeg -video_size "${CAMERA_SIZE}" -framerate "${CAMERA_FRAMERATE}" -i "${CAMERA_DEVICE}" \
        "${FILTER_ARGS[@]}" -frames:v "${CAMERA_FRAMES}" -q:v 4 -update 1 "${CAMERA_TMP}" >>"${CAMERA_LOG}" 2>&1; then
        if [[ -w "$(dirname "${CAMERA_OUTPUT}")" ]]; then
            mv "${CAMERA_TMP}" "${CAMERA_OUTPUT}"
            chmod 644 "${CAMERA_OUTPUT}" || true
        else
            sudo -n install -m 0644 "${CAMERA_TMP}" "${CAMERA_OUTPUT}"
            rm -f "${CAMERA_TMP}" 2>/dev/null || sudo -n rm -f "${CAMERA_TMP}"
        fi
    else
        rm -f "${CAMERA_TMP}" 2>/dev/null || sudo -n rm -f "${CAMERA_TMP}" || true
    fi
    sleep "${CAMERA_INTERVAL}"
done
