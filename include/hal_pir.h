#ifndef STEPAWARE_HAL_PIR_H
#define STEPAWARE_HAL_PIR_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Hardware Abstraction Layer for PIR Motion Sensor
 *
 * Provides interface to AM312 PIR motion sensor with warm-up time handling,
 * mock mode for testing, and event counting.
 *
 * Technical Specifications (AM312):
 * - Detection Range: up to 12 meters
 * - Detection Angle: 65 degrees
 * - Output: Digital HIGH when motion detected
 * - Trigger Mode: Repeatable (continuously outputs HIGH while motion detected)
 * - Warm-up Time: ~60 seconds
 * - Output Delay: ~2.3 seconds
 * - Operating Voltage: 3.3V - 12V
 * - Current Draw: <50μA idle, ~220μA active
 */
class HAL_PIR {
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
    ~HAL_PIR();

    /**
     * @brief Initialize the PIR sensor
     *
     * Configures GPIO pin and starts warm-up timer.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update sensor state (call in main loop)
     *
     * Polls sensor pin, updates warm-up timer, and counts motion events.
     */
    void update();

    /**
     * @brief Check if motion is currently detected
     *
     * @return true if motion detected right now
     */
    bool motionDetected();

    /**
     * @brief Check if sensor warm-up period is complete
     *
     * PIR sensors require ~60 seconds to stabilize after power-on.
     *
     * @return true if sensor is ready for reliable detection
     */
    bool isReady();

    /**
     * @brief Get remaining warm-up time
     *
     * @return uint32_t Milliseconds remaining in warm-up period (0 if ready)
     */
    uint32_t getWarmupTimeRemaining();

    /**
     * @brief Get total motion events detected
     *
     * @return uint32_t Number of times motion was detected (rising edge count)
     */
    uint32_t getMotionEventCount();

    /**
     * @brief Reset motion event counter
     */
    void resetMotionEventCount();

    // Mock/Test Methods (only active if mock_mode = true)

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

    // Timing
    uint32_t m_startTime;           ///< When sensor was initialized (millis)
    uint32_t m_warmupDuration;      ///< Warm-up period (default: PIR_WARMUP_TIME_MS)

    // Statistics
    uint32_t m_motionEventCount;    ///< Total motion events detected

    // Mock mode state
    uint32_t m_mockMotionEndTime;   ///< When mock motion should end
};

#endif // STEPAWARE_HAL_PIR_H
