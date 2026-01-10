#include "WaterPressureSensor.h"
#include "Logger.h"
#include <Arduino.h>
#include <algorithm>

WaterPressureSensor::WaterPressureSensor(bool mock)
    : currentReadingIndex(0), useMockData(mock), mockWaterLevel(0),
      adcCalHandle(nullptr), calibrationInitialized(false), zeroReadingVoltage_mv(590),
      secondPointVoltage_mv(0), secondPointLevel_cm(0.0f), twoPointCalibrationActive(false) {
    // Default zero level is 590mV (typical for our setup)
    // This can be calibrated by measuring the sensor voltage at 0cm water level
    
    Timestamp lastReadTime;

    // Initialize buffer to invalid readings
    for (int i = 0; i < READINGS_BUFFER_SIZE; i++) {
        readingsBuffer[i].valid = false;
        readingsBuffer[i].level_cm = 0;
        readingsBuffer[i].timestamp = Timestamp{false,0,0};
    }
}


bool WaterPressureSensor::init() {

    // When mocked, don't setup i2c with ADC as this will error
    if (!useMockData) {
        ads.begin();
        ads.setGain(GAIN_ONE);
        ads.setDataRate(RATE_ADS1115_8SPS);
    }

    SensorReading firstReading = readLevel();
    
    if (!firstReading.valid) {
        return false; 
    }
    return true;
}


WaterPressureSensor::~WaterPressureSensor() {
    // Clean up ADC calibration handle if it was initialized
    // (Note: Arduino framework handles ADC cleanup automatically)
    adcCalHandle = nullptr;
    calibrationInitialized = false;
}


void WaterPressureSensor::setZeroLevelMilliVolts(int millivolts) {
    zeroReadingVoltage_mv = millivolts;
}

void WaterPressureSensor::setCalibrationPoint(int pointIndex, int millivolts, float level_cm) {
    if (pointIndex == 0) {
        zeroReadingVoltage_mv = millivolts;
    } else if (pointIndex == 1) {
        secondPointVoltage_mv = millivolts;
        secondPointLevel_cm = level_cm;
        twoPointCalibrationActive = true;
    }
}

bool WaterPressureSensor::hasTwoPointCalibration() {
    return twoPointCalibrationActive;
}

int WaterPressureSensor::getZeroPointMilliVolts() {
    return zeroReadingVoltage_mv;
}

int WaterPressureSensor::getSecondPointMilliVolts() {
    return secondPointVoltage_mv;
}

float WaterPressureSensor::getSecondPointLevelCm() {
    return secondPointLevel_cm;
}


float WaterPressureSensor::voltageToCentimeters(int voltage_mv) {
    // Convert calibrated voltage (mV) to centimeters
    // This provides more accurate readings than raw ADC conversion
    
    // Use 2-point calibration if available
    if (twoPointCalibrationActive && secondPointVoltage_mv != zeroReadingVoltage_mv) {
        // Linear interpolation: y = y0 + (y1 - y0) * (x - x0) / (x1 - x0)
        // Where: x0 = zeroReadingVoltage_mv, y0 = 0.0
        //        x1 = secondPointVoltage_mv, y1 = secondPointLevel_cm
        //        x = voltage_mv, y = level_cm
        
        float voltage_diff = secondPointVoltage_mv - zeroReadingVoltage_mv;
        if (voltage_diff != 0) {
            float level_cm = (voltage_mv - zeroReadingVoltage_mv) * (secondPointLevel_cm / voltage_diff);
            return level_cm;
        }
    }
    
    // Fallback to single-point calibration
    const int MAX_VOLTAGE_MV = 4096;
    
    // Calculate voltage range
    float voltage_range = MAX_VOLTAGE_MV - zeroReadingVoltage_mv;
    
    // Calculate voltage per centimeter
    // Using the same conversion factor approach as rawToCentimeters
    // For a 100cm range: voltage_per_cm = voltage_range / CM_MAX
    // Or use a fixed value if you know the sensor's characteristics
    float voltage_per_cm = voltage_range / (float)CM_MAX;
    
    // Convert voltage to centimeters
    return (voltage_mv - zeroReadingVoltage_mv) / voltage_per_cm;
}


SensorReading WaterPressureSensor::readLevel() {
    SensorReading reading;    
    reading.valid = true; // Assume valid until proven otherwise
    
    if (useMockData) {
        mockWaterLevel = random(4, 20);
        reading.level_cm = mockWaterLevel;
    } else {
        
        int16_t rawADC = ads.readADC_SingleEnded(CHANNEL);
        reading.millivolts = ads.computeVolts(rawADC) * 1000;
        LOG_DEBUG("WaterPressureSensor: millivolts reading = %.2f mV", reading.millivolts);
        float computedVolts = ads.computeVolts(rawADC);
        LOG_DEBUG("WaterPressureSensor: raw ADC = %d, computedVolts = %.5f V", rawADC, computedVolts);
        // Use voltage-based conversion for accurate readings
        reading.level_cm = voltageToCentimeters(reading.millivolts);
        
        // Validate reading based on voltage
        if (reading.millivolts < (zeroReadingVoltage_mv - READING_ERROR_MARGIN_MV)) reading.valid = false;
    }
    
    reading.timestamp = TimeManagement::getInstance().getCurrentTimestamp();
    bufferPush(reading);

    // Return the median of the buffer instead of the raw reading
    reading.level_cm = calculateMedianFromBuffer();
    
    return reading;
}


void WaterPressureSensor::bufferPush(SensorReading newReading) {
    int bufferArraySize = sizeof(this->readingsBuffer) / sizeof(SensorReading);
    if (this->currentReadingIndex == bufferArraySize - 1) {
        this->currentReadingIndex = 0;
    } else {
        this->currentReadingIndex += 1;
    }
    this->readingsBuffer[currentReadingIndex] = newReading;
}

float WaterPressureSensor::calculateMedianFromBuffer() {
    // Create a temporary array of valid readings from the buffer
    float validReadings[READINGS_BUFFER_SIZE];
    int validCount = 0;
    
    // Collect all valid readings
    for (int i = 0; i < READINGS_BUFFER_SIZE; i++) {
        if (readingsBuffer[i].valid) {
            validReadings[validCount] = readingsBuffer[i].level_cm;
            validCount++;
        }
    }
    
    // If no valid readings, return 0
    if (validCount == 0) {
        return 0;
    }
    
    // Sort the valid readings
    std::sort(validReadings, validReadings + validCount);
    
    // Calculate median
    if (validCount % 2 == 1) {
        // Odd number of readings: return the middle one
        return validReadings[validCount / 2];
    } else {
        // Even number of readings: return average of two middle values
        return (validReadings[validCount / 2 - 1] + validReadings[validCount / 2]) / 2.0;
    }
}

