#include "display_manager.h"
#include "config_manager.h"
#include "match_controller.h"

DisplayManager displayMgr;

// Segment definitions for 7-segment big digits:
// a: top, b: top-right, c: bot-right, d: bottom, e: bot-left, f: top-left, g: middle
static const uint8_t DIGIT_SEGMENTS[10] = {
    0b00111111, // 0: a b c d e f
    0b00000110, // 1: b c
    0b01011011, // 2: a b d e g
    0b01001111, // 3: a b c d g
    0b01100110, // 4: b c f g
    0b01101101, // 5: a c d f g
    0b01111101, // 6: a c d e f g
    0b00000111, // 7: a b c
    0b01111111, // 8: a b c d e f g
    0b01101111  // 9: a b c d f g
};

DisplayManager::DisplayManager() {
    _dmaDisplay = nullptr;
    _currentScreen = SCREEN_BOOT;
    _previousScreen = SCREEN_BOOT;
    _lastRenderTime = 0;
    _lastMarqueeTime = 0;
    _marqueeOffsetRed = 0;
    _marqueeOffsetBlue = 0;
    _marqueeOffsetUpcoming = 0;
    _marqueeOffsetKO = 0;
    _marqueeOffsetBanner = 0;
    _marqueeOffsetAP = 0;
    _marqueeOffsetIdle = 0;
    _marqueeOffsetFinished = 0;
    _marqueeOffsetDecision = 0;
    _flashState = false;
    _lastFlashTime = 0;

    _apSSID = "";
    _apIP = "";

    _isScrollingIP = false;
    _ipScrollText = "";
    _ipScrollX = 0;
    _ipScrollLastStep = 0;
}

bool DisplayManager::begin() {
    Serial.println("[Display] Initializing WaveShare 80x40 RGB Matrix Panel...");

    HUB75_I2S_CFG mxconfig(
        PANEL_RES_X,
        PANEL_RES_Y,
        PANEL_CHAIN
    );

    mxconfig.gpio.r1 = PIN_R1;
    mxconfig.gpio.g1 = PIN_G1;
    mxconfig.gpio.b1 = PIN_B1;
    mxconfig.gpio.r2 = PIN_R2;
    mxconfig.gpio.g2 = PIN_G2;
    mxconfig.gpio.b2 = PIN_B2;
    mxconfig.gpio.a = PIN_A;
    mxconfig.gpio.b = PIN_B;
    mxconfig.gpio.c = PIN_C;
    mxconfig.gpio.d = PIN_D;
    mxconfig.gpio.e = PIN_E;
    mxconfig.gpio.lat = PIN_LAT;
    mxconfig.gpio.oe = PIN_OE;
    mxconfig.gpio.clk = PIN_CLK;

    // Hardware timing & anti-flicker configuration:
    // 1. Positive edge clocking prevents clock edge race conditions on shift registers
    mxconfig.clkphase = true;
    mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_16M;
    // 2. Blank OE for 4 clock ticks during row address latch to eliminate row bleed/ghosting
    mxconfig.latch_blanking = 4;
    // 3. Double-buffering prevents tearing and visual redraw flicker
    mxconfig.double_buff = true;
    mxconfig.min_refresh_rate = 240;

    _dmaDisplay = new MatrixPanel_I2S_DMA(mxconfig);
    if (!_dmaDisplay->begin()) {
        Serial.println("[Display] Failed to initialize HUB75 DMA display!");
        return false;
    }

    _dmaDisplay->setBrightness8(configMgr.settings.brightness);
    _dmaDisplay->clearScreen();

    // Cache colors
    _colorBlack      = _dmaDisplay->color565(0, 0, 0);
    _colorWhite      = _dmaDisplay->color565(255, 255, 255);
    _colorRed        = _dmaDisplay->color565(255, 20, 20);
    _colorBlue       = _dmaDisplay->color565(30, 140, 255);
    _colorGreen      = _dmaDisplay->color565(20, 255, 40);
    _colorYellow     = _dmaDisplay->color565(255, 210, 0);
    _colorOrange     = _dmaDisplay->color565(255, 120, 0);
    _colorDarkGray   = _dmaDisplay->color565(75, 75, 75);
    _colorBrightGray = _dmaDisplay->color565(160, 160, 160);
    _colorCyan       = _dmaDisplay->color565(0, 240, 255);

    Serial.println("[Display] Matrix initialized successfully with double buffering and anti-flicker");
    return true;
}

void DisplayManager::setBrightness(uint8_t brightness) {
    if (_dmaDisplay) {
        _dmaDisplay->setBrightness8(brightness);
    }
}

void DisplayManager::clear() {
    if (_dmaDisplay) {
        _dmaDisplay->clearScreen();
    }
}

