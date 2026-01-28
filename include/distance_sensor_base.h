#ifndef STEPAWARE_DISTANCE_SENSOR_BASE_H
#define STEPAWARE_DISTANCE_SENSOR_BASE_H

#include <Arduino.h>
#include "config.h"
#include "sensor_types.h"

/**
 * @brief Base class for distance-based motion detection
 *
 * Provides shared logic for all distance sensors (ultrasonic, IR, etc.):
 * - Rolling window averaging for noise reduction
 * - Movement detection (distinguishes motion from static objects)
 * - Direction detection (approaching vs receding)
 * - Distance-based thresholding
 * - Dual-mode approach detection (gradual vs sudden appearance)
 *
 * ## Dual-Mode Approach Detection
 *
 * The sensor uses two different detection modes to handle different scenarios:
 *
 * ### Mode 1: Gradual Approach (Normal Mode)
 * Detects objects approaching from outside the detection range.
 * - Readings start OUTSIDE detection range (distance > detectionThreshold)
 * - Readings show APPROACHING direction (distance decreasing over time)
 * - Object crosses INTO detection range (distance <= detectionThreshold)
 * - **Triggers immediately** - no confirmation delay needed
 * - Example: Person walking toward sensor from 2m away
 *
 * ### Mode 2: Sudden Appearance (Side/Hand Mode)
 * Detects objects that appear within range without prior approach.
 * - First valid readings are INSIDE detection range (distance <= detectionThreshold)
 * - No prior readings from outside showing approach
 * - Flags as "sudden appearance" requiring direction confirmation
 * - Waits for DIRECTION_CONFIRMATION_WINDOW_CYCLES to build direction data
 * - Only triggers if confirmed direction matches trigger mode
 * - Example: Hand waved in front of sensor, person walking in from side
 *
 * ### Detection Logic
 * 1. Track raw sensor readings (non-averaged) to detect sudden appearances
 * 2. If 2 consecutive valid raw readings within range without prior outside approach
 *    → Flag as sudden appearance, await direction confirmation
 * 3. If readings show approach from outside range
 *    → Flag as gradual approach, trigger immediately when entering range
 * 4. During direction confirmation, wait for windowed averaging to stabilize
 * 5. Only trigger if direction matches configured trigger mode
 *
 * This dual-mode approach prevents false alarms from objects appearing from
 * the side while maintaining fast response to actual approaching targets.
 *
 * Subclasses only need to implement getDistanceReading() to provide
 * raw distance measurements from their specific hardware.
 */
class DistanceSensorBase {
public:
    /**
     * @brief Constructor
     *
     * @param minDistance Minimum valid detection distance (mm)
     * @param maxDistance Maximum valid detection distance (mm)
     * @param windowSize Rolling window size (3-20 samples, default 10)
     */
    DistanceSensorBase(uint32_t minDistance, uint32_t maxDistance, uint8_t windowSize = 10);
    virtual ~DistanceSensorBase() = default;

    // =========================================================================
    // Public Interface
    // =========================================================================

    /**
     * @brief Update sensor state (call in main loop)
     *
     * Reads distance, updates rolling window, detects movement/direction.
     * Subclasses should call this from their update() method.
     */
    void updateDistanceSensor();

    /**
     * @brief Check if motion is currently detected
     *
     * Motion detected when: in range + movement + direction matches trigger mode
     *
     * @return true if motion detected
     */
    bool isMotionDetected() const { return m_objectDetected; }

    /**
     * @brief Get current distance measurement (window average)
     *
     * @return Distance in millimeters, 0 if invalid/out of range
     */
    uint32_t getCurrentDistance() const { return m_currentDistance; }

    /**
     * @brief Get motion direction
     *
     * @return DIRECTION_APPROACHING, DIRECTION_RECEDING, or DIRECTION_STATIONARY
     */
    MotionDirection getDirection() const { return m_direction; }

    /**
     * @brief Get last motion event
     *
     * @return Last event type
     */
    MotionEvent getLastEvent() const { return m_lastEvent; }

