/**
 * @file power_logic.h
 * @brief Dependency-free battery-state and PIR-recalibration logic for stepaware_power.
 *
 * Plain C++11 (no ESPHome / ESP-IDF headers) so it is unit-tested natively
 * (test/test_esphome_power, [env:native_power]). Header-only (inline definitions in
 * power_logic_impl.h).
 * Clocks are injected: the battery monitor is driven by readings, the recalibration scheduler by a WallTime the caller supplies, and the
 * recalibration sequencer by a millisecond clock function pointer.
 *
 * Ported from the Arduino firmware:
 *   - BatteryMonitor   <- PowerManager::updateBatteryStatus() / handlePowerState()
 *   - RecalScheduler   <- RecalScheduler::update()            (src/recal_scheduler.cpp)
 *   - RecalSequencer   <- HAL_PIR::recalibrate() / update()   (src/hal_pir.cpp)
 * New for ESPHome:
 *   - Dormant mode     <- persisted "switched off" flag, boot re-entry decision, wake hold
 */
#ifndef STEPAWARE_POWER_LOGIC_H
#define STEPAWARE_POWER_LOGIC_H

#include <stdint.h>

namespace step_power {

// =============================================================================================
// Battery state
// =============================================================================================

/// Voltage classification. Values are persisted in RTC memory; append only.
enum BatteryLevel : uint8_t {
    BATTERY_UNKNOWN = 0,  ///< Not enough plausible readings yet
    BATTERY_NORMAL = 1,
    BATTERY_LOW = 2,
    BATTERY_CRITICAL = 3,
};

struct BatteryConfig {
    float lowV;               ///< Below this: low (BATTERY_VOLTAGE_LOW, config.h)
    float criticalV;          ///< Below this: critical (BATTERY_VOLTAGE_CRITICAL, config.h)
    float hysteresisV;        ///< A state clears only above threshold + hysteresis
    float minValidV;          ///< Readings below are ADC failures (LiPo cannot power the MCU)
    float maxValidV;          ///< Readings above are wiring / divider errors
    uint8_t minSamples;       ///< Plausible readings before any state is reported
    uint8_t criticalSamples;  ///< Consecutive critical readings before shutdown is allowed

    BatteryConfig()
        : lowV(3.3f),
          criticalV(3.0f),
          hysteresisV(0.1f),
          minValidV(2.5f),
          maxValidV(5.0f),
          minSamples(3),
          criticalSamples(3) {}
};

/// State that survives deep sleep (kept in RTC memory by the component).
struct BatteryPersist {
    uint8_t level;          ///< BatteryLevel
    uint8_t validCount;     ///< Saturating count of plausible readings
    uint8_t criticalCount;  ///< Consecutive plausible on-battery readings below criticalV
};

class BatteryMonitor {
public:
    BatteryMonitor();

    void setConfig(const BatteryConfig &config) { m_config = config; }
    const BatteryConfig &getConfig() const { return m_config; }

    /// Restore state saved before deep sleep. Never counts as a fresh critical reading.
    void restore(const BatteryPersist &state);
    BatteryPersist persist() const;

    /**
     * @brief Feed one published battery voltage.
     *
     * NAN / implausible readings are ignored for classification and reset the consecutive
     * critical count: a failed ADC can only make a shutdown less likely.
     *
     * @return true if the reported state (level, low, critical, USB) changed
     */
    bool onReading(float volts, bool usb);

    /// Update the USB state between readings. USB resets the critical count.
    /// @return true if the reported state changed
    bool setUsb(bool usb);

