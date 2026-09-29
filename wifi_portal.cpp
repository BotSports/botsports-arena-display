#include "wifi_portal.h"
#include <ArduinoJson.h>
#include "match_controller.h"
#include "display_manager.h"
#include "arena_sync.h"

WiFiPortal wifiPortal;

WiFiPortal::WiFiPortal() : _server(HTTP_PORT) {
    _state = WIFI_STATE_DISCONNECTED;
    _apSSID = "";
    _connectStartTime = 0;
    _shouldReboot = false;
    _rebootTime = 0;
    _serverStarted = false;
    _apModeRequested = false;
}

void WiFiPortal::begin() {
    uint64_t macRaw = ESP.getEfuseMac();
    uint8_t* macBytes = (uint8_t*)&macRaw;
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02X%02X", macBytes[4], macBytes[5]);
    _apSSID = String(AP_SSID_PREFIX) + suffix;

    if (configMgr.settings.wifi_ssid.length() > 0) {
        Serial.printf("[WiFi] Configured SSID found: %s. Attempting connection...\n", configMgr.settings.wifi_ssid.c_str());
        connectToWiFi();
    } else {
        Serial.println("[WiFi] No WiFi credentials configured. Starting AP mode immediately.");
        startAPMode();
    }
}

bool WiFiPortal::connectToWiFi() {
    _state = WIFI_STATE_CONNECTING;
    _connectStartTime = millis();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("botsports-arena");
    WiFi.begin(configMgr.settings.wifi_ssid.c_str(), configMgr.settings.wifi_pass.c_str());

    Serial.printf("[WiFi] Connecting to '%s' (timeout: %d s)...\n",
                  configMgr.settings.wifi_ssid.c_str(), WIFI_CONNECT_TIMEOUT_SEC);

    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
        if (millis() - _connectStartTime > (WIFI_CONNECT_TIMEOUT_SEC * 1000UL)) {
            Serial.println("\n[WiFi] Connection timed out! Switching to AP mode.");
            startAPMode();
            return false;
        }
    }

    Serial.println("\n[WiFi] Connected successfully!");
    Serial.printf("[WiFi] IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
    _state = WIFI_STATE_CONNECTED;

    // Start mDNS responder so user can navigate to http://botsports-arena.local
    if (MDNS.begin("botsports-arena")) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.println("[mDNS] Responder started at http://botsports-arena.local");
    }

    _startServer();
    return true;
}

void WiFiPortal::startAPMode() {
    _state = WIFI_STATE_AP_MODE;
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_DEFAULT_IP, AP_GATEWAY, AP_SUBNET);
    bool apStarted = WiFi.softAP(_apSSID.c_str());

    if (apStarted) {
        Serial.printf("[WiFi] SoftAP '%s' started. IP: %s\n", _apSSID.c_str(), WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Failed to start SoftAP!");
    }

    // Start Captive Portal DNS Server (redirect all DNS requests to 192.168.4.1)
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(DNS_PORT, "*", AP_DEFAULT_IP);

    if (MDNS.begin("botsports-arena")) {
        MDNS.addService("http", "tcp", HTTP_PORT);
    }

    _startServer();
}

void WiFiPortal::_startServer() {
    if (!_serverStarted) {
        _setupWebServer();
        _server.begin();
        _serverStarted = true;
        Serial.println("[Web] Arena Control Panel & Setup Server started on port 80");
    }
}

void WiFiPortal::stopAPMode() {
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
}

void WiFiPortal::handle() {
    if (_shouldReboot && millis() >= _rebootTime) {
        Serial.println("[System] Rebooting...");
        delay(100);
        ESP.restart();
    }

    if (_apModeRequested) {
        _apModeRequested = false;
        startAPMode();
    }

    if (_state == WIFI_STATE_AP_MODE) {
        _dnsServer.processNextRequest();
    }

    if (_serverStarted) {
        _server.handleClient();
    }

    if (_state == WIFI_STATE_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost WiFi connection! Reconnecting...");
            _state = WIFI_STATE_CONNECTING;
            WiFi.reconnect();
        }
    }
}

