#include "api_client.h"
#include "arena_sync.h"

BotSportsAPIClient apiClient;

BotSportsAPIClient::BotSportsAPIClient() {
    _mutex = xSemaphoreCreateRecursiveMutex();
    _data.valid = false;
    _data.http_code = 0;
    _data.has_active_event = false;
    _data.has_current_match = false;
    _data.has_clock = false;
    _data.has_upcoming_match = false;
    _lastPollTime = 0;
}

bool BotSportsAPIClient::pollStatus() {
    if (configMgr.settings.api_key.length() == 0) {
        lock();
        _data.valid = false;
        _data.error_message = "No API Key configured";
        unlock();
        return false;
    }

    _secureClient.setInsecure(); // Allow HTTPS without bundling complete root CA certs
    _secureClient.setTimeout(2);

    HTTPClient http;
    String url = configMgr.settings.api_url + "/app/arenas/device/status";

    String corner = configMgr.settings.corner;
    if (corner == "red" || corner == "blue") {
        url += "?corner=" + corner;
    }

    if (!http.begin(_secureClient, url)) {
        lock();
        _data.valid = false;
        _data.error_message = "HTTP connection failed";
        unlock();
        return false;
    }

    http.addHeader("X-Arena-Api-Key", configMgr.settings.api_key);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "BotSports-Arena-Display/" FIRMWARE_VERSION);
    http.setTimeout(1200);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        ArenaStatusData tempData;
        tempData.http_code = httpCode;
        _parseStatusJson(payload, tempData);
        tempData.valid = true;
        tempData.error_message = "";
        http.end();

        lock();
        _data = tempData;
        unlock();
        return true;
    } else {
        String errMsg;
        if (httpCode == 401 || httpCode == 403) {
            errMsg = "Invalid Arena Key";
        } else if (httpCode == 404) {
            errMsg = "Arena Not Found";
        } else if (httpCode < 0) {
            errMsg = "Network Timeout";
        } else {
            errMsg = "HTTP " + String(httpCode);
        }
        Serial.printf("[API] Status request failed: %s (Code %d)\n", errMsg.c_str(), httpCode);
        http.end();

        lock();
        _data.valid = false;
        _data.http_code = httpCode;
        _data.error_message = errMsg;
        unlock();
        return false;
    }
}

