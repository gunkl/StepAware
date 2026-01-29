#include "direction_detector.h"
#include "debug_logger.h"

DirectionDetector::DirectionDetector(HAL_MotionSensor* farSensor, HAL_MotionSensor* nearSensor)
    : m_farSensor(farSensor),
      m_nearSensor(nearSensor),
      m_currentState(DIR_IDLE),
      m_approachingConfirmed(false),
      m_stateStartTime(0),
      m_lastFarTriggerTime(0),
      m_lastNearTriggerTime(0),
      m_directionConfirmTime(0),
      m_confirmationWindowMs(5000),      // Default: 5 seconds
      m_simultaneousThresholdMs(500),    // Default: 500ms
      m_patternTimeoutMs(10000),         // Default: 10 seconds
      m_approachingCount(0),
      m_unknownCount(0),
      m_lastFarState(false),
      m_lastNearState(false)
{
}

void DirectionDetector::begin()
{
    // Reset all state
    resetState();

    // Initialize previous states from sensors
    if (m_farSensor) {
        m_lastFarState = m_farSensor->motionDetected();
    }
    if (m_nearSensor) {
        m_lastNearState = m_nearSensor->motionDetected();
    }

    DEBUG_LOG_SENSOR("DirectionDetector: Initialized (confirmWindow=%u ms, simultThresh=%u ms, timeout=%u ms)",
                   m_confirmationWindowMs, m_simultaneousThresholdMs, m_patternTimeoutMs);
}

void DirectionDetector::update()
{
    // Validate sensors
    if (!m_farSensor || !m_nearSensor) {
        return;
    }

    // Read current sensor states
    bool farActive = m_farSensor->motionDetected();
    bool nearActive = m_nearSensor->motionDetected();

    // Detect rising edges (triggers)
    if (farActive && !m_lastFarState) {
        handleFarTrigger();
    }
    if (nearActive && !m_lastNearState) {
        handleNearTrigger();
    }

    // Detect falling edges (clears)
    if (!farActive && m_lastFarState) {
        handleFarClear();
    }
    if (!nearActive && m_lastNearState) {
        handleNearClear();
    }

    // Update state machine logic
    processStateMachine();

    // Check for pattern timeout (reset if stuck in non-IDLE state too long)
    if (m_currentState != DIR_IDLE) {
        uint32_t now = millis();
        if (now - m_stateStartTime > m_patternTimeoutMs) {
            DEBUG_LOG_SENSOR("DirectionDetector: Pattern timeout (state=%s, duration=%u ms) - resetting",
                           getStateName(), now - m_stateStartTime);
            m_unknownCount++;  // Count as unknown pattern
            resetState();
        }
    }

    // Save current states for next iteration
    m_lastFarState = farActive;
    m_lastNearState = nearActive;
}

// =========================================================================
// Direction Detection
// =========================================================================

MotionDirection DirectionDetector::getDirection() const
{
    if (m_approachingConfirmed) {
        return DIRECTION_APPROACHING;
    }
    return DIRECTION_UNKNOWN;
}

bool DirectionDetector::isApproaching() const
{
    return m_approachingConfirmed;
}

bool DirectionDetector::isDirectionConfirmed() const
{
    return m_approachingConfirmed;
}

uint32_t DirectionDetector::getDirectionConfidenceMs() const
{
    if (!m_approachingConfirmed || m_directionConfirmTime == 0) {
        return 0;
    }
    return millis() - m_directionConfirmTime;
}

// =========================================================================
// Statistics
// =========================================================================

void DirectionDetector::resetStatistics()
{
    m_approachingCount = 0;
    m_unknownCount = 0;
    DEBUG_LOG_SENSOR("DirectionDetector: Statistics reset");
}

// =========================================================================
// Configuration
// =========================================================================

void DirectionDetector::setConfirmationWindowMs(uint32_t window_ms)
{
    m_confirmationWindowMs = window_ms;
    DEBUG_LOG_SENSOR("DirectionDetector: Confirmation window set to %u ms", window_ms);
}

