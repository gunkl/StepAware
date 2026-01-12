#include "hal_pir.h"

HAL_PIR::HAL_PIR(uint8_t pin, bool mock_mode)
    : m_pin(pin)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_motionDetected(false)
    , m_lastState(false)
    , m_sensorReady(false)
    , m_startTime(0)
    , m_warmupDuration(PIR_WARMUP_TIME_MS)
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

    DEBUG_PRINTLN("[HAL_PIR] Initializing PIR sensor...");

    if (!m_mockMode) {
        // Configure GPIO pin as input
        pinMode(m_pin, INPUT);
        DEBUG_PRINTF("[HAL_PIR] Pin %d configured as INPUT\n", m_pin);
    } else {
        DEBUG_PRINTLN("[HAL_PIR] MOCK MODE: Simulating sensor");
    }

    m_startTime = millis();
    m_initialized = true;

    DEBUG_PRINTF("[HAL_PIR] Warm-up period: %u ms (~%u seconds)\n",
                 m_warmupDuration, m_warmupDuration / 1000);
    DEBUG_PRINTLN("[HAL_PIR] ✓ Initialization complete");

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
            DEBUG_PRINTLN("[HAL_PIR] Warm-up complete - sensor ready");
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
        DEBUG_PRINTF("[HAL_PIR] Motion detected (event #%u)\n", m_motionEventCount);
    }

    // Detect falling edge (motion ended)
    if (!currentState && m_lastState) {
        DEBUG_PRINTLN("[HAL_PIR] Motion cleared");
    }

    m_lastState = currentState;
    m_motionDetected = currentState;
}

bool HAL_PIR::motionDetected() {
    return m_motionDetected;
}

bool HAL_PIR::isReady() {
    return m_sensorReady;
}

uint32_t HAL_PIR::getWarmupTimeRemaining() {
    if (m_sensorReady) {
        return 0;
    }

    uint32_t elapsed = millis() - m_startTime;
    if (elapsed >= m_warmupDuration) {
        return 0;
    }

    return m_warmupDuration - elapsed;
}

uint32_t HAL_PIR::getMotionEventCount() {
    return m_motionEventCount;
}

void HAL_PIR::resetMotionEventCount() {
    m_motionEventCount = 0;
    DEBUG_PRINTLN("[HAL_PIR] Motion event counter reset");
}

// Mock/Test Methods

void HAL_PIR::mockTriggerMotion(uint32_t duration_ms) {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_PIR] WARNING: mockTriggerMotion() called but mock mode not enabled");
        return;
    }

    m_motionDetected = true;

    if (duration_ms > 0) {
        m_mockMotionEndTime = millis() + duration_ms;
        DEBUG_PRINTF("[HAL_PIR] MOCK: Motion triggered for %u ms\n", duration_ms);
    } else {
        m_mockMotionEndTime = 0;
        DEBUG_PRINTLN("[HAL_PIR] MOCK: Motion triggered (edge only)");
    }
}

void HAL_PIR::mockClearMotion() {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_PIR] WARNING: mockClearMotion() called but mock mode not enabled");
        return;
    }

    m_motionDetected = false;
    m_mockMotionEndTime = 0;
    DEBUG_PRINTLN("[HAL_PIR] MOCK: Motion cleared");
}

void HAL_PIR::mockSetReady(bool ready) {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_PIR] WARNING: mockSetReady() called but mock mode not enabled");
        return;
    }

    m_sensorReady = ready;
    DEBUG_PRINTF("[HAL_PIR] MOCK: Sensor ready state set to %s\n", ready ? "TRUE" : "FALSE");
}
