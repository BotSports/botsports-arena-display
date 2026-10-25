#pragma once

#include <Arduino.h>
#include "config.h"
#include "api_client.h"

enum OperatingMode {
    OPMODE_API,         // Synchronized with BotSports API
    OPMODE_STANDALONE,  // Standalone arena match timer and control
    OPMODE_TEST         // Displaying interactive test screen
};

class MatchController {
public:
    MatchController();
    void begin();
    void update();

    // Mode Selection
    bool setMode(OperatingMode mode);
    OperatingMode getMode() const { return _mode; }
    String getModeString() const;

    // Standalone Timer Controls
    void startTimer(bool withCountIn = false);
    void triggerCountIn();
    void pauseTimer(const String& reason = "");
    void resetTimer();
    void setDuration(float seconds);
    float getDuration() const { return _durationSec; }
    void setHazardsThreshold(float seconds);
    float getHazardsThreshold() const { return _hazardsThresholdSec; }
    bool isHazardsActive() const;
    int getCountInSeconds() const { return _countInSeconds; }
    String getPauseReason() const { return _pauseReason; }
    float getRemainingSeconds() const;
    ClockState getClockState() const { return _clockState; }
    void syncClock(ClockState state, float remainingSec, float durationSec, int countInSec = -1, float hazardsSec = 60.0f, bool hazardsActive = false, const String& pauseReason = "");

    // Standalone Match Info
    void setMatchDetails(int matchNum, int roundNum, const String& redName, const String& blueName);
    int getMatchNumber() const { return _matchNumber; }
    int getRoundNumber() const { return _roundNumber; }
    String getRedName() const { return _redName; }
    String getBlueName() const { return _blueName; }
    String getMatchStatus() const { return _matchStatus; } // "Scheduled", "Ongoing", "Finished", "Decided"

    // 3-Tier Readiness
    void setRedReadiness(bool pit, bool gate, bool match);
    void setBlueReadiness(bool pit, bool gate, bool match);
    bool getRedPitReady() const { return _redPitReady; }
    bool getRedCheckedIn() const { return _redCheckedIn; }
    bool getRedMatchReady() const { return _redMatchReady; }
    bool getBluePitReady() const { return _bluePitReady; }
    bool getBlueCheckedIn() const { return _blueCheckedIn; }
    bool getBlueMatchReady() const { return _blueMatchReady; }

    // Bout Lifecycle & Decisions
    void finishMatch();
    void recordResult(const String& winnerCorner, const String& outcomeType); // winner: "red", "blue", "tied"; outcome: "ko", "decision", "forfeit"
    void clearResult();
    bool isFinished() const { return _matchStatus == "Finished"; }
    bool isDecided() const { return _matchStatus == "Decided"; }
    String getOutcomeType() const { return _outcomeType; }
    String getWinnerCorner() const { return _winnerCorner; }
    String getWinnerName() const { return _winnerName; }
    float getOutcomeTime() const { return _outcomeTimeRemaining; }
    unsigned long getOutcomeElapsedMillis() const;

    // Knockout (KO) - legacy / convenience wrappers
    void triggerKO(const String& winnerCorner);
    void clearKO() { clearResult(); }
    bool isKO() const { return getCurrentOutcomeType() == "ko"; }
    String getKOWinner() const { return getCurrentWinnerCorner(); }
    String getKOWinnerName() const { return getCurrentWinnerName(); }
    float getKOTime() const { return getCurrentOutcomeTime(); }
    unsigned long getKOElapsedMillis() const;

    // Interactive Screen Testing
    void triggerScreenTest(const String& testName);
    String getActiveTestName() const { return _activeTestName; }

    // Unified Accessors (Returns Standalone or API data based on current mode)
    bool hasActiveMatch() const;
    int getCurrentMatchNumber() const;
    int getCurrentRoundNumber() const;
    String getCurrentRedName() const;
    String getCurrentBlueName() const;
    float getCurrentRemainingSeconds() const;
    ClockState getCurrentClockState() const;
    int getCurrentCountInSeconds() const;
    bool isCurrentHazardsActive() const;
    String getCurrentPauseReason() const;
    String getCurrentMatchStatus() const;
    bool getCurrentRedPitReady() const;
    bool getCurrentRedCheckedIn() const;
    bool getCurrentRedMatchReady() const;
    bool getCurrentBluePitReady() const;
    bool getCurrentBlueCheckedIn() const;
    bool getCurrentBlueMatchReady() const;
    String getCurrentOutcomeType() const;
    String getCurrentWinnerCorner() const;
    String getCurrentWinnerName() const;
    float getCurrentOutcomeTime() const;

    void lock() const;
    void unlock() const;

private:
    mutable SemaphoreHandle_t _mutex;
    OperatingMode _mode;

    // Standalone state
    int _matchNumber;
    int _roundNumber;
    String _matchStatus;
    float _durationSec;
    float _remainingSec;
    float _hazardsThresholdSec;
    ClockState _clockState;
    unsigned long _lastClockUpdateMillis;
    int _countInSeconds; // 3, 2, 1, 0, or -1
    unsigned long _countInStartMillis;
    String _pauseReason;

    String _redName;
    String _blueName;

    // 3-Tier Readiness
    bool _redPitReady;
    bool _redCheckedIn;
    bool _redMatchReady;
    bool _bluePitReady;
    bool _blueCheckedIn;
    bool _blueMatchReady;

    // Outcome & Decision state
    String _outcomeType; // "ko", "decision", "forfeit", or ""
    String _winnerCorner; // "red", "blue", "tied", or ""
    String _winnerName;
    float _outcomeTimeRemaining;
    unsigned long _outcomeStartMillis;

    // Screen Test state
    String _activeTestName;
    unsigned long _testStartMillis;
};

extern MatchController matchCtrl;