void DisplayManager::setScreen(DisplayScreen screen) {
    if (_isScrollingIP && screen != SCREEN_SCROLLING_IP) {
        _previousScreen = screen;
        return;
    }
    _currentScreen = screen;
}

void DisplayManager::triggerIPScroll(const String& ip) {
    if (!_dmaDisplay) return;

    _ipScrollText = "IP: " + ip + "  *  http://botsports-arena.local";
    _ipScrollX = PANEL_RES_X;
    _ipScrollLastStep = millis();
    _isScrollingIP = true;

    if (_currentScreen != SCREEN_SCROLLING_IP) {
        _previousScreen = _currentScreen;
    }
    _currentScreen = SCREEN_SCROLLING_IP;

    Serial.printf("[Display] Scrolling IP across display: %s\n", _ipScrollText.c_str());
}

void DisplayManager::update() {
    if (!_dmaDisplay) return;

    // Advance marquee scrolling every 90ms
    if (millis() - _lastMarqueeTime >= 90) {
        _lastMarqueeTime = millis();
        _marqueeOffsetRed++;
        _marqueeOffsetBlue++;
        _marqueeOffsetUpcoming++;
        _marqueeOffsetKO++;
        _marqueeOffsetBanner++;
        _marqueeOffsetAP++;
        _marqueeOffsetIdle++;
        _marqueeOffsetFinished++;
        _marqueeOffsetDecision++;
    }

    // Toggle flash state every 500ms
    if (millis() - _lastFlashTime >= 500) {
        _lastFlashTime = millis();
        _flashState = !_flashState;
    }

    // Limit screen redraw to ~30 FPS
    if (millis() - _lastRenderTime < 33) {
        return;
    }
    _lastRenderTime = millis();

    switch (_currentScreen) {
        case SCREEN_SCROLLING_IP:
            _renderIPScroll();
            break;
        case SCREEN_AP_MODE:
            _renderAPMode();
            break;
        case SCREEN_COUNT_IN:
            _renderCountIn();
            break;
        case SCREEN_ACTIVE_MATCH:
            _renderActiveMatch();
            break;
        case SCREEN_FINISHED:
            _renderFinished();
            break;
        case SCREEN_KO:
            _renderKO();
            break;
        case SCREEN_DECISION:
            _renderDecision();
            break;
        case SCREEN_UPCOMING_MATCH:
            _renderUpcomingMatch();
            break;
        case SCREEN_IDLE_EVENT:
            _renderIdleEvent();
            break;
        default:
            break;
    }

    // Flip DMA back buffer to front buffer smoothly (eliminates redraw flicker)
    if (_dmaDisplay) {
        _dmaDisplay->flipDMABuffer();
    }
}

void DisplayManager::_renderIPScroll() {
    // Step scroll position every 40ms (~25 px/sec smooth movement)
    if (millis() - _ipScrollLastStep >= 40) {
        _ipScrollLastStep = millis();
        _ipScrollX--;

        int textPixelWidth = _ipScrollText.length() * 6;
        if (_ipScrollX < -textPixelWidth) {
            // Finished scrolling complete text
            _isScrollingIP = false;
            _currentScreen = _previousScreen;
            Serial.println("[Display] Finished scrolling IP. Restoring previous screen.");
            return;
        }
    }

    _dmaDisplay->fillScreen(_colorBlack);

    // Top Header
    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorYellow);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(5, 1);
    _dmaDisplay->print("DEVICE NETWORK IP");

    // Middle Scrolling IP Text
    _dmaDisplay->setTextColor(_colorCyan);
    _dmaDisplay->setCursor(_ipScrollX, 16);
    _dmaDisplay->print(_ipScrollText);

    // Bottom Line: Web app hint in bright readable gray
    _dmaDisplay->setTextColor(_colorBrightGray);
    _dmaDisplay->setCursor(2, 29);
    _dmaDisplay->print("Control: port 80");
}

void DisplayManager::showBoot() {
    if (!_dmaDisplay) return;
    _dmaDisplay->fillScreen(_colorBlack);

    _dmaDisplay->setTextWrap(false);

    // Line 1: starting 2px down (y=2)
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorYellow);
    _dmaDisplay->setCursor(14, 2);
    _dmaDisplay->print("BOTSPORTS");

    // Line 2: 2px spacing -> y = 2 + 7 + 2 = 11
    _dmaDisplay->setTextColor(_colorWhite);
    _dmaDisplay->setCursor(2, 11);
    _dmaDisplay->print("ARENA DISPLAY");

    // Line 3: 2px spacing -> y = 11 + 7 + 2 = 20
    // Version in Blue using tiny font (TomThumb, 3x5 font = 5px height)
    _dmaDisplay->setFont(&TomThumb);
    _dmaDisplay->setTextColor(_dmaDisplay->color565(60, 160, 255));
    _dmaDisplay->setCursor(24, 25); // baseline at y=25 draws y=21..25
    _dmaDisplay->print("v" FIRMWARE_VERSION);
    _dmaDisplay->setFont(NULL); // restore default font

    _dmaDisplay->flipDMABuffer();
}