    BatteryLevel getLevel() const { return m_level; }
    bool isUsb() const { return m_usb; }
    bool isKnown() const { return m_level != BATTERY_UNKNOWN; }
    /// Low includes critical. Always false on USB power (charging).
    bool isLow() const;
    /// Always false on USB power.
    bool isCritical() const;
    uint8_t getCriticalCount() const { return m_criticalCount; }
    bool hasFreshCritical() const { return m_freshCritical; }
    /**
     * @brief True once a critical shutdown is justified by the readings alone.
     *
     * Requires: not on USB, criticalSamples consecutive plausible readings below criticalV
     * (may span deep-sleep sessions via restore()), and at least one of them taken this boot.
     */
    bool isShutdownConfirmed() const;
    /// "USB", "Unknown", "Normal", "Low" or "Critical".
    const char *getStateName() const;

private:
    BatteryLevel classify(float volts) const;
    uint8_t stateKey() const;

    BatteryConfig m_config;
    BatteryLevel m_level;
    uint8_t m_validCount;
    uint8_t m_criticalCount;
    bool m_freshCritical;
    bool m_usb;
};

// =============================================================================================
// PIR recalibration scheduling
// =============================================================================================

/// Wall-clock reading supplied by the caller (ESPHome time component on the device).
struct WallTime {
    bool valid;      ///< Clock has been set (SNTP / Home Assistant / preserved over deep sleep)
    int64_t epoch;   ///< Seconds since 1970 (UTC)
    uint8_t hour;    ///< Local time
    uint8_t minute;
    uint8_t second;
};

struct RecalConfig {
    uint8_t windowStartHour;  ///< Inclusive local hour (default 2)
    uint8_t windowEndHour;    ///< Exclusive local hour (default 4); may wrap past midnight
    uint32_t quiescenceS;     ///< No motion for at least this long (default 1 h)
    uint32_t cooldownS;       ///< Minimum time between recalibrations (default 2 h)

    RecalConfig() : windowStartHour(2), windowEndHour(4), quiescenceS(3600), cooldownS(7200) {}
};

enum RecalDecision : uint8_t {
    RECAL_GO = 0,
    RECAL_NO_TIME,         ///< Clock not valid
    RECAL_BUSY,            ///< Already recalibrating
    RECAL_OUTSIDE_WINDOW,
    RECAL_RECENT_MOTION,
    RECAL_COOLDOWN,
    RECAL_DONE_TONIGHT,    ///< Already recalibrated in this window instance
};

/// True if @p hour lies in [start, end), wrapping past midnight when start > end.
bool recalHourInWindow(uint8_t start, uint8_t end, uint8_t hour);

/**
 * @brief Nightly recalibration decision (ported from RecalScheduler::update()).
 *
 * Uses wall-clock epochs rather than millis() so the decision survives deep sleep: the
 * component keeps the last-motion and last-recalibration epochs in RTC memory.
 * An epoch of 0 means "unknown / never"; unknown motion satisfies quiescence (as the old
 * lastMotionMs == 0 did).
 */
class RecalScheduler {
public:
    RecalScheduler();

    void setConfig(const RecalConfig &config) { m_config = config; }
    const RecalConfig &getConfig() const { return m_config; }

    /// Record motion at @p epoch (keeps the most recent; 0 is ignored).
    void noteMotion(int64_t epoch);
    int64_t getLastMotionEpoch() const { return m_lastMotionEpoch; }
    void setLastMotionEpoch(int64_t epoch) { m_lastMotionEpoch = epoch; }

    void markRecalibrated(int64_t epoch) { m_lastRecalEpoch = epoch; }
    int64_t getLastRecalEpoch() const { return m_lastRecalEpoch; }
    void setLastRecalEpoch(int64_t epoch) { m_lastRecalEpoch = epoch; }

    RecalDecision evaluate(const WallTime &now, bool recalibrating) const;

