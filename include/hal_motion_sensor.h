#ifndef STEPAWARE_HAL_MOTION_SENSOR_H
#define STEPAWARE_HAL_MOTION_SENSOR_H

#include <stdint.h>
#include "sensor_types.h"

/**
 * @file hal_motion_sensor.h
 * @brief Abstract base class for motion sensors
 *
 * Defines the common interface for all motion sensor implementations.
 * Concrete implementations (PIR, IR, Ultrasonic) inherit from this class.
 *
 * Design Goals:
 * - Polymorphic sensor usage (swap sensors at runtime or compile-time)
 * - Capability-based feature detection
 * - Backward compatible with existing PIR implementation
 * - Support for mock mode testing
 *
 * Usage:
 * ```cpp
 * HAL_MotionSensor* sensor = new HAL_PIR(PIN_PIR);
 * sensor->begin();
 *
 * void loop() {
 *     sensor->update();
 *     if (sensor->motionDetected()) {
 *         // Handle motion
 *     }
 * }
 * ```
 */
class HAL_MotionSensor {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~HAL_MotionSensor() = default;

    // =========================================================================
    // Core Interface (must be implemented by all sensors)
    // =========================================================================

    /**
     * @brief Initialize the sensor
     *
     * @return true if initialization successful
     */
    virtual bool begin() = 0;

    /**
     * @brief Update sensor state (call every loop iteration)
     *
     * Reads sensor, updates internal state, and triggers events.
     */
    virtual void update() = 0;

    /**
     * @brief Check if motion is currently detected
     *
     * @return true if motion detected
     */
    virtual bool motionDetected() const = 0;

    /**
     * @brief Check if sensor is ready for detection
     *
     * For sensors with warmup periods, returns false until ready.
     *
     * @return true if sensor is ready
     */
    virtual bool isReady() const = 0;

    // =========================================================================
    // Type and Capability Interface
    // =========================================================================

    /**
     * @brief Get sensor type
     *
     * @return Sensor type enum value
     */
    virtual SensorType getSensorType() const = 0;

    /**
     * @brief Get sensor capabilities
     *
     * @return Reference to capabilities structure
     */
    virtual const SensorCapabilities& getCapabilities() const = 0;

    // =========================================================================
    // Extended Interface (optional, based on capabilities)
    // =========================================================================

    /**
     * @brief Get distance measurement
     *
     * Only valid for sensors with supportsDistanceMeasurement capability.
     *
     * @return Distance in millimeters, 0 if not supported
     */
    virtual uint32_t getDistance() const { return 0; }

    /**
     * @brief Get motion direction
     *
     * Only valid for sensors with supportsDirectionDetection capability.
     *
     * @return Motion direction enum value
     */
    virtual MotionDirection getDirection() const { return DIRECTION_UNKNOWN; }

    /**
     * @brief Get remaining warmup time
     *
     * Only valid for sensors with requiresWarmup capability.
     *
     * @return Remaining warmup time in milliseconds, 0 if ready
     */
    virtual uint32_t getWarmupTimeRemaining() const { return 0; }

    /**
     * @brief Get last motion event
     *
     * @return Last motion event type
     */
    virtual MotionEvent getLastEvent() const { return MOTION_EVENT_NONE; }

    // =========================================================================
    // Statistics Interface
    // =========================================================================

    /**
     * @brief Get total event count since reset
     *
     * @return Number of motion events detected
     */
    virtual uint32_t getEventCount() const = 0;

    /**
     * @brief Reset event counter
     */
    virtual void resetEventCount() = 0;

    /**
     * @brief Get timestamp of last event
     *
     * @return Timestamp in milliseconds since boot
     */
    virtual uint32_t getLastEventTime() const = 0;

    // =========================================================================
    // Configuration Interface
    // =========================================================================

    /**
     * @brief Set detection threshold (for distance-based sensors)
     *
     * @param threshold_mm Detection threshold in millimeters
     */
    virtual void setDetectionThreshold(uint32_t threshold_mm) { (void)threshold_mm; }

