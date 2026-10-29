#pragma once

#include <Arduino.h>

// ============================================================================
// Firmware Information
// ============================================================================
#define FIRMWARE_VERSION    "1.4.4"
#define DEVICE_NAME_PREFIX  "BotSports-Arena"
#define DEFAULT_HAZARDS_SECONDS 60.0f
#define GITHUB_REPO_SLUG    "BotSports/botsports-arena-display"
#define GITHUB_VERSION_URL  "https://github.com/BotSports/botsports-arena-display/releases/latest/download/version.json"
#define GITHUB_LATEST_API   "https://api.github.com/repos/BotSports/botsports-arena-display/releases/latest"


// ============================================================================
// BotSports API Configuration
// ============================================================================
#define DEFAULT_API_URL_DEV   "https://api-dev.botsports.org"
#define DEFAULT_API_URL_PROD  "https://api.botsports.org"

// Default polling interval in normal state (milliseconds)
#define API_POLL_INTERVAL_MS        1500
// Faster polling during active matches for state changes (milliseconds)
#define API_POLL_INTERVAL_MATCH_MS  1000
// Polling interval when idle / no active event (milliseconds)
#define API_POLL_INTERVAL_IDLE_MS   3000

// ============================================================================
// Wireless Multi-Display Sync Link (Master / Slave)
// ============================================================================
#define ARENA_SYNC_PORT             4210
#define ARENA_SYNC_MAGIC            0x42535054 // "BSPT" in ASCII
#define ARENA_SYNC_DEFAULT_KEY      "arena_1"
#define ARENA_SYNC_BROADCAST_MS     120        // Send sync packet every 120ms
#define ARENA_SYNC_TIMEOUT_MS       3000       // Timeout before Slave marks link lost

enum SyncRole {
    SYNC_ROLE_SOLO   = 0, // Standalone display (not synced)
    SYNC_ROLE_MASTER = 1, // Talks to API, broadcasts sync to Slaves
    SYNC_ROLE_SLAVE  = 2  // Does NOT talk to API, receives sync from Master
};

// ============================================================================
// Display Configuration: WaveShare RGB-Matrix-P4-80x40 (HUB75, 1/20 scan)
// ============================================================================
#define PANEL_RES_X   80   // Width in pixels
#define PANEL_RES_Y   40   // Height in pixels
#define PANEL_CHAIN   1    // 1 panel

// Recommended HUB75 Pin mapping for M5Stack StampS3 / StampS3A
// (StampS3 breaks out GPIO 1-15, 39-44, 46. GPIO 0 is user button, GPIO 21/38 RGB LED)
#ifndef PIN_R1
#define PIN_R1   1
#endif
#ifndef PIN_G1
#define PIN_G1   2
#endif
#ifndef PIN_B1
#define PIN_B1   3
#endif
#ifndef PIN_R2
#define PIN_R2   4
#endif
#ifndef PIN_G2
#define PIN_G2   5
#endif
#ifndef PIN_B2
#define PIN_B2   6
#endif

#ifndef PIN_A
#define PIN_A    7
#endif
#ifndef PIN_B
#define PIN_B    8
#endif
#ifndef PIN_C
#define PIN_C    9
#endif
#ifndef PIN_D
#define PIN_D    10
#endif
#ifndef PIN_E
#define PIN_E    11
#endif

#ifndef PIN_LAT
#define PIN_LAT  12
#endif
#ifndef PIN_OE
#define PIN_OE   46
#endif
#ifndef PIN_CLK
#define PIN_CLK  14
#endif

// Default display brightness (1 to 255)
#define DEFAULT_BRIGHTNESS  90

// ============================================================================
// Hardware User Input (M5Stack StampS3 Built-in Button)
// ============================================================================
#define PIN_BUTTON_USER     0   // StampS3 G0 button (Active LOW)
#define BUTTON_HOLD_AP_MS   3000 // Hold 3 seconds to force AP mode

// ============================================================================
// WiFi & Captive Portal Configuration
// ============================================================================
#define WIFI_CONNECT_TIMEOUT_SEC  15
#define AP_SSID_PREFIX            "BotSports-Arena-"
#define AP_DEFAULT_IP             IPAddress(192, 168, 4, 1)
#define AP_GATEWAY                IPAddress(192, 168, 4, 1)
#define AP_SUBNET                 IPAddress(255, 255, 255, 0)
#define DNS_PORT                  53
#define HTTP_PORT                 80