    /**
     * @brief Get event count
     *
     * @return Total number of motion events detected
     */
    uint32_t getEventCount() const { return m_eventCount; }

    /**
     * @brief Get last event timestamp
     *
     * @return Timestamp in milliseconds since boot
     */
    uint32_t getLastEventTime() const { return m_lastEventTime; }

    /**
     * @brief Reset event counter
     */
    void resetEventCount() { m_eventCount = 0; }

    // =========================================================================
    // Configuration Methods
    // =========================================================================

    /**
     * @brief Set detection threshold
     *
     * Objects closer than this trigger motion detection (if other conditions met).
     *
     * @param threshold_mm Detection threshold in millimeters
     */
    void setDetectionThreshold(uint32_t threshold_mm) { m_detectionThreshold = threshold_mm; }

    /**
     * @brief Get detection threshold
     *
     * @return Detection threshold in millimeters
     */
    uint32_t getDetectionThreshold() const { return m_detectionThreshold; }

    /**
     * @brief Set distance range for detection
     *
     * @param min_mm Minimum detection distance (mm)
     * @param max_mm Maximum detection distance (mm)
     */
    void setDistanceRange(uint32_t min_mm, uint32_t max_mm) {
        m_minDistance = min_mm;
        m_maxDistance = max_mm;
    }

    /**
     * @brief Set rolling window size for averaging
     *
     * Smaller window = faster response but more noise
     * Larger window = slower response but smoother
     *
     * For pedestrian detection at walking speed (~1.25 m/s):
     * - Window size 3-5: ~180-300ms response, good for fast alerts
     * - Window size 10: ~600ms response (default)
     *
     * @param size Window size (3-20 samples)
     */
    void setSampleWindowSize(uint8_t size);

    /**
     * @brief Get current rolling window size
     *
     * @return Window size in samples
     */
    uint8_t getSampleWindowSize() const { return m_sampleWindowSize; }

    /**
     * @brief Get minimum detection distance
     *
     * @return Minimum distance in millimeters
     */
    uint32_t getMinDistance() const { return m_minDistance; }

    /**
     * @brief Get maximum detection distance
     *
     * @return Maximum distance in millimeters
     */
    uint32_t getMaxDistance() const { return m_maxDistance; }

    /**
     * @brief Set maximum detection distance
     *
     * @param maxDistance Maximum distance in millimeters
     */
    void setMaxDistance(uint32_t maxDistance) { m_maxDistance = maxDistance; }

    /**
     * @brief Enable/disable direction detection
     *
     * @param enable true to enable direction-based filtering
     */
    void setDirectionDetection(bool enable) {
        m_directionEnabled = enable;
        if (!enable) {
            m_direction = DIRECTION_UNKNOWN;
        }
    }

    /**
     * @brief Check if direction detection is enabled
     *
     * @return true if enabled
     */
    bool isDirectionDetectionEnabled() const { return m_directionEnabled; }

    /**
     * @brief Set direction trigger mode
     *
     * @param mode 0=approaching only, 1=receding only, 2=both directions
     */
    void setDirectionTriggerMode(uint8_t mode) { m_directionTriggerMode = mode; }

    /**
     * @brief Get direction trigger mode
     *
     * @return Direction trigger mode
     */
    uint8_t getDirectionTriggerMode() const { return m_directionTriggerMode; }

    /**
     * @brief Set direction sensitivity
     *
     * Minimum distance change to register as directional movement.
     *
     * @param sensitivity_mm Sensitivity in millimeters (default: 20mm)
     */
    void setDirectionSensitivity(uint32_t sensitivity_mm) {
        m_directionSensitivity = sensitivity_mm;
    }