void DisplayManager::showConnecting(const String& ssid, int progress) {
    if (!_dmaDisplay) return;
    _dmaDisplay->fillScreen(_colorBlack);

    _dmaDisplay->setTextWrap(false);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorCyan);
    _dmaDisplay->setCursor(2, 4);
    _dmaDisplay->print("CONNECTING...");

    _dmaDisplay->setTextColor(_colorWhite);
    _dmaDisplay->setCursor(2, 16);
    String truncSSID = ssid.length() > 12 ? ssid.substring(0, 11) + ".." : ssid;
    _dmaDisplay->print(truncSSID);

    int barW = 76;
    int fillW = (barW * progress) / 100;
    _dmaDisplay->drawRect(2, 28, barW, 6, _colorBrightGray);
    if (fillW > 0) {
        _dmaDisplay->fillRect(3, 29, fillW, 4, _colorYellow);
    }
    _dmaDisplay->flipDMABuffer();
}

void DisplayManager::showOTAProgress(const String& status, int progress) {
    if (!_dmaDisplay) return;
    _dmaDisplay->fillScreen(_colorBlack);

    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorYellow);
    _dmaDisplay->setTextWrap(false);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(5, 1);
    _dmaDisplay->print("OTA FIRMWARE");

    _dmaDisplay->setTextColor(_colorCyan);
    _dmaDisplay->setCursor(2, 15);
    String truncStatus = status.length() > 13 ? status.substring(0, 13) : status;
    _dmaDisplay->print(truncStatus);

    int barW = 76;
    int clamped = constrain(progress, 0, 100);
    int fillW = ((barW - 2) * clamped) / 100;
    _dmaDisplay->drawRect(2, 28, barW, 8, _colorBrightGray);
    if (fillW > 0) {
        _dmaDisplay->fillRect(3, 29, fillW, 6, _colorGreen);
    }
    _dmaDisplay->flipDMABuffer();
}

void DisplayManager::showAPMode(const String& apSSID, const String& ip) {
    _apSSID = apSSID;
    _apIP = ip;
    setScreen(SCREEN_AP_MODE);
}

void DisplayManager::_renderAPMode() {
    _dmaDisplay->fillScreen(_colorBlack);
    _dmaDisplay->setTextWrap(false);
    _dmaDisplay->setTextSize(1);

    // Top banner (Orange bar, black text)
    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorOrange);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(6, 1);
    _dmaDisplay->print("WIFI SETUP MODE");

    // Line 2 (y=11): AP SSID (Yellow prefix, white scrolling SSID)
    _dmaDisplay->setTextColor(_colorYellow);
    _dmaDisplay->setCursor(2, 11);
    _dmaDisplay->print("AP:");
    _drawScrollingText(20, 11, _apSSID, _colorWhite, _marqueeOffsetAP, PANEL_RES_X - 20, _colorBlack);

    // Line 3 (y=20): IP Address (Cyan prefix, white scrolling IP)
    _dmaDisplay->setTextColor(_colorCyan);
    _dmaDisplay->setCursor(2, 20);
    _dmaDisplay->print("IP:");
    _drawScrollingText(20, 20, _apIP, _colorWhite, _marqueeOffsetAP, PANEL_RES_X - 20, _colorBlack);

    // Line 4 (y=29): Bottom instruction in bright readable gray, scrolling!
    _drawScrollingText(2, 29, "Open 192.168.4.1 to configure arena", _colorBrightGray, _marqueeOffsetBanner, PANEL_RES_X - 4, _colorBlack);
}

void DisplayManager::showError(const String& title, const String& subtitle) {
    if (!_dmaDisplay) return;
    _dmaDisplay->fillScreen(_colorBlack);

    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorRed);
    _dmaDisplay->setTextColor(_colorWhite);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setCursor(2, 1);
    _dmaDisplay->print(title);

    _dmaDisplay->setTextColor(_colorYellow);
    _dmaDisplay->setCursor(2, 18);
    _dmaDisplay->print(subtitle);
    _dmaDisplay->flipDMABuffer();
}

