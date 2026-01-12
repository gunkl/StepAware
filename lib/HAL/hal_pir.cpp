#include "hal_pir.h"
#include <config.h>

// Static callback for interrupt handling
static void (*s_interruptCallback)() = nullptr;

// Static interrupt service routine
static void IRAM_ATTR pirISR() {
    if (s_interruptCallback != nullptr) {
        s_interruptCallback();
    }
}

HAL_PIR::HAL_PIR(uint8_t pin, bool mock)
    : m_pin(pin)
    , m_mock(mock)
    , m_initialized(false)
    , m_warmupStartTime(0)
    , m_motionEventCount(0)
    , m_interruptEnabled(false)
#if MOCK_HARDWARE
    , m_mockMotionState(false)
    , m_mockMotionEndTime(0)
#endif
{
}

HAL_PIR::~HAL_PIR() {
    if (m_interruptEnabled) {
        disableInterrupt();
    }
}

bool HAL_PIR::begin() {
    if (m_initialized) {
        return true;  // Already initialized
    }

    m_warmupStartTime = millis();

    if (!m_mock) {
        // Configure GPIO pin as input with pull-down
        pinMode(m_pin, INPUT_PULLDOWN);

        DEBUG_PRINTF("[HAL_PIR] Initialized on GPIO%d (real hardware)\n", m_pin);
    } else {
#if MOCK_HARDWARE
        DEBUG_PRINTF("[HAL_PIR] Initialized on GPIO%d (MOCK mode)\n", m_pin);
        m_mockMotionState = false;
#endif
    }

    m_initialized = true;

    // Log warm-up requirement
    DEBUG_PRINTLN("[HAL_PIR] Sensor warming up (60 seconds required)");

    return true;
}

bool HAL_PIR::motionDetected() {
    if (!m_initialized) {
        return false;
    }

#if MOCK_HARDWARE
    if (m_mock) {
        // Check if mock motion pulse has expired
        if (m_mockMotionEndTime > 0 && millis() >= m_mockMotionEndTime) {
            m_mockMotionState = false;
            m_mockMotionEndTime = 0;
        }
        return m_mockMotionState;
    }
#endif

    // Read real hardware
    return (readPin() == HIGH);
}

bool HAL_PIR::isReady() {
    if (!m_initialized) {
        return false;
    }

    return (millis() - m_warmupStartTime >= PIR_WARMUP_TIME_MS);
}

uint32_t HAL_PIR::getWarmupTimeRemaining() {
    if (!m_initialized || isReady()) {
        return 0;
    }

    uint32_t elapsed = millis() - m_warmupStartTime;
    return PIR_WARMUP_TIME_MS - elapsed;
}

bool HAL_PIR::enableInterrupt(void (*callback)()) {
    if (!m_initialized) {
        DEBUG_PRINTLN("[HAL_PIR] ERROR: Cannot enable interrupt before initialization");
        return false;
    }

    if (m_mock) {
        DEBUG_PRINTLN("[HAL_PIR] Interrupt mode not available in mock mode");
        return false;
    }

    if (callback == nullptr) {
        DEBUG_PRINTLN("[HAL_PIR] ERROR: Callback function is nullptr");
        return false;
    }

    s_interruptCallback = callback;
    attachInterrupt(digitalPinToInterrupt(m_pin), pirISR, RISING);
    m_interruptEnabled = true;

    DEBUG_PRINTF("[HAL_PIR] Interrupt enabled on GPIO%d (RISING edge)\n", m_pin);
    return true;
}

void HAL_PIR::disableInterrupt() {
    if (!m_interruptEnabled) {
        return;
    }

    if (!m_mock) {
        detachInterrupt(digitalPinToInterrupt(m_pin));
    }

    s_interruptCallback = nullptr;
    m_interruptEnabled = false;

    DEBUG_PRINTLN("[HAL_PIR] Interrupt disabled");
}

uint32_t HAL_PIR::getMotionEventCount() {
    return m_motionEventCount;
}

void HAL_PIR::resetMotionEventCount() {
    m_motionEventCount = 0;
    DEBUG_PRINTLN("[HAL_PIR] Motion event counter reset");
}

int HAL_PIR::readPin() {
    if (m_mock) {
        return LOW;  // Should not be called in mock mode
    }

    return digitalRead(m_pin);
}

// ============================================================================
// Mock Hardware Methods
// ============================================================================

#if MOCK_HARDWARE

void HAL_PIR::mockSetMotion(bool detected) {
    if (!m_mock) {
        return;  // Only works in mock mode
    }

    bool previousState = m_mockMotionState;
    m_mockMotionState = detected;

    // Increment counter on rising edge
    if (detected && !previousState) {
        m_motionEventCount++;
        DEBUG_PRINTLN("[HAL_PIR] MOCK: Motion detected (event count: " + String(m_motionEventCount) + ")");
    }

    // Clear end time if motion manually set
    if (detected) {
        m_mockMotionEndTime = 0;
    }
}

bool HAL_PIR::mockGetMotion() {
    if (!m_mock) {
        return false;
    }

    return m_mockMotionState;
}

void HAL_PIR::mockTriggerMotion(uint32_t duration_ms) {
    if (!m_mock) {
        return;
    }

    DEBUG_PRINTF("[HAL_PIR] MOCK: Triggering motion pulse (%u ms)\n", duration_ms);

    m_mockMotionState = true;
    m_mockMotionEndTime = millis() + duration_ms;
    m_motionEventCount++;
}

#endif // MOCK_HARDWARE
