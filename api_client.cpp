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
    _data.clock_pause_reason = "";
    _lastPollTime = 0;

    _keyValid = false;
    _keyValidationError = "";
    _streamConnected = false;
    _lastStreamConnectAttempt = 0;
    _lastStreamDataReceived = 0;
    _stream = nullptr;
    _currentSseEvent = "";
    _currentSseData = "";
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

    String streamPath = basePath + "/app/arenas/device/stream";
    Serial.printf("[SSE] Connecting to %s:%d%s ...\n", host.c_str(), port, streamPath.c_str());

    if (isHttps) {
        _streamClientSecure.setInsecure();
        _streamClientSecure.setTimeout(5);
        if (!_streamClientSecure.connect(host.c_str(), port)) {
            Serial.println("[SSE] HTTPS socket connection failed!");
            disconnectStream();
            return false;
        }
        _stream = &_streamClientSecure;
    } else {
        _streamClient.setTimeout(5);
        if (!_streamClient.connect(host.c_str(), port)) {
            Serial.println("[SSE] HTTP socket connection failed!");
            disconnectStream();
            return false;
        }
        _stream = &_streamClient;
    }

    // Send HTTP GET for SSE
    _stream->print(String("GET ") + streamPath + " HTTP/1.1\r\n");
    _stream->print(String("Host: ") + host + "\r\n");
    _stream->print(String("X-Arena-Api-Key: ") + configMgr.settings.api_key + "\r\n");
    _stream->print("Accept: text/event-stream\r\n");
    _stream->print("Cache-Control: no-cache\r\n");
    _stream->print("Connection: keep-alive\r\n");
    _stream->print("User-Agent: BotSports-Arena-Display/" FIRMWARE_VERSION "\r\n");
    _stream->print("\r\n");

    // Read HTTP response status line
    unsigned long timeout = millis() + 4000;
    String statusLine = "";
    while (_stream->connected() && millis() < timeout) {
        if (_stream->available()) {
            statusLine = _stream->readStringUntil('\n');
            statusLine.trim();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (statusLine.indexOf(" 200 ") < 0) {
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

    // Fetch initial complete status snapshot immediately
    pollStatus();
    return true;
}

void BotSportsAPIClient::handleStream() {
    if (configMgr.settings.api_key.length() == 0 || !_keyValid) {
        disconnectStream();
        return;
    }

    // Auto-reconnect if dropped
    if (!_streamConnected || !_stream || !_stream->connected()) {
        disconnectStream();
        if (millis() - _lastStreamConnectAttempt >= 3000) {
            _lastStreamConnectAttempt = millis();
            connectStream();
        }
        return;
    }

    // Read incoming SSE stream data
    while (_stream && _stream->connected() && _stream->available()) {
        String line = _stream->readStringUntil('\n');
        line.trim();
        _lastStreamDataReceived = millis();

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
            // End of SSE block
            if (_currentSseEvent.length() > 0 || _currentSseData.length() > 0) {
                _dispatchStreamEvent(_currentSseEvent, _currentSseData);
                _currentSseEvent = "";
                _currentSseData = "";
            }
        }
    }
}

void BotSportsAPIClient::_dispatchStreamEvent(const String& eventType, const String& dataStr) {
    Serial.printf("[SSE] Stream Event: '%s'\n", eventType.c_str());

    if (eventType == "connected") {
        Serial.println("[SSE] Connected handshake acknowledged. Refreshing status snapshot...");
        pollStatus();
        return;
    }

    if (eventType == "clock_tick" || eventType == "match.clock_updated") {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, dataStr);
        if (!err) {
            JsonObject clock;
            if (doc["data"]["clock"].is<JsonObject>()) {
                clock = doc["data"]["clock"].as<JsonObject>();
            } else if (doc["clock"].is<JsonObject>()) {
                clock = doc["clock"].as<JsonObject>();
            }
            if (!clock.isNull()) {
                lock();
                _data.has_clock = true;
                String statusStr = clock["status"] | "stopped";
                if (statusStr.equalsIgnoreCase("running")) {
                    _data.clock_state = CLOCK_RUNNING;
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
                if (clock["remaining_seconds"].is<float>()) {
                    _data.clock_remaining_sec = clock["remaining_seconds"].as<float>();
                }
                _data.clock_pause_reason = clock["pause_reason"] | "";
                _data.clock_sync_millis = millis();
                unlock();

                // Broadcast immediately to linked slave displays!
                arenaSync.broadcastState(true);
            }
        }
        return;
    }

    if (eventType == "match.status_changed" || eventType == "match_updated" ||
        eventType == "match.participants_changed" || eventType == "queue_reordered" ||
        eventType == "round_scheduled" || eventType == "bot_ready" || eventType == "match.bot_ready") {
        Serial.printf("[SSE] State update trigger '%s' -> polling fresh arena status\n", eventType.c_str());
        if (pollStatus()) {
            arenaSync.broadcastState(true);
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
    http.setTimeout(1500);

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
        target.clock_pause_reason = clock["pause_reason"] | "";
        target.clock_sync_millis = millis();
    } else {
        target.has_clock = false;
        target.clock_pause_reason = "";
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
    _data.clock_pause_reason = String(pkt.pause_reason);
    _data.clock_sync_millis = millis();
    unlock();
}