void DisplayManager::_renderActiveMatch() {
    _dmaDisplay->fillScreen(_colorBlack);

    int mNum = matchCtrl.getCurrentMatchNumber();
    int rNum = matchCtrl.getCurrentRoundNumber();
    String redName = matchCtrl.getCurrentRedName();
    String blueName = matchCtrl.getCurrentBlueName();
    ClockState cState = matchCtrl.getCurrentClockState();
    bool hazardsActive = matchCtrl.isCurrentHazardsActive();
    String pauseReason = matchCtrl.getCurrentPauseReason();

    // 1. Top Header Bar: Round & Match number or Alert
    _dmaDisplay->setTextSize(1);

    if (cState == CLOCK_PAUSED && pauseReason.length() > 0) {
        String label = pauseReason;
        label.replace('_', ' ');
        label.toUpperCase();
        _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorYellow);
        _drawScrollingText(0, 1, "PAUSED: " + label, _colorBlack, _marqueeOffsetBanner, PANEL_RES_X, _colorYellow);
    } else if (hazardsActive && cState == CLOCK_RUNNING) {
        if (_flashState) {
            _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorRed);
            _dmaDisplay->setTextSize(1);
            _dmaDisplay->setTextColor(_colorWhite);
            _dmaDisplay->setCursor(1, 1);
            _dmaDisplay->print("!! HAZARDS !!");
        } else {
            _dmaDisplay->setTextColor(_colorYellow);
            _dmaDisplay->setCursor(1, 1);
            _dmaDisplay->printf("R%d M%d", rNum, mNum);

            _dmaDisplay->setTextColor(_colorOrange);
            _dmaDisplay->setCursor(44, 1);
            _dmaDisplay->print("HAZARD");
        }
    } else {
        String headerRight = (matchCtrl.getMode() == OPMODE_STANDALONE) ? "LOCAL" : apiClient.getData().arena_name;
        if (headerRight.length() == 0) headerRight = "ARENA";
        _drawScrollingText(36, 1, headerRight, _colorWhite, _marqueeOffsetBanner, PANEL_RES_X - 36, _colorBlack);

        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(1, 1);
        _dmaDisplay->printf("R%d M%d", rNum, mNum);
    }

    _dmaDisplay->drawFastHLine(0, 9, PANEL_RES_X, _colorDarkGray);

    // 2. Middle: Large Bold Match Clock
    float remSec = matchCtrl.getCurrentRemainingSeconds();
    int minutes = (int)remSec / 60;
    int seconds = (int)remSec % 60;

    uint16_t clockColor = _colorGreen;
    if (cState == CLOCK_PAUSED) {
        clockColor = _flashState ? _colorYellow : _colorDarkGray;
    } else if (cState == CLOCK_EXPIRED) {
        clockColor = _flashState ? _colorRed : _colorBlack;
    } else if (cState == CLOCK_RUNNING) {
        if (remSec <= 10.0f) {
            clockColor = _flashState ? _colorRed : _colorWhite;
        } else if (remSec <= 30.0f) {
            clockColor = _colorOrange;
        } else if (hazardsActive) {
            clockColor = _flashState ? _colorYellow : _colorGreen;
        }
    } else {
        clockColor = _colorCyan; // Stopped
    }

    if (cState == CLOCK_EXPIRED && _flashState) {
        _dmaDisplay->setTextSize(2);
        _dmaDisplay->setTextColor(_colorRed);
        _dmaDisplay->setCursor(12, 11);
        _dmaDisplay->print("TIME!");
    } else {
        _drawBigClock(23, 11, minutes, seconds, clockColor);
    }

    // 3. Bottom Rows: Combatant bot names + 3-tier readiness dots [Pit (Amber), Gate (Cyan), Match (Green)]
    bool rPit, rGate, rMatch, bPit, bGate, bMatch;
    if (matchCtrl.getMode() == OPMODE_STANDALONE || matchCtrl.getMode() == OPMODE_TEST) {
        rPit = matchCtrl.getCurrentRedPitReady();
        rGate = matchCtrl.getCurrentRedCheckedIn();
        rMatch = matchCtrl.getCurrentRedMatchReady();
        bPit = matchCtrl.getCurrentBluePitReady();
        bGate = matchCtrl.getCurrentBlueCheckedIn();
        bMatch = matchCtrl.getCurrentBlueMatchReady();
    } else {
        apiClient.getReadiness(rPit, rGate, rMatch, bPit, bGate, bMatch);
    }

    String myCorner = configMgr.settings.corner;
    if (myCorner == "red") {
        // Red Corner Display Unit: emphasize Red Fighter
        _dmaDisplay->fillRect(0, 26, PANEL_RES_X, 14, _dmaDisplay->color565(50, 5, 5));
        _drawScrollingText(15, 26, "RED: " + redName, _colorWhite, _marqueeOffsetRed, PANEL_RES_X - 15, _dmaDisplay->color565(50, 5, 5));
        _dmaDisplay->fillRect(1, 28, 3, 4, _colorRed);
        _dmaDisplay->fillRect(5, 29, 2, 2, rPit ? _colorOrange : _colorDarkGray);
        _dmaDisplay->fillRect(8, 29, 2, 2, rGate ? _colorCyan : _colorDarkGray);
        _dmaDisplay->fillRect(11, 29, 2, 2, rMatch ? _colorGreen : _colorDarkGray);

        _drawScrollingText(7, 33, "vs " + blueName, _colorBrightGray, _marqueeOffsetBlue, PANEL_RES_X - 7, _dmaDisplay->color565(50, 5, 5));
        _dmaDisplay->fillRect(1, 35, 3, 3, _colorBlue);
    } else if (myCorner == "blue") {
        // Blue Corner Display Unit: emphasize Blue Fighter
        _dmaDisplay->fillRect(0, 26, PANEL_RES_X, 14, _dmaDisplay->color565(5, 15, 50));
        _drawScrollingText(15, 26, "BLU: " + blueName, _colorWhite, _marqueeOffsetBlue, PANEL_RES_X - 15, _dmaDisplay->color565(5, 15, 50));
        _dmaDisplay->fillRect(1, 28, 3, 4, _colorBlue);
        _dmaDisplay->fillRect(5, 29, 2, 2, bPit ? _colorOrange : _colorDarkGray);
        _dmaDisplay->fillRect(8, 29, 2, 2, bGate ? _colorCyan : _colorDarkGray);
        _dmaDisplay->fillRect(11, 29, 2, 2, bMatch ? _colorGreen : _colorDarkGray);

        _drawScrollingText(7, 33, "vs " + redName, _colorBrightGray, _marqueeOffsetRed, PANEL_RES_X - 7, _dmaDisplay->color565(5, 15, 50));
        _dmaDisplay->fillRect(1, 35, 3, 3, _colorRed);
    } else {
        // Standard Dual Arena Display: both names scroll horizontally with readiness dots
        _drawScrollingText(15, 26, redName, _colorWhite, _marqueeOffsetRed, PANEL_RES_X - 15, _colorBlack);
        _dmaDisplay->fillRect(1, 28, 3, 4, _colorRed);
        _dmaDisplay->fillRect(5, 29, 2, 2, rPit ? _colorOrange : _colorDarkGray);
        _dmaDisplay->fillRect(8, 29, 2, 2, rGate ? _colorCyan : _colorDarkGray);
        _dmaDisplay->fillRect(11, 29, 2, 2, rMatch ? _colorGreen : _colorDarkGray);

        _drawScrollingText(15, 33, blueName, _colorWhite, _marqueeOffsetBlue, PANEL_RES_X - 15, _colorBlack);
        _dmaDisplay->fillRect(1, 35, 3, 4, _colorBlue);
        _dmaDisplay->fillRect(5, 36, 2, 2, bPit ? _colorOrange : _colorDarkGray);
        _dmaDisplay->fillRect(8, 36, 2, 2, bGate ? _colorCyan : _colorDarkGray);
        _dmaDisplay->fillRect(11, 36, 2, 2, bMatch ? _colorGreen : _colorDarkGray);
    }
}