String WiFiPortal::getIPAddress() const {
    if (_state == WIFI_STATE_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (_state == WIFI_STATE_AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

int8_t WiFiPortal::getRSSI() const {
    if (_state == WIFI_STATE_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

void WiFiPortal::_setupWebServer() {
    _server.on("/", HTTP_GET, [this]() { _handleRoot(); });

    // Config & System APIs
    _server.on("/api/scan", HTTP_GET, [this]() { _handleScan(); });
    _server.on("/api/save", HTTP_POST, [this]() { _handleSave(); });
    _server.on("/api/reset", HTTP_POST, [this]() { _handleReset(); });
    _server.on("/api/status", HTTP_GET, [this]() { _handleStatus(); });

    // Arena Control Panel APIs
    _server.on("/api/control/status", HTTP_GET, [this]() { _handleControlStatus(); });
    _server.on("/api/control/timer", HTTP_POST, [this]() { _handleControlTimer(); });
    _server.on("/api/control/ko", HTTP_POST, [this]() { _handleControlKO(); });
    _server.on("/api/control/match", HTTP_POST, [this]() { _handleControlMatch(); });
    _server.on("/api/control/mode", HTTP_POST, [this]() { _handleControlMode(); });
    _server.on("/api/control/test", HTTP_POST, [this]() { _handleControlTest(); });
    _server.on("/api/control/brightness", HTTP_POST, [this]() { _handleControlBrightness(); });

    // Captive portal probes (when in SoftAP mode)
    _server.on("/generate_204", HTTP_GET, [this]() { _handleCaptivePortal(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { _handleCaptivePortal(); });
    _server.on("/canonical.html", HTTP_GET, [this]() { _handleCaptivePortal(); });
    _server.on("/ncsi.txt", HTTP_GET, [this]() { _handleCaptivePortal(); });
    _server.on("/connecttest.txt", HTTP_GET, [this]() { _handleCaptivePortal(); });

    _server.onNotFound([this]() {
        if (_state == WIFI_STATE_AP_MODE) {
            _handleCaptivePortal();
        } else {
            _server.send(404, "text/plain", "Not found");
        }
    });
}

void WiFiPortal::_handleRoot() {
    _server.send(200, "text/html", _getHTMLPage());
}

void WiFiPortal::_handleCaptivePortal() {
    _server.sendHeader("Location", String("http://") + getIPAddress() + "/", true);
    _server.send(302, "text/plain", "");
}

void WiFiPortal::_handleScan() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject net = array.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WiFiPortal::_handleSave() {
    String oldSSID = configMgr.settings.wifi_ssid;
    String oldPass = configMgr.settings.wifi_pass;

    if (!_server.hasArg("plain")) {
        configMgr.settings.wifi_ssid = _server.arg("ssid");
        configMgr.settings.wifi_pass = _server.arg("pass");
        configMgr.settings.api_key   = _server.arg("api_key");
        configMgr.settings.api_url   = _server.arg("api_url");
        configMgr.settings.corner    = _server.arg("corner");
        if (_server.hasArg("sync_role")) {
            configMgr.settings.sync_role = (uint8_t)_server.arg("sync_role").toInt();
        }
        if (_server.hasArg("sync_key")) {
            configMgr.settings.sync_key = _server.arg("sync_key");
        }
        if (_server.hasArg("bright")) {
            configMgr.settings.brightness = (uint8_t)_server.arg("bright").toInt();
        }
    } else {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, _server.arg("plain"));
        if (err) {
            _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            return;
        }
        if (doc["ssid"].is<const char*>()) configMgr.settings.wifi_ssid = doc["ssid"].as<String>();
        if (doc["pass"].is<const char*>()) {
            String p = doc["pass"].as<String>();
            if (p.length() > 0) configMgr.settings.wifi_pass = p;
        }
        if (doc["api_key"].is<const char*>()) {
            String k = doc["api_key"].as<String>();
            configMgr.settings.api_key = k;
        }
        if (doc["api_url"].is<const char*>()) configMgr.settings.api_url = doc["api_url"].as<String>();
        if (doc["corner"].is<const char*>()) configMgr.settings.corner = doc["corner"].as<String>();
        if (doc["sync_role"].is<int>()) configMgr.settings.sync_role = (uint8_t)doc["sync_role"].as<int>();
        if (doc["sync_key"].is<const char*>()) configMgr.settings.sync_key = doc["sync_key"].as<String>();
        if (doc["bright"].is<int>()) configMgr.settings.brightness = (uint8_t)doc["bright"].as<int>();
    }

    configMgr.settings.wifi_ssid.trim();
    configMgr.settings.api_key.trim();
    configMgr.settings.api_url.trim();
    configMgr.settings.sync_key.trim();

    if (configMgr.settings.api_url.length() == 0) {
        configMgr.settings.api_url = DEFAULT_API_URL_DEV;
    }
    if (configMgr.settings.sync_key.length() == 0) {
        configMgr.settings.sync_key = ARENA_SYNC_DEFAULT_KEY;
    }
    if (configMgr.settings.brightness < 5) {
        configMgr.settings.brightness = 5;
    }

    displayMgr.setBrightness(configMgr.settings.brightness);

    // Validate API key if configured
    bool keyValid = false;
    String validationNote = "";
    if (configMgr.settings.api_key.length() > 0) {
        if (isConnected()) {
            String err;
            keyValid = apiClient.validateApiKey(configMgr.settings.api_key, configMgr.settings.api_url, err);
            if (keyValid) {
                validationNote = "Key verified for " + apiClient.getData().arena_name + "!";
            } else {
                validationNote = "Warning: Key validation failed (" + err + ").";
                if (matchCtrl.getMode() == OPMODE_API) {
                    matchCtrl.setMode(OPMODE_STANDALONE);
                    configMgr.settings.op_mode = (uint8_t)OPMODE_STANDALONE;
                }
            }
        } else {
            validationNote = "Key saved. Will verify once connected to Wi-Fi.";
        }
    } else {
        apiClient.setKeyValid(false);
        if (matchCtrl.getMode() == OPMODE_API) {
            matchCtrl.setMode(OPMODE_STANDALONE);
            configMgr.settings.op_mode = (uint8_t)OPMODE_STANDALONE;
        }
    }

    bool saved = configMgr.saveSettings();
    if (!saved) {
        _server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save settings to flash memory.\"}");
        return;
    }

    bool wifiChanged = (configMgr.settings.wifi_ssid != oldSSID || configMgr.settings.wifi_pass != oldPass);
    if (wifiChanged && configMgr.settings.wifi_ssid.length() > 0) {
        _server.send(200, "application/json", "{\"status\":\"ok\",\"key_valid\":" + String(keyValid ? "true" : "false") + ",\"message\":\"Wi-Fi changed. Rebooting in 2s...\"}");
        _shouldReboot = true;
        _rebootTime = millis() + 2000;
    } else {
        _server.send(200, "application/json", "{\"status\":\"ok\",\"key_valid\":" + String(keyValid ? "true" : "false") + ",\"message\":\"Settings saved! " + validationNote + "\"}");
    }
}

void WiFiPortal::_handleReset() {
    configMgr.resetToDefaults();
    _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Reset complete. Rebooting...\"}");
    _shouldReboot = true;
    _rebootTime = millis() + 1500;
}

void WiFiPortal::_handleStatus() {
    JsonDocument doc;
    doc["version"] = FIRMWARE_VERSION;
    doc["ap_ssid"] = _apSSID;
    doc["ip"] = getIPAddress();
    doc["is_ap"] = isAPMode();
    doc["wifi_connected"] = isConnected();
    doc["configured_ssid"] = configMgr.settings.wifi_ssid;
    doc["api_url"] = configMgr.settings.api_url;
    doc["api_key"] = configMgr.settings.api_key;
    doc["has_api_key"] = (configMgr.settings.api_key.length() > 0);
    doc["api_key_valid"] = apiClient.isKeyValid();
    doc["stream_connected"] = apiClient.isStreamConnected();
    doc["key_validation_error"] = apiClient.getKeyValidationError();
    doc["corner"] = configMgr.settings.corner;
    doc["brightness"] = configMgr.settings.brightness;
    doc["mode"] = matchCtrl.getModeString();
    doc["sync_role"] = configMgr.settings.sync_role;
    doc["sync_key"] = configMgr.settings.sync_key;
    doc["sync_status"] = arenaSync.getSyncStatusText();
    doc["slave_count"] = arenaSync.getSlaveCount();

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WiFiPortal::_handleControlStatus() {
    JsonDocument doc;
    doc["mode"] = matchCtrl.getModeString();

    ClockState cState = matchCtrl.getCurrentClockState();
    String clockStateStr = "stopped";
    if (cState == CLOCK_RUNNING) clockStateStr = "running";
    else if (cState == CLOCK_PAUSED) clockStateStr = "paused";
    else if (cState == CLOCK_EXPIRED) clockStateStr = "expired";
    doc["clock_state"] = clockStateStr;

    doc["remaining_seconds"] = matchCtrl.getCurrentRemainingSeconds();
    doc["duration_seconds"] = matchCtrl.getDuration();
    doc["match_number"] = matchCtrl.getCurrentMatchNumber();
    doc["round_number"] = matchCtrl.getCurrentRoundNumber();
    doc["red_name"] = matchCtrl.getCurrentRedName();
    doc["blue_name"] = matchCtrl.getCurrentBlueName();

    doc["is_ko"] = matchCtrl.isKO();
    doc["ko_winner"] = matchCtrl.getKOWinner();
    doc["ko_winner_name"] = matchCtrl.getKOWinnerName();
    doc["ko_time"] = matchCtrl.getKOTime();

    doc["active_test"] = matchCtrl.getActiveTestName();
    doc["brightness"] = configMgr.settings.brightness;
    doc["has_api_match"] = apiClient.getData().has_current_match;
    doc["api_key_valid"] = apiClient.isKeyValid();
    doc["stream_connected"] = apiClient.isStreamConnected();
    doc["key_validation_error"] = apiClient.getKeyValidationError();
    doc["clock_pause_reason"] = apiClient.getData().clock_pause_reason;

    doc["sync_role"] = configMgr.settings.sync_role;
    doc["sync_key"] = configMgr.settings.sync_key;
    doc["sync_status"] = arenaSync.getSyncStatusText();
    doc["slave_count"] = arenaSync.getSlaveCount();

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void WiFiPortal::_handleControlTimer() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    String action = doc["action"] | "";
    if (action.equalsIgnoreCase("start") || action.equalsIgnoreCase("resume")) {
        matchCtrl.startTimer();
    } else if (action.equalsIgnoreCase("pause")) {
        matchCtrl.pauseTimer();
    } else if (action.equalsIgnoreCase("reset")) {
        matchCtrl.resetTimer();
    } else if (action.equalsIgnoreCase("set_duration")) {
        float secs = doc["seconds"] | 180.0f;
        matchCtrl.setDuration(secs);
    }

    arenaSync.broadcastState(true); // Immediate sync to slave displays
    _handleControlStatus();
}

void WiFiPortal::_handleControlKO() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    String corner = doc["corner"] | "";
    if (corner.equalsIgnoreCase("red") || corner.equalsIgnoreCase("blue")) {
        matchCtrl.triggerKO(corner);
    } else if (corner.equalsIgnoreCase("clear")) {
        matchCtrl.clearKO();
        displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    }

    arenaSync.broadcastState(true); // Immediate sync to slave displays
    _handleControlStatus();
}

void WiFiPortal::_handleControlMatch() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    int mNum = doc["match_number"] | matchCtrl.getMatchNumber();
    int rNum = doc["round_number"] | matchCtrl.getRoundNumber();
    String red = doc["red_name"] | matchCtrl.getRedName();
    String blue = doc["blue_name"] | matchCtrl.getBlueName();

    matchCtrl.setMatchDetails(mNum, rNum, red, blue);
    arenaSync.broadcastState(true);
    _handleControlStatus();
}

void WiFiPortal::_handleControlMode() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    String mode = doc["mode"] | "";
    if (mode.equalsIgnoreCase("standalone")) {
        matchCtrl.setMode(OPMODE_STANDALONE);
        apiClient.disconnectStream();
        arenaSync.broadcastState(true);
        _handleControlStatus();
    } else if (mode.equalsIgnoreCase("api")) {
        if (!apiClient.isKeyValid()) {
            String err;
            if (configMgr.settings.api_key.length() > 0 && isConnected()) {
                if (!apiClient.validateApiKey(configMgr.settings.api_key, configMgr.settings.api_url, err)) {
                    _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot switch to BotSports Sync: " + err + "\"}");
                    return;
                }
            } else {
                _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot switch to BotSports Sync: No valid Arena API Key configured\"}");
                return;
            }
        }
        if (!matchCtrl.setMode(OPMODE_API)) {
            _server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot switch to BotSports Sync: Invalid API Key\"}");
            return;
        }
        arenaSync.broadcastState(true);
        _handleControlStatus();
    }
}

