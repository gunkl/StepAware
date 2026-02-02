#ifndef STEPAWARE_RECAL_SCHEDULER_H
#define STEPAWARE_RECAL_SCHEDULER_H

#include <Arduino.h>
#include <time.h>
#include "hal_pir.h"

/**
 * @brief Automatic nightly recalibration scheduler for PIR sensors.
 *
 * Monitors conditions and triggers a power-cycle recalibration during a
 * quiet overnight window. All conditions must be true simultaneously:
 *
 *   1. NTP time is synced
 *   2. Local hour is within the recal window (default 2:00–3:59 AM)
 *   3. No motion detected for at least the quiescence period (default 1 hour)
 *   4. Sensor is not already recalibrating
 *   5. Cooldown since last recal has elapsed (default 2 hours)
 *
 * Non-blocking: call update() every loop iteration. Time is read via
 * standard time.h (time() + localtime()), which on ESP32 reflects the
 * timezone set by configTime() in NTPManager.
 */
class RecalScheduler {
public:
    /**
     * @brief Construct scheduler bound to a specific PIR sensor.
     *
     * Both PIR sensors share one power wire, so bind to the near sensor
     * (the one that owns the power pin). One recalibrate() call handles both.
     *
     * @param sensor Pointer to the HAL_PIR that owns PIN_PIR_POWER
     */
    explicit RecalScheduler(HAL_PIR* sensor);

    /**
     * @brief Initialize the scheduler. Call once during setup.
     */
    void begin();

    /**
     * @brief Update scheduler state. Call every loop iteration.
     *
     * Evaluates all trigger conditions and calls sensor->recalibrate()
     * if they are all satisfied.
     *
     * @param ntpSynced     Whether NTP time is currently valid
     * @param lastMotionMs  millis() timestamp of the most recent motion event
     *                      across all sensors
     */
    void update(bool ntpSynced, uint32_t lastMotionMs);

    /**
     * @brief Check if a recalibration was triggered this cycle.
     *
     * Returns true for exactly one update() cycle after triggering.
     *
     * @return true if recalibration was just triggered
     */
    bool wasTriggered() const { return m_triggered; }

    // =========================================================================
    // Configuration (all have sensible defaults)
    // =========================================================================

    /** Set the time window start hour (0–23, inclusive). Default: 2 */
    void setWindowStartHour(uint8_t hour) { m_windowStartHour = hour; }

    /** Set the time window end hour (0–23, exclusive). Default: 4 */
    void setWindowEndHour(uint8_t hour) { m_windowEndHour = hour; }

    /** Set minimum quiescence period in ms. Default: 3600000 (1 hour) */
    void setQuiescencePeriodMs(uint32_t ms) { m_quiescencePeriodMs = ms; }

    /** Set cooldown between recals in ms. Default: 7200000 (2 hours) */
    void setCooldownMs(uint32_t ms) { m_cooldownMs = ms; }

    // Testability seam: replace the time source for unit tests.
    // Production code uses time(NULL); tests inject a mock returning a
    // controlled epoch value.
    static time_t (*s_timeFunc)();

private:
    HAL_PIR* m_sensor;

    // Configuration
    uint8_t  m_windowStartHour;     ///< Inclusive (default: 2)
    uint8_t  m_windowEndHour;       ///< Exclusive (default: 4)
    uint32_t m_quiescencePeriodMs;  ///< Default: 3600000 (1 hour)
    uint32_t m_cooldownMs;          ///< Default: 7200000 (2 hours)

    // State
    bool     m_hasRecalibrated;     ///< True after first successful recal trigger
    uint32_t m_lastRecalMs;         ///< millis() of last triggered recal
    bool     m_triggered;           ///< True for one cycle after triggering
};

#endif // STEPAWARE_RECAL_SCHEDULER_H
