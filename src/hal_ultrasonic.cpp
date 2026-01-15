#include "hal_ultrasonic.h"

// Static capabilities for Ultrasonic sensor
const SensorCapabilities HAL_Ultrasonic::s_capabilities = {
    .supportsBinaryDetection = true,
    .supportsDistanceMeasurement = true,
    .supportsDirectionDetection = true,
    .requiresWarmup = false,
    .supportsDeepSleepWake = false,  // Requires active measurement
    .minDetectionDistance = 20,       // 2cm minimum
    .maxDetectionDistance = 4000,     // 400cm maximum
    .detectionAngleDegrees = 15,      // 15 degree cone
    .typicalWarmupMs = 0,
    .typicalCurrentMa = 15,           // ~15mA during measurement
    .sensorTypeName = "Ultrasonic Distance Sensor (HC-SR04)"
};

HAL_Ultrasonic::HAL_Ultrasonic(uint8_t triggerPin, uint8_t echoPin, bool mock_mode)
    : m_triggerPin(triggerPin)
    , m_echoPin(echoPin)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_currentDistance(0)
    , m_lastDistance(0)
    , m_detectionThreshold(DEFAULT_THRESHOLD_MM)
    , m_minDistance(SENSOR_MIN_DISTANCE_CM * 10)  // Convert cm to mm
    , m_maxDistance(SENSOR_MAX_DISTANCE_CM * 10)  // Convert cm to mm
    , m_objectDetected(false)
    , m_directionEnabled(true)
    , m_direction(DIRECTION_UNKNOWN)
    , m_directionSensitivity(DEFAULT_SENSITIVITY_MM)
    , m_rapidSampleCount(SENSOR_RAPID_SAMPLE_COUNT)
    , m_rapidSampleMs(SENSOR_RAPID_SAMPLE_MS)
    , m_lastEvent(MOTION_EVENT_NONE)
    , m_eventCount(0)
    , m_lastEventTime(0)
    , m_lastMeasurementTime(0)
    , m_measurementInterval(MIN_MEASUREMENT_INTERVAL_MS)
    , m_mockDistance(0)
{
}

HAL_Ultrasonic::~HAL_Ultrasonic() {
    // No cleanup needed
}

bool HAL_Ultrasonic::begin() {
    if (m_initialized) {
        return true;
    }

    DEBUG_PRINTLN("[HAL_Ultrasonic] Initializing ultrasonic sensor...");

    if (!m_mockMode) {
        // Configure GPIO pins
        pinMode(m_triggerPin, OUTPUT);
        pinMode(m_echoPin, INPUT);

        // Ensure trigger is LOW
        digitalWrite(m_triggerPin, LOW);

        DEBUG_PRINTF("[HAL_Ultrasonic] Trigger pin %d, Echo pin %d configured\n",
                     m_triggerPin, m_echoPin);
    } else {
        DEBUG_PRINTLN("[HAL_Ultrasonic] MOCK MODE: Simulating sensor");
    }

    m_initialized = true;

    DEBUG_PRINTF("[HAL_Ultrasonic] Detection threshold: %u mm\n", m_detectionThreshold);
    DEBUG_PRINTLN("[HAL_Ultrasonic] Initialization complete");

    return true;
}

void HAL_Ultrasonic::update() {
    if (!m_initialized) {
        return;
    }

    // Rate limit measurements
    uint32_t now = millis();
    if (now - m_lastMeasurementTime < m_measurementInterval) {
        return;
    }
    m_lastMeasurementTime = now;

    // Store previous distance for direction detection
    m_lastDistance = m_currentDistance;

    // Perform measurement
    if (!m_mockMode) {
        m_currentDistance = measureDistance();
    } else {
        m_currentDistance = m_mockDistance;
    }

    // Update direction if enabled
    if (m_directionEnabled) {
        updateDirection();
    }

    // Check for threshold crossing events
    checkThresholdEvents();
}

