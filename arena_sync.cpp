#include "arena_sync.h"
#include "config_manager.h"
#include "match_controller.h"
#include "api_client.h"
#include "display_manager.h"

ArenaSyncManager arenaSync;

ArenaSyncManager::ArenaSyncManager() {
    _txSeq = 0;
    _lastBroadcastMillis = 0;
    _lastPacketReceivedMillis = 0;
    _udpStarted = false;
    _numLinkedSlaves = 0;
    for (int i = 0; i < 4; ++i) {
        _linkedSlaves[i].lastSeenMillis = 0;
    }
}

void ArenaSyncManager::begin() {
    if (configMgr.settings.sync_role == SYNC_ROLE_SOLO) {
        Serial.println("[Sync] Multi-display sync link disabled (Solo Mode).");
        return;
    }

    if (_udp.begin(ARENA_SYNC_PORT)) {
        _udpStarted = true;
        Serial.printf("[Sync] Wireless Arena Sync active on UDP port %d (Role: %s, Key: '%s')\n",
                      ARENA_SYNC_PORT,
                      (configMgr.settings.sync_role == SYNC_ROLE_MASTER) ? "MASTER" : "SLAVE",
                      configMgr.settings.sync_key.c_str());
    } else {
        Serial.println("[Sync] Failed to bind UDP socket for arena sync!");
    }
}

bool ArenaSyncManager::isMaster() const {
    return configMgr.settings.sync_role == SYNC_ROLE_MASTER;
}

bool ArenaSyncManager::isSlave() const {
    return configMgr.settings.sync_role == SYNC_ROLE_SLAVE;
}

bool ArenaSyncManager::isSynced() const {
    if (isSlave()) {
        return (millis() - _lastPacketReceivedMillis < ARENA_SYNC_TIMEOUT_MS);
    }
    return getSlaveCount() > 0;
}

int ArenaSyncManager::getSlaveCount() const {
    int active = 0;
    for (int i = 0; i < _numLinkedSlaves; ++i) {
        if (millis() - _linkedSlaves[i].lastSeenMillis < ARENA_SYNC_TIMEOUT_MS) {
            active++;
        }
    }
    return active;
}

unsigned long ArenaSyncManager::getLastSyncAgeMs() const {
    if (_lastPacketReceivedMillis == 0) return 999999;
    return millis() - _lastPacketReceivedMillis;
}

String ArenaSyncManager::getSyncStatusText() const {
    if (configMgr.settings.sync_role == SYNC_ROLE_SOLO) {
        return "Solo Mode (Sync Disabled)";
    }
    if (isMaster()) {
        int slaves = getSlaveCount();
        if (slaves > 0) {
            return "Master (Broadcasting to " + String(slaves) + " Display" + (slaves > 1 ? "s" : "") + ")";
        }
        return "Master (Broadcasting on '" + configMgr.settings.sync_key + "')";
    }
    if (isSlave()) {
        if (isSynced()) {
            return "Synced to Master (" + String(getLastSyncAgeMs()) + "ms ago)";
        }
        return "Searching for Master ('" + configMgr.settings.sync_key + "')...";
    }
    return "Unknown";
}

