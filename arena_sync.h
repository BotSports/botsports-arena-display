#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

// Binary packed sync packet for sub-millisecond wireless transport
struct __attribute__((packed)) ArenaSyncPacket {
    uint32_t magic;               // ARENA_SYNC_MAGIC ("BSPT")
    uint8_t version;             // 1
    char sync_key[16];           // Arena identifier (e.g. "arena_1")
    uint32_t seq;                // Sequential packet counter
    uint8_t op_mode;             // 0=OPMODE_API, 1=OPMODE_STANDALONE, 2=OPMODE_TEST
    uint8_t clock_state;         // 0=STOPPED, 1=RUNNING, 2=PAUSED, 3=EXPIRED
    float remaining_seconds;     // Clock seconds remaining at TX instant
    float duration_seconds;      // Total match duration
    uint16_t match_number;       // Current match number
    uint16_t round_number;       // Current round number
    char red_name[32];           // Red corner bot name
    char blue_name[32];          // Blue corner bot name
    uint8_t is_ko;               // 1 if knockout occurred
    uint8_t ko_winner;           // 1 = Red, 2 = Blue
    char ko_winner_name[32];     // Winner bot name
    float ko_time;               // Time remaining at KO
    uint8_t has_current_match;   // 1 if active match in progress
    uint8_t has_active_event;    // 1 if active event associated
    char active_event_name[32];  // Event name
    char arena_name[24];         // Arena name
    uint8_t has_upcoming;        // 1 if upcoming match on deck
    char upcoming_red[32];       // Next Red bot name
    char upcoming_blue[32];      // Next Blue bot name
    uint16_t upcoming_match_num; // Next match number
    uint16_t upcoming_round_num; // Next round number
    uint8_t screen_mode;         // SCREEN_ACTIVE_MATCH, SCREEN_UPCOMING_MATCH, SCREEN_IDLE_EVENT, SCREEN_KO
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