void DirectionDetector::setSimultaneousThresholdMs(uint32_t threshold_ms)
{
    m_simultaneousThresholdMs = threshold_ms;
    DEBUG_LOG_SENSOR("DirectionDetector: Simultaneous threshold set to %u ms", threshold_ms);
}

void DirectionDetector::setPatternTimeoutMs(uint32_t timeout_ms)
{
    m_patternTimeoutMs = timeout_ms;
    DEBUG_LOG_SENSOR("DirectionDetector: Pattern timeout set to %u ms", timeout_ms);
}

// =========================================================================
// Debugging
// =========================================================================

const char* DirectionDetector::getStateName() const
{
    switch (m_currentState) {
        case DIR_IDLE:          return "IDLE";
        case DIR_FAR_ONLY:      return "FAR_ONLY";
        case DIR_NEAR_ONLY:     return "NEAR_ONLY";
        case DIR_BOTH_ACTIVE:   return "BOTH_ACTIVE";
        case DIR_APPROACHING:   return "APPROACHING";
        default:                return "UNKNOWN";
    }
}

bool DirectionDetector::getFarSensorState() const
{
    return m_farSensor ? m_farSensor->motionDetected() : false;
}

bool DirectionDetector::getNearSensorState() const
{
    return m_nearSensor ? m_nearSensor->motionDetected() : false;
}

// =========================================================================
// Internal Methods - Edge Handlers
// =========================================================================

void DirectionDetector::handleFarTrigger()
{
    uint32_t now = millis();
    m_lastFarTriggerTime = now;

    DEBUG_LOG_SENSOR("DirectionDetector: FAR sensor TRIGGERED (state=%s)", getStateName());

    // Check if near sensor is also active (simultaneous trigger)
    if (m_nearSensor->motionDetected()) {
        // Both sensors are now active
        // Check if they triggered within simultaneous threshold
        if (m_lastNearTriggerTime > 0) {
            uint32_t delta = (now > m_lastNearTriggerTime) ?
                           (now - m_lastNearTriggerTime) :
                           (m_lastNearTriggerTime - now);

            if (delta < m_simultaneousThresholdMs) {
                DEBUG_LOG_SENSOR("DirectionDetector: Simultaneous triggers detected (delta=%u ms) - UNKNOWN direction",
                               delta);
                m_unknownCount++;
                // Don't change state - let processStateMachine handle it
                return;
            }
        }
    }

    // Far sensor triggered - transition to FAR_ONLY if currently IDLE
    if (m_currentState == DIR_IDLE) {
        m_currentState = DIR_FAR_ONLY;
        m_stateStartTime = now;
        DEBUG_LOG_SENSOR("DirectionDetector: State transition: IDLE → FAR_ONLY");
    }
}

void DirectionDetector::handleNearTrigger()
{
    uint32_t now = millis();
    m_lastNearTriggerTime = now;

    DEBUG_LOG_SENSOR("DirectionDetector: NEAR sensor TRIGGERED (state=%s)", getStateName());

    // Check if far sensor is also active (simultaneous trigger)
    if (m_farSensor->motionDetected()) {
        // Both sensors are now active
        // Check if they triggered within simultaneous threshold
        if (m_lastFarTriggerTime > 0) {
            uint32_t delta = (now > m_lastFarTriggerTime) ?
                           (now - m_lastFarTriggerTime) :
                           (m_lastFarTriggerTime - now);

            if (delta < m_simultaneousThresholdMs) {
                DEBUG_LOG_SENSOR("DirectionDetector: Simultaneous triggers detected (delta=%u ms) - UNKNOWN direction",
                               delta);
                m_unknownCount++;
                // Don't change state - let processStateMachine handle it
                return;
            }
        }
    }

    // Near sensor triggered - handle based on current state
    if (m_currentState == DIR_IDLE) {
        // Near triggered first - hand wave or nearby object
        m_currentState = DIR_NEAR_ONLY;
        m_stateStartTime = now;
        DEBUG_LOG_SENSOR("DirectionDetector: State transition: IDLE → NEAR_ONLY (hand wave or nearby object)");
        m_unknownCount++;  // Count as unknown pattern
    }
    else if (m_currentState == DIR_FAR_ONLY) {
        // APPROACHING pattern detected: far triggered first, now near triggered
        // Verify near triggered within confirmation window
        if (m_lastFarTriggerTime > 0) {
            uint32_t delta = now - m_lastFarTriggerTime;
            if (delta <= m_confirmationWindowMs) {
                DEBUG_LOG_SENSOR("DirectionDetector: APPROACHING pattern confirmed (far→near, delta=%u ms)", delta);
                confirmApproaching();
            } else {
                DEBUG_LOG_SENSOR("DirectionDetector: Far→near but outside window (delta=%u ms > %u ms) - timeout",
                               delta, m_confirmationWindowMs);
                m_unknownCount++;
            }
        }
    }
}

