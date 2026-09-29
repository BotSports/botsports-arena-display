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
    void startTimer();
    void pauseTimer();
    void resetTimer();
    void setDuration(float seconds);
    float getDuration() const { return _durationSec; }
    float getRemainingSeconds() const;
    ClockState getClockState() const { return _clockState; }
    void syncClock(ClockState state, float remainingSec, float durationSec);

    // Standalone Match Info
    void setMatchDetails(int matchNum, int roundNum, const String& redName, const String& blueName);
    int getMatchNumber() const { return _matchNumber; }
    int getRoundNumber() const { return _roundNumber; }
    String getRedName() const { return _redName; }
    String getBlueName() const { return _blueName; }

    // Knockout (KO)
    void triggerKO(const String& winnerCorner); // "red" or "blue"
    void clearKO();
    bool isKO() const { return _isKO; }
    String getKOWinner() const { return _koWinnerCorner; }
    String getKOWinnerName() const { return _koWinnerName; }
    float getKOTime() const { return _koTimeRemaining; }
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

    void lock() const;
    void unlock() const;

private:
    mutable SemaphoreHandle_t _mutex;
    OperatingMode _mode;

    // Standalone state
    int _matchNumber;
    int _roundNumber;
    float _durationSec;
    float _remainingSec;
    ClockState _clockState;
    unsigned long _lastClockUpdateMillis;

    String _redName;
    String _blueName;

    // Knockout state
    bool _isKO;
    String _koWinnerCorner; // "red" or "blue"
    String _koWinnerName;
    float _koTimeRemaining;
    unsigned long _koStartMillis;

    // Screen Test state
    String _activeTestName;
    unsigned long _testStartMillis;
};

extern MatchController matchCtrl;
