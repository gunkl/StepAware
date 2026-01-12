#ifndef STEPAWARE_HAL_PIR_H
#define STEPAWARE_HAL_PIR_H

#include <Arduino.h>
#include <config.h>

/**
 * @brief Hardware Abstraction Layer for AM312 PIR Motion Sensor
 *
 * This class provides an interface to the AM312 passive infrared motion sensor.
 * The AM312 outputs a digital HIGH signal when motion is detected and LOW when
 * no motion is present. It has built-in timing delays of approximately 2.3 seconds.
 *
 * Features:
 * - Interrupt-driven motion detection
 * - Warm-up period handling (1 minute)
 * - Mock mode for development without hardware
 *
 * Specifications (AM312):
 * - Operating Voltage: 2.7V - 3.3V
 * - Detection Range: Up to 12 meters
 * - Detection Angle: 65° (top-down), 50° (horizontal)
 * - Output Delay: 2.3 seconds
 * - Power Consumption: 15µA
 */
class HAL_PIR {
public:
    /**
     * @brief Construct a new HAL_PIR object
     *
     * @param pin GPIO pin connected to PIR sensor output (default: PIN_PIR_SENSOR)
     * @param mock Enable mock mode for testing without hardware (default: MOCK_HARDWARE)
     */
    HAL_PIR(uint8_t pin = PIN_PIR_SENSOR, bool mock = MOCK_HARDWARE);

    /**
     * @brief Destructor
     */
    ~HAL_PIR();

    /**
     * @brief Initialize the PIR sensor
     *
     * Sets up GPIO pin mode and optionally attaches interrupt handler.
     * In mock mode, initializes simulation state.
     *
     * @return true if initialization successful, false otherwise
     */
    bool begin();

    /**
     * @brief Check if motion is currently detected
     *
     * Reads the current state of the PIR sensor output pin.
     * In real mode: Returns HIGH if motion detected, LOW otherwise
     * In mock mode: Returns simulated motion state
     *
     * @return true if motion detected, false otherwise
     */
    bool motionDetected();

    /**
     * @brief Check if sensor warm-up period has completed
     *
     * The AM312 sensor requires approximately 60 seconds to stabilize
     * after power-on before reliable motion detection is possible.
     *
     * @return true if warm-up complete, false if still warming up
     */
    bool isReady();

    /**
     * @brief Get time remaining in warm-up period
     *
     * @return uint32_t Milliseconds remaining in warm-up period, 0 if ready
     */
    uint32_t getWarmupTimeRemaining();

    /**
     * @brief Enable interrupt-driven motion detection
     *
     * Attaches an interrupt handler that will be called when the PIR
     * sensor output changes state (rising edge = motion detected).
     *
     * @param callback Function to call when motion detected
     * @return true if interrupt enabled successfully
     */
    bool enableInterrupt(void (*callback)());

    /**
     * @brief Disable interrupt-driven motion detection
     */
    void disableInterrupt();

    /**
     * @brief Get the number of motion events detected
     *
     * Counter is incremented each time motion is detected.
     * Useful for activity tracking and statistics.
     *
     * @return uint32_t Total number of motion events since initialization
     */
    uint32_t getMotionEventCount();

    /**
     * @brief Reset the motion event counter
     */
    void resetMotionEventCount();

    // ========================================================================
    // Mock Hardware Methods (only active when MOCK_HARDWARE = 1)
    // ========================================================================

#if MOCK_HARDWARE
    /**
     * @brief Simulate motion detection (mock mode only)
     *
     * @param detected Set to true to simulate motion, false for no motion
     */
    void mockSetMotion(bool detected);

    /**
     * @brief Get current mock motion state
     *
     * @return true if mock motion is active
     */
    bool mockGetMotion();

    /**
     * @brief Simulate a motion trigger pulse (mock mode only)
     *
     * Sets motion state to HIGH for specified duration, then back to LOW.
     *
     * @param duration_ms Duration of motion pulse in milliseconds
     */
    void mockTriggerMotion(uint32_t duration_ms = MOTION_WARNING_DURATION_MS);
#endif

private:
    uint8_t m_pin;                  ///< GPIO pin number
    bool m_mock;                    ///< Mock mode enabled
    bool m_initialized;             ///< Initialization state
    uint32_t m_warmupStartTime;     ///< Time when initialization began (ms)
    uint32_t m_motionEventCount;    ///< Total motion events detected
    bool m_interruptEnabled;        ///< Interrupt handler attached

#if MOCK_HARDWARE
    bool m_mockMotionState;         ///< Simulated motion state
    uint32_t m_mockMotionEndTime;   ///< When mock motion pulse should end
#endif

    /**
     * @brief Read the raw GPIO pin state
     *
     * @return int Pin state (HIGH or LOW)
     */
    int readPin();
};

#endif // STEPAWARE_HAL_PIR_H
