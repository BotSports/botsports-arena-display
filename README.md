# BotSports Arena RGB Matrix Display

Firmware for an in/on-arena combat robotics display driven by the [BotSports API](https://api.botsports.org) or run in complete **Standalone Arena Mode**. Designed for the **M5Stack StampS3 / StampS3A** (ESP32-S3) driving a **WaveShare 80×40 RGB LED Matrix Panel** (HUB75 interface, 1/20 scan).

---

## Features

### 1. Dual Operational Modes
- **BotSports API Sync Mode**:
  - Automatically synchronizes with the live BotSports arena status API (`GET /app/arenas/device/status`).
  - Real-time match state, tournament round, match number, and assigned combatants.
  - Sub-second local clock interpolation against server time with 0 network drift.
- **Standalone Arena Mode**:
  - Operate complete combat matches without needing an active internet connection or BotSports event.
  - Control the match timer (Start / Pause / Resume / Reset) from the built-in web control panel.
  - Custom match duration (1:00, 2:00, 3:00 standard, or custom seconds).
  - Customize Match #, Round #, and Red/Blue Bot names directly from the web interface.

### 2. Knockout (KO) Action & Dramatic Animation
- **KO Victory Buttons** (`RED WINS (KO)` / `BLUE WINS (KO)`):
  - Automatically halts the match clock and records the exact timestamp of the knockout.
  - **Phase 1 (First 2.5s)**: High-energy strobe and explosion border with giant centered **"K.O.!"** text.
  - **Phase 2**: Presentation screen showcasing **"KNOCKOUT - RED/BLUE"**, the winning bot's name in corner color, and the remaining combat time.

### 3. Always-On Web Control Panel & mDNS
- Served on **port 80** in **both SoftAP and connected Wi-Fi Station mode**.
- Accessible directly via IP (`http://<device-ip>`) or local mDNS address:
  ```
  http://botsports-arena.local
  ```
- **Tab 1: Live Arena Control**:
  - Giant live digital match clock synced with the LED matrix.
  - Start, Pause, Resume, Reset buttons.
  - Quick duration presets (1 min, 2 min, 3 min).
  - Red Corner KO and Blue Corner KO trigger buttons.
  - Match details editor (Bot names, Match/Round numbers).
- **Tab 2: Screen FX & Animation Test Suite**:
  - Test Red KO FX & Blue KO FX.
  - Test "TIME!" expired animation.
  - Test Active Match HUD, Upcoming Match screen, Standby screen, Boot screen, and Error screen.
  - Real-time live LED brightness slider (adjusts display intensity instantaneously).
- **Tab 3: Device & Wi-Fi Configuration**:
  - Wi-Fi network scanner and credential manager.
  - Arena API key configuration.
  - Server environment selector (Development, Production, Custom URL).
  - Display target filter (Main Display, Red Corner HUD, Blue Corner HUD).

### 4. Wireless Multi-Display Synchronization
- **Zero-Cable Arena Linking**:
  - Wirelessly synchronizes 2 (or more) matrix displays installed in the same combat arena.
  - **Master Display**: Connects to Wi-Fi and handles all BotSports API polling (or runs local standalone match control). Broadcasts packed binary sync packets via UDP (port 4210) every 120ms and immediately upon any event (timer start/pause/KO).
  - **Slave Display**: Listens wirelessly for packets matching its configured **Arena Sync Key** (e.g. `arena_1`). The slave **never polls the API**, preventing API rate limits and eliminating network contention.
  - **Dead-Exact Clock Unison**: Master and Slave both calculate remaining seconds using local hardware timestamps synchronized to the Master's clock down to the sub-millisecond, providing smooth 30+ FPS countdown with zero jitter.
  - **Corner-Specific Views**: Each synced display can be assigned a local corner focus (`corner = "red"` for Red Corner, `corner = "blue"` for Blue Corner, or `corner = "all"` for Main Arena). While the clock, match round, and KO status stay 100% synchronized, each display highlights its corner's fighter and combat metrics.
  - **Bidirectional Heartbeat**: Slaves report link status and signal strength back to the Master, visible in real time on the web control panel.

### 5. Smart Onboard Button (StampS3 G0)
- **Tap Button (< 1s)**: Scrolls the current IP address and `botsports-arena.local` across the display.
- **Hold Button (> 3s)**: Forces the device into SoftAP configuration mode (`BotSports-Arena-XXXX`).

---

## Hardware & Power Requirements

| Component | Description |
| :--- | :--- |
| **Microcontroller** | [M5Stack StampS3 or StampS3A](https://docs.m5stack.com/en/core/StampS3) (ESP32-S3FN8, 8MB Flash) |
| **Display Panel** | [WaveShare RGB-Matrix-P4-80x40](https://www.waveshare.com/rgb-matrix-p4-80x40.htm) (HUB75, 4mm pitch, 80×40 resolution, 1/20 scan) |
| **Power Supply** | Dedicated 5V / 4A DC Power Supply (feeds panel VH4 socket & StampS3 5V in) |

> [!IMPORTANT]
> **Powering the StampS3 and Panel**:
> - The 16-pin HUB75 ribbon cable carries logic signals and GND only; it **does NOT carry 5V power**.
> - The 80×40 LED panel draws up to 4A at 5V under full illumination. Power the panel directly via the included VH4 power cable from an external 5V/4A DC power supply.
> - **You can power the StampS3 from the same 5V supply**: connect the 5V line of the external power supply to StampS3 **Pin 13 (`5V`)**, and connect the power supply ground to StampS3 **`GND`**.
> - **Do not** attempt to power the LED matrix from the ESP32 USB port or onboard 3.3V pin. Ensure external power supply GND is tied to StampS3 GND.

---

## Wiring Diagram (StampS3 to HUB75)

Standard 16-pin HUB75 IDC header pinout mapping to M5Stack StampS3 GPIO pins (defined in [`config.h`](file:///home/pmumby/Development/Personal/botsports-arena-display/config.h)):

```
          HUB75 16-Pin Connector
               +-------+
       R1 ( 1) | 1   2 | G1 ( 2)
       B1 ( 3) | 3   4 | GND
       R2 ( 4) | 5   6 | G2 ( 5)
       B2 ( 6) | 7   8 | E  (11)
        A ( 7) | 9  10 | B  ( 8)
        C ( 9) | 11 12 | D  (10)
      CLK (14) | 13 14 | LAT (12)
       OE (46) | 15 16 | GND
               +-------+
```

| HUB75 Pin | Function | StampS3 GPIO | Note |
| :--- | :--- | :--- | :--- |
| **R1** | Red Data (Upper half) | **GPIO 1** | |
| **G1** | Green Data (Upper half) | **GPIO 2** | |
| **B1** | Blue Data (Upper half) | **GPIO 3** | |
| **R2** | Red Data (Lower half) | **GPIO 4** | |
| **G2** | Green Data (Lower half) | **GPIO 5** | |
| **B2** | Blue Data (Lower half) | **GPIO 6** | |
| **A** | Row Address A | **GPIO 7** | |
| **B** | Row Address B | **GPIO 8** | |
| **C** | Row Address C | **GPIO 9** | |
| **D** | Row Address D | **GPIO 10** | |
| **E** | Row Address E | **GPIO 11** | Required for 1/20 scan |
| **LAT / STB**| Data Latch | **GPIO 12** | |
| **OE** | Output Enable (Active Low) | **GPIO 46** | Output Enable |
| **CLK** | Shift Clock | **GPIO 14** | |
| **GND** | Signal Ground | **GND** | Connect to StampS3 GND & PSU GND |

---

## Accessing the Control Panel

You can open the web interface from any smartphone, tablet, or laptop on the same network:

1. **If on Wi-Fi**: Open `http://botsports-arena.local` or `http://<device-ip>` in your browser.
   *(Tip: Tap the StampS3 G0 button to view the IP on the screen at any time).*
2. **If in SoftAP Mode**: Connect to `BotSports-Arena-XXXX` and open `http://192.168.4.1`.

---

## Building & Flashing

### Option A: Using `arduino-cli`

```bash
# 1. Install board support (if not already installed)
arduino-cli core install esp32:esp32

# 2. Install required libraries
arduino-cli lib install "ESP32 HUB75 LED MATRIX PANEL DMA Display" "Adafruit GFX Library" "ArduinoJson"

# 3. Compile sketch
arduino-cli compile -b esp32:esp32:m5stack_stamp_s3 .

# 4. Upload to device (specify serial port, e.g. /dev/ttyACM0)
arduino-cli upload -b esp32:esp32:m5stack_stamp_s3 -p /dev/ttyACM0 .
```

### Option B: Using PlatformIO

```bash
# Build firmware
pio run

# Flash to device
pio run --target upload

# Open serial monitor
pio device monitor
```

---

## Project Structure

```
botsports-arena-display/
├── botsports-arena-display.ino   # Main application loop and display coordinator
├── config.h                      # Pin definitions, defaults, and timings
├── config_manager.h / .cpp       # NVS preferences management (Flash storage)
├── wifi_portal.h / .cpp          # Web server, mDNS, REST APIs, and responsive web app
├── match_controller.h / .cpp     # Standalone arena match logic, timer state, KO engine
├── arena_sync.h / .cpp           # Wireless UDP multi-display clock sync protocol
├── api_client.h / .cpp           # HTTPS client for BotSports Arena API & clock interpolation
├── display_manager.h / .cpp      # HUB75 matrix driver, big 7-segment clock, KO animations
├── button_manager.h / .cpp       # G0 button tap (scroll IP) and hold (AP mode) handler
├── platformio.ini                # PlatformIO build configuration
└── README.md                     # Documentation and user guide
```

---

## License

MIT License. Developed for the BotSports robotics competition ecosystem.
