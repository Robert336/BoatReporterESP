#include <Arduino.h>
#include "LightCode.h"
#include "Logger.h"


LightCode::LightCode(int pin)
    : ledPin(pin), pattern(PATTERN_OFF), lastToggleTime(0), ledState(false), doubleBinkPhase(0) {
    
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    lastToggleTime = millis();
}

void LightCode::setPattern(BlinkPattern newPattern) {
    // Log pattern change for debugging
    const char* patternNames[] = {"OFF", "SOLID", "SLOW_BLINK", "FAST_BLINK", "DOUBLE_BLINK"};
    const char* oldPatternName = (pattern >= 0 && pattern <= 4) ? patternNames[pattern] : "UNKNOWN";
    const char* newPatternName = (newPattern >= 0 && newPattern <= 4) ? patternNames[newPattern] : "UNKNOWN";
    
    if (pattern != newPattern) {
        LOG_DEBUG("[LIGHT] Pattern change: %s -> %s", oldPatternName, newPatternName);
    }
    
    this->pattern = newPattern;
    lastToggleTime = millis();
    ledState = false;
    doubleBinkPhase = 0;
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
            interval = 100; // 100ms on, 100ms off
            break;
        case PATTERN_SLOW_BLINK:
            interval = 500;
            break;
        case PATTERN_DOUBLE_BLINK:
            // Double blink pattern: ON-OFF-ON-OFF with pause
            // Phase 0: ON (150ms), Phase 1: OFF (150ms), Phase 2: ON (150ms), Phase 3: OFF (800ms)
            switch (doubleBinkPhase) {
                case 0: interval = 150; ledState = true; break;  // First ON
                case 1: interval = 150; ledState = false; break; // First OFF
                case 2: interval = 150; ledState = true; break;  // Second ON
                case 3: interval = 800; ledState = false; break; // Long OFF (pause)
            }
            
            if (msSinceToggle >= interval) {
                digitalWrite(ledPin, ledState ? HIGH : LOW);
                lastToggleTime = now;
                doubleBinkPhase = (doubleBinkPhase + 1) % 4; // Cycle through 0-3
            }
            return;
    }
    // Only toggle if interval has passed
    if (msSinceToggle >= interval) {
        ledState = !ledState;
        lastToggleTime = now;
        digitalWrite(ledPin, ledState ? HIGH : LOW);
    }
}