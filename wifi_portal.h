#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "config_manager.h"

enum WiFiPortalState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE
};

class WiFiPortal {
public:
    WiFiPortal();
    void begin();
    void handle();

    bool connectToWiFi();
    void startAPMode();
    void stopAPMode();
    void requestAPMode() { _apModeRequested = true; }

    WiFiPortalState getState() const { return _state; }
    bool isConnected() const { return _state == WIFI_STATE_CONNECTED; }
    bool isAPMode() const { return _state == WIFI_STATE_AP_MODE; }
    bool isOTAInProgress() const { return _otaInProgress; }
    String getAPSSID() const { return _apSSID; }
    String getIPAddress() const;
    int8_t getRSSI() const;

private:
    WiFiPortalState _state;
    String _apSSID;
    DNSServer _dnsServer;
    WebServer _server;
    unsigned long _connectStartTime;
    bool _shouldReboot;
    unsigned long _rebootTime;
    bool _serverStarted;
    volatile bool _apModeRequested;
    volatile bool _otaInProgress;
    bool _otaSuccess;
    String _otaError;
    size_t _otaTotalBytes;
    size_t _otaExpectedSize;
    int _lastOtaDisplayPct;

    void _setupWebServer();
    void _startServer();

    // Route handlers
    void _handleRoot();
    void _handleScan();
    void _handleSave();
    void _handleReset();
    void _handleStatus();
    void _handleControlStatus();
    void _handleControlTimer();
    void _handleControlKO();
    void _handleControlMatch();
    void _handleControlReady();
    void _handleControlResult();
    void _handleControlMode();
    void _handleControlSaveDefaults();
    void _handleControlTest();
    void _handleControlBrightness();
    void _handleOTAComplete();
    void _handleOTAUpload();
    void _handleOTACheck();
    void _handleOTAPull();
    void _handleCaptivePortal();

};

extern WiFiPortal wifiPortal;
