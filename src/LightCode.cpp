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
    bool newState;

    switch (pattern) {
        case PATTERN_OFF:
            newState = false; // Stay OFF
            digitalWrite(ledPin, LOW);
            return;
        case PATTERN_SOLID:
            newState = true; // Stay ON
            digitalWrite(ledPin, HIGH);
            return;
        case PATTERN_FAST_BLINK:
            interval = 100; // 500ms on, 500ms off
            break;
        case PATTERN_SLOW_BLINK:
            interval = 500;
            break;
    }       

    // Check for state change
    if (msSinceToggle >= interval) {
        newState = !ledState; // Toggle state
        lastToggleTime = now;
    }

    if (newState == ledState) return; // Exit here, No change to LED

    ledState = newState; // Change current state to the new state, write to LED
    if (ledState) {
        digitalWrite(ledPin, HIGH);
    } else {
        digitalWrite(ledPin, LOW);
    }
}