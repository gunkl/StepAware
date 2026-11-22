/**
 * @file power_logic_impl.h
 * @brief Inline definitions for power_logic.h (included from its end; do not include
 *        directly).
 *
 * Header-only on purpose: ESPHome includes every component header into the generated
 * main.cpp, and an existing ESPHome build directory does not pick up newly added .cpp
 * files without "esphome clean" (the ESP-IDF CMake source glob is cached).
 */
#ifndef STEPAWARE_POWER_LOGIC_IMPL_H
#define STEPAWARE_POWER_LOGIC_IMPL_H

#include "power_logic.h"

namespace step_power {

// =============================================================================================
// BatteryMonitor
// =============================================================================================

inline BatteryMonitor::BatteryMonitor()
    : m_level(BATTERY_UNKNOWN),
      m_validCount(0),
      m_criticalCount(0),
      m_freshCritical(false),
      m_usb(false) {}

inline void BatteryMonitor::restore(const BatteryPersist &state) {
    m_level = state.level <= BATTERY_CRITICAL ? static_cast<BatteryLevel>(state.level)
                                              : BATTERY_UNKNOWN;
    m_validCount = state.validCount;
    m_criticalCount = state.criticalCount;
    // A restored level is only meaningful once enough samples backed it.
    if (m_validCount < m_config.minSamples) {
        m_level = BATTERY_UNKNOWN;
    }
    m_freshCritical = false;
}

inline BatteryPersist BatteryMonitor::persist() const {
    BatteryPersist p;
    p.level = static_cast<uint8_t>(m_level);
    p.validCount = m_validCount;
    p.criticalCount = m_criticalCount;
    return p;
}

inline bool BatteryMonitor::isLow() const {
    return !m_usb && (m_level == BATTERY_LOW || m_level == BATTERY_CRITICAL);
}

inline bool BatteryMonitor::isCritical() const { return !m_usb && m_level == BATTERY_CRITICAL; }

inline bool BatteryMonitor::isShutdownConfirmed() const {
    return !m_usb && m_freshCritical && m_config.criticalSamples > 0 &&
           m_criticalCount >= m_config.criticalSamples;
}

inline const char *BatteryMonitor::getStateName() const {
    if (m_usb) {
        return "USB";
    }
    switch (m_level) {
        case BATTERY_NORMAL:
            return "Normal";
        case BATTERY_LOW:
            return "Low";
        case BATTERY_CRITICAL:
            return "Critical";
        default:
            return "Unknown";
    }
}

inline uint8_t BatteryMonitor::stateKey() const {
    return static_cast<uint8_t>((m_usb ? 0x10 : 0) | static_cast<uint8_t>(m_level));
}

inline BatteryLevel BatteryMonitor::classify(float volts) const {
    // Hysteresis: a level is left only once the voltage is hysteresisV above its threshold.
    bool wasCritical = m_level == BATTERY_CRITICAL;
    bool wasLow = m_level == BATTERY_LOW || wasCritical;
    float critLimit = m_config.criticalV + (wasCritical ? m_config.hysteresisV : 0.0f);
    float lowLimit = m_config.lowV + (wasLow ? m_config.hysteresisV : 0.0f);
    if (volts < critLimit) {
        return BATTERY_CRITICAL;
    }
    if (volts < lowLimit) {
        return BATTERY_LOW;
    }
    return BATTERY_NORMAL;
}

inline bool BatteryMonitor::onReading(float volts, bool usb) {
    uint8_t before = stateKey();
    m_usb = usb;

    // NAN fails both comparisons, so "!(in range)" also rejects it.
    bool plausible = volts >= m_config.minValidV && volts <= m_config.maxValidV;
    if (!plausible) {
        m_criticalCount = 0;
        m_freshCritical = false;
        return stateKey() != before;
    }

    if (m_validCount < 255) {
        m_validCount++;
    }

    // Track the classification on USB too, so hysteresis is continuous when it is unplugged.
    BatteryLevel level = classify(volts);
    m_level = m_validCount >= m_config.minSamples ? level : BATTERY_UNKNOWN;

    if (!usb && volts < m_config.criticalV) {
        if (m_criticalCount < 255) {
            m_criticalCount++;
        }
        m_freshCritical = true;
    } else {
        m_criticalCount = 0;
        m_freshCritical = false;
    }
    return stateKey() != before;
}

inline bool BatteryMonitor::setUsb(bool usb) {
    uint8_t before = stateKey();
    m_usb = usb;
    if (usb) {
        m_criticalCount = 0;
        m_freshCritical = false;
    }
    return stateKey() != before;
}

// =============================================================================================
// RecalScheduler
// =============================================================================================

static const int64_t SECONDS_PER_HOUR = 3600;
static const int64_t SECONDS_PER_DAY = 86400;

inline bool recalHourInWindow(uint8_t start, uint8_t end, uint8_t hour) {
    if (start == end || start > 23 || end > 24 || hour > 23) {
        return false;
    }
    if (start < end) {
        return hour >= start && hour < end;
    }
    return hour >= start || hour < end;  // wraps past midnight
}

inline RecalScheduler::RecalScheduler() : m_lastMotionEpoch(0), m_lastRecalEpoch(0) {}

inline void RecalScheduler::noteMotion(int64_t epoch) {
    if (epoch > m_lastMotionEpoch) {
        m_lastMotionEpoch = epoch;
    }
}

inline RecalDecision RecalScheduler::evaluate(const WallTime &now, bool recalibrating) const {
    // 1. Reliable time (old: NTP synced).
    if (!now.valid || now.epoch <= 0) {
        return RECAL_NO_TIME;
    }
    // 2. Not already recalibrating.
    if (recalibrating) {
        return RECAL_BUSY;
    }
    // 3. Local hour inside the window.
    if (!recalHourInWindow(m_config.windowStartHour, m_config.windowEndHour, now.hour)) {
        return RECAL_OUTSIDE_WINDOW;
    }
    // 4. Quiescence. Unknown (0) satisfies it, as lastMotionMs == 0 did. Motion stamped in
    //    the future (clock moved back) counts as recent.
    if (m_lastMotionEpoch != 0 &&
        now.epoch - m_lastMotionEpoch < static_cast<int64_t>(m_config.quiescenceS)) {
        return RECAL_RECENT_MOTION;
    }
    // A last-recal stamp more than a day in the future is a clock fault: ignore it, otherwise
    // recalibration would be suppressed until the clock caught up.
    int64_t lastRecal = m_lastRecalEpoch;
    if (lastRecal > now.epoch + SECONDS_PER_DAY) {
        lastRecal = 0;
    }
    if (lastRecal != 0) {
        // 5. Cooldown since the last recalibration.
        if (now.epoch - lastRecal < static_cast<int64_t>(m_config.cooldownS)) {
            return RECAL_COOLDOWN;
        }
        // 6. Once per night: nothing since the start of the current window instance.
        int64_t hoursIntoWindow =
            (static_cast<int64_t>(now.hour) - m_config.windowStartHour + 24) % 24;
        int64_t windowStart = now.epoch - hoursIntoWindow * SECONDS_PER_HOUR -
                              static_cast<int64_t>(now.minute) * 60 - now.second;
        if (lastRecal >= windowStart) {
            return RECAL_DONE_TONIGHT;
        }
    }
    return RECAL_GO;
}

inline const char *RecalScheduler::decisionName(RecalDecision decision) {
    switch (decision) {
        case RECAL_GO:
            return "go";
        case RECAL_NO_TIME:
            return "no valid time";
        case RECAL_BUSY:
            return "already recalibrating";
        case RECAL_OUTSIDE_WINDOW:
            return "outside window";
        case RECAL_RECENT_MOTION:
            return "recent motion";
        case RECAL_COOLDOWN:
            return "cooldown";
        case RECAL_DONE_TONIGHT:
            return "done tonight";
        default:
            return "?";
    }
}

// =============================================================================================
// RecalSequencer
// =============================================================================================


inline RecalSequencer::RecalSequencer(MillisFn clock)
    : m_clock(clock),
      m_rail(nullptr),
      m_railCtx(nullptr),
      m_powerOffMs(DEFAULT_POWER_OFF_MS),
      m_warmupMs(DEFAULT_WARMUP_MS),
      m_phaseStartMs(0),
      m_phase(RECAL_IDLE) {}

inline bool RecalSequencer::start() {
    if (m_rail == nullptr || m_clock == nullptr) {
        return false;
    }
    if (m_phase != RECAL_IDLE) {
        return true;  // already in progress, not an error (HAL_PIR::recalibrate)
    }
    m_rail(m_railCtx, false);
    m_phaseStartMs = m_clock();
    m_phase = RECAL_POWER_OFF;
    return true;
}

inline bool RecalSequencer::update() {
    if (m_phase == RECAL_IDLE) {
        return false;
    }
    uint32_t now = m_clock();
    uint32_t elapsed = now - m_phaseStartMs;
    if (m_phase == RECAL_POWER_OFF) {
        if (elapsed < m_powerOffMs) {
            return false;
        }
        m_rail(m_railCtx, true);
        m_phaseStartMs = now;
        m_phase = RECAL_WARMUP;
        return true;
    }
    if (elapsed < m_warmupMs) {
        return false;
    }
    m_phase = RECAL_IDLE;
    return true;
}

// =============================================================================================
// Dormant mode
// =============================================================================================

static const uint32_t DORMANT_RECORD_MAGIC = 0xD0000000UL;
static const uint32_t DORMANT_RECORD_MAGIC_MASK = 0xFF000000UL;
static const uint32_t DORMANT_RECORD_ACTIVE = 0x00000100UL;

inline const char *dormantReasonName(uint8_t reason) {
    switch (reason) {
        case DORMANT_REASON_NONE:
            return "None";
        case DORMANT_REASON_GESTURE:
            return "Button hold";
        case DORMANT_REASON_HOME_ASSISTANT:
            return "Home Assistant";
        case DORMANT_REASON_CRITICAL_BATTERY:
            return "Critical battery";
        default:
            return "Other";
    }
}

inline uint32_t DormantRecord::encode() const {
    return DORMANT_RECORD_MAGIC | (active ? DORMANT_RECORD_ACTIVE : 0) |
           static_cast<uint32_t>(reason);
}

inline DormantRecord DormantRecord::decode(uint32_t raw) {
    if ((raw & DORMANT_RECORD_MAGIC_MASK) != DORMANT_RECORD_MAGIC ||
        (raw & 0x00FFFE00UL) != 0) {
        return DormantRecord();
    }
    return DormantRecord((raw & DORMANT_RECORD_ACTIVE) != 0, static_cast<uint8_t>(raw & 0xFF));
}

inline DormantBootAction decideDormantBoot(const DormantBootInput &in) {
    if (!in.flagActive) {
        return DORMANT_ACTION_NORMAL;
    }
    switch (in.kind) {
        case DORMANT_BOOT_BUTTON_WAKE:
            return in.wakeHoldMs > 0 ? DORMANT_ACTION_VERIFY_HOLD : DORMANT_ACTION_WAKE;
        case DORMANT_BOOT_RESET:
            return in.buttonHeld ? DORMANT_ACTION_WAKE : DORMANT_ACTION_REENTER;
        case DORMANT_BOOT_OTHER_WAKE:
        default:
            return DORMANT_ACTION_REENTER;
    }
}

inline DormantBootKind dormantBootKindFor(WakeCause cause) {
    switch (cause) {
        case WAKE_BUTTON:
            return DORMANT_BOOT_BUTTON_WAKE;
        case WAKE_PIR:
        case WAKE_TIMER:
        case WAKE_SPURIOUS_GPIO:
        case WAKE_OTHER_SLEEP:
            return DORMANT_BOOT_OTHER_WAKE;
        case WAKE_POWER_ON:
        case WAKE_SOFTWARE_RESET:
        case WAKE_CRASH:
        default:
            return DORMANT_BOOT_RESET;
    }
}

inline const char *dormantBootActionName(DormantBootAction action) {
    switch (action) {
        case DORMANT_ACTION_NORMAL:
            return "normal";
        case DORMANT_ACTION_WAKE:
            return "wake";
        case DORMANT_ACTION_VERIFY_HOLD:
            return "verify hold";
        case DORMANT_ACTION_REENTER:
            return "re-enter dormant";
        default:
            return "?";
    }
}

inline void WakeHoldVerifier::start(uint32_t nowMs) {
    m_startMs = nowMs;
    m_released = false;
    m_releasedSinceMs = 0;
    m_result = m_requiredMs == 0 ? CONFIRMED : PENDING;
}

inline WakeHoldVerifier::Result WakeHoldVerifier::update(uint32_t nowMs, bool pressed) {
    if (m_result != PENDING) {
        return m_result;
    }
    if (pressed) {
        m_released = false;
    } else if (!m_released) {
        m_released = true;
        m_releasedSinceMs = nowMs;
    }
    if (m_released && nowMs - m_releasedSinceMs >= m_debounceMs) {
        m_result = REJECTED;
    } else if (nowMs - m_startMs >= m_requiredMs) {
        m_result = pressed ? CONFIRMED : REJECTED;
    }
    return m_result;
}

}  // namespace step_power

#endif  // STEPAWARE_POWER_LOGIC_IMPL_H
