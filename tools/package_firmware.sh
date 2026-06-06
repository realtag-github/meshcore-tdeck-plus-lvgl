#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
FIRMWARE_DIR="$ROOT_DIR/firmware"
PIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-$FIRMWARE_DIR/.pio/build}"
BUILD_ENVS="${BUILD_ENVS:-tdeck-plus-915}"
ESPTOOL_CMD=()
BOOT_APP0_BIN="${BOOT_APP0_BIN:-}"

if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL_CMD=(esptool.py)
elif python3 -c 'import esptool' >/dev/null 2>&1; then
  ESPTOOL_CMD=(python3 -m esptool)
else
  ESPTOOL_PATH="$(find "$HOME/.platformio/packages" ${HOME}/.platformio/packages /root/.platformio/packages -path '*/esptool.py' -type f 2>/dev/null | head -n 1 || true)"
  if [[ "$ESPTOOL_PATH" == "" ]]; then
    echo "esptool.py not found" >&2
    exit 1
  fi
  if [[ -x /opt/platformio/bin/python3 ]]; then
    PIO_PYTHON=/opt/platformio/bin/python3
  else
    PIO_PYTHON="$(find "$HOME/.platformio/penv" ${HOME}/.platformio/penv /root/.platformio/penv \( -name python -o -name python3 \) -type f 2>/dev/null | head -n 1 || true)"
  fi
  if [[ "${PIO_PYTHON:-}" == "" ]]; then
    PIO_PYTHON=python3
  fi
  ESPTOOL_CMD=("$PIO_PYTHON" "$ESPTOOL_PATH")
fi

if [[ "$BOOT_APP0_BIN" == "" ]]; then
  BOOT_APP0_BIN="$(find "$HOME/.platformio/packages" ${HOME}/.platformio/packages /root/.platformio/packages -path '*/framework-arduinoespressif32/tools/partitions/boot_app0.bin' -type f 2>/dev/null | head -n 1 || true)"
fi
if [[ "$BOOT_APP0_BIN" == "" || ! -f "$BOOT_APP0_BIN" ]]; then
  echo "boot_app0.bin not found" >&2
  exit 1
fi

if [[ -L "$DIST_DIR" ]]; then
  DIST_LINK_TARGET="$(readlink "$DIST_DIR")"
  if [[ "$DIST_LINK_TARGET" != /* ]]; then
    DIST_LINK_TARGET="$(cd "$(dirname "$DIST_DIR")" && pwd)/$DIST_LINK_TARGET"
  fi
  mkdir -p "$DIST_LINK_TARGET"
elif [[ -e "$DIST_DIR" && ! -d "$DIST_DIR" ]]; then
  echo "DIST_DIR exists but is not a directory: $DIST_DIR" >&2
  exit 1
else
  mkdir -p "$DIST_DIR"
fi
rm -f "$DIST_DIR"/MeshCore-TDeckPlus-Launcher-*.bin \
      "$DIST_DIR"/tdeck-plus-*-firmware.bin \
      "$DIST_DIR"/tdeck-plus-*-bootloader.bin \
      "$DIST_DIR"/tdeck-plus-*-partitions.bin \
      "$DIST_DIR"/tdeck-plus-*-merged.bin \
      "$DIST_DIR"/manifest.txt

cd "$FIRMWARE_DIR"
BUILD_ENVS="$BUILD_ENVS" "$ROOT_DIR/tools/verify_firmware_parallel.sh"

cat > "$DIST_DIR/manifest.txt" <<MANIFEST
MeshCore T-Deck Plus firmware package
Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
MANIFEST

read -r -a ENVS <<< "$BUILD_ENVS"
for env in "${ENVS[@]}"; do
  build_dir="$PIO_BUILD_DIR/$env"
  region="${env#tdeck-plus-}"
  cp "$build_dir/firmware.bin" "$DIST_DIR/$env-firmware.bin"
  cp "$build_dir/firmware.bin" "$DIST_DIR/MeshCore-TDeckPlus-Launcher-$region.bin"
  cp "$build_dir/bootloader.bin" "$DIST_DIR/$env-bootloader.bin"
  cp "$build_dir/partitions.bin" "$DIST_DIR/$env-partitions.bin"
  cp "$BOOT_APP0_BIN" "$DIST_DIR/$env-boot_app0.bin"
  "${ESPTOOL_CMD[@]}" --chip esp32s3 merge_bin \
    -o "$DIST_DIR/$env-merged.bin" \
    0x0000 "$build_dir/bootloader.bin" \
    0x8000 "$build_dir/partitions.bin" \
    0xe000 "$BOOT_APP0_BIN" \
    0x10000 "$build_dir/firmware.bin" >/dev/null
  for artifact in "$env-firmware.bin" "MeshCore-TDeckPlus-Launcher-$region.bin" "$env-bootloader.bin" "$env-partitions.bin" "$env-boot_app0.bin" "$env-merged.bin"; do
    path="$DIST_DIR/$artifact"
    size=$(wc -c < "$path")
    sha=$(sha256sum "$path" | awk '{print $1}')
    printf '%s %s bytes sha256=%s\n' "$artifact" "$size" "$sha" >> "$DIST_DIR/manifest.txt"
  done
done

cat >> "$DIST_DIR/manifest.txt" <<'MANIFEST'

Launcher notes:
- Use MeshCore-TDeckPlus-Launcher-915.bin from SD/WebUI/OTA in bmorcelli Launcher.
- The Launcher binaries are app-only PlatformIO images and should start with the ESP image magic byte 0xE9.
- The *-merged.bin files are full-flash USB/esptool images with bootloader, OTA boot data, partition table, and app image.
- Flash a *-merged.bin once over USB to enable WiFi OTA; after that upload tdeck-plus-915-firmware.bin to the IP printed as "wifi ota: ready".
- Router WiFi credentials can be set over serial with: wifi set <ssid> <password>
- Credentials can also be injected at build time with MESHCORE_WIFI_SSID and MESHCORE_WIFI_PASSWORD.
MANIFEST

cat "$DIST_DIR/manifest.txt"
