#pragma once

#include <cstdint>
#include <cmath>
#include "TimeManagement.h"
#include <Adafruit_ADS1X15.h>


static constexpr int I2C_SDA_PIN = 21;
static constexpr int I2C_SCL_PIN = 22;
static constexpr uint8_t ADS1115_I2C_ADDRESS = 0x48;
static constexpr int BUS_RECOVERY_MAX_ATTEMPTS = 10;

// The usable range of the water sensor in centimeters
constexpr float WATER_LEVEL_RANGE_MIN_CM = 5.0;
constexpr float WATER_LEVEL_RANGE_MAX_CM = 100.0;
static constexpr int READING_ERROR_MARGIN_MV = 15;

// Size of the circular buffer for smoothing readings
static constexpr int READINGS_BUFFER_SIZE = 10; 

static constexpr uint8_t CHANNEL = 0;
static constexpr int CM_MAX = 100; // Max centimeters the sensor can read

// Rate-of-change tracking: one snapshot every 5 min, 7 slots = 30 min window
static constexpr int RATE_BUFFER_SIZE = 7;
static constexpr uint32_t RATE_SAMPLE_INTERVAL_MS = 300000; // 5 minutes

struct SensorReading {
    bool valid;   // Check if the reading is trustworthy
    float level_cm; // Water level in centimeters
    float millivolts; // Millivolts reading according to the ESP's ADC
    Timestamp timestamp;
};

struct LevelSnapshot {
    float level_cm;
    uint32_t millis_ts;
    bool valid;
};

class WaterPressureSensor {
public: 
    WaterPressureSensor(bool mock = false);
    ~WaterPressureSensor();
    bool init(); // setup sensor
    SensorReading readLevel();
    void setZeroLevelMilliVolts(int millivolts); // Configure the voltage reading at 0cm water level
    void setCalibrationPoint(int pointIndex, int millivolts, float level_cm); // Set calibration point (0=zero, 1=second point)
    bool hasTwoPointCalibration(); // Check if 2-point calibration is configured
    int getZeroPointMilliVolts(); // Get zero point voltage
    int getSecondPointMilliVolts(); // Get second point voltage
    float getSecondPointLevelCm(); // Get second point level
    bool isBusUnrecoverable() const { return busUnrecoverable; }

    // Returns cm change extrapolated to 30 min from oldest→newest snapshot.
    // Returns NAN when fewer than 2 valid snapshots exist.
    float getRateOfChange_cm30min() const;

    // Made public for unit testing - convert voltage to water level
    float voltageToCentimeters(int voltage_mv);

private:
    Adafruit_ADS1115 ads;
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
    
    void bufferPush(SensorReading newReading);
    float calculateMedianFromBuffer(); // Calculate rolling median of buffer
    void recoverBus();

    uint32_t lastLogTime;    // Throttle debug logging
    uint32_t lastSampleTime; // millis() of last ADC read (1-second gate)
    SensorReading lastReading; // cached result returned between samples
    int busRecoveryAttempts;
    bool busUnrecoverable;

    LevelSnapshot rateBuffer[RATE_BUFFER_SIZE];
    int rateBufferIndex;
    uint32_t lastRateSampleTime;
};

