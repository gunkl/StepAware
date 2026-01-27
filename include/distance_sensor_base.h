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
     * @brief Calculate average of sample window
     *
     * @return Average distance in mm, 0 if window empty
     */
    uint32_t calculateWindowAverage() const;

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
     * @brief Check for threshold crossing events
     */
    void checkThresholdEvents();

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Distance state
    uint32_t m_currentDistance;     ///< Current distance (window average, mm)
    uint32_t m_detectionThreshold;  ///< Detection threshold (mm)
    uint32_t m_minDistance;         ///< Minimum valid distance (mm)
    uint32_t m_maxDistance;         ///< Maximum valid distance (mm)
    bool m_objectDetected;          ///< Motion currently detected

    // Direction detection
    bool m_directionEnabled;        ///< Direction detection enabled
    MotionDirection m_direction;    ///< Current direction
    uint32_t m_directionSensitivity;///< Min change for direction (mm)
    uint8_t m_directionTriggerMode; ///< 0=approaching, 1=receding, 2=both

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

    // Event tracking
    MotionEvent m_lastEvent;
    uint32_t m_eventCount;
    uint32_t m_lastEventTime;

    // Constants
    static constexpr uint32_t DEFAULT_THRESHOLD_MM = 500;      // 50cm
    static constexpr uint32_t DEFAULT_SENSITIVITY_MM = 20;     // 2cm
    static constexpr uint32_t MOVEMENT_THRESHOLD_MM = 200;     // 20cm for human motion
};

#endif // STEPAWARE_DISTANCE_SENSOR_BASE_H
