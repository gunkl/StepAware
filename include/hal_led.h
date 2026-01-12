#ifndef STEPAWARE_HAL_LED_H
#define STEPAWARE_HAL_LED_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Hardware Abstraction Layer for LED Control
 *
 * Provides PWM-based LED control with various blink patterns, brightness levels,
 * and mock mode for testing without hardware.
 *
 * Features:
 * - PWM brightness control (0-255)
 * - Pre-defined blink patterns
 * - Pattern duration support (finite or infinite)
 * - Mock mode for testing
 * - Non-blocking operation
 */
class HAL_LED {
public:
    /**
     * @brief LED blink patterns
     */
    enum Pattern {
        PATTERN_OFF,                ///< LED off
        PATTERN_ON,                 ///< LED on (steady)
        PATTERN_BLINK_FAST,         ///< Fast blink (250ms on/off)
        PATTERN_BLINK_SLOW,         ///< Slow blink (1s on/off)
        PATTERN_BLINK_WARNING,      ///< Warning blink (500ms on/off)
        PATTERN_PULSE,              ///< Smooth pulsing (breathing)
        PATTERN_CUSTOM              ///< Custom pattern (user-defined)
    };

    /**
     * @brief Construct a new HAL_LED object
     *
     * @param pin GPIO pin number for LED
     * @param pwm_channel PWM channel to use (0-15 on ESP32)
     * @param mock_mode True to enable mock/simulation mode for testing
     */
    HAL_LED(uint8_t pin, uint8_t pwm_channel, bool mock_mode = false);

    /**
     * @brief Destructor
     */
    ~HAL_LED();

    /**
     * @brief Initialize the LED
     *
     * Sets up PWM channel and configures GPIO pin.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update LED state (call in main loop)
     *
     * Handles pattern timing, brightness transitions, and pattern expiration.
     */
    void update();

    /**
     * @brief Turn LED on at specified brightness
     *
     * @param brightness Brightness level 0-255 (default: full brightness)
     */
    void on(uint8_t brightness = LED_BRIGHTNESS_FULL);

    /**
     * @brief Turn LED off
     */
    void off();

    /**
     * @brief Set LED brightness
     *
     * @param brightness Brightness level 0-255
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current brightness level
     *
     * @return uint8_t Current brightness (0-255)
     */
    uint8_t getBrightness();

    /**
     * @brief Set LED pattern
     *
     * Convenience method for setting a pattern without duration.
     * Pattern runs indefinitely until changed.
     *
     * @param pattern Pattern to use
     */
    void setPattern(Pattern pattern);

    /**
     * @brief Start a LED pattern with optional duration
     *
     * @param pattern Pattern to use
     * @param duration_ms Duration in milliseconds (0 = infinite)
     */
    void startPattern(Pattern pattern, uint32_t duration_ms = 0);

    /**
     * @brief Stop current pattern
     *
     * Turns LED off and stops pattern.
     */
    void stopPattern();

    /**
     * @brief Get current pattern
     *
     * @return Pattern Currently active pattern
     */
    Pattern getPattern();

    /**
     * @brief Check if pattern is currently active
     *
     * @return true if pattern is running (not PATTERN_OFF)
     */
    bool isPatternActive();

    /**
     * @brief Check if LED is currently on
     *
     * @return true if LED is on (brightness > 0)
     */
    bool isOn();

    /**
     * @brief Set custom pattern timing
     *
     * Allows defining custom on/off intervals for PATTERN_CUSTOM.
     *
     * @param on_ms Milliseconds LED should be on
     * @param off_ms Milliseconds LED should be off
     */
    void setCustomPattern(uint32_t on_ms, uint32_t off_ms);

private:
    uint8_t m_pin;                  ///< GPIO pin number
    uint8_t m_pwmChannel;           ///< PWM channel
    bool m_mockMode;                ///< Mock mode enabled
    bool m_initialized;             ///< Initialization complete

    // State
    Pattern m_currentPattern;       ///< Current pattern
    uint8_t m_brightness;           ///< Current brightness (0-255)
    bool m_isOn;                    ///< LED is currently on

    // Pattern timing
    uint32_t m_patternStartTime;    ///< When pattern started (millis)
    uint32_t m_patternDuration;     ///< Pattern duration (0 = infinite)
    uint32_t m_lastToggleTime;      ///< Last toggle time for blinking
    bool m_patternState;            ///< Current on/off state in pattern

    // Custom pattern
    uint32_t m_customOnMs;          ///< Custom pattern on time
    uint32_t m_customOffMs;         ///< Custom pattern off time

    /**
     * @brief Get interval for current pattern
     *
     * @return uint32_t Milliseconds for current pattern interval
     */
    uint32_t getPatternInterval();

    /**
     * @brief Apply brightness to hardware
     *
     * Writes PWM value to pin or updates mock state.
     */
    void applyBrightness();

    /**
     * @brief Process pattern logic
     *
     * Updates LED state based on current pattern and timing.
     */
    void processPattern();
};

#endif // STEPAWARE_HAL_LED_H
