# BotSports Arena Display — Agent Instructions & Development Rules

## 1. Strict Semantic Versioning (`MAJOR.MINOR.PATCH`)

All firmware changes **MUST** strictly adhere to [Semantic Versioning 2.0.0](https://semver.org/):

- **`MAJOR` (`X.0.0`)**: Breaking changes to the BotSports API protocol, wireless UDP multi-display sync packet format (`ArenaSyncPacket`), or NVS storage schema that require resetting or re-pairing devices.
- **`MINOR` (`1.X.0`)**: New features, new display screens/animations, new web control panel capabilities, or new API/SSE event handlers added in a backwards-compatible manner.
- **`PATCH` (`1.3.X`)**: Backwards-compatible bug fixes, timing/interpolation adjustments, UI layout tweaks, or performance optimizations.

### Version Update Checklist (Required on Every Firmware Change)
Whenever you modify any firmware code (`.ino`, `.cpp`, `.h`):
1. **Bump `FIRMWARE_VERSION`** in [`config.h`](./config.h) according to the change scope (`MAJOR`, `MINOR`, or `PATCH`) before compiling.
2. **Keep HTML fallback version labels in sync** in [`wifi_portal.cpp`](./wifi_portal.cpp) (`#fw_version_badge` and `#ota_ver_label`).
3. **Build using `./build.sh`** so that the versioned OTA binary (`dist/botsports-arena-display-v<VERSION>.bin` and `build/esp32.esp32.m5stack_stamp_s3/botsports-arena-display-v<VERSION>.bin`) is always generated with the matching version number in its filename.
4. **GitHub Releases (`Release-VX.X.X`)**:
   - Pushing a tag matching `Release-VX.X.X` (e.g. `Release-V1.3.2`) triggers [`.github/workflows/release-firmware.yml`](./.github/workflows/release-firmware.yml), which automatically stamps `FIRMWARE_VERSION` in `config.h` and `wifi_portal.cpp`, compiles the ESP32-S3 binary, generates release notes and SHA-256 checksums, and publishes a GitHub Release with `botsports-arena-display-vX.X.X.bin`, `botsports-arena-display-vX.X.X.merged.bin`, and `version.json`.
   - Use `./release.sh [patch|minor|major|X.Y.Z]` to bump the version and create the `Release-VX.X.X` tag.

---

## 2. Building & Generating Versioned OTA Binaries

- Always use `./build.sh` (or `./build.sh --upload <PORT>`) instead of bare `arduino-cli compile` so that versioned `.bin` artifacts are generated automatically:
  ```bash
  ./build.sh
  ```
- Output binaries are written to:
  - `dist/botsports-arena-display-v<VERSION>.bin` (for Web Control Panel OTA upload)
  - `build/esp32.esp32.m5stack_stamp_s3/botsports-arena-display-v<VERSION>.bin`
- Never overwrite or leave stale unversioned binaries without updating the versioned artifact.

---

## 3. Hardware & USB Flashing Safety

- **Verify serial port identity before flashing via USB**:
  - Never flash `/dev/ttyACM0` or other attached USB serial peripherals (keyboards, bridge devices, etc.) without first verifying the target ESP32-S3 port with `arduino-cli board list` or `udevadm info`.
  - Target board FQBN: `esp32:esp32:m5stack_stamp_s3`.
- **Dual-Core Architecture Constraints**:
  - **Core 0**: Runs `networkTask` (Priority 2: WebServer, DNS, UDP ArenaSync) and `apiTask` (Priority 1: HTTPS SSE stream & REST fallback).
  - **Core 1**: Runs `loop()` (60 FPS HUB75 DMA LED matrix rendering and button handling).
  - Never perform blocking network/TLS operations on Core 1, and never trigger polling TLS handshakes while `CLOCK_COUNT_IN` or `CLOCK_RUNNING` is active or while an OTA update (`wifiPortal.isOTAInProgress()`) is underway.
