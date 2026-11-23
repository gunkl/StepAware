/**
 * @file battery_calibration_impl.h
 * @brief Inline definitions for battery_calibration.h (included from its end; do not include
 *        directly).
 */
#ifndef STEPAWARE_BATTERY_CALIBRATION_IMPL_H
#define STEPAWARE_BATTERY_CALIBRATION_IMPL_H

#include "battery_calibration.h"

namespace step_battery {

inline FullChargeCalibrator::FullChargeCalibrator()
    : m_factor(1.0f),
      m_sessions(0),
      m_lastRejected(0.0f),
      m_inSession(false),
      m_baseFactor(1.0f),
      m_baseSessions(0),
      m_sessionBestV(0.0f),
      m_rejectedBestV(0.0f),
      m_head(0),
      m_count(0) {
    for (uint8_t i = 0; i < MAX_SAMPLES; i++) {
        m_times[i] = 0;
        m_volts[i] = 0.0f;
    }
}

inline void FullChargeCalibrator::restore(const CalibrationPersist &state) {
    // NAN fails both comparisons, so "!(in range)" also rejects it.
    bool valid = state.magic == MAGIC && state.sessions > 0 &&
                 state.factor >= m_config.minFactor && state.factor <= m_config.maxFactor;
    if (valid) {
        m_factor = state.factor;
        m_sessions = state.sessions;
    } else {
        m_factor = 1.0f;
        m_sessions = 0;
    }
    m_inSession = false;
    clearWindow();
}

inline CalibrationPersist FullChargeCalibrator::persist() const {
    CalibrationPersist p;
    p.factor = m_factor;
    p.sessions = m_sessions;
    p.magic = MAGIC;
    return p;
}

inline void FullChargeCalibrator::reset() {
    m_factor = 1.0f;
    m_sessions = 0;
    m_lastRejected = 0.0f;
    m_inSession = false;
    clearWindow();
}

inline void FullChargeCalibrator::startSession() {
    m_inSession = true;
    m_baseFactor = m_factor;
    m_baseSessions = m_sessions;
    m_sessionBestV = 0.0f;
    m_rejectedBestV = 0.0f;
    clearWindow();
}

inline void FullChargeCalibrator::clearWindow() {
    m_head = 0;
    m_count = 0;
}

inline uint8_t FullChargeCalibrator::indexOf(uint8_t i) const {
    return static_cast<uint8_t>((m_head + i) % MAX_SAMPLES);
}

inline void FullChargeCalibrator::trimWindow(uint32_t nowMs) {
    // Unsigned subtraction keeps the age right across a millis() wrap.
    while (m_count > 0 && nowMs - m_times[m_head] > m_config.stableMs) {
        m_head = static_cast<uint8_t>((m_head + 1) % MAX_SAMPLES);
        m_count--;
    }
}

inline bool FullChargeCalibrator::windowIsFlat(uint32_t nowMs, float &meanV) const {
    if (m_count < m_config.minSamples || m_count < 2) {
        return false;
    }
    // The window must actually span (nearly) stableMs, not just hold enough samples.
    uint32_t spanMs = nowMs - m_times[m_head];
    if (spanMs < m_config.stableMs - m_config.stableMs / 10U) {
        return false;
    }

    // Least squares on (hours since the oldest sample, volts). Relative times keep the float
    // math small; double keeps the sums exact enough on the host and on the C3.
    double sumT = 0.0, sumV = 0.0, sumTT = 0.0, sumTV = 0.0;
    float minV = m_volts[m_head];
    float maxV = minV;
    for (uint8_t i = 0; i < m_count; i++) {
        uint8_t idx = indexOf(i);
        double t = static_cast<double>(m_times[idx] - m_times[m_head]) / 3600000.0;
        double v = m_volts[idx];
        sumT += t;
        sumV += v;
        sumTT += t * t;
        sumTV += t * v;
        if (m_volts[idx] < minV) minV = m_volts[idx];
        if (m_volts[idx] > maxV) maxV = m_volts[idx];
    }
    double n = m_count;
    double denom = n * sumTT - sumT * sumT;
    if (denom <= 0.0) {
        return false;
    }
    double slope = (n * sumTV - sumT * sumV) / denom;  // volts per hour
    if (slope < 0.0) slope = -slope;

    if (maxV - minV > m_config.maxSpreadV || slope > m_config.maxSlopeVPerHour) {
        return false;
    }
    meanV = static_cast<float>(sumV / n);
    return true;
}

inline CalibrationEvent FullChargeCalibrator::onReading(uint32_t nowMs, float measuredV,
                                                        bool usb) {
    if (!usb) {
        m_inSession = false;
        clearWindow();
        return CAL_NONE;
    }
    // NAN fails both comparisons, so "!(in range)" also rejects it. Ignored, not a reset.
    if (!(measuredV >= m_config.minValidV && measuredV <= m_config.maxValidV)) {
        return CAL_NONE;
    }
    if (!m_inSession) {
        startSession();
    }

    if (m_count == MAX_SAMPLES) {  // Ring full: drop the oldest
        m_head = static_cast<uint8_t>((m_head + 1) % MAX_SAMPLES);
        m_count--;
    }
    uint8_t tail = indexOf(m_count);
    m_times[tail] = nowMs;
    m_volts[tail] = measuredV;
    m_count++;
    trimWindow(nowMs);

    float meanV = 0.0f;
    if (!windowIsFlat(nowMs, meanV)) {
        return CAL_NONE;
    }
    // Only a higher plateau than this session's best can improve the estimate (1 mV margin
    // so a steady plateau doesn't re-trigger on noise).
    const float MARGIN_V = 0.001f;
    float candidate = m_config.fullV / meanV;
    if (candidate < m_config.minFactor || candidate > m_config.maxFactor) {
        if (meanV <= m_rejectedBestV + MARGIN_V) {
            return CAL_NONE;
        }
        m_rejectedBestV = meanV;
        m_lastRejected = candidate;
        return CAL_REJECTED;
    }
    if (meanV <= m_sessionBestV + MARGIN_V) {
        return CAL_NONE;
    }
    m_sessionBestV = meanV;

    // Blend from the factor stored before this session, so a session counts once however many
    // times its plateau improves.
    if (m_baseSessions == 0) {
        m_factor = candidate;
    } else {
        uint16_t n = m_baseSessions + 1;
        uint16_t weightDiv = n < m_config.blendCount ? n : m_config.blendCount;
        if (weightDiv == 0) weightDiv = 1;
        m_factor = m_baseFactor + (candidate - m_baseFactor) / static_cast<float>(weightDiv);
    }
    m_sessions = m_baseSessions < 0xFFFF ? static_cast<uint16_t>(m_baseSessions + 1)
                                         : m_baseSessions;
    return CAL_UPDATED;
}

}  // namespace step_battery

#endif  // STEPAWARE_BATTERY_CALIBRATION_IMPL_H
