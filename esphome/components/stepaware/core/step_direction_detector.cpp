#include "step_direction_detector.h"

namespace step_logic {

StepDirectionDetector::StepDirectionDetector(StepClockFn clock)
    : m_clock(clock)
    , m_currentState(DIR_IDLE)
    , m_approachingConfirmed(false)
    , m_leavingConfirmed(false)
    , m_stateStartTime(0)
    , m_lastFarTriggerTime(0)
    , m_lastNearTriggerTime(0)
    , m_directionConfirmTime(0)
    , m_confirmationWindowMs(DIR_CONFIRMATION_WINDOW_MS)
    , m_simultaneousThresholdMs(DIR_SIMULTANEOUS_THRESHOLD_MS)
    , m_patternTimeoutMs(DIR_PATTERN_TIMEOUT_MS)
    , m_approachingCount(0)
    , m_leavingCount(0)
    , m_unknownCount(0)
    , m_farActive(false)
    , m_nearActive(false)
    , m_lastFarState(false)
    , m_lastNearState(false)
{
}

void StepDirectionDetector::begin(bool farActive, bool nearActive) {
    resetState();
    m_farActive = farActive;
    m_nearActive = nearActive;
    m_lastFarState = farActive;
    m_lastNearState = nearActive;
}

void StepDirectionDetector::update(bool farActive, bool nearActive) {
    m_farActive = farActive;
    m_nearActive = nearActive;

    // Rising edges
    if (farActive && !m_lastFarState) {
        handleFarTrigger();
    }
    if (nearActive && !m_lastNearState) {
        handleNearTrigger();
    }

    // Falling edges
    if (!farActive && m_lastFarState) {
        handleFarClear();
    }
    if (!nearActive && m_lastNearState) {
        handleNearClear();
    }

    processStateMachine();

    // Pattern timeout: stuck in a non-IDLE state too long
    if (m_currentState != DIR_IDLE) {
        uint32_t now = m_clock();
        if (now - m_stateStartTime > m_patternTimeoutMs) {
            m_unknownCount++;
            resetState();
        }
    }

    m_lastFarState = farActive;
    m_lastNearState = nearActive;
}

uint32_t StepDirectionDetector::getDirectionConfidenceMs() const {
    if (!m_approachingConfirmed || m_directionConfirmTime == 0) {
        return 0;
    }
    return m_clock() - m_directionConfirmTime;
}

void StepDirectionDetector::seedFarTrigger(uint32_t offsetMs) {
    uint32_t now = m_clock();
    m_currentState = DIR_FAR_ONLY;
    m_stateStartTime = now - offsetMs;
    m_lastFarTriggerTime = now - offsetMs;
    m_lastFarState = true;  // Suppress rising-edge re-detection on the next update()
}

void StepDirectionDetector::resetStatistics() {
    m_approachingCount = 0;
    m_leavingCount = 0;
    m_unknownCount = 0;
}

const char* StepDirectionDetector::getStateName() const {
    switch (m_currentState) {
        case DIR_IDLE:        return "IDLE";
        case DIR_FAR_ONLY:    return "FAR_ONLY";
        case DIR_NEAR_ONLY:   return "NEAR_ONLY";
        case DIR_BOTH_ACTIVE: return "BOTH_ACTIVE";
        case DIR_APPROACHING: return "APPROACHING";
        case DIR_LEAVING:     return "LEAVING";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Edge handlers
// ---------------------------------------------------------------------------

void StepDirectionDetector::handleFarTrigger() {
    uint32_t now = m_clock();
    m_lastFarTriggerTime = now;

    // Simultaneous with a near trigger?
    if (m_nearActive && m_lastNearTriggerTime > 0) {
        uint32_t delta = (now > m_lastNearTriggerTime) ? (now - m_lastNearTriggerTime)
                                                       : (m_lastNearTriggerTime - now);
        if (delta < m_simultaneousThresholdMs) {
            m_unknownCount++;
            return;  // Leave state to processStateMachine()
        }
    }

    if (m_currentState == DIR_IDLE) {
        m_currentState = DIR_FAR_ONLY;
        m_stateStartTime = now;
    } else if (m_currentState == DIR_NEAR_ONLY && m_nearActive) {
        // Addition: near first, far now -> LEAVING (if inside the window)
        if (m_lastNearTriggerTime > 0 && now - m_lastNearTriggerTime <= m_confirmationWindowMs) {
            confirmLeaving();
        }
    }
}

void StepDirectionDetector::handleNearTrigger() {
    uint32_t now = m_clock();
    m_lastNearTriggerTime = now;

    // Simultaneous with a far trigger?
    if (m_farActive && m_lastFarTriggerTime > 0) {
        uint32_t delta = (now > m_lastFarTriggerTime) ? (now - m_lastFarTriggerTime)
                                                      : (m_lastFarTriggerTime - now);
        if (delta < m_simultaneousThresholdMs) {
            m_unknownCount++;
            return;
        }
    }

    if (m_currentState == DIR_IDLE) {
        // Near first: hand wave or nearby object
        m_currentState = DIR_NEAR_ONLY;
        m_stateStartTime = now;
        m_unknownCount++;
    } else if (m_currentState == DIR_FAR_ONLY) {
        if (m_lastFarTriggerTime > 0) {
            uint32_t delta = now - m_lastFarTriggerTime;
            if (delta <= m_confirmationWindowMs) {
                confirmApproaching();
            } else {
                m_unknownCount++;
            }
        }
    }
}

void StepDirectionDetector::handleFarClear() {
    // FAR_ONLY stays latched so near can still confirm APPROACHING (Issue #51)
    if (m_currentState == DIR_FAR_ONLY) {
        return;
    }
    if (!m_nearActive) {
        if (m_currentState != DIR_IDLE && !m_approachingConfirmed) {
            resetState();
        }
    }
}

void StepDirectionDetector::handleNearClear() {
    if (!m_farActive) {
        if (m_currentState != DIR_IDLE) {
            resetState();
        }
    }
}

// ---------------------------------------------------------------------------
// Level-based state machine
// ---------------------------------------------------------------------------

void StepDirectionDetector::processStateMachine() {
    bool farActive = m_farActive;
    bool nearActive = m_nearActive;

    switch (m_currentState) {
        case DIR_IDLE:
            break;

        case DIR_FAR_ONLY:
            if (farActive && nearActive) {
                m_currentState = DIR_BOTH_ACTIVE;
            } else if (!farActive) {
                uint32_t elapsed = m_clock() - m_lastFarTriggerTime;
                if (elapsed > m_confirmationWindowMs) {
                    resetState();
                }
            }
            break;

        case DIR_NEAR_ONLY:
            if (nearActive && farActive) {
                m_currentState = DIR_BOTH_ACTIVE;
            } else if (!nearActive) {
                resetState();
            }
            break;

        case DIR_BOTH_ACTIVE:
        case DIR_APPROACHING:
        case DIR_LEAVING:
            if (!farActive && !nearActive) {
                resetState();
            }
            break;
    }
}

void StepDirectionDetector::confirmApproaching() {
    m_currentState = DIR_APPROACHING;
    m_approachingConfirmed = true;
    m_directionConfirmTime = m_clock();
    m_approachingCount++;
}

void StepDirectionDetector::confirmLeaving() {
    m_currentState = DIR_LEAVING;
    m_leavingConfirmed = true;
    m_leavingCount++;
}

void StepDirectionDetector::resetState() {
    m_currentState = DIR_IDLE;
    m_approachingConfirmed = false;
    m_leavingConfirmed = false;
    m_stateStartTime = 0;
    m_directionConfirmTime = 0;
    // Trigger times are kept for simultaneous-trigger detection
}

}  // namespace step_logic