void DirectionDetector::handleFarClear()
{
    DEBUG_LOG_SENSOR("DirectionDetector: FAR sensor CLEARED (state=%s)", getStateName());

    // Far sensor cleared - update state based on near sensor
    if (!m_nearSensor->motionDetected()) {
        // Both sensors now clear - reset to IDLE
        if (m_currentState != DIR_IDLE && !m_approachingConfirmed) {
            DEBUG_LOG_SENSOR("DirectionDetector: Both sensors cleared - resetting to IDLE");
            resetState();
        }
    }
}

void DirectionDetector::handleNearClear()
{
    DEBUG_LOG_SENSOR("DirectionDetector: NEAR sensor CLEARED (state=%s)", getStateName());

    // Near sensor cleared - update state based on far sensor
    if (!m_farSensor->motionDetected()) {
        // Both sensors now clear - reset to IDLE
        if (m_currentState != DIR_IDLE) {
            DEBUG_LOG_SENSOR("DirectionDetector: Both sensors cleared - resetting to IDLE");
            resetState();
        }
    }
}

// =========================================================================
// Internal Methods - State Machine
// =========================================================================

void DirectionDetector::processStateMachine()
{
    // Get current sensor states
    bool farActive = m_farSensor->motionDetected();
    bool nearActive = m_nearSensor->motionDetected();

    // Update state based on sensor states
    switch (m_currentState) {
        case DIR_IDLE:
            // Handled by edge handlers
            break;

        case DIR_FAR_ONLY:
            if (farActive && nearActive) {
                // Both active - check if this is approaching pattern
                // This would have been detected in handleNearTrigger
                m_currentState = DIR_BOTH_ACTIVE;
            }
            else if (!farActive) {
                // Far cleared without near ever triggering
                DEBUG_LOG_SENSOR("DirectionDetector: Far cleared without near trigger - resetting");
                resetState();
            }
            break;

        case DIR_NEAR_ONLY:
            if (nearActive && farActive) {
                // Both active - but near triggered first (not approaching)
                m_currentState = DIR_BOTH_ACTIVE;
            }
            else if (!nearActive) {
                // Near cleared without far ever triggering
                DEBUG_LOG_SENSOR("DirectionDetector: Near cleared without far trigger - resetting");
                resetState();
            }
            break;

        case DIR_BOTH_ACTIVE:
            if (!farActive && !nearActive) {
                // Both cleared
                resetState();
            }
            break;

        case DIR_APPROACHING:
            if (!farActive && !nearActive) {
                // Approaching motion completed - both sensors cleared
                DEBUG_LOG_SENSOR("DirectionDetector: Approaching motion completed - both cleared");
                resetState();
            }
            break;
    }
}

void DirectionDetector::confirmApproaching()
{
    m_currentState = DIR_APPROACHING;
    m_approachingConfirmed = true;
    m_directionConfirmTime = millis();
    m_approachingCount++;

    DEBUG_LOG_SENSOR("DirectionDetector: *** APPROACHING CONFIRMED *** (count=%u, confidence=0 ms)",
                   m_approachingCount);
}

void DirectionDetector::resetState()
{
    m_currentState = DIR_IDLE;
    m_approachingConfirmed = false;
    m_stateStartTime = 0;
    // Don't reset trigger times - they're useful for detecting simultaneous triggers
    // m_lastFarTriggerTime = 0;
    // m_lastNearTriggerTime = 0;
    m_directionConfirmTime = 0;
}