    static const char *decisionName(RecalDecision decision);

private:
    RecalConfig m_config;
    int64_t m_lastMotionEpoch;
    int64_t m_lastRecalEpoch;
};

// =============================================================================================
// PIR recalibration sequence (power-cycle the shared PIR rail)
// =============================================================================================

typedef uint32_t (*MillisFn)();
typedef void (*RailFn)(void *ctx, bool on);

enum RecalPhase : uint8_t {
    RECAL_IDLE = 0,
    RECAL_POWER_OFF,  ///< Rail LOW (AM312 capacitor discharge)
    RECAL_WARMUP,     ///< Rail HIGH again, PIR outputs not yet trustworthy
};

class RecalSequencer {
public:
    /// PIR_RECAL_POWER_OFF_MS / PIR_WARMUP_TIME_MS in config.h.
    static constexpr uint32_t DEFAULT_POWER_OFF_MS = 2000;
    static constexpr uint32_t DEFAULT_WARMUP_MS = 60000;

    explicit RecalSequencer(MillisFn clock);

    void setRail(RailFn rail, void *ctx) {
        m_rail = rail;
        m_railCtx = ctx;
    }
    void setTiming(uint32_t powerOffMs, uint32_t warmupMs) {
        m_powerOffMs = powerOffMs;
        m_warmupMs = warmupMs;
    }

    /**
     * @brief Cut the rail and start the sequence.
     * @return false if no rail is configured; true if started or already in progress
     */
    bool start();

    /// Advance the sequence. @return true if the phase changed.
    bool update();

    RecalPhase getPhase() const { return m_phase; }
    bool isActive() const { return m_phase != RECAL_IDLE; }
    uint32_t getPowerOffMs() const { return m_powerOffMs; }
    uint32_t getWarmupMs() const { return m_warmupMs; }

private:
    MillisFn m_clock;
    RailFn m_rail;
    void *m_railCtx;
    uint32_t m_powerOffMs;
    uint32_t m_warmupMs;
    uint32_t m_phaseStartMs;
    RecalPhase m_phase;
};

// =============================================================================================
// Wake cause
// =============================================================================================

/// Wake cause as classified at boot. Values are persisted in RTC memory; append only.
enum WakeCause : uint8_t {
    WAKE_POWER_ON = 0,     ///< Cold boot, reset pin, or unknown non-sleep reset
    WAKE_SOFTWARE_RESET,   ///< esp_restart(): OTA reboot, restart button, safe-mode exit
    WAKE_CRASH,            ///< Panic / watchdog / brownout reset
    WAKE_PIR,              ///< Deep-sleep GPIO wake with a PIR pin HIGH
    WAKE_BUTTON,           ///< Deep-sleep GPIO wake with the button LOW (wins over PIR)
    WAKE_TIMER,            ///< Deep-sleep timer (periodic check-in)
    WAKE_SPURIOUS_GPIO,    ///< Deep-sleep GPIO wake but no wake pin active any more
    WAKE_OTHER_SLEEP,      ///< Deep-sleep wake from an unexpected source
};

// =============================================================================================
// Dormant mode ("switched off": button-only wake, persisted in flash)
// =============================================================================================

/// Why the device went dormant. Persisted in flash; append only.
enum DormantReason : uint8_t {
    DORMANT_REASON_NONE = 0,       ///< Never been dormant
    DORMANT_REASON_GESTURE = 1,    ///< Button held >= dormant threshold, then released
    DORMANT_REASON_HOME_ASSISTANT = 2,  ///< "Shut Down" button / API
    DORMANT_REASON_CRITICAL_BATTERY = 3,
    DORMANT_REASON_OTHER = 4,      ///< Lambda / unknown caller
};

/// "Button hold", "Home Assistant", "Critical battery", "Other" or "None".
const char *dormantReasonName(uint8_t reason);

/**
 * @brief Flash record of the dormant state: { active flag, last reason }.
 *
 * Stored as one uint32_t ESPHome preference so it survives total power loss. The reason is
 * kept after waking (active cleared) so "Last Shutdown" can still say why it went.
 * Layout: 0xD0 magic in bits 24-31, active in bit 8, reason in bits 0-7. Anything without
 * the magic (never written, erased NVS, garbage) decodes as "not dormant, no reason".
 */
struct DormantRecord {
    bool active;
    uint8_t reason;

