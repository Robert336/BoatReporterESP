#pragma once

#ifdef UNIT_TESTING

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Mock Arduino types
typedef bool boolean;
typedef uint8_t byte;

// Pin modes
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

// Pin states
#define LOW 0x0
#define HIGH 0x1

// Mock time tracking
static uint32_t mock_millis_value = 0;

// Arduino core functions
inline uint32_t millis() {
    return mock_millis_value;
}

inline void delay(uint32_t ms) {
    mock_millis_value += ms;
}

inline void pinMode(uint8_t pin, uint8_t mode) {
    // No-op for testing
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    // No-op for testing
}

inline int digitalRead(uint8_t pin) {
    return HIGH; // Default state
}

// Mock Serial for logging
class MockSerial {
public:
    void begin(unsigned long baud) {}
    
    void printf(const char* format, ...) {
        // Optionally print to stdout for debugging
        // va_list args;
        // va_start(args, format);
        // vprintf(format, args);
        // va_end(args);
    }
    
    void println() {}
    void println(const char* str) {}
    void print(const char* str) {}
};

// Global Serial object - defined in MockArduino.cpp to avoid ODR violations
extern MockSerial Serial;

// Mock random function
inline long random(long min, long max) {
    return min + (rand() % (max - min));
}

// Mock String class (simplified) - Rule of Three compliant
class String {
public:
    String() : data(nullptr), len(0) {}
    
    String(const char* str) {
        if (str) {
            len = strlen(str);
            data = new char[len + 1];
            strcpy(data, str);
        } else {
            data = nullptr;
            len = 0;
        }
    }
    
    // Copy constructor
    String(const String& other) {
        if (other.data) {
            len = other.len;
            data = new char[len + 1];
            strcpy(data, other.data);
        } else {
            data = nullptr;
            len = 0;
        }
    }
    
    // Copy assignment operator
    String& operator=(const String& other) {
        if (this != &other) {
            delete[] data;
            if (other.data) {
                len = other.len;
                data = new char[len + 1];
                strcpy(data, other.data);
            } else {
                data = nullptr;
                len = 0;
            }
        }
        return *this;
    }
    
    ~String() {
        if (data) delete[] data;
    }
    
    const char* c_str() const {
        return data ? data : "";
    }
    
    size_t length() const { return len; }
    
private:
    char* data;
    size_t len;
};

// Helper function to set mock time (for testing)
inline void setMockMillis(uint32_t value) {
    mock_millis_value = value;
}

// Helper function to advance mock time (for testing)
inline void advanceMockMillis(uint32_t delta) {
    mock_millis_value += delta;
}

#endif // UNIT_TESTING
