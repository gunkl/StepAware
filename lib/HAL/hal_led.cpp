#include "hal_led.h"
#include <config.h>

HAL_LED::HAL_LED(uint8_t pin, uint8_t pwmChannel, bool mock)
    : m_pin(pin)
    , m_pwmChannel(pwmChannel)
    , m_mock(mock)
    , m_initialized(false)
    , m_brightness(0)
    , m_pattern(PATTERN_OFF)
    , m_ledState(false)
    , m_lastToggleTime(0)
    , m_onTime(0)
    , m_offTime(0)
    , m_patternStartTime(0)
    , m_patternDuration(0)
    , m_pulseDirection(1)
    , m_pulseValue(0)
#if MOCK_HARDWARE
    , m_mockState(false)
    , m_mockBrightness(0)
    , m_mockBlinkCount(0)
#endif
{
}

HAL_LED::~HAL_LED() {
    if (m_initialized && !m_mock) {
        ledcDetachPin(m_pin);
    }
}

bool HAL_LED::begin() {
    if (m_initialized) {
        return true;
    }

    if (!m_mock) {
        // Configure LED PWM
        ledcSetup(m_pwmChannel, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
        ledcAttachPin(m_pin, m_pwmChannel);
        ledcWrite(m_pwmChannel, 0);  // Start with LED off

        DEBUG_PRINTF("[HAL_LED] Initialized on GPIO%d, PWM channel %d (real hardware)\n",
                     m_pin, m_pwmChannel);
    } else {
#if MOCK_HARDWARE
        DEBUG_PRINTF("[HAL_LED] Initialized on GPIO%d (MOCK mode)\n", m_pin);
#endif
    }

    m_initialized = true;
    m_brightness = 0;
    m_ledState = false;

    return true;
}

void HAL_LED::setBrightness(uint8_t brightness) {
    m_brightness = brightness;
    applyBrightness();

    DEBUG_PRINTF("[HAL_LED] Brightness set to %d\n", brightness);
}

uint8_t HAL_LED::getBrightness() {
    return m_brightness;
}

void HAL_LED::on() {
    setBrightness(LED_BRIGHTNESS_FULL);
    m_ledState = true;
}

void HAL_LED::off() {
    setBrightness(LED_BRIGHTNESS_OFF);
    m_ledState = false;
}

void HAL_LED::toggle() {
    if (m_ledState) {
        off();
    } else {
        on();
    }

#if MOCK_HARDWARE
    if (m_mock && m_ledState) {
        m_mockBlinkCount++;
    }
#endif
}

bool HAL_LED::isOn() {
    return (m_brightness > 0);
}

void HAL_LED::setPattern(BlinkPattern pattern) {
    m_pattern = pattern;
    m_lastToggleTime = millis();

    getPatternTiming(pattern, m_onTime, m_offTime);

    DEBUG_PRINTF("[HAL_LED] Pattern set to %d (on=%u ms, off=%u ms)\n",
                 pattern, m_onTime, m_offTime);

    // Initialize pattern state
    if (pattern == PATTERN_OFF) {
        off();
    } else if (pattern == PATTERN_ON) {
        on();
    } else if (pattern == PATTERN_PULSE) {
        m_pulseDirection = 1;
        m_pulseValue = 0;
    } else {
        // Start with LED on for blinking patterns
        on();
    }
}

void HAL_LED::setCustomPattern(uint32_t onTime, uint32_t offTime) {
    m_pattern = PATTERN_CUSTOM;
    m_onTime = onTime;
    m_offTime = offTime;
    m_lastToggleTime = millis();

    DEBUG_PRINTF("[HAL_LED] Custom pattern set (on=%u ms, off=%u ms)\n", onTime, offTime);
}

HAL_LED::BlinkPattern HAL_LED::getPattern() {
    return m_pattern;
}

void HAL_LED::update() {
    if (!m_initialized) {
        return;
    }

    // Check if timed pattern has expired
    if (m_patternDuration > 0) {
        if (millis() - m_patternStartTime >= m_patternDuration) {
            stopPattern();
            return;
        }
    }

    uint32_t currentTime = millis();

    switch (m_pattern) {
        case PATTERN_OFF:
        case PATTERN_ON:
            // Static patterns, nothing to update
            break;

        case PATTERN_BLINK_FAST:
        case PATTERN_BLINK_SLOW:
        case PATTERN_BLINK_WARNING:
        case PATTERN_CUSTOM: {
            // Blinking patterns
            uint32_t interval = m_ledState ? m_onTime : m_offTime;

            if (currentTime - m_lastToggleTime >= interval) {
                toggle();
                m_lastToggleTime = currentTime;
            }
            break;
        }

        case PATTERN_PULSE: {
            // Smooth pulsing effect
            if (currentTime - m_lastToggleTime >= 10) {  // Update every 10ms
                if (m_pulseDirection == 1) {
                    // Fading in
                    m_pulseValue += 5;
                    if (m_pulseValue >= 255) {
                        m_pulseValue = 255;
                        m_pulseDirection = 0;
                    }
                } else {
                    // Fading out
                    if (m_pulseValue >= 5) {
                        m_pulseValue -= 5;
                    } else {
                        m_pulseValue = 0;
                        m_pulseDirection = 1;
                    }
                }

                setBrightness(m_pulseValue);
                m_lastToggleTime = currentTime;
            }
            break;
        }
    }
}

void HAL_LED::startPattern(BlinkPattern pattern, uint32_t duration_ms) {
    setPattern(pattern);
    m_patternStartTime = millis();
    m_patternDuration = duration_ms;

    if (duration_ms > 0) {
        DEBUG_PRINTF("[HAL_LED] Pattern started for %u ms\n", duration_ms);
    }
}

void HAL_LED::stopPattern() {
    setPattern(PATTERN_OFF);
    m_patternDuration = 0;
    DEBUG_PRINTLN("[HAL_LED] Pattern stopped");
}

bool HAL_LED::isPatternActive() {
    if (m_patternDuration == 0) {
        return (m_pattern != PATTERN_OFF);  // Infinite pattern is active
    }

    return (millis() - m_patternStartTime < m_patternDuration);
}

void HAL_LED::applyBrightness() {
    if (!m_initialized) {
        return;
    }

#if MOCK_HARDWARE
    if (m_mock) {
        m_mockBrightness = m_brightness;
        m_mockState = (m_brightness > 0);
        return;
    }
#endif

    // Apply to real hardware PWM
    ledcWrite(m_pwmChannel, m_brightness);
}

void HAL_LED::getPatternTiming(BlinkPattern pattern, uint32_t& onTime, uint32_t& offTime) {
    switch (pattern) {
        case PATTERN_BLINK_FAST:
            onTime = LED_BLINK_FAST_MS;
            offTime = LED_BLINK_FAST_MS;
            break;

        case PATTERN_BLINK_SLOW:
            onTime = LED_BLINK_SLOW_MS;
            offTime = LED_BLINK_SLOW_MS;
            break;

        case PATTERN_BLINK_WARNING:
            onTime = LED_BLINK_WARNING_MS;
            offTime = LED_BLINK_WARNING_MS;
            break;

        case PATTERN_CUSTOM:
            // Use existing custom times
            break;

        default:
            onTime = 0;
            offTime = 0;
            break;
    }
}

// ============================================================================
// Mock Hardware Methods
// ============================================================================

#if MOCK_HARDWARE

bool HAL_LED::mockIsOn() {
    if (!m_mock) {
        return false;
    }
    return m_mockState;
}

uint8_t HAL_LED::mockGetBrightness() {
    if (!m_mock) {
        return 0;
    }
    return m_mockBrightness;
}

uint32_t HAL_LED::mockGetBlinkCount() {
    if (!m_mock) {
        return 0;
    }
    return m_mockBlinkCount;
}

void HAL_LED::mockResetBlinkCount() {
    if (!m_mock) {
        return;
    }
    m_mockBlinkCount = 0;
    DEBUG_PRINTLN("[HAL_LED] MOCK: Blink counter reset");
}

#endif // MOCK_HARDWARE
