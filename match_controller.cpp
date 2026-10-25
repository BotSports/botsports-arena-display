#include "match_controller.h"
#include "config_manager.h"
#include "display_manager.h"
#include "api_client.h"

MatchController matchCtrl;

MatchController::MatchController() {
    _mode = OPMODE_API;
    _matchNumber = 1;
    _roundNumber = 1;
    _matchStatus = "Scheduled";
    _durationSec = 180.0f;
    _remainingSec = 180.0f;
    _hazardsThresholdSec = DEFAULT_HAZARDS_SECONDS;
    _clockState = CLOCK_STOPPED;
    _lastClockUpdateMillis = 0;
    _countInSeconds = -1;
    _countInStartMillis = 0;
    _pauseReason = "";

    _redName = "Tombstone";
    _blueName = "Minotaur";

    _redPitReady = false;
    _redCheckedIn = false;
    _redMatchReady = false;
    _bluePitReady = false;
    _blueCheckedIn = false;
    _blueMatchReady = false;

    _outcomeType = "";
    _winnerCorner = "";
    _winnerName = "";
    _outcomeTimeRemaining = 0.0f;
    _outcomeStartMillis = 0;

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
    _hazardsThresholdSec = configMgr.settings.hazards_seconds;
    if (_mode == OPMODE_API && configMgr.settings.api_key.length() == 0) {
        _mode = OPMODE_STANDALONE;
        configMgr.settings.op_mode = (uint8_t)OPMODE_STANDALONE;
        configMgr.saveSettings();
        Serial.println("[MatchCtrl] No API key configured. Defaulted to Standalone Arena Mode.");
    } else if (_mode != OPMODE_API && _mode != OPMODE_STANDALONE) {
        _mode = OPMODE_STANDALONE;
    }
    Serial.printf("[MatchCtrl] Starting in %s Mode (restored from NVS).\n", (_mode == OPMODE_API) ? "BotSports API Sync" : "Standalone Arena");
}

bool MatchController::setMode(OperatingMode mode) {
    if (mode == OPMODE_API) {
        if (!apiClient.isKeyValid()) {
            Serial.println("[MatchCtrl] REJECTED switch to BotSports API Mode: No verified valid API key!");
            return false;
        }
        _mode = OPMODE_API;
        _activeTestName = "";
        clearResult();
        configMgr.settings.op_mode = (uint8_t)OPMODE_API;
        configMgr.saveSettings();
        Serial.println("[MatchCtrl] Switched to BotSports API Mode (saved to NVS)");
        return true;
    } else if (mode == OPMODE_STANDALONE) {
        _mode = OPMODE_STANDALONE;
        _activeTestName = "";
        configMgr.settings.op_mode = (uint8_t)OPMODE_STANDALONE;
        configMgr.saveSettings();
        Serial.println("[MatchCtrl] Switched to Standalone Arena Mode (saved to NVS)");
        return true;
    } else if (mode == OPMODE_TEST) {
        _mode = OPMODE_TEST;
        Serial.println("[MatchCtrl] Switched to Screen Test Mode");
        return true;
    }
    return false;
}

String MatchController::getModeString() const {
    switch (_mode) {
        case OPMODE_API: return "api";
        case OPMODE_STANDALONE: return "standalone";
        case OPMODE_TEST: return "test";
        default: return "unknown";
    }
}

void MatchController::triggerCountIn() {
    clearResult();
    _matchStatus = "Ongoing";
    _clockState = CLOCK_COUNT_IN;
    _countInSeconds = 3;
    _countInStartMillis = millis();
    _pauseReason = "";
    displayMgr.setScreen(SCREEN_COUNT_IN);
    Serial.println("[MatchCtrl] Triggered 3-2-1 Count-In sequence!");
}

void MatchController::startTimer(bool withCountIn) {
    if (withCountIn) {
        triggerCountIn();
        return;
    }
    clearResult();
    _matchStatus = "Ongoing";
    _clockState = CLOCK_RUNNING;
    _countInSeconds = -1;
    _pauseReason = "";
    _lastClockUpdateMillis = millis();
    displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    Serial.printf("[MatchCtrl] Timer started/resumed at %.1fs\n", _remainingSec);
}

void MatchController::pauseTimer(const String& reason) {
    if (_clockState == CLOCK_RUNNING) {
        float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
        _remainingSec -= elapsed;
        if (_remainingSec < 0.0f) _remainingSec = 0.0f;
    } else if (_clockState == CLOCK_COUNT_IN) {
        _countInSeconds = -1;
    }
    _clockState = CLOCK_PAUSED;
    _pauseReason = reason;
    _lastClockUpdateMillis = millis();
    displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    Serial.printf("[MatchCtrl] Timer paused at %.1fs (Reason: %s)\n", _remainingSec, reason.c_str());
}

