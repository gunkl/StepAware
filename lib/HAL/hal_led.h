#ifndef STEPAWARE_HAL_LED_H
#define STEPAWARE_HAL_LED_H

#include <Arduino.h>
#include <config.h>

/**
 * @brief Hardware Abstraction Layer for LED Control
 *
 * This class provides an interface for controlling LEDs with PWM brightness
 * control and various blinking patterns. Supports both real hardware and
 * mock mode for testing.
 *
 * Features:
 * - PWM brightness control (0-255)
 * - Blinking patterns (fast, slow, warning)
 * - Non-blocking operation
 * - Mock mode for development without hardware
 */
class HAL_LED {
public:
    /**
     * @brief LED blinking patterns
     */
    enum BlinkPattern {
        PATTERN_OFF,           ///< LED always off
        PATTERN_ON,            ///< LED always on
        PATTERN_BLINK_FAST,    ///< Fast blink (250ms on/off)
        PATTERN_BLINK_SLOW,    ///< Slow blink (1s on/off)
        PATTERN_BLINK_WARNING, ///< Warning blink (500ms on/off)
        PATTERN_PULSE,         ///< Smooth pulsing (breathing effect)
        PATTERN_CUSTOM         ///< Custom timing
    };

    /**
     * @brief Construct a new HAL_LED object
     *
     * @param pin GPIO pin connected to LED (default: PIN_HAZARD_LED)
     * @param pwmChannel PWM channel to use (default: LED_PWM_CHANNEL)
     * @param mock Enable mock mode for testing (default: MOCK_HARDWARE)
     */
    HAL_LED(uint8_t pin = PIN_HAZARD_LED, uint8_t pwmChannel = LED_PWM_CHANNEL, bool mock = MOCK_HARDWARE);

    /**
     * @brief Destructor
     */
    ~HAL_LED();

    /**
     * @brief Initialize the LED
     *
     * Sets up GPIO pin and PWM channel.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Set LED brightness
     *
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current LED brightness
     *
     * @return uint8_t Current brightness (0-255)
     */
    uint8_t getBrightness();

    /**
     * @brief Turn LED on at full brightness
     */
    void on();

    /**
     * @brief Turn LED off
     */
    void off();

    /**
     * @brief Toggle LED state
     */
    void toggle();

    /**
     * @brief Check if LED is currently on
     *
     * @return true if LED is on (brightness > 0)
     */
    bool isOn();

    /**
     * @brief Set LED blinking pattern
     *
     * @param pattern Blinking pattern to use
     */
    void setPattern(BlinkPattern pattern);

    /**
     * @brief Set custom blink timing
     *
     * @param onTime Time LED is on (ms)
     * @param offTime Time LED is off (ms)
     */
    void setCustomPattern(uint32_t onTime, uint32_t offTime);

    /**
     * @brief Get current blinking pattern
     *
     * @return BlinkPattern Current pattern
     */
    BlinkPattern getPattern();

    /**
     * @brief Update LED state (must be called regularly in loop)
     *
     * This method handles non-blocking blink patterns and effects.
     * Call this every loop iteration for smooth operation.
     */
    void update();

    /**
     * @brief Start LED pattern for specified duration
     *
     * @param pattern Pattern to display
     * @param duration_ms Duration in milliseconds (0 = infinite)
     */
    void startPattern(BlinkPattern pattern, uint32_t duration_ms = 0);

    /**
     * @brief Stop current pattern and turn off LED
     */
    void stopPattern();

    /**
     * @brief Check if timed pattern is still active
     *
     * @return true if pattern duration has not expired
     */
    bool isPatternActive();

    // ========================================================================
    // Mock Hardware Methods
    // ========================================================================

#if MOCK_HARDWARE
    /**
     * @brief Get mock LED state (for testing)
     *
     * @return true if mock LED is on
     */
    bool mockIsOn();

    /**
     * @brief Get mock LED brightness (for testing)
     *
     * @return uint8_t Mock brightness value
     */
    uint8_t mockGetBrightness();

    /**
     * @brief Get number of blinks in mock mode
     *
     * @return uint32_t Total blink count
     */
    uint32_t mockGetBlinkCount();

    /**
     * @brief Reset mock blink counter
     */
    void mockResetBlinkCount();
#endif

private:
    uint8_t m_pin;                  ///< GPIO pin number
    uint8_t m_pwmChannel;           ///< PWM channel
    bool m_mock;                    ///< Mock mode enabled
    bool m_initialized;             ///< Initialization state

    uint8_t m_brightness;           ///< Current brightness (0-255)
    BlinkPattern m_pattern;         ///< Current blink pattern
    bool m_ledState;                ///< Current LED state (on/off)

    // Blinking control
    uint32_t m_lastToggleTime;      ///< Time of last toggle (ms)
    uint32_t m_onTime;              ///< On duration for blink (ms)
    uint32_t m_offTime;             ///< Off duration for blink (ms)

    // Pattern duration
    uint32_t m_patternStartTime;    ///< When pattern started (ms)
    uint32_t m_patternDuration;     ///< Pattern duration (ms, 0=infinite)

    // Pulse effect
    uint8_t m_pulseDirection;       ///< Pulse direction (0=down, 1=up)
    uint8_t m_pulseValue;           ///< Current pulse brightness

#if MOCK_HARDWARE
    bool m_mockState;               ///< Mock LED state
    uint8_t m_mockBrightness;       ///< Mock brightness
    uint32_t m_mockBlinkCount;      ///< Number of blinks
#endif

    /**
     * @brief Apply brightness to hardware PWM
     */
    void applyBrightness();

    /**
     * @brief Get blink timing for pattern
     *
     * @param pattern Pattern to get timing for
     * @param onTime Output: on time (ms)
     * @param offTime Output: off time (ms)
     */
    void getPatternTiming(BlinkPattern pattern, uint32_t& onTime, uint32_t& offTime);
};

#endif // STEPAWARE_HAL_LED_H
