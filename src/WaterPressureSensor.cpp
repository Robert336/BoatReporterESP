#include "WaterPressureSensor.h"
#include "Logger.h"
#include <Arduino.h>
#include <Wire.h>
#include <algorithm>

WaterPressureSensor::WaterPressureSensor(bool mock)
    : currentReadingIndex(0), useMockData(mock), mockWaterLevel(0),
      adcCalHandle(nullptr), calibrationInitialized(false), zeroReadingVoltage_mv(590),
      secondPointVoltage_mv(0), secondPointLevel_cm(0.0f), twoPointCalibrationActive(false),
      lastLogTime(0), lastSampleTime(0), lastReading{},
      busRecoveryAttempts(0), busUnrecoverable(false),
      rateBufferIndex(0), lastRateSampleTime(0),
      lastStuckRawADC(0), stuckSampleCount(0) {
    for (int i = 0; i < READINGS_BUFFER_SIZE; i++) {
        readingsBuffer[i].valid = false;
        readingsBuffer[i].level_cm = 0;
        readingsBuffer[i].timestamp = Timestamp{false,0,0};
    }
    for (int i = 0; i < RATE_BUFFER_SIZE; i++) {
        rateBuffer[i].valid = false;
        rateBuffer[i].level_cm = 0.0f;
        rateBuffer[i].millis_ts = 0;
    }
}


bool WaterPressureSensor::init() {

    // When mocked, don't setup i2c with ADC as this will error
    if (!useMockData) {
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        Wire.setClock(100000); // 100kHz — ADS1115 can time out at 400kHz default
        if (!ads.begin()) {
            LOG_CRITICAL("WaterPressureSensor: ADS1115 not found on I2C bus (SDA=%d SCL=%d)", I2C_SDA_PIN, I2C_SCL_PIN);
            return false;
        }
        ads.setGain(GAIN_ONE);
        ads.setDataRate(RATE_ADS1115_32SPS);
        ads.startADCReading(MUX_BY_CHANNEL[CHANNEL], /*continuous=*/true);
        // Ensure first readLevel() call samples immediately
        lastSampleTime = millis() - 1001;
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
        // Slow sine wave: 1-hour period, range 0-60cm
        // Crosses Tier 1 (30cm) and Tier 2 (50cm) thresholds each cycle
        uint32_t t = millis();
        float base = 30.0f + 30.0f * sin(2.0f * 3.14159f * t / 3600000.0f);
        mockWaterLevel = base + (float)(random(-200, 200)) / 100.0f;
        if (mockWaterLevel < 0.0f) mockWaterLevel = 0.0f;
        reading.level_cm = mockWaterLevel;
        reading.millivolts = 590.0f + (mockWaterLevel / 100.0f) * (4096.0f - 590.0f);
    } else {
        if (millis() - lastSampleTime < 1000) {
            return lastReading;
        }
        lastSampleTime = millis();

        if (busUnrecoverable) {
            reading.valid = false;
            reading.timestamp = TimeManagement::getInstance().getCurrentTimestamp();
            lastReading = reading;
            return reading;
        }

        Wire.beginTransmission(ADS1115_I2C_ADDRESS);
        if (Wire.endTransmission() != 0) {
            busRecoveryAttempts++;
            LOG_CRITICAL("WaterPressureSensor: I2C bus error, recovery attempt %d/%d", busRecoveryAttempts, BUS_RECOVERY_MAX_ATTEMPTS);
            if (busRecoveryAttempts >= BUS_RECOVERY_MAX_ATTEMPTS) {
                busUnrecoverable = true;
                LOG_CRITICAL("WaterPressureSensor: I2C bus unrecoverable after %d attempts", BUS_RECOVERY_MAX_ATTEMPTS);
            } else {
                recoverBus();
            }
            reading.valid = false;
            reading.timestamp = TimeManagement::getInstance().getCurrentTimestamp();
            lastReading = reading;
            return reading;
        }

        int16_t rawADC = ads.getLastConversionResults();
        // Compute voltage once and reuse — avoids calling ads.computeVolts() twice
        // on the same raw value (P6 fix).
        float computedVolts = ads.computeVolts(rawADC);
        reading.millivolts = computedVolts * 1000.0f;
        uint32_t now = millis();
        if (now - lastLogTime >= 1000) {
            LOG_DEBUG("WaterPressureSensor: millivolts reading = %.2f mV", reading.millivolts);
            LOG_DEBUG("WaterPressureSensor: raw ADC = %d, computedVolts = %.5f V", rawADC, computedVolts);
            lastLogTime = now;
        }
        reading.level_cm = voltageToCentimeters(reading.millivolts);
        if (reading.millivolts < (zeroReadingVoltage_mv - READING_ERROR_MARGIN_MV)) reading.valid = false;

        // Over-range guard: the sensor can't physically read past its span, so a
        // higher computed level means an electrical fault, not rising water.
        if (reading.level_cm > WATER_LEVEL_RANGE_MAX_CM + READING_OVERRANGE_MARGIN_CM) {
            reading.valid = false;
        }

        // Flatline guard: identical raw codes for minutes => frozen/dead sensor.
        if (rawADC == lastStuckRawADC) {
            stuckSampleCount++;
            if (stuckSampleCount == (uint32_t)STUCK_SAMPLE_THRESHOLD) {
                LOG_CRITICAL("WaterPressureSensor: flatline detected — raw ADC %d unchanged for %d samples",
                             rawADC, STUCK_SAMPLE_THRESHOLD);
            }
        } else {
            stuckSampleCount = 0;
            lastStuckRawADC = rawADC;
        }
        if (stuckSampleCount >= (uint32_t)STUCK_SAMPLE_THRESHOLD) {
            reading.valid = false;
        }
    }

    reading.timestamp = TimeManagement::getInstance().getCurrentTimestamp();
    bufferPush(reading);
    reading.level_cm = calculateMedianFromBuffer();
    lastReading = reading;

    if (reading.valid && (lastRateSampleTime == 0 || millis() - lastRateSampleTime >= RATE_SAMPLE_INTERVAL_MS)) {
        lastRateSampleTime = millis();
        rateBufferIndex = (rateBufferIndex + 1) % RATE_BUFFER_SIZE;
        rateBuffer[rateBufferIndex] = { reading.level_cm, lastRateSampleTime, true };
    }

    return reading;
}


