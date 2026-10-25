#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

// Binary packed sync packet for sub-millisecond wireless transport (v2)
struct __attribute__((packed)) ArenaSyncPacket {
    uint32_t magic;               // ARENA_SYNC_MAGIC ("BSPT")
    uint8_t version;             // 2
    char sync_key[16];           // Arena identifier (e.g. "arena_1")
    uint32_t seq;                // Sequential packet counter
    uint8_t op_mode;             // 0=OPMODE_API, 1=OPMODE_STANDALONE, 2=OPMODE_TEST
    uint8_t clock_state;         // 0=STOPPED, 1=COUNT_IN, 2=RUNNING, 3=PAUSED, 4=EXPIRED
    int8_t count_in_seconds;     // 3, 2, 1, 0 ("FIGHT!"), or -1
    float remaining_seconds;     // Clock seconds remaining at TX instant
    float duration_seconds;      // Total match duration
    float hazards_seconds;       // Hazards activation threshold
    uint8_t is_hazards_active;   // 1 if hazards active, 0 otherwise
    uint16_t match_number;       // Current match number
    uint16_t round_number;       // Current round number
    uint8_t match_status;        // 0=Scheduled, 1=Ongoing, 2=Finished, 3=Decided
    char red_name[32];           // Red corner bot name
    uint8_t red_pit_ready;       // 1 if pit ready
    uint8_t red_checked_in;      // 1 if gate checked in
    uint8_t red_match_ready;     // 1 if match ready
    char blue_name[32];          // Blue corner bot name
    uint8_t blue_pit_ready;      // 1 if pit ready
    uint8_t blue_checked_in;     // 1 if gate checked in
    uint8_t blue_match_ready;    // 1 if match ready
    uint8_t is_ko;               // 1 if knockout occurred
    uint8_t outcome_type;        // 0=None, 1=KO, 2=Decision, 3=Forfeit/DQ, 4=Tied
    uint8_t winner_corner;       // 0=None, 1=Red, 2=Blue, 3=Tied
    char winner_name[32];        // Winner bot name
    float outcome_time;          // Time remaining at conclusion
    uint8_t has_current_match;   // 1 if active match in progress
    uint8_t has_active_event;    // 1 if active event associated
    char active_event_name[32];  // Event name
    char arena_name[24];         // Arena name
    uint8_t has_upcoming;        // 1 if upcoming match on deck
    char upcoming_red[32];       // Next Red bot name
    char upcoming_blue[32];      // Next Blue bot name
    uint16_t upcoming_match_num; // Next match number
    uint16_t upcoming_round_num; // Next round number
    uint8_t screen_mode;         // DisplayScreen enum
    char pause_reason[16];       // Clock pause reason (e.g. "debris_sweep")
};


// Heartbeat packet sent from Slave to Master
struct __attribute__((packed)) ArenaHeartbeatPacket {
    uint32_t magic;               // ARENA_SYNC_MAGIC
    char sync_key[16];           // Arena identifier
    char corner[8];              // Slave corner configuration: "all", "red", "blue"
    int8_t rssi;                 // Slave RSSI
};

struct LinkedSlaveInfo {
    IPAddress ip;
    String corner;
    int8_t rssi;
    unsigned long lastSeenMillis;
};

class ArenaSyncManager {
public:
    ArenaSyncManager();
    void begin();
    void update();

    void broadcastState(bool immediate = false);

    bool isMaster() const;
    bool isSlave() const;
    bool isSynced() const;
    int getSlaveCount() const;
    unsigned long getLastSyncAgeMs() const;
    String getSyncStatusText() const;

private:
    WiFiUDP _udp;
    uint32_t _txSeq;
    unsigned long _lastBroadcastMillis;
    unsigned long _lastPacketReceivedMillis;
    bool _udpStarted;

    // Slave tracking on Master
    LinkedSlaveInfo _linkedSlaves[4];
    int _numLinkedSlaves;

    void _processIncomingPacket(uint8_t* buf, size_t len, IPAddress remoteIP);
    void _applySyncPacket(const ArenaSyncPacket& pkt);
    void _sendHeartbeatToMaster(IPAddress masterIP);
};

extern ArenaSyncManager arenaSync;