    /**
     * @brief Set sample interval for adaptive threshold calculation
     *
     * The movement threshold adapts based on sample rate:
     * threshold = sampleInterval * VELOCITY_THRESHOLD (25 mm/ms)
     *
     * This ensures that fast movements aren't missed:
     * - Faster sampling (e.g., 50ms) → lower threshold (1250mm) → more sensitive
     * - Slower sampling (e.g., 100ms) → higher threshold (2500mm) → less sensitive
     *
     * @param interval_ms Sample interval in milliseconds (default: 75ms)
     */
    void setSampleInterval(uint32_t interval_ms) {
        m_sampleIntervalMs = interval_ms;
    }

    /**
     * @brief Get sample interval
     *
     * @return Sample interval in milliseconds
     */
    uint32_t getSampleInterval() const { return m_sampleIntervalMs; }

protected:
    // =========================================================================
    // Protected Interface - Subclass Implementation Required
    // =========================================================================

    /**
     * @brief Get raw distance reading from hardware
     *
     * Subclasses must implement this to read from their specific sensor.
     *
     * @return Distance in millimeters, 0 if timeout/error/out of range
     */
    virtual uint32_t getDistanceReading() = 0;

private:
    // =========================================================================
    // Distance Processing (Shared Logic)
    // =========================================================================

    /**
     * @brief Add distance sample to rolling window
     *
     * @param distance_mm Distance measurement to add
     */
    void addSampleToWindow(uint32_t distance_mm);

    /**
     * @brief Calculate median of sample window (better outlier rejection than average)
     *
     * @return Median distance in mm, 0 if window empty
     */
    uint32_t calculateWindowAverage() const;

    /**
     * @brief Reset sample window with current distance
     *
     * Fills entire window buffer with specified distance to eliminate
     * stale readings from before sudden appearance detection.
     *
     * @param distance_mm Distance to fill window with
     */
    void resetWindowWithDistance(uint32_t distance_mm, uint16_t previousAverage = 0);

    /**
     * @brief Check if movement detected based on window averages
     *
     * Uses rolling window analysis to distinguish actual movement from noise:
     * - Compares current window average to previous window average
     * - Requires significant change (≥200mm) to filter out sensor noise
     * - Validates reading consistency (spread within window <100mm)
     * - Prevents false triggers from static objects or small vibrations
     *
     * Movement detected when:
     * - Change ≥200mm AND readings are consistent (spread <100mm), OR
     * - Change ≥300mm (very large movement, regardless of consistency)
     *
     * @return true if significant movement detected (human motion pattern)
     */
    bool isMovementDetected() const;

    /**
     * @brief Update direction based on distance change
     */
    void updateDirection();

    /**
     * @brief Update dual-mode detection state based on raw reading
     *
     * Tracks whether object is approaching from outside range (gradual)
     * or appeared suddenly within range (side/hand).
     *
     * @param rawDistance Current raw (non-averaged) distance reading
     */
    void updateDualModeDetectionState(uint32_t rawDistance);

    /**
     * @brief Check for threshold crossing events
     */
    void checkThresholdEvents();

    // =========================================================================
    // Member Variables
    // =========================================================================

protected:
    // Distance range limits (accessible by derived classes for validation)
    uint32_t m_minDistance;         ///< Minimum valid distance (mm)
    uint32_t m_maxDistance;         ///< Maximum valid distance (mm)

private:
    // Distance state
    uint32_t m_currentDistance;     ///< Current distance (window average, mm)
    uint32_t m_detectionThreshold;  ///< Detection threshold (mm)
    bool m_objectDetected;          ///< Motion currently detected

    // Direction detection
    bool m_directionEnabled;        ///< Direction detection enabled
    MotionDirection m_direction;    ///< Current confirmed direction
    MotionDirection m_lastLoggedDirection; ///< Last logged direction (for change detection)
    uint32_t m_directionSensitivity;///< Min change for direction (mm)
    uint8_t m_directionTriggerMode; ///< 0=approaching, 1=receding, 2=both

