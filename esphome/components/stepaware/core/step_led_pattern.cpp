#include "step_led_pattern.h"

namespace step_logic {

StepLedPattern::StepLedPattern(StepClockFn clock)
    : m_clock(clock)
    , m_currentPattern(PATTERN_OFF)
    , m_level(0)
    , m_isOn(false)
    , m_lastReportedLevel(0)
    , m_levelDirty(true)
    , m_patternStartTime(0)
    , m_patternDuration(0)
    , m_lastToggleTime(0)
    , m_patternState(false)
    , m_onBrightness(LED_BRIGHTNESS_FULL)
    , m_blinkFastMs(LED_BLINK_FAST_MS)
    , m_blinkSlowMs(LED_BLINK_SLOW_MS)
    , m_blinkWarningMs(LED_BLINK_WARNING_MS)
    , m_customOnMs(500)
    , m_customOffMs(500)
{
}

void StepLedPattern::update() {
    // Pattern duration expiry
    if (m_patternDuration > 0) {
        if (m_clock() - m_patternStartTime >= m_patternDuration) {
            stopPattern();
            return;
        }
    }
    processPattern();
}

void StepLedPattern::on(uint8_t brightness) {
    m_currentPattern = PATTERN_ON;
    applyLevel(true, brightness);
}

void StepLedPattern::off() {
    m_currentPattern = PATTERN_OFF;
    applyLevel(false, 0);
}

void StepLedPattern::startPattern(Pattern pattern, uint32_t durationMs) {
    uint32_t now = m_clock();
    m_currentPattern = pattern;
    m_patternStartTime = now;
    m_patternDuration = durationMs;
    m_lastToggleTime = now;

    switch (pattern) {
        case PATTERN_OFF:
            m_patternState = false;
            applyLevel(false, 0);
            break;
        case PATTERN_ON:
            m_patternState = true;
            applyLevel(true, m_onBrightness);
            break;
        case PATTERN_BLINK_FAST:
        case PATTERN_BLINK_SLOW:
        case PATTERN_BLINK_WARNING:
        case PATTERN_CUSTOM:
            // Deliberate change: start in the ON phase so a warning is visible at once.
            m_patternState = true;
            applyLevel(true, m_onBrightness);
            break;
        case PATTERN_PULSE:
            m_patternState = false;
            applyLevel(false, 0);
            break;
    }
}

void StepLedPattern::stopPattern() {
    m_currentPattern = PATTERN_OFF;
    m_patternDuration = 0;
    m_patternState = false;
    applyLevel(false, 0);
}

bool StepLedPattern::levelChanged() {
    uint8_t level = getLevel();
    if (m_levelDirty || level != m_lastReportedLevel) {
        m_levelDirty = false;
        m_lastReportedLevel = level;
        return true;
    }
    return false;
}

void StepLedPattern::setOnBrightness(uint8_t brightness) {
    m_onBrightness = brightness;
    // Apply immediately if the LED is currently in a lit, non-pulse phase.
    if (m_isOn && m_currentPattern != PATTERN_PULSE) {
        m_level = brightness;
    }
}

void StepLedPattern::setBlinkIntervals(uint32_t fastMs, uint32_t slowMs, uint32_t warningMs) {
    m_blinkFastMs = fastMs;
    m_blinkSlowMs = slowMs;
    m_blinkWarningMs = warningMs;
}

void StepLedPattern::setCustomPattern(uint32_t onMs, uint32_t offMs) {
    m_customOnMs = onMs;
    m_customOffMs = offMs;
}

const char* StepLedPattern::getPatternName(Pattern pattern) {
    switch (pattern) {
        case PATTERN_OFF:           return "OFF";
        case PATTERN_ON:            return "ON";
        case PATTERN_BLINK_FAST:    return "BLINK_FAST";
        case PATTERN_BLINK_SLOW:    return "BLINK_SLOW";
        case PATTERN_BLINK_WARNING: return "BLINK_WARNING";
        case PATTERN_PULSE:         return "PULSE";
        case PATTERN_CUSTOM:        return "CUSTOM";
    }
    return "UNKNOWN";
}

uint32_t StepLedPattern::getPatternInterval() const {
    switch (m_currentPattern) {
        case PATTERN_BLINK_FAST:    return m_blinkFastMs;
        case PATTERN_BLINK_SLOW:    return m_blinkSlowMs;
        case PATTERN_BLINK_WARNING: return m_blinkWarningMs;
        case PATTERN_CUSTOM:        return m_patternState ? m_customOnMs : m_customOffMs;
        default:                    return 0;
    }
}

void StepLedPattern::processPattern() {
    uint32_t now = m_clock();

    switch (m_currentPattern) {
        case PATTERN_OFF:
        case PATTERN_ON:
            break;

        case PATTERN_BLINK_FAST:
        case PATTERN_BLINK_SLOW:
        case PATTERN_BLINK_WARNING:
        case PATTERN_CUSTOM: {
            uint32_t interval = getPatternInterval();
            if (now - m_lastToggleTime >= interval) {
                m_patternState = !m_patternState;
                m_lastToggleTime = now;
                applyLevel(m_patternState, m_patternState ? m_onBrightness : 0);
            }
            break;
        }

        case PATTERN_PULSE: {
            // Triangle wave: fade in then out over 2 x LED_BLINK_SLOW_MS
            uint32_t pulseInterval = m_blinkSlowMs * 2;
            if (pulseInterval < 2) {
                pulseInterval = 2;
            }
            uint32_t half = pulseInterval / 2;
            uint32_t elapsed = (now - m_patternStartTime) % pulseInterval;
            uint32_t scaled;
            if (elapsed < half) {
                scaled = (elapsed * 255) / half;
            } else {
                scaled = 255 - (((elapsed - half) * 255) / half);
            }
            // Scale by the configured on-brightness
            uint8_t level = static_cast<uint8_t>((scaled * m_onBrightness) / 255);
            // Update at most every 20 ms (as HAL_LED did)
            if (now - m_lastToggleTime >= 20) {
                m_lastToggleTime = now;
                applyLevel(level > 0, level);
            }
            break;
        }
    }
}

void StepLedPattern::applyLevel(bool isOn, uint8_t level) {
    m_isOn = isOn;
    m_level = level;
}

}  // namespace step_logic
