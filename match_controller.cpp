#include "match_controller.h"
#include "config_manager.h"
#include "display_manager.h"

MatchController matchCtrl;

MatchController::MatchController() {
    _mode = OPMODE_API;
    _matchNumber = 1;
    _roundNumber = 1;
    _durationSec = 180.0f; // 3 minutes standard combat match
    _remainingSec = 180.0f;
    _clockState = CLOCK_STOPPED;
    _lastClockUpdateMillis = 0;

    _redName = "Tombstone";
    _blueName = "Minotaur";

    _isKO = false;
    _koWinnerCorner = "";
    _koWinnerName = "";
    _koTimeRemaining = 0.0f;
    _koStartMillis = 0;

    _activeTestName = "";
    _testStartMillis = 0;
    _mutex = xSemaphoreCreateRecursiveMutex();
}

void MatchController::lock() const {
    if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
}

void MatchController::unlock() const {
    if (_mutex) xSemaphoreGiveRecursive(_mutex);
}

void MatchController::begin() {
    if (!_mutex) _mutex = xSemaphoreCreateRecursiveMutex();
    _mode = (OperatingMode)configMgr.settings.op_mode;
    if (_mode != OPMODE_API && _mode != OPMODE_STANDALONE) {
        _mode = OPMODE_STANDALONE;
    }
    Serial.printf("[MatchCtrl] Starting in %s Mode (restored from NVS).\n", (_mode == OPMODE_API) ? "BotSports API Sync" : "Standalone Arena");
}

void MatchController::setMode(OperatingMode mode) {
    _mode = mode;
    if (_mode == OPMODE_API) {
        _activeTestName = "";
        clearKO();
        configMgr.settings.op_mode = (uint8_t)OPMODE_API;
        configMgr.saveSettings();
        Serial.println("[MatchCtrl] Switched to BotSports API Mode (saved to NVS)");
    } else if (_mode == OPMODE_STANDALONE) {
        _activeTestName = "";
        configMgr.settings.op_mode = (uint8_t)OPMODE_STANDALONE;
        configMgr.saveSettings();
        Serial.println("[MatchCtrl] Switched to Standalone Arena Mode (saved to NVS)");
    } else if (_mode == OPMODE_TEST) {
        Serial.println("[MatchCtrl] Switched to Screen Test Mode");
    }
}

String MatchController::getModeString() const {
    switch (_mode) {
        case OPMODE_API: return "api";
        case OPMODE_STANDALONE: return "standalone";
        case OPMODE_TEST: return "test";
        default: return "unknown";
    }
}

void MatchController::startTimer() {
    if (_isKO) {
        clearKO();
    }
    _clockState = CLOCK_RUNNING;
    _lastClockUpdateMillis = millis();
    Serial.printf("[MatchCtrl] Timer started/resumed at %.1fs\n", _remainingSec);
}

void MatchController::pauseTimer() {
    if (_clockState == CLOCK_RUNNING) {
        float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
        _remainingSec -= elapsed;
        if (_remainingSec < 0.0f) _remainingSec = 0.0f;
    }
    _clockState = CLOCK_PAUSED;
    _lastClockUpdateMillis = millis();
    Serial.printf("[MatchCtrl] Timer paused at %.1fs\n", _remainingSec);
}

void MatchController::resetTimer() {
    _clockState = CLOCK_STOPPED;
    _remainingSec = _durationSec;
    _lastClockUpdateMillis = millis();
    clearKO();
    Serial.printf("[MatchCtrl] Timer reset to %.1fs\n", _durationSec);
}

void MatchController::setDuration(float seconds) {
    if (seconds < 5.0f) seconds = 5.0f;
    _durationSec = seconds;
    if (_clockState == CLOCK_STOPPED || _clockState == CLOCK_EXPIRED) {
        _remainingSec = _durationSec;
    }
    Serial.printf("[MatchCtrl] Match duration set to %.0f seconds\n", _durationSec);
}

void MatchController::syncClock(ClockState state, float remainingSec, float durationSec) {
    if (durationSec >= 5.0f) _durationSec = durationSec;
    _clockState = state;
    _remainingSec = remainingSec;
    _lastClockUpdateMillis = millis();
}

float MatchController::getRemainingSeconds() const {
    if (_clockState == CLOCK_RUNNING) {
        float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
        float rem = _remainingSec - elapsed;
        return (rem > 0.0f) ? rem : 0.0f;
    }
    return _remainingSec;
}

void MatchController::setMatchDetails(int matchNum, int roundNum, const String& redName, const String& blueName) {
    if (matchNum > 0) _matchNumber = matchNum;
    if (roundNum > 0) _roundNumber = roundNum;
    if (redName.length() > 0) _redName = redName;
    if (blueName.length() > 0) _blueName = blueName;
    Serial.printf("[MatchCtrl] Match details updated: M%d R%d, Red: %s, Blue: %s\n",
                  _matchNumber, _roundNumber, _redName.c_str(), _blueName.c_str());
}

