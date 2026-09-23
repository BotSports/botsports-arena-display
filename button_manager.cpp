#include "button_manager.h"

ButtonManager buttonMgr;

ButtonManager::ButtonManager() {
    _pin = PIN_BUTTON_USER;
    _pressStartTime = 0;
    _isPressed = false;
    _holdReported = false;
    _holdTriggered = false;
    _tapTriggered = false;
}

void ButtonManager::begin() {
    pinMode(_pin, INPUT_PULLUP);
}

void ButtonManager::update() {
    bool rawState = (digitalRead(_pin) == LOW); // Active LOW on StampS3 G0

    if (rawState && !_isPressed) {
        // Just pressed down
        _isPressed = true;
        _pressStartTime = millis();
        _holdReported = false;
    } else if (rawState && _isPressed) {
        // Still holding down
        if (!_holdReported && (millis() - _pressStartTime >= BUTTON_HOLD_AP_MS)) {
            Serial.println("[Button] User button held for >3 seconds. Triggering AP config mode!");
            _holdReported = true;
            _holdTriggered = true;
        }
    } else if (!rawState && _isPressed) {
        // Just released
        unsigned long duration = millis() - _pressStartTime;
        _isPressed = false;

        // If it was a short tap (50ms <= duration < 2500ms) and hold was not reported
        if (!_holdReported && duration >= 50 && duration < 2500) {
            Serial.printf("[Button] User button tapped (%lu ms). Triggering IP scroll!\n", duration);
            _tapTriggered = true;
        }
    }
}

bool ButtonManager::isHoldTriggered() {
    if (_holdTriggered) {
        _holdTriggered = false;
        return true;
    }
    return false;
}

bool ButtonManager::isTapTriggered() {
    if (_tapTriggered) {
        _tapTriggered = false;
        return true;
    }
    return false;
}
