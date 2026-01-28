#include "hal_pir.h"
#include "logger.h"
#include "debug_logger.h"

// Static capabilities for PIR sensor
const SensorCapabilities HAL_PIR::s_capabilities = {
    .supportsBinaryDetection = true,
    .supportsDistanceMeasurement = false,
    .supportsDirectionDetection = false,
    .requiresWarmup = true,
    .supportsDeepSleepWake = true,
    .minDetectionDistance = 0,
    .maxDetectionDistance = 12000,     // 12m typical for AM312
    .detectionAngleDegrees = 65,       // 65 degree FOV
    .typicalWarmupMs = 60000,          // 60 seconds
    .typicalCurrentMa = 1,             // ~220uA, round up to 1mA
    .sensorTypeName = "PIR Motion Sensor (AM312)"
};

HAL_PIR::HAL_PIR(uint8_t pin, bool mock_mode)
    : m_pin(pin)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_motionDetected(false)
    , m_lastState(false)
    , m_sensorReady(false)
    , m_lastEvent(MOTION_EVENT_NONE)
    , m_startTime(0)
    , m_warmupDuration(PIR_WARMUP_TIME_MS)
    , m_lastEventTime(0)
    , m_motionEventCount(0)
    , m_mockMotionEndTime(0)
{
}

HAL_PIR::~HAL_PIR() {
    // No cleanup needed
}

bool HAL_PIR::begin() {
    if (m_initialized) {
        return true;
    }

    DEBUG_LOG_SENSOR("HAL_PIR: Initializing PIR sensor...");

    if (!m_mockMode) {
        // Configure GPIO pin as input
        pinMode(m_pin, INPUT);
        DEBUG_LOG_SENSOR("HAL_PIR: Pin %d configured as INPUT", m_pin);
    } else {
        DEBUG_LOG_SENSOR("HAL_PIR: MOCK MODE - Simulating sensor");
    }

    m_startTime = millis();
    m_initialized = true;

    DEBUG_LOG_SENSOR("HAL_PIR: Warm-up period: %u ms (~%u seconds)",
              m_warmupDuration, m_warmupDuration / 1000);
    DEBUG_LOG_SENSOR("HAL_PIR: Initialization complete");

    return true;
}

void HAL_PIR::update() {
    if (!m_initialized) {
        return;
    }

    // Check if warm-up period is complete
    if (!m_sensorReady) {
        if (millis() - m_startTime >= m_warmupDuration) {
            m_sensorReady = true;
            DEBUG_LOG_SENSOR("HAL_PIR: Warm-up complete - sensor ready");
        }
    }

    // Read sensor state
    bool currentState;

    if (!m_mockMode) {
        // Read real hardware
        currentState = digitalRead(m_pin) == HIGH;
    } else {
        // Mock mode: check if mock motion is active
        if (m_mockMotionEndTime > 0 && millis() >= m_mockMotionEndTime) {
            m_mockMotionEndTime = 0;
            m_motionDetected = false;
        }
        currentState = m_motionDetected;
    }

    // Detect rising edge (motion started)
    if (currentState && !m_lastState) {
        m_motionEventCount++;
        m_lastEventTime = millis();
        m_lastEvent = MOTION_EVENT_DETECTED;
        DEBUG_LOG_SENSOR("HAL_PIR: Motion detected (event #%u)", m_motionEventCount);
    }

    // Detect falling edge (motion ended)
    if (!currentState && m_lastState) {
        m_lastEventTime = millis();
        m_lastEvent = MOTION_EVENT_CLEARED;
        DEBUG_LOG_SENSOR("HAL_PIR: Motion cleared");
    }

    m_lastState = currentState;
    m_motionDetected = currentState;
}

bool HAL_PIR::motionDetected() const {
    return m_motionDetected;
}

bool HAL_PIR::isReady() const {
    return m_sensorReady;
}

const SensorCapabilities& HAL_PIR::getCapabilities() const {
    return s_capabilities;
}

uint32_t HAL_PIR::getWarmupTimeRemaining() const {
    if (m_sensorReady) {
        return 0;
    }

    uint32_t elapsed = millis() - m_startTime;
    if (elapsed >= m_warmupDuration) {
        return 0;
    }

    return m_warmupDuration - elapsed;
}

void HAL_PIR::resetEventCount() {
    m_motionEventCount = 0;
    DEBUG_LOG_SENSOR("HAL_PIR: Motion event counter reset");
}

// =========================================================================
// Mock Mode Methods
// =========================================================================

void HAL_PIR::mockSetMotion(bool detected) {
    if (!m_mockMode) {
        DEBUG_LOG_SENSOR("HAL_PIR: mockSetMotion() called but mock mode not enabled");
        return;
    }

    m_motionDetected = detected;
    m_mockMotionEndTime = 0;
    DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Motion set to %s", detected ? "TRUE" : "FALSE");
}

void HAL_PIR::mockSetReady() {
    if (!m_mockMode) {
        DEBUG_LOG_SENSOR("HAL_PIR: mockSetReady() called but mock mode not enabled");
        return;
    }

    m_sensorReady = true;
    DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Sensor marked as ready");
}

void HAL_PIR::mockTriggerMotion(uint32_t duration_ms) {
    if (!m_mockMode) {
        DEBUG_LOG_SENSOR("HAL_PIR: mockTriggerMotion() called but mock mode not enabled");
        return;
    }

    m_motionDetected = true;

    if (duration_ms > 0) {
        m_mockMotionEndTime = millis() + duration_ms;
        DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Motion triggered for %u ms", duration_ms);
    } else {
        m_mockMotionEndTime = 0;
        DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Motion triggered (edge only)");
    }
}

void HAL_PIR::mockClearMotion() {
    if (!m_mockMode) {
        DEBUG_LOG_SENSOR("HAL_PIR: mockClearMotion() called but mock mode not enabled");
        return;
    }

    m_motionDetected = false;
    m_mockMotionEndTime = 0;
    DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Motion cleared");
}

void HAL_PIR::mockSetReady(bool ready) {
    if (!m_mockMode) {
        DEBUG_LOG_SENSOR("HAL_PIR: mockSetReady() called but mock mode not enabled");
        return;
    }

    m_sensorReady = ready;
    DEBUG_LOG_SENSOR("HAL_PIR: MOCK - Sensor ready state set to %s", ready ? "TRUE" : "FALSE");
}