void MatchController::triggerKO(const String& winnerCorner) {
    // Stop the clock
    pauseTimer();

    _isKO = true;
    _koWinnerCorner = winnerCorner;
    _koWinnerCorner.toLowerCase();

    if (_koWinnerCorner == "red") {
        _koWinnerName = getCurrentRedName();
    } else {
        _koWinnerCorner = "blue";
        _koWinnerName = getCurrentBlueName();
    }

    _koTimeRemaining = getCurrentRemainingSeconds();
    _koStartMillis = millis();

    displayMgr.setScreen(SCREEN_KO);

    Serial.printf("[MatchCtrl] KNOCKOUT! Winner: %s (%s Corner) at %.1fs\n",
                  _koWinnerName.c_str(), _koWinnerCorner.c_str(), _koTimeRemaining);
}

void MatchController::clearKO() {
    _isKO = false;
    _koWinnerCorner = "";
    _koWinnerName = "";
    _koTimeRemaining = 0.0f;
    _koStartMillis = 0;
}

unsigned long MatchController::getKOElapsedMillis() const {
    if (!_isKO) return 0;
    return millis() - _koStartMillis;
}

void MatchController::triggerScreenTest(const String& testName) {
    _activeTestName = testName;
    _testStartMillis = millis();

    if (testName == "resume" || testName == "normal") {
        _mode = (configMgr.settings.api_key.length() > 0) ? OPMODE_API : OPMODE_STANDALONE;
        _activeTestName = "";
        clearKO();
        Serial.println("[MatchCtrl] Test ended. Resumed normal operational mode.");
        return;
    }

    _mode = OPMODE_TEST;
    Serial.printf("[MatchCtrl] Triggered screen test: '%s'\n", testName.c_str());

    if (testName == "boot") {
        displayMgr.setScreen(SCREEN_BOOT);
        displayMgr.showBoot();
    } else if (testName == "connecting") {
        displayMgr.setScreen(SCREEN_CONNECTING_WIFI);
        displayMgr.showConnecting("Arena-WiFi-5G", 65);
    } else if (testName == "ap") {
        displayMgr.setScreen(SCREEN_AP_MODE);
        displayMgr.showAPMode("BotSports-Arena-TEST", "192.168.4.1");
    } else if (testName == "idle") {
        displayMgr.setScreen(SCREEN_IDLE_EVENT);
    } else if (testName == "match" || testName == "match_running") {
        clearKO();
        displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    } else if (testName == "upcoming") {
        clearKO();
        displayMgr.setScreen(SCREEN_UPCOMING_MATCH);
    } else if (testName == "ko_red") {
        triggerKO("red");
    } else if (testName == "ko_blue") {
        triggerKO("blue");
    } else if (testName == "expired") {
        clearKO();
        _remainingSec = 0.0f;
        _clockState = CLOCK_EXPIRED;
        displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    } else if (testName == "error") {
        displayMgr.setScreen(SCREEN_ERROR);
        displayMgr.showError("DEVICE ERROR", "Test Error Message");
    }
}

void MatchController::update() {
    if (_mode == OPMODE_STANDALONE) {
        if (_clockState == CLOCK_RUNNING) {
            float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
            if (elapsed >= 0.05f) { // Update every 50ms
                _remainingSec -= elapsed;
                _lastClockUpdateMillis = millis();

                if (_remainingSec <= 0.0f) {
                    _remainingSec = 0.0f;
                    _clockState = CLOCK_EXPIRED;
                    Serial.println("[MatchCtrl] Standalone match clock expired! TIME!");
                }
            }
        }
    }
}

bool MatchController::hasActiveMatch() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return true;
    }
    return apiClient.getData().has_current_match;
}

int MatchController::getCurrentMatchNumber() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _matchNumber;
    }
    return apiClient.getData().current_match.match_number;
}

int MatchController::getCurrentRoundNumber() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _roundNumber;
    }
    return apiClient.getData().current_match.round_number;
}

String MatchController::getCurrentRedName() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _redName;
    }
    return apiClient.getData().current_match.red.name;
}

String MatchController::getCurrentBlueName() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _blueName;
    }
    return apiClient.getData().current_match.blue.name;
}

float MatchController::getCurrentRemainingSeconds() const {
    if (_mode == OPMODE_STANDALONE) {
        return getRemainingSeconds();
    } else if (_mode == OPMODE_TEST) {
        if (_activeTestName == "expired") return 0.0f;
        return _remainingSec;
    }
    return apiClient.getInterpolatedRemainingSeconds();
}

ClockState MatchController::getCurrentClockState() const {
    if (_mode == OPMODE_STANDALONE) {
        return _clockState;
    } else if (_mode == OPMODE_TEST) {
        if (_activeTestName == "expired") return CLOCK_EXPIRED;
        return _clockState;
    }
    return apiClient.getEffectiveClockState();
}
