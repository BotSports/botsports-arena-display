#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "config_manager.h"

enum ClockState {
    CLOCK_STOPPED,
    CLOCK_RUNNING,
    CLOCK_PAUSED,
    CLOCK_EXPIRED
};

struct BotCombatant {
    String name;
    String team;
    bool ready;
    float score;
};

struct MatchInfo {
    int id;
    int round_number;
    int match_number;
    String status; // "Pending", "Ongoing", "Completed", etc.
    BotCombatant red;
    BotCombatant blue;
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

    // Current Match
    bool has_current_match;
    MatchInfo current_match;

    // Match Clock
    bool has_clock;
    ClockState clock_state;
    float clock_duration_sec;
    float clock_remaining_sec;
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
    float getInterpolatedRemainingSeconds() const;
    ClockState getEffectiveClockState() const;

private:
    mutable SemaphoreHandle_t _mutex;
    ArenaStatusData _data;
    WiFiClientSecure _secureClient;
    unsigned long _lastPollTime;

    void _parseStatusJson(const String& payload, ArenaStatusData& target);
};

extern BotSportsAPIClient apiClient;