void ArenaSyncManager::broadcastState(bool immediate) {
    if (!isMaster() || !_udpStarted) return;

    int activeSlaves = getSlaveCount();
    unsigned long interval = (activeSlaves > 0) ? ARENA_SYNC_BROADCAST_MS : 3000;
    if ((!immediate || activeSlaves == 0) && (millis() - _lastBroadcastMillis < interval)) {
        return;
    }
    _lastBroadcastMillis = millis();

    ArenaSyncPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ARENA_SYNC_MAGIC;
    pkt.version = 2;
    strncpy(pkt.sync_key, configMgr.settings.sync_key.c_str(), sizeof(pkt.sync_key) - 1);
    pkt.seq = _txSeq++;
    pkt.op_mode = (uint8_t)matchCtrl.getMode();

    // Match & Clock State
    pkt.clock_state = (uint8_t)matchCtrl.getCurrentClockState();
    pkt.count_in_seconds = (int8_t)matchCtrl.getCurrentCountInSeconds();
    pkt.remaining_seconds = matchCtrl.getCurrentRemainingSeconds();
    pkt.duration_seconds = matchCtrl.getDuration();
    pkt.hazards_seconds = matchCtrl.getHazardsThreshold();
    pkt.is_hazards_active = matchCtrl.isCurrentHazardsActive() ? 1 : 0;

    pkt.match_number = (uint16_t)matchCtrl.getCurrentMatchNumber();
    pkt.round_number = (uint16_t)matchCtrl.getCurrentRoundNumber();

    String st = matchCtrl.getCurrentMatchStatus();
    st.toLowerCase();
    if (st == "ongoing") pkt.match_status = 1;
    else if (st == "finished") pkt.match_status = 2;
    else if (st == "decided" || st == "completed") pkt.match_status = 3;
    else pkt.match_status = 0; // Scheduled

    strncpy(pkt.red_name, matchCtrl.getCurrentRedName().c_str(), sizeof(pkt.red_name) - 1);
    pkt.red_pit_ready = matchCtrl.getCurrentRedPitReady() ? 1 : 0;
    pkt.red_checked_in = matchCtrl.getCurrentRedCheckedIn() ? 1 : 0;
    pkt.red_match_ready = matchCtrl.getCurrentRedMatchReady() ? 1 : 0;

    strncpy(pkt.blue_name, matchCtrl.getCurrentBlueName().c_str(), sizeof(pkt.blue_name) - 1);
    pkt.blue_pit_ready = matchCtrl.getCurrentBluePitReady() ? 1 : 0;
    pkt.blue_checked_in = matchCtrl.getCurrentBlueCheckedIn() ? 1 : 0;
    pkt.blue_match_ready = matchCtrl.getCurrentBlueMatchReady() ? 1 : 0;

    // Outcome & Decision state
    String outcome = matchCtrl.getCurrentOutcomeType();
    outcome.toLowerCase();
    if (outcome == "ko") pkt.outcome_type = 1;
    else if (outcome == "decision") pkt.outcome_type = 2;
    else if (outcome == "forfeit" || outcome == "disqualified") pkt.outcome_type = 3;
    else if (outcome == "tied") pkt.outcome_type = 4;
    else pkt.outcome_type = 0;

    pkt.is_ko = (pkt.outcome_type == 1) ? 1 : 0;

    String winCorner = matchCtrl.getCurrentWinnerCorner();
    winCorner.toLowerCase();
    if (winCorner == "red") pkt.winner_corner = 1;
    else if (winCorner == "blue") pkt.winner_corner = 2;
    else if (winCorner == "tied") pkt.winner_corner = 3;
    else pkt.winner_corner = 0;

    strncpy(pkt.winner_name, matchCtrl.getCurrentWinnerName().c_str(), sizeof(pkt.winner_name) - 1);
    pkt.outcome_time = matchCtrl.getCurrentOutcomeTime();

    // API Event & Arena Info
    const ArenaStatusData& apiData = apiClient.getData();
    pkt.has_current_match = matchCtrl.hasActiveMatch() ? 1 : 0;
    pkt.has_active_event = apiData.has_active_event ? 1 : 0;
    strncpy(pkt.active_event_name, apiData.active_event_name.c_str(), sizeof(pkt.active_event_name) - 1);
    strncpy(pkt.arena_name, apiData.arena_name.c_str(), sizeof(pkt.arena_name) - 1);

    // Upcoming match
    pkt.has_upcoming = apiData.has_upcoming_match ? 1 : 0;
    if (apiData.has_upcoming_match) {
        pkt.upcoming_match_num = apiData.upcoming_match.match_number;
        pkt.upcoming_round_num = apiData.upcoming_match.round_number;
        strncpy(pkt.upcoming_red, apiData.upcoming_match.red.name.c_str(), sizeof(pkt.upcoming_red) - 1);
        strncpy(pkt.upcoming_blue, apiData.upcoming_match.blue.name.c_str(), sizeof(pkt.upcoming_blue) - 1);
    }

    pkt.screen_mode = (uint8_t)displayMgr.getScreen();
    strncpy(pkt.pause_reason, matchCtrl.getCurrentPauseReason().c_str(), sizeof(pkt.pause_reason) - 1);

    // Broadcast target
    IPAddress bcastIP = WiFi.broadcastIP();
    if (WiFi.getMode() & WIFI_AP) {
        bcastIP = IPAddress(192, 168, 4, 255);
    }

    _udp.beginPacket(bcastIP, ARENA_SYNC_PORT);
    _udp.write((const uint8_t*)&pkt, sizeof(pkt));
    _udp.endPacket();
}

void ArenaSyncManager::update() {
    if (!_udpStarted || configMgr.settings.sync_role == SYNC_ROLE_SOLO) return;

    // 1. Process all incoming UDP packets
    int packetSize = _udp.parsePacket();
    while (packetSize > 0) {
        uint8_t buf[512];
        int len = _udp.read(buf, sizeof(buf));
        if (len > 0) {
            _processIncomingPacket(buf, (size_t)len, _udp.remoteIP());
        }
        packetSize = _udp.parsePacket();
    }

    // 2. Master periodic state broadcast
    if (isMaster()) {
        broadcastState(false);
    }
}

