#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "wifi_portal.h"
#include "api_client.h"
#include "display_manager.h"
#include "button_manager.h"
#include "match_controller.h"
#include "arena_sync.h"

static unsigned long lastApiPollTime = 0;
static unsigned long screenCycleTime = 0;
static bool showingUpcomingCycle = false;

// ============================================================================
// Core 0 Task: Network Handling (Captive Portal, Web Server, Multi-Display Sync)
// Priority 2 ensures sub-millisecond HTTP / DNS / UDP response times!
// ============================================================================
void networkTask(void *param) {
    Serial.printf("[Tasks] Network task started on Core %d\n", xPortGetCoreID());
    for (;;) {
        wifiPortal.handle();
        arenaSync.update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ============================================================================
// Core 0 Task: Background BotSports API Polling
// ============================================================================
// Core 0 Task: Background BotSports SSE Real-Time Stream & Event Dispatch
// Runs on Core 0 so TLS stream socket and event dispatch never block the display!
// ============================================================================
void apiTask(void *param) {
    Serial.printf("[Tasks] Real-time SSE / API task started on Core %d\n", xPortGetCoreID());
    for (;;) {
        if (wifiPortal.isOTAInProgress()) {
            if (apiClient.isStreamConnected()) {
                apiClient.disconnectStream();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (matchCtrl.getMode() == OPMODE_API && wifiPortal.isConnected() && !arenaSync.isSlave()) {
            if (apiClient.isKeyValid()) {
                apiClient.handleStream();
            } else {
                // Key is invalid - cannot run API mode!
                Serial.println("[Tasks] API Key not valid! Reverting to Standalone Arena Mode.");
                matchCtrl.setMode(OPMODE_STANDALONE);
                apiClient.disconnectStream();
            }
        } else {
            if (apiClient.isStreamConnected()) {
                apiClient.disconnectStream();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n==================================================");
    Serial.println("   BotSports Arena RGB Matrix Display Controller   ");
    Serial.println("   Version: " FIRMWARE_VERSION);
    Serial.println("==================================================");

    // 1. Initialize NVS Configuration
    configMgr.begin();
    configMgr.loadSettings();

    // 2. Initialize Match Controller
    matchCtrl.begin();

    // 3. Initialize Display
    displayMgr.begin();
    displayMgr.showBoot();
    delay(800);

    // 4. Initialize Onboard Button
    buttonMgr.begin();

    // 5. Initialize WiFi / Captive Portal & Web Control Server
    if (configMgr.settings.wifi_ssid.length() > 0) {
        displayMgr.showConnecting(configMgr.settings.wifi_ssid, 30);
    }
    wifiPortal.begin();

    // 6. Initialize Wireless Multi-Display Sync Link
    arenaSync.begin();

    if (wifiPortal.isAPMode()) {
        displayMgr.showAPMode(wifiPortal.getAPSSID(), wifiPortal.getIPAddress());
    } else if (wifiPortal.isConnected()) {
        displayMgr.showConnecting(configMgr.settings.wifi_ssid, 100);
        delay(300);

        // In API mode, validate API key before starting
        if (matchCtrl.getMode() == OPMODE_API && !arenaSync.isSlave()) {
            Serial.println("[System] Master validating API key against BotSports API...");
            String err;
            if (apiClient.validateApiKey(configMgr.settings.api_key, configMgr.settings.api_url, err)) {
                Serial.printf("[System] BotSports API key verified for arena: '%s'\n", apiClient.getData().arena_name.c_str());
                arenaSync.broadcastState(true);
            } else {
                Serial.printf("[System] BotSports API key invalid (%s). Switching to Standalone Mode.\n", err.c_str());
                matchCtrl.setMode(OPMODE_STANDALONE);
            }
        }
    }

    // 7. Launch Parallel FreeRTOS Tasks pinned to Core 0
    // Core 0 handles networking, HTTP/DNS web server, UDP sync, and HTTPS API polling.
    // Core 1 (Arduino loop) is 100% dedicated to display rendering, marquee animation,
    // and button debounce with zero blocking!
    xTaskCreatePinnedToCore(
        networkTask,
        "NetTask",
        8192,
        NULL,
        2,
        NULL,
        0 // Pin to Core 0
    );

    xTaskCreatePinnedToCore(
        apiTask,
        "ApiTask",
        10240,
        NULL,
        1,
        NULL,
        0 // Pin to Core 0
    );

    Serial.println("[System] Dual-Core Parallel Architecture successfully initialized!");
}

void loop() {
    // 0. If OTA firmware update is in progress, let the OTA progress screen stay on the matrix undisturbed
    if (wifiPortal.isOTAInProgress()) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }

    // 1. Check user button: Hold (>3s) triggers AP mode, Tap (<1s) scrolls IP address
    buttonMgr.update();
    if (buttonMgr.isHoldTriggered()) {
        Serial.println("[System] User button triggered AP Config Mode!");
        wifiPortal.requestAPMode();
        displayMgr.showAPMode(wifiPortal.getAPSSID(), wifiPortal.getIPAddress());
        return;
    }
    if (buttonMgr.isTapTriggered()) {
        Serial.println("[System] User button tapped: scrolling IP address across display!");
        displayMgr.triggerIPScroll(wifiPortal.getIPAddress());
    }

    // 2. Update Match Controller (standalone timer countdown, KO status)
    matchCtrl.update();

    // 3. If currently animating the IP scroll banner, let displayMgr render it
    if (displayMgr.isScrollingIP()) {
        displayMgr.update();
        vTaskDelay(pdMS_TO_TICKS(15));
        return;
    }

    // 4. In AP mode without user activity, maintain the animated AP setup screen
    if (wifiPortal.isAPMode() && matchCtrl.getMode() != OPMODE_TEST && matchCtrl.getMode() != OPMODE_STANDALONE) {
        displayMgr.setScreen(SCREEN_AP_MODE);
    } else {
        // 5. Mode-dependent display and state handling:
        OperatingMode currentMode = matchCtrl.getMode();

        if (currentMode == OPMODE_TEST) {
            // Screen test mode is managed directly via matchCtrl.triggerScreenTest()
        } else if (arenaSync.isSlave()) {
            // Slave Display Mode:
            // Everything is mirrored from the Master wirelessly via arenaSync._applySyncPacket().
            // Does NOT query the BotSports API.
            if (arenaSync.getLastSyncAgeMs() > 10000 && !wifiPortal.isAPMode()) {
                displayMgr.setScreen(SCREEN_CONNECTING_WIFI);
                displayMgr.showConnecting("Sync: " + configMgr.settings.sync_key, 30);
            }
        } else if (currentMode == OPMODE_STANDALONE) {
            // Standalone Arena Match Mode
            if (matchCtrl.getCurrentClockState() == CLOCK_COUNT_IN) {
                displayMgr.setScreen(SCREEN_COUNT_IN);
            } else if (matchCtrl.isKO()) {
                displayMgr.setScreen(SCREEN_KO);
            } else if (matchCtrl.getCurrentOutcomeType().length() > 0 && matchCtrl.getCurrentOutcomeType() != "ko") {
                displayMgr.setScreen(SCREEN_DECISION);
            } else if (matchCtrl.getCurrentMatchStatus().equalsIgnoreCase("Finished")) {
                displayMgr.setScreen(SCREEN_FINISHED);
            } else {
                displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
            }
        } else {
            // OPMODE_API: Synchronized with BotSports API (Master Display)
            if (wifiPortal.isConnected()) {
                ArenaStatusData data = apiClient.getData();

                if (!data.valid) {
                    displayMgr.setScreen(SCREEN_ERROR);
                    displayMgr.showError("BOTSPORTS", data.error_message);
                } else if (apiClient.isDecisionHoldActive() || matchCtrl.getCurrentOutcomeType().length() > 0 || matchCtrl.getCurrentMatchStatus().equalsIgnoreCase("Decided")) {
                    if (matchCtrl.getCurrentOutcomeType() == "ko") {
                        displayMgr.setScreen(SCREEN_KO);
                    } else {
                        displayMgr.setScreen(SCREEN_DECISION);
                    }
                } else if (matchCtrl.getCurrentClockState() == CLOCK_COUNT_IN) {
                    displayMgr.setScreen(SCREEN_COUNT_IN);
                } else if (matchCtrl.getCurrentMatchStatus().equalsIgnoreCase("Finished")) {
                    displayMgr.setScreen(SCREEN_FINISHED);
                } else if (data.has_current_match) {
                    displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
                } else if (data.has_upcoming_match) {
                    displayMgr.setScreen(SCREEN_UPCOMING_MATCH);
                } else {
                    displayMgr.setScreen(SCREEN_IDLE_EVENT);
                }
            } else {
                // Reconnecting to WiFi
                displayMgr.setScreen(SCREEN_CONNECTING_WIFI);
                displayMgr.showConnecting(configMgr.settings.wifi_ssid, 50);
            }
        }
    }

    // 6. Update display rendering (runs at smooth ~60 FPS on dedicated Core 1)
    displayMgr.update();
    vTaskDelay(pdMS_TO_TICKS(15));
}
