#include "hal_led.h"

HAL_LED::HAL_LED(uint8_t pin, uint8_t pwm_channel, bool mock_mode)
    : m_pin(pin)
    , m_pwmChannel(pwm_channel)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_currentPattern(PATTERN_OFF)
    , m_brightness(0)
    , m_isOn(false)
    , m_patternStartTime(0)
    , m_patternDuration(0)
    , m_lastToggleTime(0)
    , m_patternState(false)
    , m_customOnMs(500)
    , m_customOffMs(500)
{
}

HAL_LED::~HAL_LED() {
    off();
}

bool HAL_LED::begin() {
    if (m_initialized) {
        return true;
    }

    DEBUG_PRINTF("[HAL_LED] Initializing LED on pin %d...\n", m_pin);

    if (!m_mockMode) {
        // Configure PWM channel
        ledcSetup(m_pwmChannel, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
        ledcAttachPin(m_pin, m_pwmChannel);
        ledcWrite(m_pwmChannel, 0);  // Start with LED off

        DEBUG_PRINTF("[HAL_LED] PWM configured: channel=%d, freq=%dHz, resolution=%d-bit\n",
                     m_pwmChannel, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
    } else {
        DEBUG_PRINTLN("[HAL_LED] MOCK MODE: Simulating LED");
    }

    m_initialized = true;
    DEBUG_PRINTLN("[HAL_LED] ✓ Initialization complete");

    return true;
}

void HAL_LED::update() {
    if (!m_initialized) {
        return;
    }

    // Check if pattern duration has expired
    if (m_patternDuration > 0) {
        if (millis() - m_patternStartTime >= m_patternDuration) {
            DEBUG_PRINTLN("[HAL_LED] Pattern stopped");
            stopPattern();
            return;
        }
    }

    // Process pattern logic
    processPattern();
}

void HAL_LED::on(uint8_t brightness) {
    m_brightness = brightness;
    m_isOn = true;
    m_currentPattern = PATTERN_ON;
    applyBrightness();

    if (!m_mockMode) {
        DEBUG_PRINTF("[HAL_LED] LED turned ON (brightness=%d)\n", brightness);
    }
}

void HAL_LED::off() {
    m_brightness = 0;
    m_isOn = false;
    m_currentPattern = PATTERN_OFF;
    applyBrightness();

    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_LED] LED turned OFF");
    }
}

void HAL_LED::setBrightness(uint8_t brightness) {
    m_brightness = brightness;
    if (m_isOn) {
        applyBrightness();
    }
}

uint8_t HAL_LED::getBrightness() {
    return m_brightness;
}

void HAL_LED::setPattern(Pattern pattern) {
    startPattern(pattern, 0);  // 0 = infinite duration
}

void HAL_LED::startPattern(Pattern pattern, uint32_t duration_ms) {
    m_currentPattern = pattern;
    m_patternStartTime = millis();
    m_patternDuration = duration_ms;
    m_lastToggleTime = millis();
    m_patternState = false;

    DEBUG_PRINTF("[HAL_LED] Pattern started: ");
    switch (pattern) {
        case PATTERN_OFF:           DEBUG_PRINT("PATTERN_OFF"); break;
        case PATTERN_ON:            DEBUG_PRINT("PATTERN_ON"); break;
        case PATTERN_BLINK_FAST:    DEBUG_PRINT("PATTERN_BLINK_FAST"); break;
        case PATTERN_BLINK_SLOW:    DEBUG_PRINT("PATTERN_BLINK_SLOW"); break;
        case PATTERN_BLINK_WARNING: DEBUG_PRINT("PATTERN_BLINK_WARNING"); break;
        case PATTERN_PULSE:         DEBUG_PRINT("PATTERN_PULSE"); break;
        case PATTERN_CUSTOM:        DEBUG_PRINT("PATTERN_CUSTOM"); break;
        default:                    DEBUG_PRINT("UNKNOWN"); break;
    }
    DEBUG_PRINTF(", duration: %u ms\n", duration_ms);

    // Initial state
    if (pattern == PATTERN_OFF) {
        off();
    } else if (pattern == PATTERN_ON) {
        on(LED_BRIGHTNESS_FULL);
    }
}

void HAL_LED::stopPattern() {
    m_currentPattern = PATTERN_OFF;
    m_patternDuration = 0;
    off();
}

HAL_LED::Pattern HAL_LED::getPattern() {
    return m_currentPattern;
}

bool HAL_LED::isPatternActive() {
    return m_currentPattern != PATTERN_OFF;
}

bool HAL_LED::isOn() {
    return m_isOn && m_brightness > 0;
}

void HAL_LED::setCustomPattern(uint32_t on_ms, uint32_t off_ms) {
    m_customOnMs = on_ms;
    m_customOffMs = off_ms;
    DEBUG_PRINTF("[HAL_LED] Custom pattern set: %ums ON, %ums OFF\n", on_ms, off_ms);
}

uint32_t HAL_LED::getPatternInterval() {
    switch (m_currentPattern) {
        case PATTERN_BLINK_FAST:
            return LED_BLINK_FAST_MS;

        case PATTERN_BLINK_SLOW:
            return LED_BLINK_SLOW_MS;

        case PATTERN_BLINK_WARNING:
            return LED_BLINK_WARNING_MS;

        case PATTERN_CUSTOM:
            return m_patternState ? m_customOnMs : m_customOffMs;

        default:
            return 0;
    }
}

void HAL_LED::applyBrightness() {
    if (!m_mockMode) {
        ledcWrite(m_pwmChannel, m_brightness);
    }
    // Mock mode: brightness is just stored in m_brightness member
}

void HAL_LED::processPattern() {
    uint32_t now = millis();

    switch (m_currentPattern) {
        case PATTERN_OFF:
            // Nothing to do
            break;

        case PATTERN_ON:
            // Already on, nothing to toggle
            break;

        case PATTERN_BLINK_FAST:
        case PATTERN_BLINK_SLOW:
        case PATTERN_BLINK_WARNING:
        case PATTERN_CUSTOM:
            {
                uint32_t interval = getPatternInterval();
                if (now - m_lastToggleTime >= interval) {
                    m_patternState = !m_patternState;
                    m_lastToggleTime = now;

                    if (m_patternState) {
                        m_brightness = LED_BRIGHTNESS_FULL;
                        m_isOn = true;
                    } else {
                        m_brightness = 0;
                        m_isOn = false;
                    }

                    applyBrightness();
                }
            }
            break;

        case PATTERN_PULSE:
            // Smooth PWM fade in/out
            {
                uint32_t pulseInterval = LED_BLINK_SLOW_MS * 2;  // 2 seconds for full pulse cycle
                uint32_t elapsed = (now - m_patternStartTime) % pulseInterval;

                // Calculate brightness using sine wave approximation
                // First half: fade in (0 -> 255)
                // Second half: fade out (255 -> 0)
                if (elapsed < pulseInterval / 2) {
                    // Fade in
                    m_brightness = (elapsed * 255) / (pulseInterval / 2);
                } else {
                    // Fade out
                    uint32_t fadeOutElapsed = elapsed - (pulseInterval / 2);
                    m_brightness = 255 - ((fadeOutElapsed * 255) / (pulseInterval / 2));
                }

                m_isOn = (m_brightness > 0);

                // Update LED more frequently for smooth fading (every 20ms)
                if (now - m_lastToggleTime >= 20) {
                    m_lastToggleTime = now;
                    applyBrightness();
                }
            }
            break;
    }
}
