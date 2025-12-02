#pragma once

/*
    Library to easily define and handle messaging through LEDs signaling
*/
#include <Arduino.h>

enum BlinkPattern {
    PATTERN_OFF,
    PATTERN_SOLID,
    PATTERN_SLOW_BLINK,
    PATTERN_FAST_BLINK
};

class LightCode {
    private:
        int ledPin;
        BlinkPattern pattern;
        unsigned long lastToggleTime;
        bool ledState; // True = ON

    public:
        LightCode(int ledPin);
        void setPattern(BlinkPattern pattern);
        // Call this frequently to update the state of the light
        // This is non-blocking
        void update();

};

