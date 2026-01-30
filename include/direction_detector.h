#ifndef STEPAWARE_DIRECTION_DETECTOR_H
#define STEPAWARE_DIRECTION_DETECTOR_H

#include <Arduino.h>
#include "hal_motion_sensor.h"
#include "sensor_types.h"

/**
 * @brief Direction detection state machine states
 *
 * Tracks the state of dual-PIR directional motion detection.
 */
enum DirectionState {
    DIR_IDLE,              ///< No motion detected on either sensor
    DIR_FAR_ONLY,          ///< Only far sensor triggered (person far away)
    DIR_NEAR_ONLY,         ///< Only near sensor triggered (hand wave or nearby object)
    DIR_BOTH_ACTIVE,       ///< Both sensors active
    DIR_APPROACHING        ///< Confirmed approaching motion (far → near sequence)
};

/**
 * @brief Direction Detector using two PIR sensors
 *
 * Analyzes trigger patterns from two PIR sensors positioned at different
 * distances ("far" and "near") to determine if motion is approaching.
 *
 * ## Detection Logic
 *
 * **APPROACHING Pattern** (the only pattern that triggers):
 * 1. Far sensor triggers (person enters far zone)
 * 2. Near sensor triggers while far still active (person moves closer)
 * 3. Direction confirmed as APPROACHING
 *
 * **Other Patterns** (logged but not treated as approaching):
 * - Near-only trigger: Hand wave or nearby stationary object → NO TRIGGER
 * - Simultaneous triggers: Too fast to determine direction → NO TRIGGER
 * - Far-only then cleared: Object too far away → NO TRIGGER
 *
 * ## Physical Setup
 *
 * Sensors should be positioned to create distinct "far" and "near" zones:
 *
 * **Option A: Vertical Offset** (Recommended):
 * - Far PIR: 1.5-2m height, tilted 5-10° down, covers 3-12m
 * - Near PIR: 0.5-1m height, tilted 5-10° up, covers 0.5-4m
 *
 * **Option B: Horizontal Spacing**:
 * - Far PIR: Mounted 30-50cm further from edge
 * - Near PIR: Mounted closer to edge
 * - Both at same height (e.g., 1.5m)
 *
 * ## Usage Example
 *
 * ```cpp
 * HAL_MotionSensor* farPIR = sensorManager.getSensor(1);   // Far zone (GPIO11)
 * HAL_MotionSensor* nearPIR = sensorManager.getSensor(0);  // Near zone (GPIO6)
 *
 * DirectionDetector dirDetector(farPIR, nearPIR);
 * dirDetector.begin();
 * dirDetector.setConfirmationWindowMs(5000);  // 5s window
 *
 * void loop() {
 *     dirDetector.update();
 *
 *     if (dirDetector.isApproaching()) {
 *         // Trigger hazard warning
 *     }
 * }
 * ```
 */
class DirectionDetector {
public:
    /**
     * @brief Construct a new Direction Detector
     *
     * @param farSensor Pointer to far zone motion sensor (typically slot 1)
     * @param nearSensor Pointer to near zone motion sensor (typically slot 0)
     */
    DirectionDetector(HAL_MotionSensor* farSensor, HAL_MotionSensor* nearSensor);

    /**
     * @brief Initialize the direction detector
     *
     * Resets all state and prepares for detection.
     */
    void begin();

    /**
     * @brief Update direction detection state (call in main loop)
     *
     * Polls both sensors, detects edges, updates state machine, and checks timeouts.
     * Should be called every loop iteration for accurate edge detection.
     */
    void update();

    // =========================================================================
    // Direction Detection
    // =========================================================================

    /**
     * @brief Get current detected direction
     *
     * @return DIRECTION_APPROACHING if approaching confirmed, DIRECTION_UNKNOWN otherwise
     */
    MotionDirection getDirection() const;

    /**
     * @brief Check if approaching motion is confirmed
     *
     * Convenience method equivalent to: getDirection() == DIRECTION_APPROACHING
     *
     * @return true if approaching motion detected and confirmed
     */
    bool isApproaching() const;

    /**
     * @brief Check if direction is confirmed (vs. tentative)
     *
     * @return true if direction has been confirmed (both sensors triggered in sequence)
     */
    bool isDirectionConfirmed() const;

    /**
     * @brief Get direction confidence time
     *
     * How long the direction has been stable/confirmed.
     *
     * @return Milliseconds since direction was confirmed (0 if not confirmed)
     */
    uint32_t getDirectionConfidenceMs() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get count of approaching detections
     *
     * @return Total number of times approaching motion was confirmed
     */
    uint32_t getApproachingCount() const { return m_approachingCount; }