    /**
     * @brief Get current detection threshold
     *
     * @return Detection threshold in millimeters, 0 if not applicable
     */
    virtual uint32_t getDetectionThreshold() const { return 0; }

    /**
     * @brief Set sample window size (for distance sensors with averaging)
     *
     * @param size Window size in samples (3-20)
     */
    virtual void setSampleWindowSize(uint8_t size) { (void)size; }

    /**
     * @brief Enable or disable direction detection
     *
     * @param enable true to enable direction detection
     */
    virtual void setDirectionDetection(bool enable) { (void)enable; }

    /**
     * @brief Check if direction detection is enabled
     *
     * @return true if direction detection is enabled
     */
    virtual bool isDirectionDetectionEnabled() const { return false; }

    /**
     * @brief Set distance range for detection (min/max thresholds)
     *
     * Only triggers detection when object is within this range.
     *
     * @param min_mm Minimum detection distance in millimeters
     * @param max_mm Maximum detection distance in millimeters
     */
    virtual void setDistanceRange(uint32_t min_mm, uint32_t max_mm) {
        (void)min_mm;
        (void)max_mm;
    }

    /**
     * @brief Get minimum detection distance
     *
     * @return Minimum detection distance in millimeters, 0 if not applicable
     */
    virtual uint32_t getMinDistance() const { return 0; }

    /**
     * @brief Get maximum detection distance
     *
     * @return Maximum detection distance in millimeters, 0 if not applicable
     */
    virtual uint32_t getMaxDistance() const { return 0; }

    /**
     * @brief Enable rapid sampling mode for direction detection
     *
     * Takes multiple quick samples to accurately determine direction.
     *
     * @param sample_count Number of samples to take (2-20)
     * @param interval_ms Interval between samples in milliseconds
     */
    virtual void setRapidSampling(uint8_t sample_count, uint16_t interval_ms) {
        (void)sample_count;
        (void)interval_ms;
    }

    /**
     * @brief Trigger rapid sampling sequence
     *
     * Immediately takes rapid samples and updates direction.
     * Useful for switching from low-power to high-power mode.
     */
    virtual void triggerRapidSample() {}

    // =========================================================================
    // Mock Mode Interface
    // =========================================================================

    /**
     * @brief Check if running in mock mode
     *
     * @return true if mock mode enabled
     */
    virtual bool isMockMode() const { return false; }

    /**
     * @brief Inject mock motion state (mock mode only)
     *
     * @param detected Motion detected state to inject
     */
    virtual void mockSetMotion(bool detected) { (void)detected; }

    /**
     * @brief Inject mock distance reading (mock mode only)
     *
     * @param distance_mm Distance in millimeters to inject
     */
    virtual void mockSetDistance(uint32_t distance_mm) { (void)distance_mm; }

    /**
     * @brief Mark sensor as ready (mock mode only, skips warmup)
     */
    virtual void mockSetReady() {}

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get full sensor status
     *
     * @return Status structure with all sensor information
     */
    virtual SensorStatus getStatus() const {
        SensorStatus status = {};
        status.ready = isReady();
        status.motionDetected = motionDetected();
        status.lastEventTime = getLastEventTime();
        status.eventCount = getEventCount();
        status.distance = getDistance();
        status.direction = getDirection();
        status.lastEvent = getLastEvent();
        return status;
    }

    /**
     * @brief Check if sensor supports a specific capability
     *
     * @param capability Capability to check (use capability flags)
     * @return true if capability supported
     */
    bool supportsDistanceMeasurement() const {
        return getCapabilities().supportsDistanceMeasurement;
    }

    bool supportsDirectionDetection() const {
        return getCapabilities().supportsDirectionDetection;
    }

    bool supportsDeepSleepWake() const {
        return getCapabilities().supportsDeepSleepWake;
    }

    bool requiresWarmup() const {
        return getCapabilities().requiresWarmup;
    }
};

#endif // STEPAWARE_HAL_MOTION_SENSOR_H
