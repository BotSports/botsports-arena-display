#include "config_manager.h"

ConfigManager configMgr;

ConfigManager::ConfigManager() {
    settings.wifi_ssid = "";
    settings.wifi_pass = "";
    settings.api_key = "";
    settings.api_url = DEFAULT_API_URL_DEV;
    settings.brightness = DEFAULT_BRIGHTNESS;
    settings.corner = "all";
    settings.is_configured = false;
    settings.sync_role = SYNC_ROLE_MASTER; // Default to Master / Solo
    settings.sync_key = ARENA_SYNC_DEFAULT_KEY;
    settings.op_mode = 1; // Default to Standalone (0=API, 1=Standalone)
}

bool ConfigManager::begin() {
    return _prefs.begin("botsports", false);
}

void ConfigManager::loadSettings() {
    _prefs.begin("botsports", true); // read-only mode
    settings.wifi_ssid = _prefs.getString("ssid", "");
    settings.wifi_pass = _prefs.getString("pass", "");
    settings.api_key = _prefs.getString("api_key", "");
    settings.api_url = _prefs.getString("api_url", DEFAULT_API_URL_DEV);
    settings.brightness = _prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
    settings.corner = _prefs.getString("corner", "all");
    settings.is_configured = _prefs.getBool("configured", false);
    settings.sync_role = _prefs.getUChar("sync_role", SYNC_ROLE_MASTER);
    settings.sync_key = _prefs.getString("sync_key", ARENA_SYNC_DEFAULT_KEY);
    settings.op_mode = _prefs.getUChar("op_mode", 1);
    _prefs.end();

    if (settings.brightness == 0) {
        settings.brightness = DEFAULT_BRIGHTNESS;
    }
    if (settings.api_url.length() == 0) {
        settings.api_url = DEFAULT_API_URL_DEV;
    }
    if (settings.sync_key.length() == 0) {
        settings.sync_key = ARENA_SYNC_DEFAULT_KEY;
    }

    Serial.println("[Config] Settings loaded:");
    Serial.printf("  SSID: %s\n", settings.wifi_ssid.c_str());
    Serial.printf("  API URL: %s\n", settings.api_url.c_str());
    Serial.printf("  API Key: %s\n", settings.api_key.length() > 6 ? (settings.api_key.substring(0, 6) + "...").c_str() : "(empty)");
    Serial.printf("  Corner: %s\n", settings.corner.c_str());
    Serial.printf("  Brightness: %u\n", settings.brightness);
    Serial.printf("  Sync Role: %s\n", (settings.sync_role == SYNC_ROLE_MASTER) ? "Master" : ((settings.sync_role == SYNC_ROLE_SLAVE) ? "Slave" : "Solo"));
    Serial.printf("  Sync Key: %s\n", settings.sync_key.c_str());
}

bool ConfigManager::saveSettings() {
    if (!_prefs.begin("botsports", false)) { // read-write
        Serial.println("[Config] Failed to open preferences for writing");
        return false;
    }

    _prefs.putString("ssid", settings.wifi_ssid);
    _prefs.putString("pass", settings.wifi_pass);
    _prefs.putString("api_key", settings.api_key);
    _prefs.putString("api_url", settings.api_url);
    _prefs.putUChar("bright", settings.brightness);
    _prefs.putString("corner", settings.corner);
    _prefs.putBool("configured", true);
    _prefs.putUChar("sync_role", settings.sync_role);
    _prefs.putString("sync_key", settings.sync_key);
    _prefs.putUChar("op_mode", settings.op_mode);
    settings.is_configured = true;
    _prefs.end();

    Serial.println("[Config] Settings saved to NVS successfully");
    return true;
}

void ConfigManager::resetToDefaults() {
    _prefs.begin("botsports", false);
    _prefs.clear();
    _prefs.end();

    settings.wifi_ssid = "";
    settings.wifi_pass = "";
    settings.api_key = "";
    settings.api_url = DEFAULT_API_URL_DEV;
    settings.brightness = DEFAULT_BRIGHTNESS;
    settings.corner = "all";
    settings.is_configured = false;
    settings.sync_role = SYNC_ROLE_MASTER;
    settings.sync_key = ARENA_SYNC_DEFAULT_KEY;
    settings.op_mode = 1;

    Serial.println("[Config] All settings reset to factory defaults");
}
