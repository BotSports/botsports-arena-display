#include "api_client.h"
#include "arena_sync.h"
#include "display_manager.h"

BotSportsAPIClient apiClient;

BotSportsAPIClient::BotSportsAPIClient() {
    _mutex = xSemaphoreCreateRecursiveMutex();
    _data.valid = false;
    _data.http_code = 0;
    _data.has_active_event = false;
    _data.has_current_match = false;
    _data.has_clock = false;
    _data.has_upcoming_match = false;
    _data.clock_pause_reason = "";
    _data.clock_count_in_seconds = -1;
    _data.clock_hazards_sec = DEFAULT_HAZARDS_SECONDS;
    _data.is_hazards_active = false;
    _data.current_tournament_id = 0;
    _data.current_tournament_name = "";
    _lastPollTime = 0;
    _pendingStatusPoll = false;
    _pendingPollTime = 0;
    _decisionHoldUntil = 0;

    _keyValid = false;
    _keyValidationError = "";
    _streamConnected = false;
    _lastStreamConnectAttempt = 0;
    _lastStreamDataReceived = 0;
    _stream = nullptr;
    _currentSseEvent = "";
    _currentSseData = "";
    _streamLineBuffer = "";
}

bool BotSportsAPIClient::validateApiKey(const String& key, const String& url, String& outError) {
    if (key.length() == 0) {
        outError = "API Key cannot be empty";
        lock();
        _keyValid = false;
        _keyValidationError = outError;
        unlock();
        return false;
    }

    String baseUrl = (url.length() > 0) ? url : configMgr.settings.api_url;
    String reqUrl = baseUrl + "/app/arenas/device/status";
    if (configMgr.settings.tournament_id > 0) {
        reqUrl += "?tournament_id=" + String(configMgr.settings.tournament_id);
    }
    Serial.printf("[API] Validating API key against %s ...\n", reqUrl.c_str());

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(3);

    HTTPClient http;
    if (!http.begin(secureClient, reqUrl)) {
        outError = "HTTP connection failed";
        lock();
        _keyValid = false;
        _keyValidationError = outError;
        unlock();
        return false;
    }

    http.addHeader("X-Arena-Api-Key", key);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "BotSports-Arena-Display/" FIRMWARE_VERSION);
    http.setTimeout(2500);

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
        _keyValid = true;
        _keyValidationError = "";
        unlock();

        Serial.printf("[API] API Key verified successfully! Arena: '%s' (ID %d)\n",
                      tempData.arena_name.c_str(), tempData.arena_id);
        return true;
    } else {
        http.end();
        if (httpCode == 401 || httpCode == 403) {
            outError = "Invalid or unauthorized Arena API Key (HTTP " + String(httpCode) + ")";
        } else if (httpCode == 404) {
            outError = "Arena not found on server (HTTP 404)";
        } else if (httpCode < 0) {
            outError = "Connection timeout reaching server";
        } else {
            outError = "Server returned HTTP " + String(httpCode);
        }

        lock();
        _keyValid = false;
        _keyValidationError = outError;
        unlock();

        Serial.printf("[API] API Key validation failed: %s\n", outError.c_str());
        return false;
    }
}

void BotSportsAPIClient::disconnectStream() {
    lock();
    _streamConnected = false;
    if (_stream) {
        _stream->stop();
        _stream = nullptr;
    }
    _currentSseEvent = "";
    _currentSseData = "";
    _streamLineBuffer = "";
    unlock();
}

