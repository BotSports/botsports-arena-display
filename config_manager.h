#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

struct AppSettings {
    String wifi_ssid;
    String wifi_pass;
    String api_key;
    String api_url;
    uint8_t brightness;
    String corner;       // "all", "red", "blue"
    bool is_configured;

    // Wireless Multi-Display Sync Link
    uint8_t sync_role;   // 0 = Solo, 1 = Master, 2 = Slave
    String sync_key;     // Arena identifier, e.g. "arena_1"

    // Persisted Operating Mode
    uint8_t op_mode;     // 0 = OPMODE_API, 1 = OPMODE_STANDALONE

    // Tournament Scoping & Hazard Configuration
    int tournament_id;   // 0 = auto-resolve, >0 = scoped to specific tournament
    float hazards_seconds; // countdown mark for hazards activation (default 60s)
};


class ConfigManager {
public:
    ConfigManager();
    bool begin();
    void loadSettings();
    bool saveSettings();
    void resetToDefaults();

    AppSettings settings;

private:
    Preferences _prefs;
};

extern ConfigManager configMgr;
