#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "config_manager.h"

enum ClockState {
    CLOCK_STOPPED,
    CLOCK_COUNT_IN,
    CLOCK_RUNNING,
    CLOCK_PAUSED,
    CLOCK_EXPIRED
};

struct BotCombatant {
    int bot_id;
    String name;
    String team;
    bool pit_ready;
    bool arena_checked_in;
    bool ready; // match_ready
    float score;
    String result_type;
};

struct MatchInfo {
    int id;
    int round_number;
    int match_number;
    String status; // "Scheduled", "Ongoing", "Finished", "Decided", "Completed", etc.
    BotCombatant red;
    BotCombatant blue;
    String winner_corner; // "red", "blue", "tied", or ""
    String winner_name;
    String outcome_type;  // "ko", "decision", "forfeit", or ""
};

struct ArenaStatusData {
    bool valid;
    String error_message;
    int http_code;

    // Arena Info
    int arena_id;
    String arena_name;
    bool has_active_event;
    int active_event_id;
    String active_event_name;
    int current_tournament_id;
    String current_tournament_name;

    // Current Match
    bool has_current_match;
    MatchInfo current_match;

    // Match Clock
    bool has_clock;
    ClockState clock_state;
    float clock_duration_sec;
    float clock_hazards_sec;
    bool is_hazards_active;
    float clock_remaining_sec;
    int clock_count_in_seconds; // 3, 2, 1, 0, or -1
    String clock_pause_reason;
    unsigned long clock_sync_millis; // local millis() at time of sync

    // Upcoming Match
    bool has_upcoming_match;
    MatchInfo upcoming_match;
};


struct ArenaSyncPacket;

class BotSportsAPIClient {
public:
    BotSportsAPIClient();
    bool pollStatus();
    void setSyncedData(const ArenaSyncPacket& pkt);

    // API Key Validation
    bool validateApiKey(const String& key, const String& url, String& outError);
    bool isKeyValid() const {
        lock();
        bool valid = _keyValid;
        unlock();
        return valid;
    }
    void setKeyValid(bool valid) {
        lock();
        _keyValid = valid;
        unlock();
    }
    String getKeyValidationError() const {
        lock();
        String err = _keyValidationError;
        unlock();
        return err;
    }

    // SSE Real-Time Streaming
    bool connectStream();
    void disconnectStream();
    void handleStream();
    bool isStreamConnected() const {
        lock();
        bool c = _streamConnected;
        unlock();
        return c;
    }

    void lock() const {
        if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    }
    void unlock() const {
        if (_mutex) xSemaphoreGiveRecursive(_mutex);
    }

    ArenaStatusData getData() const {
        lock();
        ArenaStatusData copy = _data;
        unlock();
        return copy;
    }

    // High performance lightweight getters (zero struct copy / zero heap thrashing)
    void getReadiness(bool& rPit, bool& rGate, bool& rMatch, bool& bPit, bool& bGate, bool& bMatch) const {
        lock();
        rPit = _data.current_match.red.pit_ready;
        rGate = _data.current_match.red.arena_checked_in;
        rMatch = _data.current_match.red.ready;
        bPit = _data.current_match.blue.pit_ready;
        bGate = _data.current_match.blue.arena_checked_in;
        bMatch = _data.current_match.blue.ready;
        unlock();
    }
    bool hasCurrentMatch() const {
        lock();
        bool h = _data.has_current_match;
        unlock();
        return h;
    }
    String getMatchStatus() const {
        lock();
        String s = _data.current_match.status;
        unlock();
        return s;
    }
    String getRedName() const {
        lock();
        String s = _data.current_match.red.name;
        unlock();
        return s;
    }
    String getBlueName() const {
        lock();
        String s = _data.current_match.blue.name;
        unlock();
        return s;
    }
    int getMatchNumber() const {
        lock();
        int n = _data.current_match.match_number;
        unlock();
        return n;
    }
    int getRoundNumber() const {
        lock();
        int n = _data.current_match.round_number;
        unlock();
        return n;
    }
    bool isHazardsActive() const;
    String getOutcomeType() const {
        lock();
        String s = _data.current_match.outcome_type;
        unlock();
        return s;
    }
    String getWinnerCorner() const {
        lock();
        String s = _data.current_match.winner_corner;
        unlock();
        return s;
    }
    String getWinnerName() const {
        lock();
        String s = _data.current_match.winner_name;
        unlock();
        return s;
    }
    String getPauseReason() const {
        lock();
        String s = _data.clock_pause_reason;
        unlock();
        return s;
    }

    float getInterpolatedRemainingSeconds() const;
    ClockState getEffectiveClockState() const;
    int getInterpolatedCountInSeconds() const;

    // Decision Presentation Hold
    bool isDecisionHoldActive() const {
        lock();
        bool active = (_decisionHoldUntil > 0 && millis() < _decisionHoldUntil);
        unlock();
        return active;
    }
    unsigned long getDecisionElapsedMillis() const {
        lock();
        if (_decisionHoldUntil == 0) {
            unlock();
            return 0;
        }
        unsigned long start = (_decisionHoldUntil > 15000) ? (_decisionHoldUntil - 15000) : 0;
        unsigned long elapsed = millis() - start;
        unlock();
        return elapsed;
    }
    bool fetchMatchOutcome(int matchId);

private:
    mutable SemaphoreHandle_t _mutex;
    ArenaStatusData _data;
    WiFiClientSecure _secureClient;
    unsigned long _lastPollTime;
    bool _pendingStatusPoll;
    unsigned long _pendingPollTime;

    // Decision Presentation Hold Window
    unsigned long _decisionHoldUntil;

    // SSE state
    bool _keyValid;
    String _keyValidationError;
    bool _streamConnected;
    unsigned long _lastStreamConnectAttempt;
    unsigned long _lastStreamDataReceived;
    WiFiClientSecure _streamClientSecure;
    WiFiClient _streamClient;
    Client* _stream;
    String _currentSseEvent;
    String _currentSseData;
    String _streamLineBuffer;

    void _parseStatusJson(const String& payload, ArenaStatusData& target);
    void _dispatchStreamEvent(const String& eventType, const String& dataStr);
    void _handleMatchCompleted(int matchId, JsonDocument& doc);
};

extern BotSportsAPIClient apiClient;