void DisplayManager::_renderUpcomingMatch() {
    const ArenaStatusData& data = apiClient.getData();
    _dmaDisplay->fillScreen(_colorBlack);

    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorYellow);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(2, 1);

    if (matchCtrl.getMode() == OPMODE_STANDALONE) {
        _dmaDisplay->printf("NEXT: R%d M%d", matchCtrl.getRoundNumber(), matchCtrl.getMatchNumber() + 1);

        _dmaDisplay->setTextColor(_colorRed);
        _dmaDisplay->setCursor(2, 13);
        _dmaDisplay->print("R:");
        _drawScrollingText(16, 13, "Next Red Bot", _colorWhite, _marqueeOffsetRed, PANEL_RES_X - 16, _colorBlack);

        _dmaDisplay->setTextColor(_colorBrightGray);
        _dmaDisplay->setCursor(34, 22);
        _dmaDisplay->print("vs");

        _dmaDisplay->setTextColor(_colorBlue);
        _dmaDisplay->setCursor(2, 31);
        _dmaDisplay->print("B:");
        _drawScrollingText(16, 31, "Next Blue Bot", _colorWhite, _marqueeOffsetBlue, PANEL_RES_X - 16, _colorBlack);
        return;
    }

    if (data.has_upcoming_match) {
        _dmaDisplay->printf("NEXT: R%d M%d", data.upcoming_match.round_number, data.upcoming_match.match_number);

        _dmaDisplay->setTextColor(_colorRed);
        _dmaDisplay->setCursor(2, 13);
        _dmaDisplay->print("R:");
        _drawScrollingText(16, 13, data.upcoming_match.red.name, _colorWhite, _marqueeOffsetRed, PANEL_RES_X - 16, _colorBlack);

        _dmaDisplay->setTextColor(_colorBrightGray);
        _dmaDisplay->setCursor(34, 22);
        _dmaDisplay->print("vs");

        _dmaDisplay->setTextColor(_colorBlue);
        _dmaDisplay->setCursor(2, 31);
        _dmaDisplay->print("B:");
        _drawScrollingText(16, 31, data.upcoming_match.blue.name, _colorWhite, _marqueeOffsetBlue, PANEL_RES_X - 16, _colorBlack);
    } else {
        _dmaDisplay->print("NEXT MATCH");
        // Waiting for data scrolls smoothly across
        _drawScrollingText(2, 16, "Waiting for match data...", _colorWhite, _marqueeOffsetUpcoming, PANEL_RES_X - 4, _colorBlack);
        _drawScrollingText(2, 28, "Waiting on deck", _colorBrightGray, _marqueeOffsetUpcoming, PANEL_RES_X - 4, _colorBlack);
    }
}

