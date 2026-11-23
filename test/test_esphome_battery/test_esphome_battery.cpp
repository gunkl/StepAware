/**
 * @file test_esphome_battery.cpp
 * @brief Native tests for stepaware_battery's full-charge gain calibration
 *        (battery_calibration.h): plateau detection on USB, plausibility, blending across
 *        charge sessions, persistence and millis() wrap.
 */
#include <unity.h>
#include <math.h>
#include <stdint.h>

#include "../../esphome/components/stepaware_battery/battery_calibration.h"  // header-only

using namespace step_battery;

static const uint32_t MINUTE_MS = 60UL * 1000UL;

static uint32_t g_now = 0;

void setUp(void) { g_now = 0; }

void tearDown(void) {}

/// Feed n readings one minute apart, ramping from v0 by slopePerHour, with an optional
/// alternating +/- noise. Returns the first event that isn't CAL_NONE (or CAL_NONE).
static CalibrationEvent feed(FullChargeCalibrator &cal, int n, float v0, bool usb = true,
                             float slopePerHour = 0.0f, float noise = 0.0f) {
    CalibrationEvent first = CAL_NONE;
    for (int i = 0; i < n; i++) {
        float v = v0 + slopePerHour * static_cast<float>(i) / 60.0f;
        v += (i % 2 == 0) ? noise : -noise;
        CalibrationEvent ev = cal.onReading(g_now, v, usb);
        if (first == CAL_NONE) first = ev;
        g_now += MINUTE_MS;
    }
    return first;
}

/// Count events of a kind while feeding.
static int countEvents(FullChargeCalibrator &cal, int n, float v, CalibrationEvent kind,
                       bool usb = true) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (cal.onReading(g_now, v, usb) == kind) count++;
        g_now += MINUTE_MS;
    }
    return count;
}

// =============================================================================================
// Defaults
// =============================================================================================

void test_uncalibrated_is_identity(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_FALSE(cal.isCalibrated());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cal.factor());
    TEST_ASSERT_EQUAL_FLOAT(3.87f, cal.apply(3.87f));
    TEST_ASSERT_EQUAL_UINT16(0, cal.sessions());
}

// =============================================================================================
// Plateau detection
// =============================================================================================

void test_flat_plateau_on_usb_calibrates(void) {
    FullChargeCalibrator cal;
    // 31 readings one minute apart = a 30-minute window.
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.10f));
    TEST_ASSERT_TRUE(cal.isCalibrated());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.20f / 4.10f, cal.factor());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.20f, cal.apply(4.10f));
    TEST_ASSERT_EQUAL_UINT16(1, cal.sessions());
}

void test_steady_plateau_updates_once(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(1, countEvents(cal, 120, 4.10f, CAL_UPDATED));
    TEST_ASSERT_EQUAL_UINT16(1, cal.sessions());
}

void test_window_too_short(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(CAL_NONE, feed(cal, 26, 4.10f));  // 25 minutes
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

void test_too_few_samples_in_window(void) {
    FullChargeCalibrator cal;
    // Readings every 5 minutes: the window spans 30 min but holds only 7 samples.
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT_EQUAL(CAL_NONE, cal.onReading(g_now, 4.10f, true));
        g_now += 5 * MINUTE_MS;
    }
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

void test_rising_constant_current_phase_ignored(void) {
    FullChargeCalibrator cal;
    // 0.2 V/h: still charging toward the setpoint.
    TEST_ASSERT_EQUAL(CAL_NONE, feed(cal, 60, 3.95f, true, 0.20f));
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

void test_slow_slope_within_limit_accepted(void) {
    FullChargeCalibrator cal;
    // 0.02 V/h is under the 0.03 V/h default limit.
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.10f, true, 0.02f));
}

void test_noise_within_spread_accepted(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.10f, true, 0.0f, 0.010f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.20f / 4.10f, cal.factor());
}

