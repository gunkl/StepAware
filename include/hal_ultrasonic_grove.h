#ifndef STEPAWARE_HAL_ULTRASONIC_GROVE_H
#define STEPAWARE_HAL_ULTRASONIC_GROVE_H

#include <Arduino.h>
#include "config.h"
#include "hal_motion_sensor.h"
#include "distance_sensor_base.h"

#if !MOCK_HARDWARE
#include <NewPing.h>
#endif

/**
 * @brief Hardware Abstraction Layer for Grove Ultrasonic Distance Sensor v2.0
 *
 * Refactored architecture:
 * 1. Hardware communication: Uses NewPing library (ping_cm method)
 * 2. Inherits DistanceSensorBase for movement/direction detection logic (shared)
 * 3. Implements HAL_MotionSensor interface for StepAware product integration
 *
 * Technical Specifications (Grove Ultrasonic Ranger v2.0):
 * - Detection Range: 2cm - 350cm
 * - Accuracy: 1cm
 * - Measuring Angle: 15 degrees
 * - Operating Voltage: 3.2V - 5.2V (excellent 3.3V compatibility!)
 * - Current Draw: ~8mA during measurement (lower power than HC-SR04!)
 *
 * Key Difference from HC-SR04:
 * - Single SIG pin shared for both trigger and echo (saves 1 GPIO pin!)
 * - Better 3.3V voltage support
 * - Lower power consumption (8mA vs 15mA)
 */
class HAL_Ultrasonic_Grove : public HAL_MotionSensor, public DistanceSensorBase {
public:
    HAL_Ultrasonic_Grove(uint8_t sigPin, bool mock_mode = false);
    ~HAL_Ultrasonic_Grove() override;

    // =========================================================================
    // HAL_MotionSensor Interface Implementation
    // =========================================================================

    bool begin() override;
    void update() override;
    bool motionDetected() const override { return isMotionDetected(); }
    bool isReady() const override { return m_initialized; }
    SensorType getSensorType() const override { return SENSOR_TYPE_ULTRASONIC_GROVE; }
    const SensorCapabilities& getCapabilities() const override;
    uint32_t getWarmupTimeRemaining() const override { return 0; }

    // Event methods - delegate to DistanceSensorBase
    MotionEvent getLastEvent() const override { return DistanceSensorBase::getLastEvent(); }
    uint32_t getEventCount() const override { return DistanceSensorBase::getEventCount(); }
    void resetEventCount() override { DistanceSensorBase::resetEventCount(); }
    uint32_t getLastEventTime() const override { return DistanceSensorBase::getLastEventTime(); }
    bool isMockMode() const override { return m_mockMode; }

    // Distance methods - delegate to DistanceSensorBase
    uint32_t getDistance() const override { return getCurrentDistance(); }
    MotionDirection getDirection() const override { return DistanceSensorBase::getDirection(); }

    // Configuration - delegate to DistanceSensorBase
    void setDetectionThreshold(uint32_t threshold_mm) override {
        DistanceSensorBase::setDetectionThreshold(threshold_mm);
    }
    uint32_t getDetectionThreshold() const override {
        return DistanceSensorBase::getDetectionThreshold();
    }
    void setSampleWindowSize(uint8_t size) override {
        DistanceSensorBase::setSampleWindowSize(size);
    }
    void setDirectionDetection(bool enable) override {
        DistanceSensorBase::setDirectionDetection(enable);
    }
    bool isDirectionDetectionEnabled() const override {
        return DistanceSensorBase::isDirectionDetectionEnabled();
    }
    void setDirectionTriggerMode(uint8_t mode) {
        DistanceSensorBase::setDirectionTriggerMode(mode);
    }
    uint8_t getDirectionTriggerMode() const {
        return DistanceSensorBase::getDirectionTriggerMode();
    }
    void setDistanceRange(uint32_t min_mm, uint32_t max_mm) override {
        DistanceSensorBase::setDistanceRange(min_mm, max_mm);
    }
    uint32_t getMinDistance() const override { return DistanceSensorBase::getMinDistance(); }
    uint32_t getMaxDistance() const override { return DistanceSensorBase::getMaxDistance(); }

    // =========================================================================
    // Mock Mode Interface
    // =========================================================================

    void mockSetMotion(bool detected) override;
    void mockSetDistance(uint32_t distance_mm) override;
    void mockSetReady() override {}

    // =========================================================================
    // Ultrasonic-Specific Methods
    // =========================================================================

    void setMeasurementInterval(uint32_t interval_ms);
    uint32_t getMeasurementInterval() const { return m_measurementInterval; }
    void setDirectionSensitivity(uint32_t sensitivity_mm) {
        DistanceSensorBase::setDirectionSensitivity(sensitivity_mm);
    }
    void setRapidSampling(uint8_t sample_count, uint16_t interval_ms) override {}
    void triggerRapidSample() override {}

    // =========================================================================
    // Error Rate Monitoring (Rolling Buffer)
    // =========================================================================

    /**
     * @brief Get hardware error rate as a percentage
     *
     * Returns the percentage of failed measurements from the rolling buffer.
     * Error rate is continuously calculated from the last 100 measurements.
     *
     * A failure is counted when:
     * - Distance reading returns 0 (no echo received)
     * - Hardware timeout occurs
     *
     * @return Error rate percentage (0.0 - 100.0), or -1.0 if < 100 samples collected
     */
    float getErrorRate() const;

    /**
     * @brief Check if error rate data is available
     *
     * Error rate becomes available after the first 100 measurements.
     *
     * @return true if 100+ samples have been collected, false otherwise
     */
    bool isErrorRateAvailable() const { return m_errorRateValid; }

protected:
    /**
     * @brief Get raw distance reading from Grove ultrasonic sensor
     *
     * Implements DistanceSensorBase::getDistanceReading()
     * Uses NewPing library (ping_cm method).
     *
     * @return Distance in millimeters, 0 if error/timeout
     */
    uint32_t getDistanceReading() override;

private:
    uint8_t m_sigPin;
    bool m_mockMode;
    bool m_initialized;
    uint32_t m_lastMeasurementTime;
    uint32_t m_measurementInterval;
    uint32_t m_mockDistance;

#if !MOCK_HARDWARE
    NewPing* m_sonar;  // NewPing library instance
#endif

    // Rolling buffer for error rate tracking
    // Uses a counter instead of actual buffer for memory efficiency:
    // - Increment counter on success (max 100)
    // - Decrement counter on failure (min 0)
    // - Error rate = 100 - counter value
    int16_t m_successCounter;       // Rolling success counter (0-100)
    uint16_t m_totalSamplesCollected; // Total samples collected (for warmup)
    bool m_errorRateValid;          // True after 100 samples collected

    static const SensorCapabilities s_capabilities;
    static constexpr uint32_t MEASUREMENT_TIMEOUT_US = 30000;
    static constexpr uint32_t MIN_MEASUREMENT_INTERVAL_MS = 60;
    static constexpr uint8_t ERROR_RATE_SAMPLE_COUNT = 100;  // Rolling buffer size
};

#endif // STEPAWARE_HAL_ULTRASONIC_GROVE_H
