#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

FQBN="esp32:esp32:m5stack_stamp_s3"
ARDUINO_CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "$HOME/.local/bin/arduino-cli")}"

# Extract semantic version from config.h
VERSION=$(grep -E '^#define[[:space:]]+FIRMWARE_VERSION' config.h | sed -E 's/.*"([^"]+)".*/\1/')
if [[ -z "$VERSION" ]]; then
    echo "Error: Could not extract FIRMWARE_VERSION from config.h" >&2
    exit 1
fi

echo "=================================================="
echo " Building BotSports Arena Display Firmware v${VERSION}"
echo "=================================================="

"$ARDUINO_CLI" compile -b "$FQBN" --export-binaries .

BUILD_DIR="build/esp32.esp32.m5stack_stamp_s3"
DIST_DIR="dist"
mkdir -p "$DIST_DIR"

VERSIONED_BIN="${DIST_DIR}/botsports-arena-display-v${VERSION}.bin"
VERSIONED_BUILD_BIN="${BUILD_DIR}/botsports-arena-display-v${VERSION}.bin"
VERSIONED_MERGED_BIN="${DIST_DIR}/botsports-arena-display-v${VERSION}.merged.bin"

cp -f "${BUILD_DIR}/botsports-arena-display.ino.bin" "$VERSIONED_BIN"
cp -f "${BUILD_DIR}/botsports-arena-display.ino.bin" "$VERSIONED_BUILD_BIN"

if [[ -f "${BUILD_DIR}/botsports-arena-display.ino.merged.bin" ]]; then
    cp -f "${BUILD_DIR}/botsports-arena-display.ino.merged.bin" "$VERSIONED_MERGED_BIN"
fi

echo ""
echo "Versioned OTA firmware generated:"
echo "  -> ${VERSIONED_BIN}"
echo "  -> ${VERSIONED_BUILD_BIN}"

# Optional USB upload if --upload <PORT> is passed
if [[ "${1:-}" == "--upload" ]]; then
    PORT="${2:-/dev/ttyACM1}"
    echo ""
    echo "Uploading v${VERSION} to ${PORT}..."
    "$ARDUINO_CLI" upload -b "$FQBN" -p "$PORT" .
fi