    // Direction stability tracking (require consistent direction before confirming)
    static constexpr uint32_t DIRECTION_STABILITY_TIME_MS = 225; ///< Required stability time (225ms)
    MotionDirection m_candidateDirection; ///< Pending direction being evaluated
    uint8_t m_directionStabilityCount;    ///< Consecutive samples with same candidate direction

    // Dual-mode approach detection constants (must be defined before use)
    static constexpr uint8_t SUDDEN_APPEARANCE_READING_COUNT = 3;
    static constexpr uint8_t DIRECTION_CONFIRMATION_WINDOW_CYCLES = 2;

    // Dual-mode approach detection (gradual vs sudden appearance)
    bool m_seenApproachingFromOutside;    ///< True when detected gradual approach from outside range
    bool m_suddenAppearance;              ///< True when object appeared within range without approach
    bool m_awaitingDirectionConfirmation; ///< True while waiting for direction window cycles
    uint8_t m_confirmationCyclesRemaining;///< Window cycles remaining before triggering
    uint8_t m_consecutiveInRangeCount;    ///< Count of consecutive raw readings within range
    uint32_t m_lastRawDistance;           ///< Last raw (non-averaged) distance reading
    uint32_t m_suddenAppearanceBuffer[SUDDEN_APPEARANCE_READING_COUNT]; ///< Buffer for 3 readings to average

    // Debug tracking: last 5 raw readings for diagnostics
    static constexpr uint8_t RAW_READING_HISTORY_SIZE = 5;
    uint32_t m_rawReadingHistory[RAW_READING_HISTORY_SIZE]; ///< Last 5 raw readings
    uint8_t m_rawReadingHistoryIndex; ///< Current write position in history buffer
    uint8_t m_skipDirectionUpdateCount; ///< Skip N direction updates after wipe to preserve wipe-set candidate

    // Rolling window for movement detection
    static constexpr uint8_t MAX_SAMPLE_WINDOW_SIZE = 20;  ///< Maximum window size
    static constexpr uint8_t MIN_SAMPLE_WINDOW_SIZE = 3;   ///< Minimum window size
    uint8_t m_sampleWindowSize;         ///< Current window size (configurable)
    uint32_t m_sampleWindow[MAX_SAMPLE_WINDOW_SIZE];
    uint8_t m_sampleWindowIndex;
    uint8_t m_sampleWindowCount;
    uint32_t m_windowAverage;
    uint32_t m_lastWindowAverage;
    bool m_windowFilled;

    // Delta history for robust direction calculation (median of deltas)
    static constexpr uint8_t DELTA_HISTORY_SIZE = 5;
    int32_t m_deltaHistory[DELTA_HISTORY_SIZE]; ///< Last 5 delta values for median calculation
    uint8_t m_deltaHistoryIndex;                ///< Current write position
    uint8_t m_deltaHistoryCount;                ///< Number of valid deltas

    // Event tracking
    MotionEvent m_lastEvent;
    uint32_t m_eventCount;
    uint32_t m_lastEventTime;

    // Movement detection - sample interval for adaptive threshold calculation
    uint32_t m_sampleIntervalMs;    ///< Sample interval in milliseconds (for adaptive threshold)

    // Constants
    static constexpr uint32_t DEFAULT_THRESHOLD_MM = 500;      // 50cm
    static constexpr uint32_t DEFAULT_SENSITIVITY_MM = 20;     // 2cm
    static constexpr uint32_t MOVEMENT_THRESHOLD_MM = 200;     // 20cm for human motion (legacy, use adaptive)

    // Adaptive threshold: Based on velocity = 1 mm/ms (~3.6 km/h, pedestrian walking speed)
    // threshold = sampleInterval * VELOCITY_THRESHOLD ensures we detect pedestrian motion
    // Example: 75ms interval → 75mm threshold, 100ms → 100mm, 50ms → 50mm
    static constexpr uint32_t VELOCITY_THRESHOLD_MM_PER_MS = 1;  // ~3.6 km/h (1 m/s) pedestrian speed
};

#endif // STEPAWARE_DISTANCE_SENSOR_BASE_H