uint32_t HAL_Ultrasonic::measureDistance() {
    if (m_mockMode) {
        return m_mockDistance;
    }

#ifndef MOCK_HARDWARE
    // Send trigger pulse (10us minimum)
    digitalWrite(m_triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(m_triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(m_triggerPin, LOW);

    // Measure echo pulse duration
    unsigned long duration = pulseIn(m_echoPin, HIGH, MEASUREMENT_TIMEOUT_US);

    if (duration == 0) {
        // Timeout - no echo received (object out of range or no object)
        return 0;
    }

    // Calculate distance in mm
    // Sound travels at ~343 m/s = 0.343 mm/us
    // Distance = (duration / 2) * 0.343 (divide by 2 for round trip)
    uint32_t distance_mm = (uint32_t)((duration / 2.0f) * SOUND_SPEED_MM_PER_US);

    // Clamp to valid range
    if (distance_mm < s_capabilities.minDetectionDistance) {
        distance_mm = 0;  // Too close, invalid reading
    } else if (distance_mm > s_capabilities.maxDetectionDistance) {
        distance_mm = 0;  // Out of range
    }

    return distance_mm;
#else
    return m_mockDistance;
#endif
}

bool HAL_Ultrasonic::motionDetected() const {
    return m_objectDetected;
}

bool HAL_Ultrasonic::isReady() const {
    return m_initialized;
}

const SensorCapabilities& HAL_Ultrasonic::getCapabilities() const {
    return s_capabilities;
}

void HAL_Ultrasonic::resetEventCount() {
    m_eventCount = 0;
    DEBUG_PRINTLN("[HAL_Ultrasonic] Event counter reset");
}

void HAL_Ultrasonic::setDetectionThreshold(uint32_t threshold_mm) {
    m_detectionThreshold = threshold_mm;
    DEBUG_PRINTF("[HAL_Ultrasonic] Detection threshold set to %u mm\n", threshold_mm);
}

void HAL_Ultrasonic::setDirectionDetection(bool enable) {
    m_directionEnabled = enable;
    if (!enable) {
        m_direction = DIRECTION_UNKNOWN;
    }
    DEBUG_PRINTF("[HAL_Ultrasonic] Direction detection %s\n",
                 enable ? "enabled" : "disabled");
}

void HAL_Ultrasonic::setMeasurementInterval(uint32_t interval_ms) {
    m_measurementInterval = (interval_ms < MIN_MEASUREMENT_INTERVAL_MS)
                            ? MIN_MEASUREMENT_INTERVAL_MS
                            : interval_ms;
    DEBUG_PRINTF("[HAL_Ultrasonic] Measurement interval set to %u ms\n",
                 m_measurementInterval);
}

void HAL_Ultrasonic::setDirectionSensitivity(uint32_t sensitivity_mm) {
    m_directionSensitivity = sensitivity_mm;
    DEBUG_PRINTF("[HAL_Ultrasonic] Direction sensitivity set to %u mm\n",
                 sensitivity_mm);
}

void HAL_Ultrasonic::updateDirection() {
    if (m_currentDistance == 0 || m_lastDistance == 0) {
        m_direction = DIRECTION_UNKNOWN;
        return;
    }

    int32_t delta = (int32_t)m_currentDistance - (int32_t)m_lastDistance;

    if (abs(delta) < (int32_t)m_directionSensitivity) {
        m_direction = DIRECTION_STATIONARY;
    } else if (delta < 0) {
        // Distance decreasing = object approaching
        m_direction = DIRECTION_APPROACHING;
    } else {
        // Distance increasing = object receding
        m_direction = DIRECTION_RECEDING;
    }
}

void HAL_Ultrasonic::checkThresholdEvents() {
    bool wasDetected = m_objectDetected;

    // Object is detected if distance is valid and within min/max range
    m_objectDetected = (m_currentDistance > 0 &&
                        m_currentDistance >= m_minDistance &&
                        m_currentDistance <= m_maxDistance);

    // Rising edge - object entered detection zone
    if (m_objectDetected && !wasDetected) {
        m_eventCount++;
        m_lastEventTime = millis();
        m_lastEvent = MOTION_EVENT_THRESHOLD_CROSSED;
        DEBUG_PRINTF("[HAL_Ultrasonic] Object detected at %u mm (event #%u)\n",
                     m_currentDistance, m_eventCount);
    }

    // Falling edge - object left detection zone
    if (!m_objectDetected && wasDetected) {
        m_lastEventTime = millis();
        m_lastEvent = MOTION_EVENT_CLEARED;
        DEBUG_PRINTLN("[HAL_Ultrasonic] Object left detection zone");
    }

    // Direction-based events (if enabled)
    if (m_directionEnabled && m_objectDetected) {
        if (m_direction == DIRECTION_APPROACHING &&
            m_lastEvent != MOTION_EVENT_APPROACHING) {
            m_lastEvent = MOTION_EVENT_APPROACHING;
            m_lastEventTime = millis();
            DEBUG_PRINTF("[HAL_Ultrasonic] Object approaching at %u mm\n",
                         m_currentDistance);
        } else if (m_direction == DIRECTION_RECEDING &&
                   m_lastEvent != MOTION_EVENT_RECEDING) {
            m_lastEvent = MOTION_EVENT_RECEDING;
            m_lastEventTime = millis();
            DEBUG_PRINTF("[HAL_Ultrasonic] Object receding at %u mm\n",
                         m_currentDistance);
        }
    }
}

// =========================================================================
// Mock Mode Methods
// =========================================================================

void HAL_Ultrasonic::mockSetMotion(bool detected) {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Ultrasonic] WARNING: mockSetMotion() called but mock mode not enabled");
        return;
    }

    if (detected) {
        // Set distance to half of threshold (detected)
        m_mockDistance = m_detectionThreshold / 2;
    } else {
        // Set distance to double threshold (not detected)
        m_mockDistance = m_detectionThreshold * 2;
    }

    DEBUG_PRINTF("[HAL_Ultrasonic] MOCK: Motion set to %s (distance: %u mm)\n",
                 detected ? "TRUE" : "FALSE", m_mockDistance);
}

