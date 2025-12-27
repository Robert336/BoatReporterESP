#include <Arduino.h>
#include "LightCode.h"


LightCode::LightCode(int pin)
    : ledPin(pin), pattern(PATTERN_OFF), lastToggleTime(0), ledState(false) {
    
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    lastToggleTime = millis();
}

void LightCode::setPattern(BlinkPattern newPattern) {
    this->pattern = newPattern;
    lastToggleTime = millis();
    ledState = false;
    digitalWrite(ledPin, LOW);
}

void LightCode::update() {
    // time since last toggle in ms
    // switch statement for each pattern with different
    unsigned long now = millis();
    unsigned long msSinceToggle = now - lastToggleTime;
    unsigned long interval = 0;

    switch (pattern) {
        case PATTERN_OFF:
            digitalWrite(ledPin, LOW);
            return;
        case PATTERN_SOLID:
            digitalWrite(ledPin, HIGH);
            return;
        case PATTERN_FAST_BLINK:
            interval = 100; // 500ms on, 500ms off
            break;
        case PATTERN_SLOW_BLINK:
            interval = 500;
            break;
    }
    // Only toggle if interval has passed
    if (msSinceToggle >= interval) {
        ledState = !ledState;
        lastToggleTime = now;
        digitalWrite(ledPin, ledState ? HIGH : LOW);
    }
}