bool BotSportsAPIClient::connectStream() {
    if (configMgr.settings.api_key.length() == 0 || !_keyValid) {
        disconnectStream();
        return false;
    }

    disconnectStream();

    String url = configMgr.settings.api_url;
    bool isHttps = url.startsWith("https://");
    String host;
    uint16_t port = isHttps ? 443 : 80;
    String basePath = "";

    int schemeEnd = url.indexOf("://");
    if (schemeEnd > 0) {
        String noScheme = url.substring(schemeEnd + 3);
        int pathSlash = noScheme.indexOf('/');
        if (pathSlash > 0) {
            host = noScheme.substring(0, pathSlash);
            basePath = noScheme.substring(pathSlash);
        } else {
            host = noScheme;
        }
    } else {
        host = url;
    }

    int colonIdx = host.indexOf(':');
    if (colonIdx > 0) {
        port = host.substring(colonIdx + 1).toInt();
        host = host.substring(0, colonIdx);
    }

    if (_data.arena_id <= 0) {
        pollStatus();
    }

    String streamPath;
    if (_data.arena_id > 0) {
        streamPath = basePath + "/app/arenas/" + String(_data.arena_id) + "/stream";
    } else {
        streamPath = basePath + "/app/arenas/device/stream";
    }
    Serial.printf("[SSE] Connecting to %s:%d%s ...\n", host.c_str(), port, streamPath.c_str());

    if (isHttps) {
        _streamClientSecure.setInsecure();
        _streamClientSecure.setTimeout(5000);
        _streamClientSecure.setNoDelay(true);
        if (!_streamClientSecure.connect(host.c_str(), port)) {
            Serial.println("[SSE] HTTPS socket connection failed!");
            disconnectStream();
            return false;
        }
        _stream = &_streamClientSecure;
    } else {
        _streamClient.setTimeout(5000);
        _streamClient.setNoDelay(true);
        if (!_streamClient.connect(host.c_str(), port)) {
            Serial.println("[SSE] HTTP socket connection failed!");
            disconnectStream();
            return false;
        }
        _stream = &_streamClient;
    }

    // Send HTTP GET for SSE (note: removed "Connection: keep-alive" for HTTP/2 & modern ingress proxy compatibility)
    _stream->print(String("GET ") + streamPath + " HTTP/1.1\r\n");
    _stream->print(String("Host: ") + host + "\r\n");
    if (configMgr.settings.api_key.length() > 0) {
        _stream->print(String("X-Arena-Api-Key: ") + configMgr.settings.api_key + "\r\n");
    }
    _stream->print("Accept: text/event-stream\r\n");
    _stream->print("Cache-Control: no-cache\r\n");
    _stream->print("User-Agent: BotSports-Arena-Display/" FIRMWARE_VERSION "\r\n");
    _stream->print("\r\n");

    // Read HTTP response status line
    unsigned long timeout = millis() + 4000;
    String statusLine = "";
    while (_stream->connected() && millis() < timeout) {
        if (_stream->available()) {
            statusLine = _stream->readStringUntil('\n');
            statusLine.trim();
            if (statusLine.length() > 0) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (statusLine.indexOf(" 200 ") < 0 && statusLine.indexOf(" 200") < 0) {
        Serial.printf("[SSE] Handshake failed! Status: %s\n", statusLine.c_str());
        if (statusLine.indexOf(" 401 ") >= 0 || statusLine.indexOf(" 403 ") >= 0) {
            lock();
            _keyValid = false;
            _keyValidationError = "Invalid or inactive Arena API Key";
            unlock();
        }
        disconnectStream();
        return false;
    }

    // Read headers until empty line
    timeout = millis() + 3000;
    while (_stream->connected() && millis() < timeout) {
        if (_stream->available()) {
            String headerLine = _stream->readStringUntil('\n');
            headerLine.trim();
            if (headerLine.length() == 0) {
                break; // Handshake complete!
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    lock();
    _streamConnected = true;
    _lastStreamDataReceived = millis();
    unlock();

    Serial.println("[SSE] Real-Time SSE Stream established successfully!");

    // Fetch initial status snapshot if not already loaded
    if (!_data.valid || _data.arena_id <= 0) {
        pollStatus();
    }
    return true;
}

void BotSportsAPIClient::handleStream() {
    if (configMgr.settings.api_key.length() == 0 || !_keyValid) {
        disconnectStream();
        return;
    }

    // Auto-reconnect if dropped (timeout after 45s without keepalive ping or data)
    unsigned long silenceLimit = 45000;
    if (!_streamConnected || !_stream || !_stream->connected() || (millis() - _lastStreamDataReceived > silenceLimit)) {
        if (!_streamConnected) {
            Serial.printf("[%lu ms] [SSE] Reconnect: stream disconnected flag\n", millis());
        } else if (!_stream) {
            Serial.printf("[%lu ms] [SSE] Reconnect: stream pointer is null\n", millis());
        } else if (!_stream->connected()) {
            Serial.printf("[%lu ms] [SSE] Reconnect: socket closed by peer\n", millis());
        } else {
            Serial.printf("[%lu ms] [SSE] Reconnect: silence timeout (%lu ms since last data, limit %lu ms)\n",
                          millis(), millis() - _lastStreamDataReceived, silenceLimit);
        }

        disconnectStream();
        if (millis() - _lastStreamConnectAttempt >= 3000) {
            _lastStreamConnectAttempt = millis();
            connectStream();
        }
        return;
    }

    // Read incoming SSE stream data character-by-character (strictly non-blocking, zero truncation!)
    while (_stream && _stream->connected() && _stream->available()) {
        char c = (char)_stream->read();
        _lastStreamDataReceived = millis();
        if (c == '\r') continue;
        if (c == '\n') {
            String line = _streamLineBuffer;
            _streamLineBuffer = "";
            line.trim();

            // Handle SSE comment / keepalive ping (e.g. ": ping")
            if (line.startsWith(":")) {
                Serial.printf("[%lu ms] [SSE] Keepalive ping received\n", millis());
                continue;
            }

            if (line.startsWith("event:")) {
                _currentSseEvent = line.substring(6);
                _currentSseEvent.trim();
            } else if (line.startsWith("data:")) {
                String dataPart = line.substring(5);
                dataPart.trim();
                if (_currentSseData.length() > 0) {
                    _currentSseData += "\n";
                }
                _currentSseData += dataPart;
            } else if (line.length() == 0) {
                // End of SSE block (double newline)
                if (_currentSseEvent.length() > 0 || _currentSseData.length() > 0) {
                    _dispatchStreamEvent(_currentSseEvent, _currentSseData);
                    _currentSseEvent = "";
                    _currentSseData = "";
                }
            }
        } else {
            _streamLineBuffer += c;
        }
    }

    // Check for deferred status polling (structural changes), ONLY when the SSE stream is idle/drained
    // and NEVER during live count-in or running match!
    if (_pendingStatusPoll && _stream && (!_stream->available())) {
        lock();
        ClockState curClock = _data.clock_state;
        unlock();

        if (curClock != CLOCK_RUNNING && curClock != CLOCK_COUNT_IN) {
            if ((millis() - _pendingPollTime >= 500) && (millis() - _lastPollTime >= 4000)) {
                _pendingStatusPoll = false;
                Serial.printf("[%lu ms] [API] Executing deferred status snapshot...\n", millis());
                pollStatus();
                arenaSync.broadcastState(true);
            }
        } else {
            // Suppress during live match/count-in!
            _pendingStatusPoll = false;
        }
    }

    // Check if decision celebration hold window (15s) has expired
    if (_decisionHoldUntil > 0 && millis() >= _decisionHoldUntil) {
        Serial.printf("[%lu ms] [API] Decision hold window ended (15s elapsed). Polling next match status.\n", millis());
        lock();
        _decisionHoldUntil = 0;
        unlock();
        pollStatus();
        arenaSync.broadcastState(true);
    }
}

void BotSportsAPIClient::_dispatchStreamEvent(const String& eventType, const String& dataStr) {
    Serial.printf("[%lu ms] [SSE] Event: '%s' | Payload: %s\n", millis(), eventType.c_str(), dataStr.c_str());

    if (eventType == "connected") {
        Serial.printf("[%lu ms] [SSE] Connected handshake acknowledged.\n", millis());
        if (_data.arena_id <= 0 || (millis() - _lastPollTime > 15000)) {
            _pendingStatusPoll = true;
            _pendingPollTime = millis();
        }
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, dataStr);
    if (err) {
        Serial.printf("[%lu ms] [SSE] JSON parse error: %s\n", millis(), err.c_str());
        return;
    }

    JsonObject payloadObj = doc["data"].is<JsonObject>() ? doc["data"].as<JsonObject>() : doc.as<JsonObject>();

    if (eventType == "clock_tick" || eventType == "match.clock_updated" || eventType == "match.count_in") {
        JsonObject clock;
        if (payloadObj["clock"].is<JsonObject>()) {
            clock = payloadObj["clock"].as<JsonObject>();
        } else if (doc["data"]["clock"].is<JsonObject>()) {
            clock = doc["data"]["clock"].as<JsonObject>();
        } else if (doc["clock"].is<JsonObject>()) {
            clock = doc["clock"].as<JsonObject>();
        } else if (payloadObj.containsKey("remaining_seconds") || payloadObj.containsKey("status") || payloadObj.containsKey("count_in_seconds")) {
            clock = payloadObj;
        }

        if (!clock.isNull()) {
            lock();
            _data.has_clock = true;
            String statusStr = clock["status"] | "stopped";
            if (statusStr.equalsIgnoreCase("running")) {
                _data.clock_state = CLOCK_RUNNING;
            } else if (statusStr.equalsIgnoreCase("count_in") || eventType == "match.count_in") {
                _data.clock_state = CLOCK_COUNT_IN;
            } else if (statusStr.equalsIgnoreCase("paused")) {
                _data.clock_state = CLOCK_PAUSED;
            } else if (statusStr.equalsIgnoreCase("expired")) {
                _data.clock_state = CLOCK_EXPIRED;
            } else {
                _data.clock_state = CLOCK_STOPPED;
            }

            if (clock["duration_seconds"].is<float>()) {
                _data.clock_duration_sec = clock["duration_seconds"].as<float>();
            }
            if (clock["hazards_seconds"].is<float>()) {
                _data.clock_hazards_sec = clock["hazards_seconds"].as<float>();
            }
            _data.is_hazards_active = clock["is_hazards_active"] | false;

            if (clock["remaining_seconds"].is<float>()) {
                _data.clock_remaining_sec = clock["remaining_seconds"].as<float>();
            }
            if (clock.containsKey("count_in_seconds")) {
                _data.clock_count_in_seconds = clock["count_in_seconds"].as<int>();
            }
            _data.clock_pause_reason = clock["pause_reason"] | "";
            _data.clock_sync_millis = millis();
            unlock();

            Serial.printf("[%lu ms] [Clock] State: %s, CountIn: %d, Rem: %.2f, Haz: %d\n",
                          millis(), statusStr.c_str(), _data.clock_count_in_seconds, _data.clock_remaining_sec, (int)_data.is_hazards_active);

            // If count-in, set display screen to SCREEN_COUNT_IN
            if (_data.clock_state == CLOCK_COUNT_IN) {
                displayMgr.setScreen(SCREEN_COUNT_IN);
            } else if (_data.clock_state == CLOCK_RUNNING && displayMgr.getScreen() == SCREEN_COUNT_IN) {
                displayMgr.setScreen(SCREEN_ACTIVE_MATCH);
            }

            // Broadcast immediately to linked slave displays!
            arenaSync.broadcastState(true);
        }
        return;
    }

    // Fast in-memory readiness updates (zero HTTP blocking!)
    if (eventType == "bot_ready" || eventType == "match.bot_ready" ||
        eventType == "match.pit_ready" || eventType == "bot.pit_ready" ||
        eventType == "match.arena_checked_in" || eventType == "bot.arena_checkin" ||
        (eventType == "match_updated" && (payloadObj.containsKey("pit_ready") || payloadObj.containsKey("arena_checked_in") || payloadObj.containsKey("bot_ready") || payloadObj.containsKey("ready")))) {

        int matchId = payloadObj["match_id"] | doc["match_id"] | 0;
        int botId = payloadObj["bot_id"] | doc["bot_id"] | 0;

        lock();
        // Check current match
        if (matchId == 0 || matchId == _data.current_match.id) {
            bool isRed = (botId > 0 && botId == _data.current_match.red.bot_id);
            bool isBlue = (botId > 0 && botId == _data.current_match.blue.bot_id);
            if (!isRed && !isBlue && (payloadObj.containsKey("corner") || doc.containsKey("corner"))) {
                String c = payloadObj["corner"] | doc["corner"] | "";
                c.toLowerCase();
                if (c == "red") isRed = true;
                if (c == "blue") isBlue = true;
            }

            if (payloadObj.containsKey("pit_ready") || doc.containsKey("pit_ready")) {
                bool v = payloadObj.containsKey("pit_ready") ? (bool)payloadObj["pit_ready"] : (bool)doc["pit_ready"];
                if (isRed) _data.current_match.red.pit_ready = v;
                if (isBlue) _data.current_match.blue.pit_ready = v;
            }
            if (payloadObj.containsKey("arena_checked_in") || doc.containsKey("arena_checked_in")) {
                bool v = payloadObj.containsKey("arena_checked_in") ? (bool)payloadObj["arena_checked_in"] : (bool)doc["arena_checked_in"];
                if (isRed) _data.current_match.red.arena_checked_in = v;
                if (isBlue) _data.current_match.blue.arena_checked_in = v;
            }
            if (payloadObj.containsKey("bot_ready") || payloadObj.containsKey("ready") || payloadObj.containsKey("match_ready") ||
                doc.containsKey("bot_ready") || doc.containsKey("ready") || doc.containsKey("match_ready")) {
                bool v = payloadObj["bot_ready"] | payloadObj["ready"] | payloadObj["match_ready"] |
                         doc["bot_ready"] | doc["ready"] | doc["match_ready"];
                if (isRed) _data.current_match.red.ready = v;
                if (isBlue) _data.current_match.blue.ready = v;
            }
        }

        // Check upcoming match
        if (matchId == 0 || matchId == _data.upcoming_match.id) {
            bool isRed = (botId > 0 && botId == _data.upcoming_match.red.bot_id);
            bool isBlue = (botId > 0 && botId == _data.upcoming_match.blue.bot_id);
            if (!isRed && !isBlue && (payloadObj.containsKey("corner") || doc.containsKey("corner"))) {
                String c = payloadObj["corner"] | doc["corner"] | "";
                c.toLowerCase();
                if (c == "red") isRed = true;
                if (c == "blue") isBlue = true;
            }

            if (payloadObj.containsKey("pit_ready") || doc.containsKey("pit_ready")) {
                bool v = payloadObj.containsKey("pit_ready") ? (bool)payloadObj["pit_ready"] : (bool)doc["pit_ready"];
                if (isRed) _data.upcoming_match.red.pit_ready = v;
                if (isBlue) _data.upcoming_match.blue.pit_ready = v;
            }
            if (payloadObj.containsKey("arena_checked_in") || doc.containsKey("arena_checked_in")) {
                bool v = payloadObj.containsKey("arena_checked_in") ? (bool)payloadObj["arena_checked_in"] : (bool)doc["arena_checked_in"];
                if (isRed) _data.upcoming_match.red.arena_checked_in = v;
                if (isBlue) _data.upcoming_match.blue.arena_checked_in = v;
            }
            if (payloadObj.containsKey("bot_ready") || payloadObj.containsKey("ready") || payloadObj.containsKey("match_ready") ||
                doc.containsKey("bot_ready") || doc.containsKey("ready") || doc.containsKey("match_ready")) {
                bool v = payloadObj["bot_ready"] | payloadObj["ready"] | payloadObj["match_ready"] |
                         doc["bot_ready"] | doc["ready"] | doc["match_ready"];
                if (isRed) _data.upcoming_match.red.ready = v;
                if (isBlue) _data.upcoming_match.blue.ready = v;
            }
        }
        unlock();

        Serial.printf("[%lu ms] [Readiness] Instant update: bot_id=%d, match_id=%d | Red(P:%d,G:%d,M:%d) Blue(P:%d,G:%d,M:%d)\n",
                      millis(), botId, matchId,
                      (int)_data.current_match.red.pit_ready, (int)_data.current_match.red.arena_checked_in, (int)_data.current_match.red.ready,
                      (int)_data.current_match.blue.pit_ready, (int)_data.current_match.blue.arena_checked_in, (int)_data.current_match.blue.ready);
        arenaSync.broadcastState(true);
        return;
    }

    // Match outcome / decision / completion
    String stCheck = payloadObj["status"] | doc["status"] | "";
    if (eventType == "match_completed" || (eventType == "match.status_changed" && (stCheck == "Decided" || stCheck == "Completed"))) {
        int mId = payloadObj["match_id"] | doc["match_id"] | 0;
        Serial.printf("[%lu ms] [SSE] Match completed / decided: match_id=%d\n", millis(), mId);
        _handleMatchCompleted(mId, doc);
        return;
    }

    // Match status changed (Ongoing, Finished, etc.)
    if (eventType == "match.status_changed") {
        String newStatus = payloadObj["status"] | doc["status"] | "";
        int matchId = payloadObj["match_id"] | doc["match_id"] | 0;
        if (newStatus.length() > 0 && (matchId == 0 || matchId == _data.current_match.id)) {
            lock();
            _data.current_match.status = newStatus;
            unlock();
            Serial.printf("[%lu ms] [SSE] Fast match status change: '%s'\n", millis(), newStatus.c_str());
            arenaSync.broadcastState(true);
            return;
        }
    }

    // Ignore redundant match_updated events to avoid blocking pollStatus
    if (eventType == "match_updated") {
        return;
    }

    // Structural tournament changes that require full status poll
    if (eventType == "match.participants_changed" || eventType == "queue_reordered" ||
        eventType == "match_staged" || eventType == "round_scheduled" ||
        eventType == "match_created" || eventType == "match.created" ||
        eventType == "match_deleted" || eventType == "match.deleted") {
        if (!isDecisionHoldActive()) {
            Serial.printf("[%lu ms] [SSE] Structural update trigger '%s' -> queueing deferred status poll\n", millis(), eventType.c_str());
            _pendingStatusPoll = true;
            _pendingPollTime = millis();
        }
        return;
    }
}

bool BotSportsAPIClient::pollStatus() {
    if (configMgr.settings.api_key.length() == 0) {
        lock();
        _data.valid = false;
        _data.error_message = "No API Key configured";
        unlock();
        return false;
    }

    _lastPollTime = millis();
    unsigned long pollStart = millis();

    _secureClient.setInsecure(); // Allow HTTPS without bundling complete root CA certs
    _secureClient.setTimeout(5000);

    HTTPClient http;
    String url = configMgr.settings.api_url + "/app/arenas/device/status";

    String corner = configMgr.settings.corner;
    if (corner == "red" || corner == "blue") {
        url += "?corner=" + corner;
    }

    if (configMgr.settings.tournament_id > 0) {
        url += (url.indexOf('?') >= 0 ? "&" : "?") + String("tournament_id=") + String(configMgr.settings.tournament_id);
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
    http.setTimeout(2500);

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
        if (_decisionHoldUntil > 0 && millis() < _decisionHoldUntil) {
            tempData.current_match = _data.current_match;
            tempData.has_current_match = true;
        }
        _data = tempData;
        _keyValid = true;
        _keyValidationError = "";
        unlock();
        Serial.printf("[%lu ms] [Poll] Completed in %lu ms (HTTP %d)\n", millis(), millis() - pollStart, httpCode);
        return true;
    } else {
        String errMsg;
        if (httpCode == 401 || httpCode == 403) {
            errMsg = "Invalid Arena Key";
            lock();
            _keyValid = false;
            _keyValidationError = errMsg;
            unlock();
        } else if (httpCode == 404) {
            errMsg = "Arena Not Found";
        } else if (httpCode < 0) {
            errMsg = "Network Timeout";
        } else {
            errMsg = "HTTP " + String(httpCode);
        }
        Serial.printf("[%lu ms] [Poll] Status request failed in %lu ms: %s (Code %d)\n", millis(), millis() - pollStart, errMsg.c_str(), httpCode);
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
    target.current_tournament_id = doc["current_tournament_id"] | 0;
    target.current_tournament_name = doc["current_tournament_name"].as<String>();

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
            target.current_match.red.bot_id = red["bot_id"] | 0;
            target.current_match.red.name = red["bot_name"] | "Red Bot";
            target.current_match.red.team = red["team_name"] | "";
            target.current_match.red.pit_ready = red["pit_ready"] | false;
            target.current_match.red.arena_checked_in = red["arena_checked_in"] | false;
            target.current_match.red.ready = red["match_ready"] | red["bot_ready"] | red["ready"] | false;
            target.current_match.red.score = red["score"] | 0.0f;
            target.current_match.red.result_type = red["result_type"] | "";
        } else {
            target.current_match.red.bot_id = 0;
            target.current_match.red.name = "Red";
            target.current_match.red.team = "";
            target.current_match.red.pit_ready = false;
            target.current_match.red.arena_checked_in = false;
            target.current_match.red.ready = false;
            target.current_match.red.score = 0.0f;
            target.current_match.red.result_type = "";
        }

        // Blue corner
        JsonObject blue = doc["blue_corner"];
        if (!blue.isNull()) {
            target.current_match.blue.bot_id = blue["bot_id"] | 0;
            target.current_match.blue.name = blue["bot_name"] | "Blue Bot";
            target.current_match.blue.team = blue["team_name"] | "";
            target.current_match.blue.pit_ready = blue["pit_ready"] | false;
            target.current_match.blue.arena_checked_in = blue["arena_checked_in"] | false;
            target.current_match.blue.ready = blue["match_ready"] | blue["bot_ready"] | blue["ready"] | false;
            target.current_match.blue.score = blue["score"] | 0.0f;
            target.current_match.blue.result_type = blue["result_type"] | "";
        } else {
            target.current_match.blue.bot_id = 0;
            target.current_match.blue.name = "Blue";
            target.current_match.blue.team = "";
            target.current_match.blue.pit_ready = false;
            target.current_match.blue.arena_checked_in = false;
            target.current_match.blue.ready = false;
            target.current_match.blue.score = 0.0f;
            target.current_match.blue.result_type = "";
        }

        // Determine winner and outcome type if Decided or Completed
        String st = target.current_match.status;
        st.toLowerCase();
        if (st == "decided" || st == "completed") {
            bool redWin = (target.current_match.red.score == 1.0f) || target.current_match.red.result_type.startsWith("Win");
            bool blueWin = (target.current_match.blue.score == 1.0f) || target.current_match.blue.result_type.startsWith("Win");
            if (redWin && !blueWin) {
                target.current_match.winner_corner = "red";
                target.current_match.winner_name = target.current_match.red.name;
            } else if (blueWin && !redWin) {
                target.current_match.winner_corner = "blue";
                target.current_match.winner_name = target.current_match.blue.name;
            } else {
                target.current_match.winner_corner = "tied";
                target.current_match.winner_name = "Draw / Tied";
            }

            String rt = (target.current_match.winner_corner == "red") ? target.current_match.red.result_type : target.current_match.blue.result_type;
            rt.toLowerCase();
            if (rt.indexOf("knockout") >= 0 || rt.indexOf("ko") >= 0) {
                target.current_match.outcome_type = "ko";
            } else if (rt.indexOf("decision") >= 0) {
                target.current_match.outcome_type = "decision";
            } else if (rt.indexOf("forfeit") >= 0 || rt.indexOf("disqualif") >= 0) {
                target.current_match.outcome_type = "forfeit";
            } else {
                target.current_match.outcome_type = "decision";
            }
        } else {
            target.current_match.winner_corner = "";
            target.current_match.winner_name = "";
            target.current_match.outcome_type = "";
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
        } else if (statusStr.equalsIgnoreCase("count_in")) {
            target.clock_state = CLOCK_COUNT_IN;
        } else if (statusStr.equalsIgnoreCase("paused")) {
            target.clock_state = CLOCK_PAUSED;
        } else if (statusStr.equalsIgnoreCase("expired")) {
            target.clock_state = CLOCK_EXPIRED;
        } else {
            target.clock_state = CLOCK_STOPPED;
        }

        target.clock_duration_sec = clock["duration_seconds"] | 180.0f;
        target.clock_hazards_sec = clock["hazards_seconds"] | configMgr.settings.hazards_seconds;
        target.is_hazards_active = clock["is_hazards_active"] | false;
        target.clock_remaining_sec = clock["remaining_seconds"] | target.clock_duration_sec;
        target.clock_count_in_seconds = clock["count_in_seconds"] | -1;
        target.clock_pause_reason = clock["pause_reason"] | "";
        target.clock_sync_millis = millis();
    } else {
        target.has_clock = false;
        target.clock_pause_reason = "";
        target.clock_count_in_seconds = -1;
        target.is_hazards_active = false;
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
            target.upcoming_match.red.bot_id = c0["bot_id"] | 0;
            target.upcoming_match.red.name = c0["bot_name"] | "Red Bot";
            target.upcoming_match.red.team = c0["team_name"] | "";
        }
        if (combatants.size() > 1) {
            JsonObject c1 = combatants[1];
            target.upcoming_match.blue.bot_id = c1["bot_id"] | 0;
            target.upcoming_match.blue.name = c1["bot_name"] | "Blue Bot";
            target.upcoming_match.blue.team = c1["team_name"] | "";
        }
    } else {
        target.has_upcoming_match = false;
    }
}

bool BotSportsAPIClient::isHazardsActive() const {
    lock();
    bool active = _data.is_hazards_active;
    float hSec = _data.clock_hazards_sec;
    ClockState st = _data.clock_state;
    unlock();
    if (active) return true;
    if (st == CLOCK_RUNNING) {
        float rem = getInterpolatedRemainingSeconds();
        if (rem <= hSec && rem > 0.0f) return true;
    }
    return false;
}

int BotSportsAPIClient::getInterpolatedCountInSeconds() const {
    lock();
    if (!_data.has_clock || _data.clock_state != CLOCK_COUNT_IN) {
        unlock();
        return -1;
    }
    int countIn = _data.clock_count_in_seconds;
    unsigned long syncMillis = _data.clock_sync_millis;
    unlock();

    float elapsed = (millis() - syncMillis) / 1000.0f;
    if (countIn >= 0) {
        int current = countIn - (int)elapsed;
        if (current >= 0) return current;
        return -1;
    }

    if (elapsed < 1.0f) return 3;
    if (elapsed < 2.0f) return 2;
    if (elapsed < 3.0f) return 1;
    if (elapsed < 4.0f) return 0; // 0 = FIGHT!
    return -1;
}

float BotSportsAPIClient::getInterpolatedRemainingSeconds() const {
    lock();
    if (!_data.has_clock) {
        unlock();
        return 0.0f;
    }

    if (_data.clock_state == CLOCK_COUNT_IN) {
        float elapsed = (millis() - _data.clock_sync_millis) / 1000.0f;
        float duration = (_data.clock_duration_sec > 0.0f) ? _data.clock_duration_sec : _data.clock_remaining_sec;
        if (elapsed < 3.0f) {
            unlock();
            return duration;
        }
        float combatElapsed = elapsed - 3.0f;
        float remaining = duration - combatElapsed;
        unlock();
        return (remaining > 0.0f) ? remaining : 0.0f;
    }

    if (_data.clock_state == CLOCK_RUNNING) {
        float elapsedSinceSync = (millis() - _data.clock_sync_millis) / 1000.0f;
        float remaining = _data.clock_remaining_sec - elapsedSinceSync;
        unlock();
        return (remaining > 0.0f) ? remaining : 0.0f;
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
    unsigned long syncTime = _data.clock_sync_millis;
    unlock();

    if (st == CLOCK_COUNT_IN) {
        float elapsed = (millis() - syncTime) / 1000.0f;
        if (elapsed >= 4.0f) {
            return CLOCK_RUNNING;
        }
        return CLOCK_COUNT_IN;
    }

    if (st == CLOCK_RUNNING && getInterpolatedRemainingSeconds() <= 0.0f) {
        return CLOCK_EXPIRED;
    }

    return st;
}

bool BotSportsAPIClient::fetchMatchOutcome(int matchId) {
    if (matchId <= 0 || configMgr.settings.api_key.length() == 0) return false;
    _secureClient.setInsecure();
    _secureClient.setTimeout(5000);
    HTTPClient http;
    String url = configMgr.settings.api_url + "/app/matches/" + String(matchId);
    if (!http.begin(_secureClient, url)) return false;
    http.addHeader("X-Arena-Api-Key", configMgr.settings.api_key);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "BotSports-Arena-Display/" FIRMWARE_VERSION);
    http.setTimeout(1500);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        JsonDocument matchDoc;
        DeserializationError err = deserializeJson(matchDoc, payload);
        if (!err) {
            lock();
            _data.current_match.status = "Decided";
            JsonArray participants = matchDoc["match_participants"];
            String winCorner = "";
            String winName = "";
            String outType = "decision";
            for (JsonObject p : participants) {
                float sc = p["score"] | 0.0f;
                String resType = p["result_type"] | "";
                String corner = p["corner"] | "";
                corner.toLowerCase();
                if (sc == 1.0f || resType.startsWith("Win")) {
                    winCorner = corner;
                    winName = p["bot_name"] | "";
                    resType.toLowerCase();
                    if (resType.indexOf("knockout") >= 0 || resType.indexOf("ko") >= 0) {
                        outType = "ko";
                    } else if (resType.indexOf("forfeit") >= 0 || resType.indexOf("disqualif") >= 0) {
                        outType = "forfeit";
                    } else {
                        outType = "decision";
                    }
                    break;
                }
            }
            if (winCorner.length() == 0) {
                winCorner = "tied";
                winName = "Draw / Tied";
            }
            _data.current_match.winner_corner = winCorner;
            _data.current_match.winner_name = winName;
            _data.current_match.outcome_type = outType;
            unlock();
            return true;
        }
    } else {
        http.end();
    }
    return false;
}

void BotSportsAPIClient::_handleMatchCompleted(int matchId, JsonDocument& doc) {
    lock();
    _data.current_match.status = "Decided";
    if (doc.containsKey("winner_corner")) {
        _data.current_match.winner_corner = doc["winner_corner"].as<String>();
    }
    if (doc.containsKey("winner_name")) {
        _data.current_match.winner_name = doc["winner_name"].as<String>();
    }
    if (doc.containsKey("outcome_type")) {
        _data.current_match.outcome_type = doc["outcome_type"].as<String>();
    }
    String curWinName = _data.current_match.winner_name;
    unlock();

    if (curWinName.length() == 0 && matchId > 0) {
        fetchMatchOutcome(matchId);
    }

    lock();
    _decisionHoldUntil = millis() + 15000; // Hold for 15 seconds!
    unlock();

    Serial.printf("[API] Match %d outcome recorded: Winner='%s' (%s) Outcome='%s'. Holding display for 15s.\n",
                  matchId, _data.current_match.winner_name.c_str(),
                  _data.current_match.winner_corner.c_str(),
                  _data.current_match.outcome_type.c_str());

    arenaSync.broadcastState(true);
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
    _data.current_match.status = (pkt.match_status == 1) ? "Ongoing" : ((pkt.match_status == 2) ? "Finished" : ((pkt.match_status == 3) ? "Decided" : "Scheduled"));
    _data.current_match.red.name = String(pkt.red_name);
    _data.current_match.red.pit_ready = (pkt.red_pit_ready != 0);
    _data.current_match.red.arena_checked_in = (pkt.red_checked_in != 0);
    _data.current_match.red.ready = (pkt.red_match_ready != 0);
    _data.current_match.blue.name = String(pkt.blue_name);
    _data.current_match.blue.pit_ready = (pkt.blue_pit_ready != 0);
    _data.current_match.blue.arena_checked_in = (pkt.blue_checked_in != 0);
    _data.current_match.blue.ready = (pkt.blue_match_ready != 0);

    _data.current_match.winner_corner = (pkt.winner_corner == 1) ? "red" : ((pkt.winner_corner == 2) ? "blue" : ((pkt.winner_corner == 3) ? "tied" : ""));
    _data.current_match.winner_name = String(pkt.winner_name);
    _data.current_match.outcome_type = (pkt.outcome_type == 1) ? "ko" : ((pkt.outcome_type == 2) ? "decision" : ((pkt.outcome_type == 3) ? "forfeit" : ""));

    _data.has_upcoming_match = (pkt.has_upcoming != 0);
    _data.upcoming_match.match_number = pkt.upcoming_match_num;
    _data.upcoming_match.round_number = pkt.upcoming_round_num;
    _data.upcoming_match.red.name = String(pkt.upcoming_red);
    _data.upcoming_match.blue.name = String(pkt.upcoming_blue);

    _data.has_clock = true;
    _data.clock_state = (ClockState)pkt.clock_state;
    _data.clock_duration_sec = pkt.duration_seconds;
    _data.clock_hazards_sec = pkt.hazards_seconds;
    _data.is_hazards_active = (pkt.is_hazards_active != 0);
    _data.clock_remaining_sec = pkt.remaining_seconds;
    _data.clock_count_in_seconds = pkt.count_in_seconds;
    _data.clock_pause_reason = String(pkt.pause_reason);
    _data.clock_sync_millis = millis();
    unlock();
}
