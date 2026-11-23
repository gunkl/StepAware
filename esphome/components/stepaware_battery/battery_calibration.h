/**
 * @file battery_calibration.h
 * @brief Dependency-free full-charge gain calibration for the battery voltage reading.
 *
 * Plain C++11 (no ESPHome / ESP-IDF headers) so it is unit-tested natively
 * (test/test_esphome_battery, [env:native_battery]). Header-only (inline definitions in
 * battery_calibration_impl.h), for the same reason as stepaware_power/power_logic.h.
 *
 * Why: the voltage at GPIO5 goes through a resistor divider and the ADC, so the reading carries
 * the divider's tolerance and the ADC's gain error (easily +/-2 %, about +/-80 mV at 4.2 V). The
 * charger gives a free reference: in its constant-voltage phase it holds the cell at a known
 * voltage (nominally 4.20 V). When the reading is flat on USB, that reading is "full" in true
 * volts, and factor = full_voltage / measured corrects the gain error from then on.
 *
 * How it decides "flat": over the last stableMs of readings on USB, the least-squares slope must
 * be within maxSlopeVPerHour and the max - min spread within maxSpreadV. Within one USB session
 * the highest flat plateau wins: the charger's constant-voltage phase is the highest plateau,
 * the approach to it is still rising, and the cell relaxes below it after termination. Each
 * session that finds a plateau blends into the stored factor (weight 1 / min(n, blendCount)).
 * Offset errors are not corrected (that needs a second known voltage).
 */
#ifndef STEPAWARE_BATTERY_CALIBRATION_H
#define STEPAWARE_BATTERY_CALIBRATION_H

#include <stdint.h>

namespace step_battery {

struct CalibrationConfig {
    float fullV;              ///< Charger constant-voltage setpoint, true volts
    uint32_t stableMs;        ///< Length of the flat window
    float maxSlopeVPerHour;   ///< |least-squares slope| limit over the window
    float maxSpreadV;         ///< max - min limit over the window
    uint8_t minSamples;       ///< Readings needed in the window
    float minFactor;          ///< Plausibility: reject a factor below this...
    float maxFactor;          ///< ...or above this (divider/ADC this far off = wiring fault)
    uint8_t blendCount;       ///< From this many sessions on, a new one weighs 1 / blendCount
    float minValidV;          ///< Readings outside [minValidV, maxValidV] are ignored
    float maxValidV;

    CalibrationConfig()
        : fullV(4.20f),
          stableMs(30UL * 60UL * 1000UL),
          maxSlopeVPerHour(0.03f),
          maxSpreadV(0.04f),
          minSamples(20),
          minFactor(0.90f),
          maxFactor(1.10f),
          blendCount(4),
          minValidV(2.5f),
          maxValidV(5.0f) {}
};

/// Stored in flash. Change MAGIC if the layout changes.
struct CalibrationPersist {
    float factor;
    uint16_t sessions;
    uint16_t magic;
};

enum CalibrationEvent : uint8_t {
    CAL_NONE = 0,
    CAL_UPDATED = 1,   ///< New factor; persist it
    CAL_REJECTED = 2,  ///< Flat plateau found, but its factor is implausible (once per session)
};

class FullChargeCalibrator {
public:
    enum { MAX_SAMPLES = 64, MAGIC = 0xBA7C };

    FullChargeCalibrator();

    void setConfig(const CalibrationConfig &config) { m_config = config; }
    const CalibrationConfig &getConfig() const { return m_config; }

    /// Restore from flash; an invalid record leaves it uncalibrated.
    void restore(const CalibrationPersist &state);
    CalibrationPersist persist() const;
    /// Forget the calibration (factor 1.0) and any session in progress.
    void reset();

    /**
     * @brief Feed one uncorrected reading (pin volts x divider ratio).
     * @param nowMs  monotonic milliseconds (wrap-safe)
     * @param usb    on USB power (charging)
     */
    CalibrationEvent onReading(uint32_t nowMs, float measuredV, bool usb);

    /// Gain factor to apply: 1.0 until the first calibration.
    float factor() const { return m_sessions > 0 ? m_factor : 1.0f; }
    float apply(float measuredV) const { return measuredV * factor(); }
    bool isCalibrated() const { return m_sessions > 0; }
    uint16_t sessions() const { return m_sessions; }
    /// The factor from the last rejected plateau (for logging).
    float lastRejectedFactor() const { return m_lastRejected; }
    /// Measured voltage of the plateau used for the current factor (0 if none this session).
    float sessionPlateauV() const { return m_sessionBestV; }

private:
    void startSession();
    void clearWindow();
    void trimWindow(uint32_t nowMs);
    /// True if the readings in the window form a flat plateau; mean returned in meanV.
    bool windowIsFlat(uint32_t nowMs, float &meanV) const;
    uint8_t indexOf(uint8_t i) const;  ///< i-th oldest sample -> ring index

    CalibrationConfig m_config;
    float m_factor;
    uint16_t m_sessions;
    float m_lastRejected;

    bool m_inSession;
    float m_baseFactor;     ///< Stored factor at the start of this session
    uint16_t m_baseSessions;
    float m_sessionBestV;   ///< Highest accepted plateau this session (0 = none)
    float m_rejectedBestV;  ///< Highest rejected plateau this session (0 = none)

    uint32_t m_times[MAX_SAMPLES];
    float m_volts[MAX_SAMPLES];
    uint8_t m_head;   ///< Index of the oldest sample
    uint8_t m_count;
};

}  // namespace step_battery

// Header-only: every ESPHome component header is also included into the generated main.cpp,
// so the definitions are inline.
#include "battery_calibration_impl.h"

#endif  // STEPAWARE_BATTERY_CALIBRATION_H