void HAL_Ultrasonic::mockSetDistance(uint32_t distance_mm) {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Ultrasonic] WARNING: mockSetDistance() called but mock mode not enabled");
        return;
    }

    m_mockDistance = distance_mm;
    DEBUG_PRINTF("[HAL_Ultrasonic] MOCK: Distance set to %u mm\n", distance_mm);
}

// =========================================================================
// Distance Range and Rapid Sampling Methods
// =========================================================================

void HAL_Ultrasonic::setDistanceRange(uint32_t min_mm, uint32_t max_mm) {
    if (min_mm >= max_mm) {
        DEBUG_PRINTLN("[HAL_Ultrasonic] ERROR: min distance must be less than max distance");
        return;
    }

    m_minDistance = min_mm;
    m_maxDistance = max_mm;

    DEBUG_PRINTF("[HAL_Ultrasonic] Distance range set to %u - %u mm\n", min_mm, max_mm);
}

void HAL_Ultrasonic::setRapidSampling(uint8_t sample_count, uint16_t interval_ms) {
    if (sample_count < 2) {
        sample_count = 2;
    } else if (sample_count > 20) {
        sample_count = 20;
    }

    if (interval_ms < 50) {
        interval_ms = 50;
    }

    m_rapidSampleCount = sample_count;
    m_rapidSampleMs = interval_ms;

    DEBUG_PRINTF("[HAL_Ultrasonic] Rapid sampling configured: %u samples @ %u ms intervals\n",
                 sample_count, interval_ms);
}

void HAL_Ultrasonic::triggerRapidSample() {
    if (!m_initialized) {
        return;
    }

    DEBUG_PRINTLN("[HAL_Ultrasonic] Starting rapid sampling for direction detection");

    // Allocate buffer for samples
    uint32_t samples[20];  // Max 20 samples
    uint8_t validSamples = 0;

    // Take rapid samples
    for (uint8_t i = 0; i < m_rapidSampleCount; i++) {
        uint32_t distance;

        if (!m_mockMode) {
            distance = measureDistance();
        } else {
            distance = m_mockDistance;
        }

        // Only store valid readings
        if (distance > 0) {
            samples[validSamples++] = distance;
        }

        // Wait before next sample (except for last sample)
        if (i < m_rapidSampleCount - 1) {
            delay(m_rapidSampleMs);
        }
    }

    if (validSamples >= 2) {
        // Update direction based on rapid samples
        updateDirectionFromSamples(samples, validSamples);

        // Update current distance to average of samples
        uint32_t sum = 0;
        for (uint8_t i = 0; i < validSamples; i++) {
            sum += samples[i];
        }
        m_currentDistance = sum / validSamples;

        DEBUG_PRINTF("[HAL_Ultrasonic] Rapid sampling complete: %u valid samples, avg distance: %u mm, direction: %d\n",
                     validSamples, m_currentDistance, (int)m_direction);

        // Check for events
        checkThresholdEvents();
    } else {
        DEBUG_PRINTLN("[HAL_Ultrasonic] Rapid sampling failed: insufficient valid samples");
    }
}

void HAL_Ultrasonic::updateDirectionFromSamples(const uint32_t* samples, uint8_t count) {
    if (count < 2) {
        m_direction = DIRECTION_UNKNOWN;
        return;
    }

    // Calculate linear regression to determine trend
    // Using simplified approach: compare first half average vs second half average
    uint8_t midpoint = count / 2;

    uint32_t firstHalfSum = 0;
    uint32_t secondHalfSum = 0;

    for (uint8_t i = 0; i < midpoint; i++) {
        firstHalfSum += samples[i];
    }

    for (uint8_t i = midpoint; i < count; i++) {
        secondHalfSum += samples[i];
    }

    float firstHalfAvg = (float)firstHalfSum / midpoint;
    float secondHalfAvg = (float)secondHalfSum / (count - midpoint);

    int32_t delta = (int32_t)(secondHalfAvg - firstHalfAvg);

    if (abs(delta) < (int32_t)m_directionSensitivity) {
        m_direction = DIRECTION_STATIONARY;
    } else if (delta < 0) {
        // Distance decreasing over time = approaching
        m_direction = DIRECTION_APPROACHING;
    } else {
        // Distance increasing over time = receding
        m_direction = DIRECTION_RECEDING;
    }

    DEBUG_PRINTF("[HAL_Ultrasonic] Direction from samples: first_half=%.1f, second_half=%.1f, delta=%ld, direction=%d\n",
                 firstHalfAvg, secondHalfAvg, delta, (int)m_direction);
}