void DisplayManager::_renderIdleEvent() {
    const ArenaStatusData& data = apiClient.getData();
    _dmaDisplay->fillScreen(_colorBlack);

    String aName = (matchCtrl.getMode() == OPMODE_STANDALONE) ? "ARENA STANDALONE" : data.arena_name;
    if (aName.length() == 0) aName = "BOTSPORTS ARENA";
    _drawScrollingText(2, 2, aName, _colorCyan, _marqueeOffsetIdle, PANEL_RES_X - 4, _colorBlack);

    _dmaDisplay->drawFastHLine(0, 11, PANEL_RES_X, _colorDarkGray);

    if (matchCtrl.getMode() == OPMODE_STANDALONE) {
        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(2, 15);
        _dmaDisplay->print("READY TO FIGHT");

        _drawScrollingText(2, 26, "Use Web Controls to Start", _colorBrightGray, _marqueeOffsetUpcoming, PANEL_RES_X - 4, _colorBlack);
    } else if (data.has_active_event) {
        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(2, 14);
        _dmaDisplay->print("ACTIVE EVENT");

        _drawScrollingText(2, 25, data.active_event_name, _colorWhite, _marqueeOffsetUpcoming, PANEL_RES_X - 4, _colorBlack);
    } else {
        // No active event: RED and SCROLLING as requested!
        _drawScrollingText(2, 15, "NO ACTIVE EVENT", _colorRed, _marqueeOffsetIdle, PANEL_RES_X - 4, _colorBlack);

        _dmaDisplay->setTextColor(_colorBrightGray);
        _dmaDisplay->setCursor(18, 26);
        _dmaDisplay->print("STANDBY");
    }
}

void DisplayManager::_renderKO() {
    unsigned long elapsed = matchCtrl.getKOElapsedMillis();
    String winnerCorner = matchCtrl.getKOWinner();
    String winnerName = matchCtrl.getKOWinnerName();
    uint16_t cornerColor = (winnerCorner == "red") ? _colorRed : _colorBlue;

    // Phase 1: First 2.5 seconds - High Energy flashing K.O.!
    if (elapsed < 2500) {
        bool fastFlash = ((elapsed / 120) % 2 == 0);
        uint16_t bg = fastFlash ? _colorBlack : cornerColor;
        uint16_t fg = fastFlash ? _colorYellow : _colorWhite;

        _dmaDisplay->fillScreen(bg);

        _dmaDisplay->drawRect(0, 0, PANEL_RES_X, PANEL_RES_Y, fg);
        _dmaDisplay->drawRect(2, 2, PANEL_RES_X - 4, PANEL_RES_Y - 4, fastFlash ? cornerColor : _colorBlack);

        _dmaDisplay->setTextSize(3);
        // Moved left 5px as requested (from 14, 10 and 13, 9 to 9, 10 and 8, 9)
        _dmaDisplay->setTextColor(_colorBlack);
        _dmaDisplay->setCursor(9, 10);
        _dmaDisplay->print("K.O.!");

        _dmaDisplay->setTextColor(fg);
        _dmaDisplay->setCursor(8, 9);
        _dmaDisplay->print("K.O.!");
    } else {
        // Phase 2: Winner presentation screen
        _dmaDisplay->fillScreen(_colorBlack);

        // Top banner: Black text on corner color (or red), scrolling!
        _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, cornerColor);
        String bannerText = (winnerCorner == "red") ? "KNOCKOUT - RED WINS!" : "KNOCKOUT - BLUE WINS!";
        _drawScrollingText(0, 1, bannerText, _colorBlack, _marqueeOffsetBanner, PANEL_RES_X, cornerColor);

        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(2, 12);
        _dmaDisplay->print("WINNER:");

        _drawScrollingText(7, 20, winnerName, _colorWhite, _marqueeOffsetKO, PANEL_RES_X - 7, _colorBlack);
        _dmaDisplay->fillRect(1, 22, 4, 4, cornerColor); // ensure indicator box not overwritten

        // Time remaining line: shortened to "Time: XX:XX" so it fits without scrolling
        float koTime = matchCtrl.getKOTime();
        int m = (int)koTime / 60;
        int s = (int)koTime % 60;
        _dmaDisplay->setTextColor(_colorCyan);
        _dmaDisplay->setCursor(2, 30);
        _dmaDisplay->printf("Time: %d:%02d", m, s);
    }
}