    /**
     * @brief Get count of unknown/ambiguous patterns
     *
     * @return Total number of patterns that couldn't be classified
     */
    uint32_t getUnknownCount() const { return m_unknownCount; }

    /**
     * @brief Reset statistics counters
     */
    void resetStatistics();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set confirmation window duration
     *
     * Time window for near sensor to trigger after far sensor.
     * Shorter window = faster but may miss slow approaches.
     * Longer window = catches slow approaches but more false positives.
     *
     * @param window_ms Confirmation window in milliseconds (default: 5000ms)
     */
    void setConfirmationWindowMs(uint32_t window_ms);

    /**
     * @brief Set simultaneous trigger threshold
     *
     * If both sensors trigger within this time, it's considered simultaneous
     * (ambiguous direction, too fast to determine).
     *
     * @param threshold_ms Threshold in milliseconds (default: 500ms)
     */
    void setSimultaneousThresholdMs(uint32_t threshold_ms);

    /**
     * @brief Set pattern timeout duration
     *
     * If state machine stays in non-IDLE state for this long without
     * completing a pattern, reset to IDLE.
     *
     * @param timeout_ms Timeout in milliseconds (default: 10000ms)
     */
    void setPatternTimeoutMs(uint32_t timeout_ms);

    // =========================================================================
    // Debugging
    // =========================================================================

    /**
     * @brief Get current state machine state
     *
     * @return Current DirectionState
     */
    DirectionState getCurrentState() const { return m_currentState; }

    /**
     * @brief Get state name as string
     *
     * @return Human-readable state name
     */
    const char* getStateName() const;

    /**
     * @brief Get far sensor current state
     *
     * @return true if far sensor is currently detecting motion
     */
    bool getFarSensorState() const;

    /**
     * @brief Get near sensor current state
     *
     * @return true if near sensor is currently detecting motion
     */
    bool getNearSensorState() const;

private:
    // =========================================================================
    // Sensor References
    // =========================================================================

    HAL_MotionSensor* m_farSensor;      ///< Far zone sensor (e.g., 3-12m)
    HAL_MotionSensor* m_nearSensor;     ///< Near zone sensor (e.g., 0.5-4m)

    // =========================================================================
    // State Tracking
    // =========================================================================

    DirectionState m_currentState;      ///< Current state machine state
    bool m_approachingConfirmed;        ///< Approaching direction confirmed

    // =========================================================================
    // Timing
    // =========================================================================

    uint32_t m_stateStartTime;          ///< When current state started (millis)
    uint32_t m_lastFarTriggerTime;      ///< When far sensor last triggered (millis)
    uint32_t m_lastNearTriggerTime;     ///< When near sensor last triggered (millis)
    uint32_t m_directionConfirmTime;    ///< When direction was confirmed (millis)

    // =========================================================================
    // Configuration
    // =========================================================================

    uint32_t m_confirmationWindowMs;    ///< Time window for pattern confirmation (default: 5000ms)
    uint32_t m_simultaneousThresholdMs; ///< Threshold for simultaneous trigger (default: 500ms)
    uint32_t m_patternTimeoutMs;        ///< Pattern timeout duration (default: 10000ms)

    // =========================================================================
    // Statistics
    // =========================================================================

    uint32_t m_approachingCount;        ///< Count of approaching detections
    uint32_t m_unknownCount;            ///< Count of unknown patterns

    // =========================================================================
    // Previous Sensor States (for edge detection)
    // =========================================================================

    bool m_lastFarState;                ///< Previous far sensor state
    bool m_lastNearState;               ///< Previous near sensor state

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /**
     * @brief Process state machine logic
     *
     * Analyzes current sensor states and updates state machine.
     */
    void processStateMachine();

    /**
     * @brief Handle far sensor rising edge (trigger)
     *
     * Called when far sensor transitions from LOW to HIGH.
     */
    void handleFarTrigger();

    /**
     * @brief Handle near sensor rising edge (trigger)
     *
     * Called when near sensor transitions from LOW to HIGH.
     */
    void handleNearTrigger();

    /**
     * @brief Handle far sensor falling edge (clear)
     *
     * Called when far sensor transitions from HIGH to LOW.
     */
    void handleFarClear();

    /**
     * @brief Handle near sensor falling edge (clear)
     *
     * Called when near sensor transitions from HIGH to LOW.
     */
    void handleNearClear();

    /**
     * @brief Confirm approaching direction
     *
     * Sets approaching confirmed flag, updates timestamp, increments counter.
     */
    void confirmApproaching();

    /**
     * @brief Reset state machine to IDLE
     *
     * Clears all state, resets approaching flag, updates counters if needed.
     */
    void resetState();
};

#endif // STEPAWARE_DIRECTION_DETECTOR_H