    DormantRecord() : active(false), reason(DORMANT_REASON_NONE) {}
    DormantRecord(bool a, uint8_t r) : active(a), reason(r) {}

    uint32_t encode() const;
    static DormantRecord decode(uint32_t raw);
};

/// How this boot came about, as far as the dormant decision cares.
enum DormantBootKind : uint8_t {
    DORMANT_BOOT_BUTTON_WAKE = 0,   ///< Deep-sleep GPIO wake with the button pressed
    DORMANT_BOOT_OTHER_WAKE,        ///< Any other deep-sleep wake (PIR, timer, spurious, ...)
    DORMANT_BOOT_RESET,             ///< Power-on, brownout, crash, software reset
};

enum DormantBootAction : uint8_t {
    DORMANT_ACTION_NORMAL = 0,      ///< Not dormant: boot as usual
    DORMANT_ACTION_WAKE,            ///< Clear the flag, boot as usual (interactive, WiFi on)
    DORMANT_ACTION_VERIFY_HOLD,     ///< Button wake with wake_hold: require a longer hold first
    DORMANT_ACTION_REENTER,         ///< Go straight back to dormant sleep
};

struct DormantBootInput {
    bool flagActive;        ///< Persisted dormant flag
    DormantBootKind kind;
    bool buttonHeld;        ///< Button physically pressed right now
    bool usb;               ///< On USB power (deliberately ignored: dormant overrides USB)
    uint32_t wakeHoldMs;    ///< 0 = a single press wakes

    DormantBootInput()
        : flagActive(false), kind(DORMANT_BOOT_RESET), buttonHeld(false), usb(false),
          wakeHoldMs(0) {}
};

/**
 * @brief Boot-time dormant decision (runs first in stepaware_power::setup()).
 *
 *   flag clear                         -> NORMAL
 *   button wake, wakeHoldMs == 0       -> WAKE
 *   button wake, wakeHoldMs > 0        -> VERIFY_HOLD (bag-bump protection)
 *   other deep-sleep wake              -> REENTER   (only the button may wake it)
 *   reset boot, button held            -> WAKE      (deliberate: holding at power-on wakes it,
 *                                                    and the 15/30 s boot-hold resets still work)
 *   reset boot, button not held        -> REENTER   (power restored / USB plugged into a flat
 *                                                    battery / crash: stay off while charging)
 * USB never changes the outcome. OTA / stay-awake do not exist yet at this point.
 */
DormantBootAction decideDormantBoot(const DormantBootInput &in);

/// Map the classified wake cause onto the dormant decision's boot kind.
DormantBootKind dormantBootKindFor(WakeCause cause);

const char *dormantBootActionName(DormantBootAction action);

/**
 * @brief "Hold to wake" check after a dormant button wake (wake_hold option).
 *
 * Feed the debounced-or-not button level; a release lasting releaseDebounceMs before the
 * required hold time rejects the wake, holding for the full time confirms it.
 */
class WakeHoldVerifier {
public:
    enum Result : uint8_t { PENDING = 0, CONFIRMED, REJECTED };

    WakeHoldVerifier(uint32_t requiredMs, uint32_t releaseDebounceMs = 30)
        : m_requiredMs(requiredMs), m_debounceMs(releaseDebounceMs), m_startMs(0),
          m_releasedSinceMs(0), m_released(false), m_result(PENDING) {}

    void start(uint32_t nowMs);
    Result update(uint32_t nowMs, bool pressed);
    Result getResult() const { return m_result; }

private:
    uint32_t m_requiredMs;
    uint32_t m_debounceMs;
    uint32_t m_startMs;
    uint32_t m_releasedSinceMs;
    bool m_released;
    Result m_result;
};

}  // namespace step_power

// Header-only: every ESPHome component header is also included into the generated main.cpp,
// so the definitions are inline.
#include "power_logic_impl.h"

#endif  // STEPAWARE_POWER_LOGIC_H