void DisplayManager::_renderCountIn() {
    _dmaDisplay->fillScreen(_colorBlack);

    int countSec = (matchCtrl.getMode() == OPMODE_STANDALONE) ? matchCtrl.getCurrentCountInSeconds() : apiClient.getInterpolatedCountInSeconds();

    // Top Header: pulsing yellow countdown banner
    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 8, _flashState ? _colorYellow : _dmaDisplay->color565(120, 100, 0));
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(14, 1);
    _dmaDisplay->print("COUNTDOWN");

    if (countSec > 0) {
        // Big 3, 2, 1
        _dmaDisplay->setTextSize(3);
        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(32, 11);
        _dmaDisplay->print(countSec);
    } else {
        // FIGHT!
        _dmaDisplay->setTextSize(2);
        _dmaDisplay->setTextColor(_colorGreen);
        _dmaDisplay->setCursor(5, 12);
        _dmaDisplay->print("FIGHT!");
    }

    // Bottom combatants line: 25px Red slot (x=5..29), 4px buffer, "vs" at x=34..44, 4px buffer, 26px Blue slot (x=49..74)
    String rName = matchCtrl.getCurrentRedName();
    String bName = matchCtrl.getCurrentBlueName();

    _drawScrollingText(5, 32, rName, _colorWhite, _marqueeOffsetRed, 25, _colorBlack, 5, 5);
    _drawScrollingText(49, 32, bName, _colorWhite, _marqueeOffsetBlue, 26, _colorBlack, 5, 5);

    // Red indicator box (x=1..3)
    _dmaDisplay->fillRect(1, 34, 3, 4, _colorRed);
    // Blue indicator box (x=76..78)
    _dmaDisplay->fillRect(PANEL_RES_X - 4, 34, 3, 4, _colorBlue);

    // Center "vs" with buffer
    _dmaDisplay->setTextColor(_colorBrightGray);
    _dmaDisplay->setCursor(35, 32);
    _dmaDisplay->print("vs");
}

void DisplayManager::_renderFinished() {
    _dmaDisplay->fillScreen(_colorBlack);

    // Top banner: Orange bar with black text
    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorOrange);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorBlack);
    _dmaDisplay->setCursor(4, 1);
    _dmaDisplay->print("COMBAT FINISHED");

    // Middle: Flashing Awaiting Decision / Scoring
    if (_flashState) {
        _dmaDisplay->setTextColor(_colorYellow);
        _dmaDisplay->setCursor(2, 13);
        _dmaDisplay->print("AWAITING DECISION");
    } else {
        _dmaDisplay->setTextColor(_colorBrightGray);
        _dmaDisplay->setCursor(11, 13);
        _dmaDisplay->print("SCORING BOUT");
    }

    // Bottom: Fighters
    _dmaDisplay->fillRect(1, 25, 3, 4, _colorRed);
    _drawScrollingText(6, 24, matchCtrl.getCurrentRedName(), _colorWhite, _marqueeOffsetRed, PANEL_RES_X - 6, _colorBlack);

    _dmaDisplay->fillRect(1, 33, 3, 4, _colorBlue);
    _drawScrollingText(6, 32, matchCtrl.getCurrentBlueName(), _colorWhite, _marqueeOffsetBlue, PANEL_RES_X - 6, _colorBlack);
}

void DisplayManager::_renderDecision() {
    _dmaDisplay->fillScreen(_colorBlack);

    String winnerCorner = matchCtrl.getCurrentWinnerCorner();
    String winnerName = matchCtrl.getCurrentWinnerName();
    String outcomeType = matchCtrl.getCurrentOutcomeType();
    outcomeType.toUpperCase();

    if (winnerCorner == "tied" || winnerCorner == "draw" || outcomeType == "DRAW") {
        _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, _colorYellow);
        _dmaDisplay->setTextSize(1);
        _dmaDisplay->setTextColor(_colorBlack);
        _dmaDisplay->setCursor(8, 1);
        _dmaDisplay->print("MATCH TIED - DRAW");

        _dmaDisplay->setTextColor(_colorWhite);
        _dmaDisplay->setCursor(2, 14);
        _dmaDisplay->print("JUDGES SPLIT");

        _drawScrollingText(2, 26, matchCtrl.getCurrentRedName() + " tied " + matchCtrl.getCurrentBlueName(), _colorBrightGray, _marqueeOffsetDecision, PANEL_RES_X - 4, _colorBlack);
        return;
    }

    uint16_t cornerColor = (winnerCorner == "red") ? _colorRed : _colorBlue;
    String bannerText = (winnerCorner == "red") ? "RED WINS" : "BLUE WINS";
    if (outcomeType.length() > 0) {
        bannerText += " - " + outcomeType;
    } else {
        bannerText += " - DECISION";
    }

    // Top banner with winner corner color
    _dmaDisplay->fillRect(0, 0, PANEL_RES_X, 9, cornerColor);
    _drawScrollingText(0, 1, bannerText, _colorBlack, _marqueeOffsetBanner, PANEL_RES_X, cornerColor);

    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(_colorYellow);
    _dmaDisplay->setCursor(2, 12);
    _dmaDisplay->print("WINNER:");

    _drawScrollingText(7, 21, winnerName, _colorWhite, _marqueeOffsetDecision, PANEL_RES_X - 7, _colorBlack);
    _dmaDisplay->fillRect(1, 23, 4, 4, cornerColor);

    _drawScrollingText(2, 31, "JUDGES DECISION", _colorCyan, _marqueeOffsetBanner, PANEL_RES_X - 4, _colorBlack);
}