void test_spread_too_large_rejected(void) {
    FullChargeCalibrator cal;
    // +/-25 mV alternating: flat on average, but 50 mV spread > 40 mV.
    TEST_ASSERT_EQUAL(CAL_NONE, feed(cal, 40, 4.10f, true, 0.0f, 0.025f));
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

void test_battery_power_never_calibrates(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(CAL_NONE, feed(cal, 120, 4.10f, false));
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

void test_unplug_restarts_window(void) {
    FullChargeCalibrator cal;
    feed(cal, 20, 4.10f);
    feed(cal, 1, 4.05f, false);  // Unplugged for a minute
    TEST_ASSERT_EQUAL(CAL_NONE, feed(cal, 25, 4.10f));  // Only 24 min since replug
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 10, 4.10f));
}

void test_nan_and_implausible_readings_ignored(void) {
    FullChargeCalibrator cal;
    feed(cal, 15, 4.10f);
    TEST_ASSERT_EQUAL(CAL_NONE, cal.onReading(g_now, NAN, true));
    TEST_ASSERT_EQUAL(CAL_NONE, cal.onReading(g_now, 0.0f, true));
    TEST_ASSERT_EQUAL(CAL_NONE, cal.onReading(g_now, 9.0f, true));
    g_now += MINUTE_MS;
    // The window kept going: 16 more minutes completes it.
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 16, 4.10f));
}

// =============================================================================================
// One USB session: the highest plateau wins
// =============================================================================================

void test_higher_plateau_later_in_session_wins(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.05f));
    feed(cal, 5, 4.05f, true, 0.6f);  // Climbs ~50 mV
    // May improve in steps while the old readings age out of the window.
    TEST_ASSERT_TRUE(countEvents(cal, 40, 4.10f, CAL_UPDATED) >= 1);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.20f / 4.10f, cal.factor());
    TEST_ASSERT_EQUAL_UINT16(1, cal.sessions());  // Still one session
}

void test_relaxation_after_termination_does_not_lower(void) {
    FullChargeCalibrator cal;
    feed(cal, 31, 4.10f);  // Constant-voltage plateau
    feed(cal, 30, 4.10f, true, -0.06f);  // Charger stops; cell relaxes
    TEST_ASSERT_EQUAL(0, countEvents(cal, 60, 4.07f, CAL_UPDATED));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.20f / 4.10f, cal.factor());
}

// =============================================================================================
// Plausibility
// =============================================================================================

void test_implausible_factor_rejected_once(void) {
    FullChargeCalibrator cal;
    // Measured 3.60 V "full" -> factor 1.167 > 1.10: a wiring fault, not tolerance.
    TEST_ASSERT_EQUAL(1, countEvents(cal, 90, 3.60f, CAL_REJECTED));
    TEST_ASSERT_FALSE(cal.isCalibrated());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.20f / 3.60f, cal.lastRejectedFactor());
}

void test_implausible_low_factor_rejected(void) {
    FullChargeCalibrator cal;
    TEST_ASSERT_EQUAL(CAL_REJECTED, feed(cal, 31, 4.80f));  // 0.875 < 0.90
    TEST_ASSERT_FALSE(cal.isCalibrated());
}

// =============================================================================================
// Blending across sessions
// =============================================================================================

void test_second_session_blends_half(void) {
    FullChargeCalibrator cal;
    feed(cal, 31, 4.10f);
    float f1 = cal.factor();
    feed(cal, 1, 3.9f, false);  // Unplug ends the session
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.12f));
    float f2 = 4.20f / 4.12f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, f1 + (f2 - f1) / 2.0f, cal.factor());
    TEST_ASSERT_EQUAL_UINT16(2, cal.sessions());
}

void test_blend_weight_caps_at_blend_count(void) {
    FullChargeCalibrator cal;
    for (int s = 0; s < 4; s++) {
        feed(cal, 31, 4.10f);
        feed(cal, 1, 3.9f, false);
    }
    TEST_ASSERT_EQUAL_UINT16(4, cal.sessions());
    float before = cal.factor();
    feed(cal, 31, 4.00f);  // Session 5 weighs 1/4, not 1/5
    float target = 4.20f / 4.00f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, before + (target - before) / 4.0f, cal.factor());
}

