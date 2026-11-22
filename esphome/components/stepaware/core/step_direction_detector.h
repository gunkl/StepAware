/**
 * @file step_direction_detector.h
 * @brief Dual-PIR direction detector (port of DirectionDetector).
 *
 * The caller passes the current far/near PIR states into update() each loop instead
 * of the detector polling HAL_MotionSensor objects.
 *
 * APPROACHING (unchanged from the original): far triggers, then near triggers within
 * the confirmation window and not "simultaneously" (< simultaneous threshold). A far
 * clear while in FAR_ONLY keeps the state latched until the window expires (Issue #51).
 *
 * Addition: LEAVING is the mirror pattern (near first, far triggers while near is still
 * active, within the window, not simultaneous). It is informational only; it never
 * changes how APPROACHING is detected.
 */
#ifndef STEPAWARE_CORE_STEP_DIRECTION_DETECTOR_H
#define STEPAWARE_CORE_STEP_DIRECTION_DETECTOR_H

#include <stdint.h>
#include "step_clock.h"

namespace step_logic {

const uint32_t DIR_CONFIRMATION_WINDOW_MS = 5000;
const uint32_t DIR_SIMULTANEOUS_THRESHOLD_MS = 150;
const uint32_t DIR_PATTERN_TIMEOUT_MS = 10000;

class StepDirectionDetector {
public:
    enum DirectionState {
        DIR_IDLE,         ///< No motion on either sensor
        DIR_FAR_ONLY,     ///< Far sensor triggered first
        DIR_NEAR_ONLY,    ///< Near sensor triggered first
        DIR_BOTH_ACTIVE,  ///< Both active, no confirmed direction
        DIR_APPROACHING,  ///< Confirmed far -> near
        DIR_LEAVING       ///< Confirmed near -> far (addition)
    };

    explicit StepDirectionDetector(StepClockFn clock);

    /// Reset state and take the initial sensor levels (no edges are generated for them).
    void begin(bool farActive, bool nearActive);

    /// Feed the current sensor levels. Call every loop.
    void update(bool farActive, bool nearActive);

    bool isApproaching() const { return m_approachingConfirmed; }
    bool isDirectionConfirmed() const { return m_approachingConfirmed; }
    bool isLeaving() const { return m_leavingConfirmed; }
    uint32_t getDirectionConfidenceMs() const;

    /// Pre-set FAR_ONLY with a back-dated trigger (far PIR woke the device).
    void seedFarTrigger(uint32_t offsetMs = 500);

    uint32_t getApproachingCount() const { return m_approachingCount; }
    uint32_t getLeavingCount() const { return m_leavingCount; }
    uint32_t getUnknownCount() const { return m_unknownCount; }
    void resetStatistics();

    void setConfirmationWindowMs(uint32_t ms) { m_confirmationWindowMs = ms; }
    void setSimultaneousThresholdMs(uint32_t ms) { m_simultaneousThresholdMs = ms; }
    void setPatternTimeoutMs(uint32_t ms) { m_patternTimeoutMs = ms; }
    uint32_t getConfirmationWindowMs() const { return m_confirmationWindowMs; }
    uint32_t getSimultaneousThresholdMs() const { return m_simultaneousThresholdMs; }
    uint32_t getPatternTimeoutMs() const { return m_patternTimeoutMs; }

    DirectionState getCurrentState() const { return m_currentState; }
    const char* getStateName() const;
    bool getFarSensorState() const { return m_farActive; }
    bool getNearSensorState() const { return m_nearActive; }

private:
    StepClockFn m_clock;
    DirectionState m_currentState;
    bool m_approachingConfirmed;
    bool m_leavingConfirmed;
    uint32_t m_stateStartTime;
    uint32_t m_lastFarTriggerTime;
    uint32_t m_lastNearTriggerTime;
    uint32_t m_directionConfirmTime;
    uint32_t m_confirmationWindowMs;
    uint32_t m_simultaneousThresholdMs;
    uint32_t m_patternTimeoutMs;
    uint32_t m_approachingCount;
    uint32_t m_leavingCount;
    uint32_t m_unknownCount;
    bool m_farActive;    ///< Current far level (what motionDetected() returned)
    bool m_nearActive;   ///< Current near level
    bool m_lastFarState;
    bool m_lastNearState;

    void processStateMachine();
    void handleFarTrigger();
    void handleNearTrigger();
    void handleFarClear();
    void handleNearClear();
    void confirmApproaching();
    void confirmLeaving();
    void resetState();
};

}  // namespace step_logic

#endif  // STEPAWARE_CORE_STEP_DIRECTION_DETECTOR_H
