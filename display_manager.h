#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "config.h"
#include "api_client.h"

enum DisplayScreen {
    SCREEN_BOOT,
    SCREEN_CONNECTING_WIFI,
    SCREEN_AP_MODE,
    SCREEN_ERROR,
    SCREEN_IDLE_EVENT,
    SCREEN_UPCOMING_MATCH,
    SCREEN_ACTIVE_MATCH,
    SCREEN_COUNT_IN,
    SCREEN_FINISHED,
    SCREEN_KO,
    SCREEN_DECISION,
    SCREEN_SCROLLING_IP
};


class DisplayManager {
public:
    DisplayManager();
    bool begin();
    void setBrightness(uint8_t brightness);
    void setScreen(DisplayScreen screen);
    DisplayScreen getScreen() const { return _currentScreen; }
    void update();

    void showBoot();
    void showConnecting(const String& ssid, int progress);
    void showOTAProgress(const String& status, int progress);
    void showAPMode(const String& apSSID, const String& ip);
    void showError(const String& title, const String& subtitle);

    // IP Scroll Banner Trigger (on G0 button tap)
    void triggerIPScroll(const String& ip);
    bool isScrollingIP() const { return _isScrollingIP; }

    void clear();
    MatrixPanel_I2S_DMA* getPanel() { return _dmaDisplay; }

private:
    MatrixPanel_I2S_DMA* _dmaDisplay;
    DisplayScreen _currentScreen;
    DisplayScreen _previousScreen;
    unsigned long _lastRenderTime;
    unsigned long _lastMarqueeTime;
    int _marqueeOffsetRed;
    int _marqueeOffsetBlue;
    int _marqueeOffsetUpcoming;
    int _marqueeOffsetKO;
    int _marqueeOffsetBanner;
    int _marqueeOffsetAP;
    int _marqueeOffsetIdle;
    int _marqueeOffsetFinished;
    int _marqueeOffsetDecision;
    bool _flashState;
    unsigned long _lastFlashTime;

    // AP Setup screen state
    String _apSSID;
    String _apIP;

    // IP Scroll Banner state
    bool _isScrollingIP;
    String _ipScrollText;
    int _ipScrollX;
    unsigned long _ipScrollLastStep;

    // Color definitions
    uint16_t _colorBlack;
    uint16_t _colorWhite;
    uint16_t _colorRed;
    uint16_t _colorBlue;
    uint16_t _colorGreen;
    uint16_t _colorYellow;
    uint16_t _colorOrange;
    uint16_t _colorDarkGray;
    uint16_t _colorBrightGray;
    uint16_t _colorCyan;

    void _renderActiveMatch();
    void _renderUpcomingMatch();
    void _renderIdleEvent();
    void _renderCountIn();
    void _renderFinished();
    void _renderKO();
    void _renderDecision();
    void _renderAPMode();
    void _renderIPScroll();
    void _drawBigClock(int x, int y, int minutes, int seconds, uint16_t color);
    void _drawBigDigit(int x, int y, int digit, uint16_t color);
    void _drawBigColon(int x, int y, uint16_t color);
    void _drawScrollingText(int startX, int y, const String& text, uint16_t color, int& offset, int maxW = PANEL_RES_X, uint16_t maskBg = 0, int clipLeft = -1, int clipRight = -1);
};


extern DisplayManager displayMgr;
