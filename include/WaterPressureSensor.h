#pragma once

#include <cstdint>
#include "TimeManagement.h"
#include <Adafruit_ADS1X15.h>


struct SensorReading {
    bool valid;   // Check if the reading is trustworthy
    float level_cm; // Water level in centimeters
    float millivolts; // Millivolts reading according to the ESP's ADC
    Timestamp timestamp;
};

class WaterPressureSensor {
public: 
    WaterPressureSensor(bool mock = false);
    ~WaterPressureSensor();
    bool init(); // setup sensor
    SensorReading readLevel();
    float getRollingRateOfChange(); // cm/sec over last 10s
    void setZeroLevelMilliVolts(int millivolts); // Configure the voltage reading at 0cm water level
    void setCalibrationPoint(int pointIndex, int millivolts, float level_cm); // Set calibration point (0=zero, 1=second point)
    bool hasTwoPointCalibration(); // Check if 2-point calibration is configured
    int getZeroPointMilliVolts(); // Get zero point voltage
    int getSecondPointMilliVolts(); // Get second point voltage
    float getSecondPointLevelCm(); // Get second point level

private:
    Adafruit_ADS1115 ads;
    const uint8_t CHANNEL = 0;
    const int CM_MAX = 100; // Max centimeters the sensor can read
    static constexpr int READINGS_BUFFER_SIZE = 10; // Size of the circular buffer for smoothing readings
    SensorReading readingsBuffer[READINGS_BUFFER_SIZE]; // Circular buffer for the last readings
    int currentReadingIndex;
    Timestamp lastReadTime;
    int zeroReadingVoltage_mv; // Voltage (mV) at zero water level (0cm of water)
    int secondPointVoltage_mv; // Voltage (mV) at second calibration point
    float secondPointLevel_cm; // Water level (cm) at second calibration point
    bool twoPointCalibrationActive; // Whether 2-point calibration is active
    

    // For testing using mock data
    bool useMockData;
    float mockWaterLevel;

    // ADC calibration
    void* adcCalHandle;  // Opaque handle for ADC calibration (void* for Arduino compatibility)
    bool calibrationInitialized;
    
    float voltageToCentimeters(int voltage_mv); // Convert calibrated voltage to centimeters
    void bufferPush(SensorReading newReading);
    float calculateMedianFromBuffer(); // Calculate rolling median of buffer
};