void MatchController::resetTimer() {
    _clockState = CLOCK_STOPPED;
    _matchStatus = "Scheduled";
    _remainingSec = _durationSec;
    _countInSeconds = -1;
    _pauseReason = "";
    _lastClockUpdateMillis = millis();
    clearResult();
    displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
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

void MatchController::setHazardsThreshold(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    _hazardsThresholdSec = seconds;
    configMgr.settings.hazards_seconds = seconds;
    configMgr.saveSettings();
    Serial.printf("[MatchCtrl] Hazards threshold set to %.0f seconds\n", _hazardsThresholdSec);
}

bool MatchController::isHazardsActive() const {
    if (_clockState == CLOCK_RUNNING || _clockState == CLOCK_PAUSED) {
        return (_remainingSec <= _hazardsThresholdSec) && (_remainingSec > 0.0f);
    }
    return false;
}

float MatchController::getRemainingSeconds() const {
    if (_clockState == CLOCK_RUNNING) {
        float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
        float rem = _remainingSec - elapsed;
        return (rem > 0.0f) ? rem : 0.0f;
    }
    return _remainingSec;
}

void MatchController::syncClock(ClockState state, float remainingSec, float durationSec, int countInSec, float hazardsSec, bool hazardsActive, const String& pauseReason) {
    if (durationSec >= 5.0f) _durationSec = durationSec;
    _clockState = state;
    _remainingSec = remainingSec;
    _countInSeconds = countInSec;
    if (hazardsSec > 0.0f) _hazardsThresholdSec = hazardsSec;
    _pauseReason = pauseReason;
    _lastClockUpdateMillis = millis();
}

void MatchController::setMatchDetails(int matchNum, int roundNum, const String& redName, const String& blueName) {
    if (matchNum > 0) _matchNumber = matchNum;
    if (roundNum > 0) _roundNumber = roundNum;
    if (redName.length() > 0) _redName = redName;
    if (blueName.length() > 0) _blueName = blueName;
    Serial.printf("[MatchCtrl] Match details updated: M%d R%d, Red: %s, Blue: %s\n",
                  _matchNumber, _roundNumber, _redName.c_str(), _blueName.c_str());
}

void MatchController::setRedReadiness(bool pit, bool gate, bool match) {
    _redPitReady = pit;
    _redCheckedIn = gate;
    _redMatchReady = match;
    Serial.printf("[MatchCtrl] Red Readiness: Pit=%d, Gate=%d, Match=%d\n", pit, gate, match);
}

void MatchController::setBlueReadiness(bool pit, bool gate, bool match) {
    _bluePitReady = pit;
    _blueCheckedIn = gate;
    _blueMatchReady = match;
    Serial.printf("[MatchCtrl] Blue Readiness: Pit=%d, Gate=%d, Match=%d\n", pit, gate, match);
}

void MatchController::finishMatch() {
    if (_clockState == CLOCK_RUNNING) {
        pauseTimer();
    }
    _clockState = CLOCK_STOPPED;
    _matchStatus = "Finished";
    displayMgr.setScreen(SCREEN_FINISHED);
    Serial.printf("[MatchCtrl] Match marked as Finished (Scoring in progress) at %.1fs remaining\n", _remainingSec);
}

void MatchController::recordResult(const String& winnerCorner, const String& outcomeType) {
    if (_clockState == CLOCK_RUNNING || _clockState == CLOCK_COUNT_IN) {
        pauseTimer();
    }

    _matchStatus = "Decided";
    _outcomeType = outcomeType;
    _outcomeType.toLowerCase();
    _winnerCorner = winnerCorner;
    _winnerCorner.toLowerCase();

    if (_winnerCorner == "red") {
        _winnerName = getCurrentRedName();
    } else if (_winnerCorner == "blue") {
        _winnerName = getCurrentBlueName();
    } else {
        _winnerCorner = "tied";
        _winnerName = "Draw / Tied";
    }

    _outcomeTimeRemaining = getCurrentRemainingSeconds();
    _outcomeStartMillis = millis();

    if (_outcomeType == "ko") {
        displayMgr.setScreen(SCREEN_KO);
    } else {
        displayMgr.setScreen(SCREEN_DECISION);
    }

    Serial.printf("[MatchCtrl] Bout Outcome Decided! Winner: %s (%s Corner) via %s at %.1fs\n",
                  _winnerName.c_str(), _winnerCorner.c_str(), _outcomeType.c_str(), _outcomeTimeRemaining);
}

void MatchController::triggerKO(const String& winnerCorner) {
    recordResult(winnerCorner, "ko");
}

void MatchController::clearResult() {
    _outcomeType = "";
    _winnerCorner = "";
    _winnerName = "";
    _outcomeTimeRemaining = 0.0f;
    _outcomeStartMillis = 0;
}

unsigned long MatchController::getOutcomeElapsedMillis() const {
    if (_outcomeType.length() == 0) return 0;
    return millis() - _outcomeStartMillis;
}

void MatchController::triggerScreenTest(const String& testName) {
    _activeTestName = testName;
    _testStartMillis = millis();

    if (testName == "resume" || testName == "normal") {
        _mode = (configMgr.settings.api_key.length() > 0) ? OPMODE_API : OPMODE_STANDALONE;
        _activeTestName = "";
        clearResult();
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
        clearResult();
        displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    } else if (testName == "count_in") {
        triggerCountIn();
    } else if (testName == "hazards") {
        clearResult();
        _remainingSec = 30.0f;
        _clockState = CLOCK_RUNNING;
        displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
    } else if (testName == "finished") {
        finishMatch();
    } else if (testName == "upcoming") {
        clearResult();
        displayMgr.setScreen(SCREEN_UPCOMING_MATCH);
    } else if (testName == "ko_red") {
        triggerKO("red");
    } else if (testName == "ko_blue") {
        triggerKO("blue");
    } else if (testName == "decision_red") {
        recordResult("red", "decision");
    } else if (testName == "decision_blue") {
        recordResult("blue", "decision");
    } else if (testName == "expired") {
        clearResult();
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
        if (_clockState == CLOCK_COUNT_IN) {
            unsigned long elapsed = millis() - _countInStartMillis;
            if (elapsed < 1000) {
                _countInSeconds = 3;
            } else if (elapsed < 2000) {
                _countInSeconds = 2;
            } else if (elapsed < 3000) {
                _countInSeconds = 1;
            } else if (elapsed < 4000) {
                _countInSeconds = 0; // "FIGHT!"
            } else {
                _clockState = CLOCK_RUNNING;
                _countInSeconds = -1;
                _lastClockUpdateMillis = millis();
                displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
                Serial.println("[MatchCtrl] Count-in completed! FIGHT clock running!");
            }
        } else if (_clockState == CLOCK_RUNNING) {
            float elapsed = (millis() - _lastClockUpdateMillis) / 1000.0f;
            if (elapsed >= 0.05f) { // Update every 50ms
                _remainingSec -= elapsed;
                _lastClockUpdateMillis = millis();

                if (_remainingSec <= 0.0f) {
                    _remainingSec = 0.0f;
                    _clockState = CLOCK_EXPIRED;
                    finishMatch();
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
    return apiClient.hasCurrentMatch();
}

int MatchController::getCurrentMatchNumber() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _matchNumber;
    }
    return apiClient.getMatchNumber();
}

int MatchController::getCurrentRoundNumber() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _roundNumber;
    }
    return apiClient.getRoundNumber();
}

String MatchController::getCurrentRedName() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _redName;
    }
    return apiClient.getRedName();
}