void BotSportsAPIClient::_parseStatusJson(const String& payload, ArenaStatusData& target) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        target.valid = false;
        target.error_message = "JSON parse error";
        Serial.printf("[API] JSON deserialize error: %s\n", error.c_str());
        return;
    }

    target.arena_id = doc["arena_id"] | 0;
    target.arena_name = doc["arena_name"].as<String>();
    target.has_active_event = doc["has_active_event"] | false;
    target.active_event_id = doc["active_event_id"] | 0;
    target.active_event_name = doc["active_event_name"].as<String>();

    // Current match details
    int curMatchId = doc["current_match_id"] | 0;
    if (curMatchId > 0) {
        target.has_current_match = true;
        target.current_match.id = curMatchId;
        target.current_match.status = doc["current_match_status"].as<String>();
        target.current_match.round_number = doc["current_match_round"] | 1;
        target.current_match.match_number = doc["current_match_number"] | 1;

        // Red corner
        JsonObject red = doc["red_corner"];
        if (!red.isNull()) {
            target.current_match.red.name = red["bot_name"] | "Red Bot";
            target.current_match.red.team = red["team_name"] | "";
            target.current_match.red.ready = red["ready"] | false;
            target.current_match.red.score = red["score"] | 0.0f;
        } else {
            target.current_match.red.name = "Red";
            target.current_match.red.team = "";
            target.current_match.red.ready = false;
        }

        // Blue corner
        JsonObject blue = doc["blue_corner"];
        if (!blue.isNull()) {
            target.current_match.blue.name = blue["bot_name"] | "Blue Bot";
            target.current_match.blue.team = blue["team_name"] | "";
            target.current_match.blue.ready = blue["ready"] | false;
            target.current_match.blue.score = blue["score"] | 0.0f;
        } else {
            target.current_match.blue.name = "Blue";
            target.current_match.blue.team = "";
            target.current_match.blue.ready = false;
        }
    } else {
        target.has_current_match = false;
    }

    // Match clock
    JsonObject clock = doc["clock"];
    if (!clock.isNull()) {
        target.has_clock = true;
        String statusStr = clock["status"] | "stopped";
        if (statusStr.equalsIgnoreCase("running")) {
            target.clock_state = CLOCK_RUNNING;
        } else if (statusStr.equalsIgnoreCase("paused")) {
            target.clock_state = CLOCK_PAUSED;
        } else if (statusStr.equalsIgnoreCase("expired")) {
            target.clock_state = CLOCK_EXPIRED;
        } else {
            target.clock_state = CLOCK_STOPPED;
        }

        target.clock_duration_sec = clock["duration_seconds"] | 180.0f;
        target.clock_remaining_sec = clock["remaining_seconds"] | target.clock_duration_sec;
        target.clock_sync_millis = millis();
    } else {
        target.has_clock = false;
    }

    // Upcoming match
    JsonObject up = doc["upcoming_match"];
    if (!up.isNull()) {
        target.has_upcoming_match = true;
        target.upcoming_match.id = up["match_id"] | 0;
        target.upcoming_match.round_number = up["round_number"] | 1;
        target.upcoming_match.match_number = up["match_number"] | 1;
        target.upcoming_match.status = "Pending";

        JsonArray combatants = up["combatants"];
        if (combatants.size() > 0) {
            JsonObject c0 = combatants[0];
            target.upcoming_match.red.name = c0["bot_name"] | "Red Bot";
            target.upcoming_match.red.team = c0["team_name"] | "";
        }
        if (combatants.size() > 1) {
            JsonObject c1 = combatants[1];
            target.upcoming_match.blue.name = c1["bot_name"] | "Blue Bot";
            target.upcoming_match.blue.team = c1["team_name"] | "";
        }
    } else {
        target.has_upcoming_match = false;
    }
}

float BotSportsAPIClient::getInterpolatedRemainingSeconds() const {
    lock();
    if (!_data.has_clock) {
        unlock();
        return 0.0f;
    }

    if (_data.clock_state == CLOCK_RUNNING) {
        float elapsedSinceSync = (millis() - _data.clock_sync_millis) / 1000.0f;
        float remaining = _data.clock_remaining_sec - elapsedSinceSync;
        unlock();
        if (remaining < 0.0f) {
            return 0.0f;
        }
        return remaining;
    }

    float rem = _data.clock_remaining_sec;
    unlock();
    return rem;
}

ClockState BotSportsAPIClient::getEffectiveClockState() const {
    lock();
    if (!_data.has_clock) {
        unlock();
        return CLOCK_STOPPED;
    }

    ClockState st = _data.clock_state;
    unlock();

    if (st == CLOCK_RUNNING && getInterpolatedRemainingSeconds() <= 0.0f) {
        return CLOCK_EXPIRED;
    }

    return st;
}

void BotSportsAPIClient::setSyncedData(const ArenaSyncPacket& pkt) {
    lock();
    _data.valid = true;
    _data.error_message = "";
    _data.has_active_event = (pkt.has_active_event != 0);
    _data.active_event_name = String(pkt.active_event_name);
    _data.arena_name = String(pkt.arena_name);
    _data.has_current_match = (pkt.has_current_match != 0);
    _data.current_match.match_number = pkt.match_number;
    _data.current_match.round_number = pkt.round_number;
    _data.current_match.red.name = String(pkt.red_name);
    _data.current_match.blue.name = String(pkt.blue_name);
    _data.has_upcoming_match = (pkt.has_upcoming != 0);
    _data.upcoming_match.match_number = pkt.upcoming_match_num;
    _data.upcoming_match.round_number = pkt.upcoming_round_num;
    _data.upcoming_match.red.name = String(pkt.upcoming_red);
    _data.upcoming_match.blue.name = String(pkt.upcoming_blue);
    _data.has_clock = true;
    _data.clock_state = (ClockState)pkt.clock_state;
    _data.clock_duration_sec = pkt.duration_seconds;
    _data.clock_remaining_sec = pkt.remaining_seconds;
    _data.clock_sync_millis = millis();
    unlock();
}
