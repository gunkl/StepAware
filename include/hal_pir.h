#ifndef STEPAWARE_HAL_PIR_H
#define STEPAWARE_HAL_PIR_H

#include <Arduino.h>
#include "config.h"
#include "hal_motion_sensor.h"

/**
 * @brief Hardware Abstraction Layer for PIR Motion Sensor
 *
 * Provides interface to AM312 PIR motion sensor with warm-up time handling,
 * mock mode for testing, and event counting.
 *
 * Inherits from HAL_MotionSensor for polymorphic sensor usage.
 *
 * Technical Specifications (AM312):
 * - Detection Range: up to 12 meters
 * - Detection Angle: 65 degrees
 * - Output: Digital HIGH when motion detected
 * - Trigger Mode: Repeatable (continuously outputs HIGH while motion detected)
 * - Warm-up Time: ~60 seconds
 * - Output Delay: ~2.3 seconds
 * - Operating Voltage: 3.3V - 12V
 * - Current Draw: <50uA idle, ~220uA active
 */
class HAL_PIR : public HAL_MotionSensor {
public:
    /**
     * @brief Construct a new HAL_PIR object
     *
     * @param pin GPIO pin number for PIR sensor output
     * @param mock_mode True to enable mock/simulation mode for testing
     */
    HAL_PIR(uint8_t pin, bool mock_mode = false);

    /**
     * @brief Destructor
     */
    ~HAL_PIR() override;

    // =========================================================================
    // HAL_MotionSensor Interface Implementation
    // =========================================================================

    /**
     * @brief Initialize the PIR sensor
     *
     * Configures GPIO pin and starts warm-up timer.
     *
     * @return true if initialization successful
     */
    bool begin() override;

    /**
     * @brief Update sensor state (call in main loop)
     *
     * Polls sensor pin, updates warm-up timer, and counts motion events.
     */
    void update() override;

    /**
     * @brief Check if motion is currently detected
     *
     * @return true if motion detected right now
     */
    bool motionDetected() const override;

    /**
     * @brief Check if sensor warm-up period is complete
     *
     * PIR sensors require ~60 seconds to stabilize after power-on.
     *
     * @return true if sensor is ready for reliable detection
     */
    bool isReady() const override;

    /**
     * @brief Get sensor type
     *
     * @return SENSOR_TYPE_PIR
     */
    SensorType getSensorType() const override { return SENSOR_TYPE_PIR; }

    /**
     * @brief Get sensor capabilities
     *
     * @return Reference to PIR capabilities structure
     */
    const SensorCapabilities& getCapabilities() const override;

    /**
     * @brief Get remaining warm-up time
     *
     * @return Milliseconds remaining in warm-up period (0 if ready)
     */
    uint32_t getWarmupTimeRemaining() const override;

    /**
     * @brief Get last motion event type
     *
     * @return Last motion event
     */
    MotionEvent getLastEvent() const override { return m_lastEvent; }

    /**
     * @brief Get total motion events detected
     *
     * @return Number of times motion was detected (rising edge count)
     */
    uint32_t getEventCount() const override { return m_motionEventCount; }

    /**
     * @brief Reset motion event counter
     */
    void resetEventCount() override;

    /**
     * @brief Get timestamp of last event
     *
     * @return Timestamp in milliseconds since boot
     */
    uint32_t getLastEventTime() const override { return m_lastEventTime; }

    /**
     * @brief Check if running in mock mode
     *
     * @return true if mock mode enabled
     */
    bool isMockMode() const override { return m_mockMode; }

    /**
     * @brief Inject mock motion state (mock mode only)
     *
     * @param detected Motion detected state to inject
     */
    void mockSetMotion(bool detected) override;

    /**
     * @brief Mark sensor as ready (mock mode only, skips warmup)
     */
    void mockSetReady() override;

    // =========================================================================
    // Legacy Interface (backward compatibility)
    // =========================================================================

    /**
     * @brief Get total motion events detected
     * @deprecated Use getEventCount() instead
     * @return Number of times motion was detected
     */
    uint32_t getMotionEventCount() { return getEventCount(); }

    /**
     * @brief Reset motion event counter
     * @deprecated Use resetEventCount() instead
     */
    void resetMotionEventCount() { resetEventCount(); }

    /**
     * @brief Trigger mock motion detection
     *
     * Simulates motion sensor activation for testing.
     * Only works when mock_mode is enabled.
     *
     * @param duration_ms How long to simulate motion (0 = single edge, default)
     */
    void mockTriggerMotion(uint32_t duration_ms = 0);

    /**
     * @brief Clear mock motion
     *
     * Only works when mock_mode is enabled.
     */
    void mockClearMotion();

    /**
     * @brief Set mock sensor ready state
     * @deprecated Use mockSetReady() instead for new code
     *
     * Allows bypassing warm-up period for testing.
     * Only works when mock_mode is enabled.
     *
     * @param ready True to mark sensor as ready
     */
    void mockSetReady(bool ready);

private:
    uint8_t m_pin;                  ///< GPIO pin number
    bool m_mockMode;                ///< Mock mode enabled
    bool m_initialized;             ///< Initialization complete

    // State
    bool m_motionDetected;          ///< Current motion state
    bool m_lastState;               ///< Previous state (for edge detection)
    bool m_sensorReady;             ///< Warm-up complete
    MotionEvent m_lastEvent;        ///< Last event type

    // Timing
    uint32_t m_startTime;           ///< When sensor was initialized (millis)
    uint32_t m_warmupDuration;      ///< Warm-up period (default: PIR_WARMUP_TIME_MS)
    uint32_t m_lastEventTime;       ///< Timestamp of last event

    // Statistics
    uint32_t m_motionEventCount;    ///< Total motion events detected

    // Timing instrumentation (debug)
    uint32_t m_lastRisingEdgeMicros; ///< Microsecond timestamp of rising edge

    // Mock mode state
    uint32_t m_mockMotionEndTime;   ///< When mock motion should end

    // Static capabilities
    static const SensorCapabilities s_capabilities;
};

#endif // STEPAWARE_HAL_PIR_H