void DisplayManager::_drawBigClock(int x, int y, int minutes, int seconds, uint16_t color) {
    int m = constrain(minutes, 0, 9);
    int s10 = (seconds / 10) % 10;
    int s1 = seconds % 10;

    _drawBigDigit(x, y, m, color);
    _drawBigColon(x + 9, y, color);
    _drawBigDigit(x + 15, y, s10, color);
    _drawBigDigit(x + 25, y, s1, color);
}

void DisplayManager::_drawBigColon(int x, int y, uint16_t color) {
    if (!_dmaDisplay) return;
    _dmaDisplay->fillRect(x, y + 3, 2, 2, color);
    _dmaDisplay->fillRect(x, y + 9, 2, 2, color);
}

void DisplayManager::_drawBigDigit(int x, int y, int digit, uint16_t color) {
    if (!_dmaDisplay || digit < 0 || digit > 9) return;

    uint8_t seg = DIGIT_SEGMENTS[digit];
    if (seg & 0b00000001) _dmaDisplay->fillRect(x + 1, y, 6, 2, color);         // a
    if (seg & 0b00000010) _dmaDisplay->fillRect(x + 6, y + 1, 2, 6, color);     // b
    if (seg & 0b00000100) _dmaDisplay->fillRect(x + 6, y + 7, 2, 6, color);     // c
    if (seg & 0b00001000) _dmaDisplay->fillRect(x + 1, y + 12, 6, 2, color);    // d
    if (seg & 0b00010000) _dmaDisplay->fillRect(x, y + 7, 2, 6, color);         // e
    if (seg & 0b00100000) _dmaDisplay->fillRect(x, y + 1, 2, 6, color);         // f
    if (seg & 0b01000000) _dmaDisplay->fillRect(x + 1, y + 6, 6, 2, color);     // g
}

void DisplayManager::_drawScrollingText(int startX, int y, const String& text, uint16_t color, int& offset, int maxW, uint16_t maskBg, int clipLeft, int clipRight) {
    if (!_dmaDisplay) return;

    int textW = text.length() * 6;
    _dmaDisplay->setTextWrap(false);
    _dmaDisplay->setTextSize(1);
    _dmaDisplay->setTextColor(color);

    if (textW <= maxW) {
        _dmaDisplay->setTextWrap(false);
        _dmaDisplay->setTextSize(1);
        _dmaDisplay->setTextColor(color);
        _dmaDisplay->setCursor(startX, y);
        _dmaDisplay->print(text);
        offset = 0;
    } else {
        int totalScrollWidth = textW + 16;
        int curX = startX - (offset % totalScrollWidth);

        // Windowed character drawing: only draw characters that intersect [startX - 5, startX + maxW)
        auto drawWindowed = [&](int baseOffset) {
            for (size_t i = 0; i < text.length(); i++) {
                int chX = baseOffset + (int)i * 6;
                if (chX + 5 >= startX && chX < startX + maxW) {
                    _dmaDisplay->drawChar(chX, y, text[i], color, maskBg, 1);
                }
            }
        };

        drawWindowed(curX);
        if (curX + totalScrollWidth < startX + maxW) {
            drawWindowed(curX + totalScrollWidth);
        }

        // Cleanly clip partial character pixels on the left
        int leftW = (clipLeft >= 0) ? clipLeft : startX;
        if (leftW > 0 && startX > 0) {
            int clipX = startX - leftW;
            if (clipX < 0) clipX = 0;
            _dmaDisplay->fillRect(clipX, y, leftW, 8, maskBg);
        }

        // Cleanly clip partial character pixels on the right
        int rightW = (clipRight >= 0) ? clipRight : (PANEL_RES_X - (startX + maxW));
        if (rightW > 0 && startX + maxW < PANEL_RES_X) {
            if (startX + maxW + rightW > PANEL_RES_X) rightW = PANEL_RES_X - (startX + maxW);
            _dmaDisplay->fillRect(startX + maxW, y, rightW, 8, maskBg);
        }
    }
}