void test_improving_plateau_counts_one_session(void) {
    FullChargeCalibrator cal;
    feed(cal, 31, 4.10f);
    feed(cal, 1, 3.9f, false);
    float base = cal.factor();
    feed(cal, 31, 4.05f);
    feed(cal, 31, 4.11f);  // Better plateau, same session
    float target = 4.20f / 4.11f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, base + (target - base) / 2.0f, cal.factor());
    TEST_ASSERT_EQUAL_UINT16(2, cal.sessions());
}

// =============================================================================================
// Persistence and reset
// =============================================================================================

void test_persist_restore_roundtrip(void) {
    FullChargeCalibrator a;
    feed(a, 31, 4.10f);
    CalibrationPersist p = a.persist();
    FullChargeCalibrator b;
    b.restore(p);
    TEST_ASSERT_TRUE(b.isCalibrated());
    TEST_ASSERT_EQUAL_FLOAT(a.factor(), b.factor());
    TEST_ASSERT_EQUAL_UINT16(1, b.sessions());
}

void test_restore_rejects_bad_records(void) {
    FullChargeCalibrator cal;
    CalibrationPersist p;
    p.factor = 1.02f;
    p.sessions = 1;
    p.magic = 0x1234;  // Wrong magic
    cal.restore(p);
    TEST_ASSERT_FALSE(cal.isCalibrated());

    p.magic = FullChargeCalibrator::MAGIC;
    p.factor = NAN;
    cal.restore(p);
    TEST_ASSERT_FALSE(cal.isCalibrated());

    p.factor = 1.5f;  // Out of range
    cal.restore(p);
    TEST_ASSERT_FALSE(cal.isCalibrated());

    p.factor = 1.02f;
    p.sessions = 0;  // Never calibrated
    cal.restore(p);
    TEST_ASSERT_FALSE(cal.isCalibrated());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cal.factor());
}

void test_reset_forgets(void) {
    FullChargeCalibrator cal;
    feed(cal, 31, 4.10f);
    cal.reset();
    TEST_ASSERT_FALSE(cal.isCalibrated());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cal.factor());
    // A new plateau calibrates from scratch, not blended with the old one.
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.00f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.20f / 4.00f, cal.factor());
}

void test_custom_full_voltage(void) {
    FullChargeCalibrator cal;
    CalibrationConfig cfg;
    cfg.fullV = 4.35f;  // LiHV cell
    cal.setConfig(cfg);
    feed(cal, 31, 4.30f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.35f / 4.30f, cal.factor());
}

// =============================================================================================
// millis() wrap
// =============================================================================================

void test_millis_wrap_during_window(void) {
    FullChargeCalibrator cal;
    g_now = 0xFFFFFFFFUL - 10UL * MINUTE_MS;  // Wraps after 10 readings
    TEST_ASSERT_EQUAL(CAL_UPDATED, feed(cal, 31, 4.10f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.20f / 4.10f, cal.factor());
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_uncalibrated_is_identity);
    RUN_TEST(test_flat_plateau_on_usb_calibrates);
    RUN_TEST(test_steady_plateau_updates_once);
    RUN_TEST(test_window_too_short);
    RUN_TEST(test_too_few_samples_in_window);
    RUN_TEST(test_rising_constant_current_phase_ignored);
    RUN_TEST(test_slow_slope_within_limit_accepted);
    RUN_TEST(test_noise_within_spread_accepted);
    RUN_TEST(test_spread_too_large_rejected);
    RUN_TEST(test_battery_power_never_calibrates);
    RUN_TEST(test_unplug_restarts_window);
    RUN_TEST(test_nan_and_implausible_readings_ignored);
    RUN_TEST(test_higher_plateau_later_in_session_wins);
    RUN_TEST(test_relaxation_after_termination_does_not_lower);
    RUN_TEST(test_implausible_factor_rejected_once);
    RUN_TEST(test_implausible_low_factor_rejected);
    RUN_TEST(test_second_session_blends_half);
    RUN_TEST(test_blend_weight_caps_at_blend_count);
    RUN_TEST(test_improving_plateau_counts_one_session);
    RUN_TEST(test_persist_restore_roundtrip);
    RUN_TEST(test_restore_rejects_bad_records);
    RUN_TEST(test_reset_forgets);
    RUN_TEST(test_custom_full_voltage);
    RUN_TEST(test_millis_wrap_during_window);
    return UNITY_END();
}
