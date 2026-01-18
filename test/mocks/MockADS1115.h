#pragma once

#ifdef UNIT_TESTING

#include <stdint.h>

// Mock gain settings
typedef enum {
    GAIN_TWOTHIRDS = 0,
    GAIN_ONE = 1,
    GAIN_TWO = 2,
    GAIN_FOUR = 3,
    GAIN_EIGHT = 4,
    GAIN_SIXTEEN = 5
} adsGain_t;

// Mock data rate settings
#define RATE_ADS1115_8SPS    (0x0000)
#define RATE_ADS1115_16SPS   (0x0020)
#define RATE_ADS1115_32SPS   (0x0040)
#define RATE_ADS1115_64SPS   (0x0060)
#define RATE_ADS1115_128SPS  (0x0080)
#define RATE_ADS1115_250SPS  (0x00A0)
#define RATE_ADS1115_475SPS  (0x00C0)
#define RATE_ADS1115_860SPS  (0x00E0)

// Mock Adafruit_ADS1115 class
class Adafruit_ADS1115 {
public:
    Adafruit_ADS1115() : mockRawValue(0), mockVoltage(0.0) {}
    
    bool begin() {
        return true; // Always succeeds in mock
    }
    
    void setGain(adsGain_t gain) {
        currentGain = gain;
    }
    
    void setDataRate(uint16_t rate) {
        dataRate = rate;
    }
    
    int16_t readADC_SingleEnded(uint8_t channel) {
        return mockRawValue;
    }
    
    float computeVolts(int16_t rawValue) {
        // If mock voltage is set, use it; otherwise compute from raw value
        if (mockVoltage > 0.0001) {
            return mockVoltage;
        }
        
        // Simulate voltage conversion (simplified)
        // ADS1115 is 16-bit: -32768 to 32767
        // With GAIN_ONE, range is ±4.096V
        float voltsPerBit = 4.096 / 32768.0;
        return rawValue * voltsPerBit;
    }
    
    // Test helper methods
    void setMockRawValue(int16_t value) {
        mockRawValue = value;
    }
    
    void setMockVoltage(float voltage) {
        mockVoltage = voltage;
        // Calculate corresponding raw value
        float voltsPerBit = 4.096 / 32768.0;
        mockRawValue = (int16_t)(voltage / voltsPerBit);
    }
    
    void setMockMillivolts(int millivolts) {
        setMockVoltage(millivolts / 1000.0);
    }
    
private:
    int16_t mockRawValue;
    float mockVoltage;
    adsGain_t currentGain;
    uint16_t dataRate;
};

#endif // UNIT_TESTING