String MatchController::getCurrentBlueName() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _blueName;
    }
    return apiClient.getBlueName();
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
        if (_activeTestName == "count_in") return CLOCK_COUNT_IN;
        return _clockState;
    }
    return apiClient.getEffectiveClockState();
}

int MatchController::getCurrentCountInSeconds() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _countInSeconds;
    }
    return apiClient.getInterpolatedCountInSeconds();
}

bool MatchController::isCurrentHazardsActive() const {
    if (_mode == OPMODE_STANDALONE) {
        return isHazardsActive();
    } else if (_mode == OPMODE_TEST) {
        return (_remainingSec <= 30.0f && _remainingSec > 0.0f);
    }
    return apiClient.isHazardsActive();
}

String MatchController::getCurrentPauseReason() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _pauseReason;
    }
    return apiClient.getPauseReason();
}

String MatchController::getCurrentMatchStatus() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return _matchStatus;
    }
    return apiClient.getMatchStatus();
}

bool MatchController::getCurrentRedPitReady() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _redPitReady;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return rp;
}

bool MatchController::getCurrentRedCheckedIn() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _redCheckedIn;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return rg;
}

bool MatchController::getCurrentRedMatchReady() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _redMatchReady;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return rm;
}

bool MatchController::getCurrentBluePitReady() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _bluePitReady;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return bp;
}

bool MatchController::getCurrentBlueCheckedIn() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _blueCheckedIn;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return bg;
}

bool MatchController::getCurrentBlueMatchReady() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _blueMatchReady;
    bool rp, rg, rm, bp, bg, bm;
    apiClient.getReadiness(rp, rg, rm, bp, bg, bm);
    return bm;
}

String MatchController::getCurrentOutcomeType() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _outcomeType;
    return apiClient.getOutcomeType();
}

String MatchController::getCurrentWinnerCorner() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _winnerCorner;
    return apiClient.getWinnerCorner();
}

String MatchController::getCurrentWinnerName() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _winnerName;
    return apiClient.getWinnerName();
}

float MatchController::getCurrentOutcomeTime() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) return _outcomeTimeRemaining;
    return apiClient.getInterpolatedRemainingSeconds();
}

unsigned long MatchController::getKOElapsedMillis() const {
    if (_mode == OPMODE_STANDALONE || _mode == OPMODE_TEST) {
        return getOutcomeElapsedMillis();
    }
    return apiClient.getDecisionElapsedMillis();
}