void ArenaSyncManager::_processIncomingPacket(uint8_t* buf, size_t len, IPAddress remoteIP) {
    if (len == sizeof(ArenaSyncPacket)) {
        // Received sync packet from Master
        if (!isSlave()) return;

        ArenaSyncPacket pkt;
        memcpy(&pkt, buf, sizeof(pkt));

        if (pkt.magic != ARENA_SYNC_MAGIC || pkt.version != 2) return;
        if (strncmp(pkt.sync_key, configMgr.settings.sync_key.c_str(), sizeof(pkt.sync_key)) != 0) {
            return; // Key mismatch (different arena)
        }

        _applySyncPacket(pkt);
        _lastPacketReceivedMillis = millis();

        // Send heartbeat back to Master so Master knows this Slave is linked
        static unsigned long lastHeartbeatSent = 0;
        if (millis() - lastHeartbeatSent >= 1000) {
            lastHeartbeatSent = millis();
            _sendHeartbeatToMaster(remoteIP);
        }
    } else if (len == sizeof(ArenaHeartbeatPacket)) {
        // Received heartbeat from Slave on Master
        if (!isMaster()) return;

        ArenaHeartbeatPacket hb;
        memcpy(&hb, buf, sizeof(hb));

        if (hb.magic != ARENA_SYNC_MAGIC) return;
        if (strncmp(hb.sync_key, configMgr.settings.sync_key.c_str(), sizeof(hb.sync_key)) != 0) {
            return;
        }

        // Record or update linked Slave
        int foundIdx = -1;
        for (int i = 0; i < _numLinkedSlaves; ++i) {
            if (_linkedSlaves[i].ip == remoteIP) {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx < 0 && _numLinkedSlaves < 4) {
            foundIdx = _numLinkedSlaves++;
        }
        if (foundIdx >= 0) {
            _linkedSlaves[foundIdx].ip = remoteIP;
            _linkedSlaves[foundIdx].corner = String(hb.corner);
            _linkedSlaves[foundIdx].rssi = hb.rssi;
            _linkedSlaves[foundIdx].lastSeenMillis = millis();
        }
    }
}

void ArenaSyncManager::_sendHeartbeatToMaster(IPAddress masterIP) {
    ArenaHeartbeatPacket hb;
    hb.magic = ARENA_SYNC_MAGIC;
    strncpy(hb.sync_key, configMgr.settings.sync_key.c_str(), sizeof(hb.sync_key) - 1);
    strncpy(hb.corner, configMgr.settings.corner.c_str(), sizeof(hb.corner) - 1);
    hb.rssi = WiFi.RSSI();

    _udp.beginPacket(masterIP, ARENA_SYNC_PORT);
    _udp.write((const uint8_t*)&hb, sizeof(hb));
    _udp.endPacket();
}

void ArenaSyncManager::_applySyncPacket(const ArenaSyncPacket& pkt) {
    // Slave mirrors match and clock state with exact synchronization
    matchCtrl.setMode((OperatingMode)pkt.op_mode);

    // Update match info
    matchCtrl.setMatchDetails(pkt.match_number, pkt.round_number, String(pkt.red_name), String(pkt.blue_name));

    // Update readiness
    matchCtrl.setRedReadiness(pkt.red_pit_ready != 0, pkt.red_checked_in != 0, pkt.red_match_ready != 0);
    matchCtrl.setBlueReadiness(pkt.blue_pit_ready != 0, pkt.blue_checked_in != 0, pkt.blue_match_ready != 0);

    // Update Outcome & Decision state
    if (pkt.outcome_type > 0) {
        String winCorner = (pkt.winner_corner == 1) ? "red" : ((pkt.winner_corner == 2) ? "blue" : "tied");
        String outType = (pkt.outcome_type == 1) ? "ko" : ((pkt.outcome_type == 2) ? "decision" : "forfeit");
        matchCtrl.recordResult(winCorner, outType);
    } else if (pkt.match_status == 2) {
        matchCtrl.finishMatch();
    } else {
        matchCtrl.clearResult();
    }

    // Synchronize timer clock with master
    ClockState masterClockState = (ClockState)pkt.clock_state;
    float masterRem = pkt.remaining_seconds;
    float currentRem = matchCtrl.getCurrentRemainingSeconds();
    ClockState currentClockState = matchCtrl.getClockState();

    if (currentClockState != masterClockState || abs(masterRem - currentRem) > 0.15f || masterClockState == CLOCK_COUNT_IN) {
        matchCtrl.syncClock(masterClockState, masterRem, pkt.duration_seconds, pkt.count_in_seconds, pkt.hazards_seconds, pkt.is_hazards_active != 0, String(pkt.pause_reason));
    }

    // Mirror API client data as well so upcoming matches and event details render accurately
    apiClient.setSyncedData(pkt);

    // Mirror display screen (unless Slave is in custom screen or IP scroll)
    if (!displayMgr.isScrollingIP()) {
        displayMgr.setScreen((DisplayScreen)pkt.screen_mode);
    }
}
