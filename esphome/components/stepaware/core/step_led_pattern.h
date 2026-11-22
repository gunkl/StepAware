/**
 * @file step_led_pattern.h
 * @brief Hazard LED blink-pattern engine (port of HAL_LED pattern logic).
 *
 * Pure logic: it computes an 8-bit output level (0-255) over time. The caller
 * pushes getLevel() to the real PWM output whenever levelChanged() reports a change.
 *
 * Deliberate changes vs. HAL_LED:
 *  - Blink patterns start in the ON phase (HAL_LED started OFF and only lit the LED
 *    after the first interval, i.e. a 500 ms delay before a hazard warning was visible).
 *  - The "on" level of all patterns is configurable (setOnBrightness()); HAL_LED always
 *    used LED_BRIGHTNESS_FULL and ignored ConfigManager::ledBrightnessFull.
 *  - Blink intervals are configurable (ConfigManager::ledBlink*Ms were never applied).
 */
#ifndef STEPAWARE_CORE_STEP_LED_PATTERN_H
#define STEPAWARE_CORE_STEP_LED_PATTERN_H

#include <stdint.h>
#include "step_clock.h"

namespace step_logic {

// Brightness levels (config.h)
const uint8_t LED_BRIGHTNESS_OFF = 0;
const uint8_t LED_BRIGHTNESS_DIM = 20;
const uint8_t LED_BRIGHTNESS_MEDIUM = 128;
const uint8_t LED_BRIGHTNESS_FULL = 255;

// Blink half-periods (config.h)
const uint32_t LED_BLINK_FAST_MS = 250;
const uint32_t LED_BLINK_SLOW_MS = 1000;
const uint32_t LED_BLINK_WARNING_MS = 500;

class StepLedPattern {
public:
    enum Pattern {
        PATTERN_OFF,            ///< LED off
        PATTERN_ON,             ///< LED on (steady)
        PATTERN_BLINK_FAST,     ///< Fast blink (250 ms on/off)
        PATTERN_BLINK_SLOW,     ///< Slow blink (1 s on/off)
        PATTERN_BLINK_WARNING,  ///< Warning blink (500 ms on/off)
        PATTERN_PULSE,          ///< Triangle-wave "breathing"
        PATTERN_CUSTOM          ///< Custom on/off intervals
    };

    explicit StepLedPattern(StepClockFn clock);

    /// Advance the pattern. Call every loop.
    void update();

    void on(uint8_t brightness);
    void on() { on(m_onBrightness); }
    void off();

    /// Start a pattern; duration 0 = run until stopPattern().
    void startPattern(Pattern pattern, uint32_t durationMs);
    void setPattern(Pattern pattern) { startPattern(pattern, 0); }
    void stopPattern();

    Pattern getPattern() const { return m_currentPattern; }
    bool isPatternActive() const { return m_currentPattern != PATTERN_OFF; }
    bool isOn() const { return m_isOn && m_level > 0; }

    /// Current output level, 0-255.
    uint8_t getLevel() const { return m_isOn ? m_level : 0; }

    /// True once after the output level changed (clears the flag).
    bool levelChanged();

    /// Level used for the ON phase of every pattern (default LED_BRIGHTNESS_FULL).
    void setOnBrightness(uint8_t brightness);
    uint8_t getOnBrightness() const { return m_onBrightness; }

    void setBlinkIntervals(uint32_t fastMs, uint32_t slowMs, uint32_t warningMs);
    void setCustomPattern(uint32_t onMs, uint32_t offMs);

    static const char* getPatternName(Pattern pattern);

private:
    StepClockFn m_clock;
    Pattern m_currentPattern;
    uint8_t m_level;
    bool m_isOn;
    uint8_t m_lastReportedLevel;
    bool m_levelDirty;
    uint32_t m_patternStartTime;
    uint32_t m_patternDuration;
    uint32_t m_lastToggleTime;
    bool m_patternState;
    uint8_t m_onBrightness;
    uint32_t m_blinkFastMs;
    uint32_t m_blinkSlowMs;
    uint32_t m_blinkWarningMs;
    uint32_t m_customOnMs;
    uint32_t m_customOffMs;

    uint32_t getPatternInterval() const;
    void processPattern();
    void applyLevel(bool isOn, uint8_t level);
};

}  // namespace step_logic

#endif  // STEPAWARE_CORE_STEP_LED_PATTERN_H