float WaterPressureSensor::getRateOfChange_cm30min() const {
    const LevelSnapshot* oldest = nullptr;
    const LevelSnapshot* newest = nullptr;

    for (int i = 0; i < RATE_BUFFER_SIZE; i++) {
        if (!rateBuffer[i].valid) continue;
        if (!oldest || rateBuffer[i].millis_ts < oldest->millis_ts) oldest = &rateBuffer[i];
        if (!newest || rateBuffer[i].millis_ts > newest->millis_ts) newest = &rateBuffer[i];
    }

    if (!oldest || !newest || oldest == newest) return NAN;

    uint32_t delta_ms = newest->millis_ts - oldest->millis_ts;
    if (delta_ms < 1000) return NAN;

    float delta_cm = newest->level_cm - oldest->level_cm;
    float delta_min = delta_ms / 60000.0f;
    return delta_cm / delta_min * 30.0f;
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

void WaterPressureSensor::recoverBus() {
    // Clock SCL 9 times to release a slave holding SDA low mid-transaction
    pinMode(I2C_SCL_PIN, OUTPUT);
    pinMode(I2C_SDA_PIN, OUTPUT);
    digitalWrite(I2C_SDA_PIN, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, LOW);
        delayMicroseconds(5);
    }
    // Issue STOP condition: SDA low -> SCL high -> SDA high
    digitalWrite(I2C_SDA_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH);
    delayMicroseconds(5);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000);
    if (ads.begin()) {
        ads.setGain(GAIN_ONE);
        ads.setDataRate(RATE_ADS1115_32SPS);
        ads.startADCReading(MUX_BY_CHANNEL[CHANNEL], /*continuous=*/true);
        LOG_INFO("WaterPressureSensor: I2C bus recovered");
    }
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

