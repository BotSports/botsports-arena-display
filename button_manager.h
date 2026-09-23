#pragma once

#include <Arduino.h>
#include "config.h"

class ButtonManager {
public:
    ButtonManager();
    void begin();
    void update();

    bool isHoldTriggered();
    bool isTapTriggered();

private:
    int _pin;
    unsigned long _pressStartTime;
    bool _isPressed;
    bool _holdReported;
    bool _holdTriggered;
    bool _tapTriggered;
};

extern ButtonManager buttonMgr;