void WiFiPortal::_handleControlTest() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    String testName = doc["test"] | "";
    matchCtrl.triggerScreenTest(testName);

    _handleControlStatus();
}

void WiFiPortal::_handleControlBrightness() {
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    int val = doc["brightness"] | configMgr.settings.brightness;
    if (val < 5) val = 5;
    if (val > 255) val = 255;

    configMgr.settings.brightness = (uint8_t)val;
    displayMgr.setBrightness(configMgr.settings.brightness);

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

String WiFiPortal::_getHTMLPage() {
    return R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BotSports Arena Control Panel</title>
<style>
  :root {
    --bg: #0b0d13;
    --card: #151824;
    --border: #262c3f;
    --gold: #f59e0b;
    --gold-dark: #d97706;
    --red: #ef4444;
    --red-dark: #dc2626;
    --blue: #3b82f6;
    --blue-dark: #2563eb;
    --green: #10b981;
    --green-dark: #059669;
    --text: #f9fafb;
    --dim: #9ca3af;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { background: var(--bg); color: var(--text); padding: 14px; display: flex; justify-content: center; }
  .container { max-width: 480px; width: 100%; }

  /* Top Navigation / Tabs */
  .nav { display: flex; gap: 6px; background: #0f121d; padding: 4px; border-radius: 10px; margin-bottom: 16px; border: 1px solid var(--border); }
  .nav-btn { flex: 1; padding: 10px 4px; background: transparent; border: none; border-radius: 8px; color: var(--dim); font-size: 13px; font-weight: 700; cursor: pointer; transition: all 0.2s; }
  .nav-btn.active { background: var(--card); color: var(--gold); box-shadow: 0 2px 6px rgba(0,0,0,0.4); }

  /* Device Status Bar */
  .status-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; font-size: 12px; color: var(--dim); }
  .pill { display: inline-flex; align-items: center; gap: 6px; padding: 3px 9px; border-radius: 9999px; font-weight: 700; font-size: 11px; }
  .pill.online { background: #064e3b; color: #6ee7b7; border: 1px solid #059669; }
  .pill.ap { background: #78350f; color: #fde68a; border: 1px solid #d97706; }

  /* Card Styles */
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 18px; margin-bottom: 14px; box-shadow: 0 4px 12px rgba(0,0,0,0.3); }
  .card-title { font-size: 14px; font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em; color: var(--gold); margin-bottom: 12px; display: flex; justify-content: space-between; align-items: center; }

  /* Mode Switcher Pill */
  .mode-switch { display: flex; background: #0c0e17; border-radius: 8px; padding: 3px; border: 1px solid var(--border); margin-bottom: 14px; }
  .mode-btn { flex: 1; padding: 8px 4px; font-size: 12px; font-weight: 700; border: none; border-radius: 6px; background: transparent; color: var(--dim); cursor: pointer; }
  .mode-btn.active { background: var(--gold); color: #111; }

  /* Giant Match Timer */
  .clock-display {
    background: #090a0f; border: 2px solid var(--border); border-radius: 12px; padding: 18px 10px; text-align: center; margin-bottom: 14px;
  }
  .clock-time { font-size: 54px; font-weight: 900; letter-spacing: 2px; font-variant-numeric: tabular-nums; line-height: 1; text-shadow: 0 0 16px rgba(0,0,0,0.8); }
  .clock-status { font-size: 12px; font-weight: 800; text-transform: uppercase; letter-spacing: 0.1em; margin-top: 6px; }

  .clock-green { color: #10b981; }
  .clock-orange { color: #f59e0b; }
  .clock-red { color: #ef4444; }
  .clock-paused { color: #eab308; }
  .clock-expired { color: #dc2626; animation: blink 1s infinite; }
  @keyframes blink { 50% { opacity: 0.3; } }

  /* Button Grid */
  .btn-row { display: flex; gap: 8px; margin-bottom: 10px; }
  button {
    flex: 1; padding: 12px 10px; font-weight: 800; font-size: 14px; border: none; border-radius: 8px; cursor: pointer; transition: transform 0.1s, opacity 0.2s;
  }
  button:active { transform: scale(0.97); }
  .btn-green { background: var(--green); color: #fff; }
  .btn-green:hover { background: var(--green-dark); }
  .btn-yellow { background: var(--gold); color: #111; }
  .btn-yellow:hover { background: var(--gold-dark); }
  .btn-gray { background: #2b3247; color: var(--text); }
  .btn-gray:hover { background: #38425d; }
  .btn-red { background: var(--red); color: #fff; }
  .btn-red:hover { background: var(--red-dark); }
  .btn-blue { background: var(--blue); color: #fff; }
  .btn-blue:hover { background: var(--blue-dark); }

  /* KO Section */
  .ko-panel { background: #1e1115; border: 1px solid #4a1d24; border-radius: 10px; padding: 14px; margin-top: 14px; text-align: center; }
  .ko-title { font-size: 13px; font-weight: 800; color: #f87171; letter-spacing: 0.05em; margin-bottom: 10px; }

  /* Match Info Banner */
  .match-banner { display: flex; justify-content: space-between; align-items: center; background: #0c0e17; padding: 10px 14px; border-radius: 8px; border: 1px solid var(--border); margin-bottom: 14px; }
  .corner-tag { font-weight: 800; font-size: 14px; }
  .corner-red { color: #f87171; }
  .corner-blue { color: #60a5fa; }

  /* Form Elements */
  label { display: block; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; color: var(--dim); margin-bottom: 5px; }
  input[type="text"], input[type="password"], select {
    width: 100%; padding: 10px 12px; background: #0c0e17; border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: 14px; outline: none; margin-bottom: 12px;
  }
  input:focus, select:focus { border-color: var(--gold); }
  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }

  /* Toast notification */
  #toast { display: none; padding: 10px 14px; border-radius: 8px; font-size: 13px; font-weight: 600; margin-bottom: 14px; text-align: center; }
  #toast.success { background: #064e3b; color: #a7f3d0; border: 1px solid #059669; }
  #toast.error { background: #7f1d1d; color: #fecaca; border: 1px solid #dc2626; }
</style>
</head>
<body>
<div class="container">
  <!-- Status Header -->
  <div class="status-bar">
    <div>
      <strong id="device_name" style="color:var(--text);">BotSports Display</strong>
      <span id="mdns_link" style="margin-left:6px;"><a href="http://botsports-arena.local" style="color:var(--gold);text-decoration:none;">.local</a></span>
    </div>
    <span class="pill" id="conn_pill">Connecting...</span>
  </div>

  <!-- Tab Selector -->
  <div class="nav">
    <button type="button" class="nav-btn active" onclick="switchTab('tab_control')">&#9889; Control</button>
    <button type="button" class="nav-btn" onclick="switchTab('tab_tests')">&#129514; Screen FX</button>
    <button type="button" class="nav-btn" onclick="switchTab('tab_settings')">&#9881; Config</button>
  </div>

  <div id="toast"></div>

  <!-- ================= TAB 1: ARENA MATCH CONTROL ================= -->
  <div id="tab_control">
    <!-- Mode Switcher -->
    <div class="mode-switch">
      <button type="button" class="mode-btn" id="mode_btn_api" onclick="setMode('api')">BotSports API Sync</button>
      <button type="button" class="mode-btn" id="mode_btn_standalone" onclick="setMode('standalone')">Standalone Arena</button>
    </div>
    <div style="display:flex;gap:6px;margin-bottom:12px;font-size:11px;align-items:center;">
      <span id="key_badge" class="pill ap" style="cursor:pointer;" onclick="switchTab('tab_settings')">Key: Checking...</span>
      <span id="stream_badge" class="pill" style="display:none;">SSE: Off</span>
    </div>

    <!-- Match Banner -->
    <div class="match-banner">
      <div>
        <div class="corner-tag corner-red" id="hud_red_name">Red: Bot 1</div>
        <div style="font-size:11px;color:var(--dim);" id="hud_red_status">Red Corner</div>
      </div>
      <div style="text-align:center;">
        <strong style="color:var(--gold);font-size:13px;" id="hud_match_round">R1 M1</strong>
      </div>
      <div style="text-align:right;">
        <div class="corner-tag corner-blue" id="hud_blue_name">Blue: Bot 2</div>
        <div style="font-size:11px;color:var(--dim);" id="hud_blue_status">Blue Corner</div>
      </div>
    </div>

    <!-- Big Match Clock -->
    <div class="clock-display">
      <div class="clock-time clock-green" id="clock_digits">3:00</div>
      <div class="clock-status clock-green" id="clock_status_text">STOPPED</div>
    </div>

    <!-- Timer Control Buttons -->
    <div class="btn-row">
      <button type="button" class="btn-green" id="btn_timer_start" onclick="sendTimer('start')">&#9654; START</button>
      <button type="button" class="btn-yellow" id="btn_timer_pause" onclick="sendTimer('pause')">&#10074;&#10074; PAUSE</button>
      <button type="button" class="btn-gray" onclick="sendTimer('reset')">&#8635; RESET</button>
    </div>

    <!-- Quick Duration Buttons -->
    <div class="btn-row" style="margin-top:6px;">
      <button type="button" class="btn-gray" style="padding:7px;font-size:12px;" onclick="setDuration(60)">1:00</button>
      <button type="button" class="btn-gray" style="padding:7px;font-size:12px;" onclick="setDuration(120)">2:00</button>
      <button type="button" class="btn-gray" style="padding:7px;font-size:12px;" onclick="setDuration(180)">3:00 (Standard)</button>
    </div>

    <!-- Knockout (KO) Action Section -->
    <div class="ko-panel">
      <div class="ko-title">&#129354; KNOCKOUT (KO) VICTORY</div>
      <div class="btn-row">
        <button type="button" class="btn-red" onclick="sendKO('red')">&#129354; RED WINS (KO)</button>
        <button type="button" class="btn-blue" onclick="sendKO('blue')">&#129354; BLUE WINS (KO)</button>
      </div>
      <button type="button" class="btn-gray" style="width:100%;margin-top:4px;padding:8px;font-size:12px;" onclick="sendKO('clear')">Clear KO Screen</button>
    </div>

    <!-- Standalone Match Details Editor -->
    <div class="card" style="margin-top:14px;" id="standalone_card">
      <div class="card-title">
        <span>Edit Match Details</span>
        <span style="font-size:11px;color:var(--dim);">Local Arena Mode</span>
      </div>
      <div class="grid-2">
        <div>
          <label>Match #</label>
          <input type="text" id="input_match_num" value="1">
        </div>
        <div>
          <label>Round #</label>
          <input type="text" id="input_round_num" value="1">
        </div>
      </div>
      <div class="grid-2">
        <div>
          <label style="color:#f87171;">Red Bot Name</label>
          <input type="text" id="input_red_name" value="Tombstone">
        </div>
        <div>
          <label style="color:#60a5fa;">Blue Bot Name</label>
          <input type="text" id="input_blue_name" value="Minotaur">
        </div>
      </div>
      <button type="button" class="btn-yellow" onclick="saveMatchDetails()">Update Match Info</button>
    </div>
  </div>

  <!-- ================= TAB 2: SCREEN FX & ANIMATION TESTS ================= -->
  <div id="tab_tests" style="display:none;">
    <div class="card">
      <div class="card-title">Display FX &amp; Screen Test Suite</div>
      <p style="font-size:12px;color:var(--dim);margin-bottom:14px;">Trigger real-time animations on the WaveShare 80x40 matrix to verify rendering and visuals.</p>

      <div class="btn-row">
        <button type="button" class="btn-red" onclick="sendTest('ko_red')">&#129354; Test Red KO FX</button>
        <button type="button" class="btn-blue" onclick="sendTest('ko_blue')">&#129354; Test Blue KO FX</button>
      </div>
      <div class="btn-row">
        <button type="button" class="btn-yellow" onclick="sendTest('expired')">&#9200; Test TIME! Flash</button>
        <button type="button" class="btn-gray" onclick="sendTest('upcoming')">&#9197; Next Match Screen</button>
      </div>
      <div class="btn-row">
        <button type="button" class="btn-gray" onclick="sendTest('match')">&#9876; Active Match HUD</button>
        <button type="button" class="btn-gray" onclick="sendTest('idle')">&#9749; Standby Screen</button>
      </div>
      <div class="btn-row">
        <button type="button" class="btn-gray" onclick="sendTest('boot')">&#128640; Boot Screen</button>
        <button type="button" class="btn-gray" onclick="sendTest('ap')">&#128246; AP Setup Screen</button>
      </div>
      <div class="btn-row">
        <button type="button" class="btn-gray" onclick="sendTest('error')">&#9888; Error Screen</button>
        <button type="button" class="btn-green" onclick="sendTest('resume')">&#10004; Resume Normal</button>
      </div>
    </div>

    <!-- Live Brightness Slider -->
    <div class="card">
      <div class="card-title">Live LED Brightness</div>
      <div style="display:flex;align-items:center;gap:12px;">
        <input type="range" id="live_bright" min="10" max="255" value="90" style="flex:1;accent-color:var(--gold);" oninput="setLiveBrightness(this.value)">
        <span id="live_bright_val" style="font-weight:700;font-size:14px;color:var(--gold);min-width:30px;">90</span>
      </div>
    </div>
  </div>

  <!-- ================= TAB 3: WI-FI & DEVICE CONFIG ================= -->
  <div id="tab_settings" style="display:none;">
    <div class="card">
      <div class="card-title">Wi-Fi Setup</div>
      <label>Detected Networks</label>
      <div class="btn-row">
        <select id="wifi_select" onchange="onSelectNetwork()" style="margin-bottom:0;">
          <option value="">-- Choose detected network --</option>
        </select>
        <button type="button" class="btn-gray" style="flex:none;width:auto;padding:10px 14px;" onclick="scanWifi()">Scan</button>
      </div>
      <div style="margin-top:10px;">
        <label>SSID</label>
        <input type="text" id="wifi_ssid" placeholder="Network name">
      </div>
      <div>
        <label>Password</label>
        <input type="password" id="wifi_pass" placeholder="Network password">
      </div>
    </div>

    <div class="card">
      <div class="card-title">BotSports Arena Config</div>
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;">
        <label style="margin-bottom:0;">Arena API Key</label>
        <span id="cfg_key_badge" class="pill ap" style="font-size:10px;padding:2px 7px;">Checking...</span>
      </div>
      <input type="text" id="api_key" placeholder="bs_arena_...">
      <div id="cfg_key_msg" style="font-size:11px;color:var(--dim);margin-top:-8px;margin-bottom:12px;"></div>

      <label>Server Environment</label>
      <select id="api_env" onchange="onEnvChange()">
        <option value="https://api-dev.botsports.org">Development (api-dev.botsports.org)</option>
        <option value="https://api.botsports.org">Production (api.botsports.org)</option>
        <option value="custom">Custom Server URL</option>
      </select>

      <div id="custom_url_field" style="display:none;">
        <label>Custom API URL</label>
        <input type="text" id="api_url" placeholder="https://...">
      </div>

      <label>Display Target</label>
      <select id="corner_select">
        <option value="all">Main Arena Display (Both Corners)</option>
        <option value="red">Red Corner Unit</option>
        <option value="blue">Blue Corner Unit</option>
      </select>
    </div>

    <div class="card">
      <div class="card-title">Wireless Multi-Display Sync</div>
      <label>Sync Role</label>
      <select id="sync_role_select">
        <option value="1">Master Display (Fetches API, Broadcasts Sync)</option>
        <option value="2">Slave Display (Wireless Listener, Mirrors Master)</option>
        <option value="0">Solo Display (Sync Link Disabled)</option>
      </select>

      <label>Arena Sync Key</label>
      <input type="text" id="sync_key" value="arena_1" placeholder="e.g. arena_1">
      <div style="font-size:11px;color:var(--dim);margin-top:-8px;margin-bottom:10px;">
        Both displays on this arena must use the same key. The Master talks to BotSports; the Slave wireless mirrors the match clock and state in dead unison.
      </div>

      <div style="background:#090a10;border:1px solid var(--border);border-radius:8px;padding:8px 12px;font-size:12px;color:var(--gold);" id="sync_status_box">
        Sync Status: Checking...
      </div>
    </div>

    <button type="button" class="btn-yellow" id="btn_save_config" onclick="saveSettings()">Save &amp; Reboot</button>
    <button type="button" class="btn-gray" style="margin-top:10px;" onclick="resetDefaults()">Reset to Defaults</button>
  </div>
</div>

<script>
let currentTab = 'tab_control';

function switchTab(tabId) {
  document.getElementById('tab_control').style.display = (tabId === 'tab_control') ? 'block' : 'none';
  document.getElementById('tab_tests').style.display = (tabId === 'tab_tests') ? 'block' : 'none';
  document.getElementById('tab_settings').style.display = (tabId === 'tab_settings') ? 'block' : 'none';

  const btns = document.querySelectorAll('.nav-btn');
  btns[0].className = 'nav-btn' + (tabId === 'tab_control' ? ' active' : '');
  btns[1].className = 'nav-btn' + (tabId === 'tab_tests' ? ' active' : '');
  btns[2].className = 'nav-btn' + (tabId === 'tab_settings' ? ' active' : '');
  currentTab = tabId;
}

window.onload = function() {
  fetchInitialStatus();
  setInterval(pollControlStatus, 600); // 600ms polling for smooth web clock sync
};

function fetchInitialStatus() {
  fetch('/api/status').then(r => r.json()).then(data => {
    if (data.configured_ssid) document.getElementById('wifi_ssid').value = data.configured_ssid;
    if (data.api_key) document.getElementById('api_key').value = data.api_key;
    if (data.api_url) {
      if (data.api_url === 'https://api-dev.botsports.org' || data.api_url === 'https://api.botsports.org') {
        document.getElementById('api_env').value = data.api_url;
      } else {
        document.getElementById('api_env').value = 'custom';
        document.getElementById('custom_url_field').style.display = 'block';
        document.getElementById('api_url').value = data.api_url;
      }
    }
    const cfgKeyBadge = document.getElementById('cfg_key_badge');
    const cfgKeyMsg = document.getElementById('cfg_key_msg');
    if (cfgKeyBadge && cfgKeyMsg) {
      if (data.api_key_valid) {
        cfgKeyBadge.className = 'pill online';
        cfgKeyBadge.innerText = 'Verified ✓';
        cfgKeyMsg.innerText = 'Valid API key verified by server.';
      } else if (data.has_api_key) {
        cfgKeyBadge.className = 'pill ap';
        cfgKeyBadge.innerText = 'Invalid ✗';
        cfgKeyMsg.innerText = data.key_validation_error ? ('Error: ' + data.key_validation_error) : 'Key could not be verified.';
      } else {
        cfgKeyBadge.className = 'pill ap';
        cfgKeyBadge.innerText = 'Not Set';
        cfgKeyMsg.innerText = 'API Key required for BotSports API Sync mode.';
      }
    }
    if (data.corner) document.getElementById('corner_select').value = data.corner;
    if (data.sync_role !== undefined) document.getElementById('sync_role_select').value = data.sync_role;
    if (data.sync_key) document.getElementById('sync_key').value = data.sync_key;
    if (data.sync_status) document.getElementById('sync_status_box').innerText = 'Sync Status: ' + data.sync_status;
    if (data.brightness) {
      document.getElementById('live_bright').value = data.brightness;
      document.getElementById('live_bright_val').innerText = data.brightness;
    }

    const pill = document.getElementById('conn_pill');
    if (data.wifi_connected) {
      pill.className = 'pill online';
      pill.innerText = 'Connected: ' + data.ip;
    } else {
      pill.className = 'pill ap';
      pill.innerText = 'SoftAP: ' + data.ap_ssid;
    }
  }).catch(() => {});
}

function pollControlStatus() {
  fetch('/api/control/status').then(r => r.json()).then(data => {
    // Mode UI
    const isStandalone = (data.mode === 'standalone');
    document.getElementById('mode_btn_api').className = 'mode-btn' + (!isStandalone ? ' active' : '');
    document.getElementById('mode_btn_standalone').className = 'mode-btn' + (isStandalone ? ' active' : '');
    document.getElementById('standalone_card').style.display = isStandalone ? 'block' : 'none';

    // Key & Stream Badges
    const keyBadge = document.getElementById('key_badge');
    const streamBadge = document.getElementById('stream_badge');
    if (keyBadge) {
      if (data.api_key_valid) {
        keyBadge.className = 'pill online';
        keyBadge.innerText = 'Key: Valid ✓';
      } else {
        keyBadge.className = 'pill ap';
        keyBadge.innerText = 'Key: Invalid / Unset ✗';
      }
    }
    if (streamBadge) {
      if (!isStandalone) {
        streamBadge.style.display = 'inline-flex';
        if (data.stream_connected) {
          streamBadge.className = 'pill online';
          streamBadge.innerText = 'SSE Stream: Live ●';
        } else {
          streamBadge.className = 'pill ap';
          streamBadge.innerText = 'SSE: Connecting...';
        }
      } else {
        streamBadge.style.display = 'none';
      }
    }

    // Match banner
    document.getElementById('hud_red_name').innerText = data.red_name;
    document.getElementById('hud_blue_name').innerText = data.blue_name;
    document.getElementById('hud_match_round').innerText = `R${data.round_number} M${data.match_number}`;

    // Sync status update
    if (data.sync_status) {
      const syncBox = document.getElementById('sync_status_box');
      if (syncBox) syncBox.innerText = 'Sync Status: ' + data.sync_status;
    }

    // Clock
    const rem = Math.max(0, data.remaining_seconds);
    const m = Math.floor(rem / 60);
    const s = Math.floor(rem % 60);
    const timeStr = `${m}:${s < 10 ? '0' : ''}${s}`;
    document.getElementById('clock_digits').innerText = timeStr;

    // Clock Color & Status Text
    const clockEl = document.getElementById('clock_digits');
    const statusEl = document.getElementById('clock_status_text');
    clockEl.className = 'clock-time';
    statusEl.className = 'clock-status';

    if (data.is_ko) {
      clockEl.innerText = 'K.O.!';
      clockEl.classList.add(data.ko_winner === 'red' ? 'clock-red' : 'clock-blue');
      statusEl.innerText = `${data.ko_winner.toUpperCase()} WINS BY KNOCKOUT`;
      statusEl.classList.add(data.ko_winner === 'red' ? 'clock-red' : 'clock-blue');
    } else if (data.clock_state === 'running') {
      statusEl.innerText = 'CLOCK RUNNING';
      if (rem <= 10) { clockEl.classList.add('clock-red'); statusEl.classList.add('clock-red'); }
      else if (rem <= 30) { clockEl.classList.add('clock-orange'); statusEl.classList.add('clock-orange'); }
      else { clockEl.classList.add('clock-green'); statusEl.classList.add('clock-green'); }
    } else if (data.clock_state === 'paused') {
      const reason = (data.clock_pause_reason && data.clock_pause_reason.length > 0)
        ? ('PAUSED: ' + data.clock_pause_reason.replace(/_/g, ' ').toUpperCase())
        : 'CLOCK PAUSED';
      statusEl.innerText = reason;
      clockEl.classList.add('clock-paused');
      statusEl.classList.add('clock-paused');
    } else if (data.clock_state === 'expired') {
      statusEl.innerText = 'TIME EXPIRED!';
      clockEl.classList.add('clock-expired');
      statusEl.classList.add('clock-expired');
    } else {
      statusEl.innerText = 'STOPPED';
      clockEl.classList.add('clock-green');
      statusEl.classList.add('clock-green');
    }
  }).catch(() => {});
}

function sendTimer(action) {
  fetch('/api/control/timer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: action })
  }).then(r => r.json()).then(() => pollControlStatus());
}

function setDuration(secs) {
  fetch('/api/control/timer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'set_duration', seconds: secs })
  }).then(r => r.json()).then(() => pollControlStatus());
}

function sendKO(corner) {
  fetch('/api/control/ko', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ corner: corner })
  }).then(r => r.json()).then(() => pollControlStatus());
}

function setMode(mode) {
  fetch('/api/control/mode', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mode: mode })
  }).then(async r => {
    const data = await r.json();
    if (!r.ok) {
      showToast(data.message || 'Failed to switch mode', true);
    } else {
      showToast('Switched to ' + (mode === 'api' ? 'BotSports API Sync' : 'Standalone Arena'), false);
    }
    pollControlStatus();
  }).catch(e => {
    showToast('Failed to change mode', true);
  });
}

function saveMatchDetails() {
  const payload = {
    match_number: parseInt(document.getElementById('input_match_num').value),
    round_number: parseInt(document.getElementById('input_round_num').value),
    red_name: document.getElementById('input_red_name').value,
    blue_name: document.getElementById('input_blue_name').value
  };
  fetch('/api/control/match', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  }).then(r => r.json()).then(() => {
    showToast('Match details updated!', false);
    pollControlStatus();
  });
}

function sendTest(testName) {
  fetch('/api/control/test', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ test: testName })
  }).then(r => r.json()).then(() => {
    showToast('Display FX triggered: ' + testName, false);
    pollControlStatus();
  });
}

function setLiveBrightness(val) {
  document.getElementById('live_bright_val').innerText = val;
  fetch('/api/control/brightness', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ brightness: parseInt(val) })
  });
}

function onEnvChange() {
  const env = document.getElementById('api_env').value;
  document.getElementById('custom_url_field').style.display = (env === 'custom') ? 'block' : 'none';
}

function onSelectNetwork() {
  const sel = document.getElementById('wifi_select').value;
  if (sel) document.getElementById('wifi_ssid').value = sel;
}

function scanWifi() {
  fetch('/api/scan')
    .then(r => r.json())
    .then(nets => {
      const sel = document.getElementById('wifi_select');
      sel.innerHTML = '<option value="">-- Choose detected network --</option>';
      nets.forEach(n => {
        const opt = document.createElement('option');
        opt.value = n.ssid;
        opt.innerText = `${n.ssid} (${n.rssi} dBm)${n.secure ? ' 🔒' : ''}`;
        sel.appendChild(opt);
      });
    });
}

function showToast(msg, isError) {
  const t = document.getElementById('toast');
  t.className = isError ? 'error' : 'success';
  t.innerText = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 3000);
}

function saveSettings() {
  const btn = document.getElementById('btn_save_config');
  btn.innerText = 'Saving...';
  btn.disabled = true;

  const env = document.getElementById('api_env').value;
  const apiUrl = (env === 'custom') ? document.getElementById('api_url').value : env;

  const payload = {
    ssid: document.getElementById('wifi_ssid').value,
    pass: document.getElementById('wifi_pass').value,
    api_key: document.getElementById('api_key').value,
    api_url: apiUrl,
    corner: document.getElementById('corner_select').value,
    sync_role: parseInt(document.getElementById('sync_role_select').value),
    sync_key: document.getElementById('sync_key').value,
    bright: parseInt(document.getElementById('live_bright').value)
  };

  fetch('/api/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  })
  .then(r => r.json())
  .then(res => {
    if (res.status === 'ok') {
      showToast(res.message, false);
      if (res.message && res.message.includes('Rebooting')) {
        btn.innerText = 'Rebooting...';
      } else {
        btn.innerText = 'Saved!';
        setTimeout(() => { btn.innerText = 'Save & Reboot'; btn.disabled = false; }, 2000);
      }
      fetchInitialStatus();
    } else {
      showToast(res.message || 'Save error', true);
      btn.innerText = 'Save & Reboot';
      btn.disabled = false;
    }
  }).catch(() => {
    showToast('Network error saving settings', true);
    btn.innerText = 'Save & Reboot';
    btn.disabled = false;
  });
}

function resetDefaults() {
  if (confirm('Reset all settings to factory defaults?')) {
    fetch('/api/reset', { method: 'POST' }).then(() => {
      showToast('Resetting and rebooting...', false);
    });
  }
}
</script>
</body>
</html>
)rawhtml";
}
