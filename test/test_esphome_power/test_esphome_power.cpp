/**
 * @file test_esphome_power.cpp
 * @brief Native tests for stepaware_power's dependency-free logic (power_logic.h):
 *        battery states / critical-shutdown confirmation, PIR recalibration and the dormant
 *        mode (persisted record, boot re-entry decision, hold-to-wake).
 *
 * Recalibration cases are ported from test/test_pir_recal (HAL_PIR::recalibrate() and
 * RecalScheduler); the scheduler now works on wall-clock epochs so it survives deep sleep.
 */
#include <unity.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "../../esphome/components/stepaware_power/power_logic.h"  // header-only

using namespace step_power;

// ---------------------------------------------------------------------------
// Mock clock + rail
// ---------------------------------------------------------------------------
static uint32_t g_now = 0;
static uint32_t mockClock() { return g_now; }

static int g_railLevel = 1;
static int g_railWrites = 0;
static void mockRail(void *ctx, bool on) {
    (void)ctx;
    g_railLevel = on ? 1 : 0;
    g_railWrites++;
}

void setUp(void) {
    g_now = 0;
    g_railLevel = 1;
    g_railWrites = 0;
}

void tearDown(void) {}

// 2024-01-15 00:00:00 UTC; tests use UTC-as-local hours.
static const int64_t DAY0 = 1705276800;

static WallTime at(int64_t dayOffset, uint8_t h, uint8_t m = 0, uint8_t s = 0) {
    WallTime t;
    t.valid = true;
    t.epoch = DAY0 + dayOffset * 86400 + h * 3600 + m * 60 + s;
    t.hour = h;
    t.minute = m;
    t.second = s;
    return t;
}

static void feed(BatteryMonitor &bm, float v, int n, bool usb = false) {
    for (int i = 0; i < n; i++) {
        bm.onReading(v, usb);
    }
}

// =============================================================================================
// Battery
// =============================================================================================

void test_battery_defaults_match_config_h(void) {
    BatteryConfig c;
    TEST_ASSERT_EQUAL_FLOAT(3.3f, c.lowV);       // BATTERY_VOLTAGE_LOW
    TEST_ASSERT_EQUAL_FLOAT(3.0f, c.criticalV);  // BATTERY_VOLTAGE_CRITICAL
    TEST_ASSERT_EQUAL_UINT8(3, c.minSamples);    // MIN_BATTERY_SAMPLES
    TEST_ASSERT_EQUAL_FLOAT(2.5f, c.minValidV);  // valid only if voltage >= 2.5 V
}

void test_battery_unknown_until_min_samples(void) {
    BatteryMonitor bm;
    feed(bm, 3.1f, 2);
    TEST_ASSERT_FALSE(bm.isKnown());
    TEST_ASSERT_FALSE(bm.isLow());
    TEST_ASSERT_EQUAL_STRING("Unknown", bm.getStateName());
    feed(bm, 3.1f, 1);
    TEST_ASSERT_TRUE(bm.isKnown());
    TEST_ASSERT_TRUE(bm.isLow());
    TEST_ASSERT_FALSE(bm.isCritical());
    TEST_ASSERT_EQUAL_STRING("Low", bm.getStateName());
}

void test_battery_normal(void) {
    BatteryMonitor bm;
    feed(bm, 3.8f, 3);
    TEST_ASSERT_EQUAL(BATTERY_NORMAL, bm.getLevel());
    TEST_ASSERT_FALSE(bm.isLow());
    TEST_ASSERT_EQUAL_STRING("Normal", bm.getStateName());
}

void test_battery_critical_implies_low(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 3);
    TEST_ASSERT_TRUE(bm.isCritical());
    TEST_ASSERT_TRUE(bm.isLow());
    TEST_ASSERT_EQUAL_STRING("Critical", bm.getStateName());
}

void test_battery_low_hysteresis(void) {
    BatteryMonitor bm;
    feed(bm, 3.25f, 3);
    TEST_ASSERT_TRUE(bm.isLow());
    feed(bm, 3.35f, 1);  // above 3.3 but below 3.3 + 0.1
    TEST_ASSERT_TRUE(bm.isLow());
    feed(bm, 3.41f, 1);
    TEST_ASSERT_FALSE(bm.isLow());
    feed(bm, 3.35f, 1);  // entering again needs < 3.3
    TEST_ASSERT_FALSE(bm.isLow());
}

void test_battery_critical_hysteresis(void) {
    BatteryMonitor bm;
    feed(bm, 2.95f, 3);
    TEST_ASSERT_TRUE(bm.isCritical());
    feed(bm, 3.05f, 1);
    TEST_ASSERT_TRUE(bm.isCritical());
    feed(bm, 3.15f, 1);
    TEST_ASSERT_FALSE(bm.isCritical());
    TEST_ASSERT_TRUE(bm.isLow());
}

void test_battery_nan_ignored(void) {
    BatteryMonitor bm;
    feed(bm, NAN, 10);
    TEST_ASSERT_FALSE(bm.isKnown());
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    TEST_ASSERT_EQUAL_UINT8(0, bm.getCriticalCount());
}

void test_battery_implausible_low_ignored(void) {
    // A failed / disconnected ADC reads ~0 V; must never look critical.
    BatteryMonitor bm;
    feed(bm, 0.0f, 10);
    feed(bm, 1.9f, 10);
    feed(bm, 2.49f, 10);
    TEST_ASSERT_FALSE(bm.isKnown());
    TEST_ASSERT_FALSE(bm.isCritical());
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
}

void test_battery_implausible_high_ignored(void) {
    BatteryMonitor bm;
    feed(bm, 6.6f, 5);  // divider missing / wrong multiply
    TEST_ASSERT_FALSE(bm.isKnown());
}

void test_battery_single_critical_sample_not_confirmed(void) {
    BatteryMonitor bm;
    feed(bm, 3.8f, 3);
    feed(bm, 2.9f, 1);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    TEST_ASSERT_EQUAL_UINT8(1, bm.getCriticalCount());
}

void test_battery_three_consecutive_confirm(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 2);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    feed(bm, 2.9f, 1);
    TEST_ASSERT_TRUE(bm.isShutdownConfirmed());
}

void test_battery_good_reading_resets_count(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 2);
    feed(bm, 3.05f, 1);  // above critical (raw threshold, no hysteresis)
    feed(bm, 2.9f, 2);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    TEST_ASSERT_EQUAL_UINT8(2, bm.getCriticalCount());
}

void test_battery_nan_resets_count(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 2);
    feed(bm, NAN, 1);
    feed(bm, 2.9f, 2);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    feed(bm, 0.1f, 1);
    feed(bm, 2.9f, 2);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
}

void test_battery_usb_never_critical(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 10, true);
    TEST_ASSERT_FALSE(bm.isCritical());
    TEST_ASSERT_FALSE(bm.isLow());
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    TEST_ASSERT_EQUAL_STRING("USB", bm.getStateName());
}

void test_battery_usb_plug_resets_count(void) {
    BatteryMonitor bm;
    feed(bm, 2.9f, 3);
    TEST_ASSERT_TRUE(bm.isShutdownConfirmed());
    TEST_ASSERT_TRUE(bm.setUsb(true));
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
    TEST_ASSERT_FALSE(bm.isCritical());
    bm.setUsb(false);
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());  // needs new readings
    TEST_ASSERT_TRUE(bm.isCritical());            // level is still tracked
    feed(bm, 2.9f, 3);
    TEST_ASSERT_TRUE(bm.isShutdownConfirmed());
}

void test_battery_restore_needs_fresh_reading(void) {
    BatteryMonitor a;
    feed(a, 2.9f, 3);
    BatteryPersist p = a.persist();

    BatteryMonitor b;
    b.restore(p);
    TEST_ASSERT_TRUE(b.isCritical());
    TEST_ASSERT_EQUAL_UINT8(3, b.getCriticalCount());
    TEST_ASSERT_FALSE(b.isShutdownConfirmed());  // nothing measured this boot
    feed(b, 2.9f, 1);
    TEST_ASSERT_TRUE(b.isShutdownConfirmed());
}

void test_battery_count_accumulates_across_sleeps(void) {
    BatteryMonitor a;
    feed(a, 2.9f, 1);
    BatteryMonitor b;
    b.restore(a.persist());
    feed(b, 2.9f, 1);
    BatteryMonitor c;
    c.restore(b.persist());
    TEST_ASSERT_FALSE(c.isShutdownConfirmed());
    feed(c, 2.9f, 1);
    TEST_ASSERT_TRUE(c.isShutdownConfirmed());
}

void test_battery_restore_garbage_level(void) {
    BatteryPersist p;
    p.level = 77;
    p.validCount = 9;
    p.criticalCount = 0;
    BatteryMonitor bm;
    bm.restore(p);
    TEST_ASSERT_FALSE(bm.isKnown());
    TEST_ASSERT_FALSE(bm.isShutdownConfirmed());
}

void test_battery_state_change_reported(void) {
    BatteryMonitor bm;
    TEST_ASSERT_FALSE(bm.onReading(3.8f, false));
    TEST_ASSERT_FALSE(bm.onReading(3.8f, false));
    TEST_ASSERT_TRUE(bm.onReading(3.8f, false));   // Unknown -> Normal
    TEST_ASSERT_FALSE(bm.onReading(3.8f, false));
    TEST_ASSERT_TRUE(bm.onReading(3.2f, false));   // -> Low
    TEST_ASSERT_TRUE(bm.setUsb(true));             // -> USB
    TEST_ASSERT_FALSE(bm.setUsb(true));
}

// =============================================================================================
// Recalibration scheduler (ported from test_pir_recal + new wall-clock cases)
// =============================================================================================

void test_window_helper(void) {
    TEST_ASSERT_TRUE(recalHourInWindow(2, 4, 2));
    TEST_ASSERT_TRUE(recalHourInWindow(2, 4, 3));
    TEST_ASSERT_FALSE(recalHourInWindow(2, 4, 4));
    TEST_ASSERT_FALSE(recalHourInWindow(2, 4, 1));
    TEST_ASSERT_TRUE(recalHourInWindow(23, 2, 23));
    TEST_ASSERT_TRUE(recalHourInWindow(23, 2, 0));
    TEST_ASSERT_FALSE(recalHourInWindow(23, 2, 2));
    TEST_ASSERT_FALSE(recalHourInWindow(3, 3, 3));
}

void test_scheduler_no_trigger_without_time(void) {
    RecalScheduler s;
    WallTime t = at(0, 3, 30);
    t.valid = false;
    TEST_ASSERT_EQUAL(RECAL_NO_TIME, s.evaluate(t, false));
}

void test_scheduler_no_trigger_outside_window(void) {
    RecalScheduler s;
    TEST_ASSERT_EQUAL(RECAL_OUTSIDE_WINDOW, s.evaluate(at(0, 10, 0), false));
    TEST_ASSERT_EQUAL(RECAL_OUTSIDE_WINDOW, s.evaluate(at(0, 1, 59), false));
    TEST_ASSERT_EQUAL(RECAL_OUTSIDE_WINDOW, s.evaluate(at(0, 4, 0), false));
}

void test_scheduler_no_trigger_recent_motion(void) {
    RecalScheduler s;
    WallTime t = at(0, 3, 30);
    s.noteMotion(t.epoch - 1800);  // 30 min ago
    TEST_ASSERT_EQUAL(RECAL_RECENT_MOTION, s.evaluate(t, false));
}

void test_scheduler_quiescence_boundary(void) {
    RecalScheduler s;
    WallTime t = at(0, 3, 30);
    s.noteMotion(t.epoch - 3599);
    TEST_ASSERT_EQUAL(RECAL_RECENT_MOTION, s.evaluate(t, false));
    s.setLastMotionEpoch(t.epoch - 3600);
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(t, false));
}

void test_scheduler_future_motion_is_recent(void) {
    RecalScheduler s;
    WallTime t = at(0, 3, 30);
    s.noteMotion(t.epoch + 600);
    TEST_ASSERT_EQUAL(RECAL_RECENT_MOTION, s.evaluate(t, false));
}

void test_scheduler_note_motion_keeps_latest(void) {
    RecalScheduler s;
    s.noteMotion(1000);
    s.noteMotion(500);
    s.noteMotion(0);
    TEST_ASSERT_EQUAL_INT64(1000, s.getLastMotionEpoch());
}

void test_scheduler_triggers_in_window(void) {
    RecalScheduler s;  // no motion known: satisfies quiescence (old lastMotionMs == 0)
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(at(0, 3, 30), false));
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(at(0, 2, 0), false));
}

void test_scheduler_no_trigger_while_recalibrating(void) {
    RecalScheduler s;
    TEST_ASSERT_EQUAL(RECAL_BUSY, s.evaluate(at(0, 3, 30), true));
}

void test_scheduler_cooldown(void) {
    // Old test: re-trigger blocked, then allowed after 2 h. With a 2-hour window that never
    // happens in the same night, so widen the window to exercise the cooldown itself.
    RecalScheduler s;
    RecalConfig c;
    c.windowStartHour = 1;
    c.windowEndHour = 6;
    s.setConfig(c);
    WallTime t = at(0, 1, 0);
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(t, false));
    s.markRecalibrated(t.epoch);
    TEST_ASSERT_EQUAL(RECAL_COOLDOWN, s.evaluate(at(0, 1, 1), false));
    TEST_ASSERT_EQUAL(RECAL_COOLDOWN, s.evaluate(at(0, 2, 59, 59), false));
    // 2 h later: cooldown over, but still the same night.
    TEST_ASSERT_EQUAL(RECAL_DONE_TONIGHT, s.evaluate(at(0, 3, 0, 1), false));
}

void test_scheduler_once_per_night(void) {
    RecalScheduler s;
    WallTime t = at(0, 2, 5);
    s.markRecalibrated(t.epoch);
    TEST_ASSERT_EQUAL(RECAL_COOLDOWN, s.evaluate(at(0, 3, 59), false));
    TEST_ASSERT_EQUAL(RECAL_OUTSIDE_WINDOW, s.evaluate(at(0, 12, 0), false));
    // Next night.
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(at(1, 2, 0), false));
}

void test_scheduler_once_per_night_wrapping_window(void) {
    RecalScheduler s;
    RecalConfig c;
    c.windowStartHour = 23;
    c.windowEndHour = 5;
    c.cooldownS = 600;
    s.setConfig(c);
    WallTime t = at(0, 23, 30);
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(t, false));
    s.markRecalibrated(t.epoch);
    // Past midnight, same window instance.
    TEST_ASSERT_EQUAL(RECAL_DONE_TONIGHT, s.evaluate(at(1, 1, 0), false));
    TEST_ASSERT_EQUAL(RECAL_DONE_TONIGHT, s.evaluate(at(1, 4, 59), false));
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(at(1, 23, 0), false));
}

void test_scheduler_bogus_future_recal_ignored(void) {
    RecalScheduler s;
    WallTime t = at(0, 3, 0);
    s.setLastRecalEpoch(t.epoch + 10 * 86400);  // clock fault
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(t, false));
    s.setLastRecalEpoch(t.epoch + 60);  // small skew: treated as cooldown
    TEST_ASSERT_EQUAL(RECAL_COOLDOWN, s.evaluate(t, false));
}

void test_scheduler_previous_night_recal_allows(void) {
    RecalScheduler s;
    s.setLastRecalEpoch(at(-1, 3, 50).epoch);
    TEST_ASSERT_EQUAL(RECAL_GO, s.evaluate(at(0, 2, 0), false));
}

// =============================================================================================
// Recalibration sequencer (ported from HAL_PIR recalibrate tests)
// =============================================================================================

void test_recal_no_power_pin(void) {
    RecalSequencer r(&mockClock);
    TEST_ASSERT_FALSE(r.start());
    TEST_ASSERT_FALSE(r.isActive());
    TEST_ASSERT_EQUAL(0, g_railWrites);
}

void test_recal_initiate(void) {
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_TRUE(r.isActive());
    TEST_ASSERT_EQUAL(RECAL_POWER_OFF, r.getPhase());
    TEST_ASSERT_EQUAL(0, g_railLevel);
}

void test_recal_power_off_phase(void) {
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    r.start();
    g_now += RecalSequencer::DEFAULT_POWER_OFF_MS - 1;
    TEST_ASSERT_FALSE(r.update());
    TEST_ASSERT_EQUAL(0, g_railLevel);
    TEST_ASSERT_EQUAL(RECAL_POWER_OFF, r.getPhase());
}

void test_recal_power_restore_and_warmup(void) {
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    r.start();
    g_now += RecalSequencer::DEFAULT_POWER_OFF_MS;
    TEST_ASSERT_TRUE(r.update());
    TEST_ASSERT_EQUAL(1, g_railLevel);
    TEST_ASSERT_EQUAL(RECAL_WARMUP, r.getPhase());
    TEST_ASSERT_TRUE(r.isActive());  // PIR edges still untrustworthy
    g_now += RecalSequencer::DEFAULT_WARMUP_MS - 1;
    TEST_ASSERT_FALSE(r.update());
    TEST_ASSERT_TRUE(r.isActive());
    g_now += 1;
    TEST_ASSERT_TRUE(r.update());
    TEST_ASSERT_FALSE(r.isActive());
    TEST_ASSERT_EQUAL(1, g_railLevel);
    TEST_ASSERT_EQUAL(2, g_railWrites);
}

void test_recal_idempotent(void) {
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    r.start();
    g_now += 500;
    TEST_ASSERT_TRUE(r.start());  // already in progress: not an error, no restart
    TEST_ASSERT_EQUAL(1, g_railWrites);
    g_now += RecalSequencer::DEFAULT_POWER_OFF_MS - 500;
    TEST_ASSERT_TRUE(r.update());  // timing counted from the first start
}

void test_recal_custom_timing_and_wrap(void) {
    g_now = 0xFFFFFF00u;  // millis() wrap during the sequence
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    r.setTiming(1000, 5000);
    r.start();
    g_now += 1000;
    TEST_ASSERT_TRUE(r.update());
    g_now += 5000;
    TEST_ASSERT_TRUE(r.update());
    TEST_ASSERT_FALSE(r.isActive());
}

void test_recal_restartable(void) {
    RecalSequencer r(&mockClock);
    r.setRail(&mockRail, nullptr);
    r.setTiming(10, 10);
    r.start();
    g_now += 10;
    r.update();
    g_now += 10;
    r.update();
    TEST_ASSERT_FALSE(r.isActive());
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL(0, g_railLevel);
}


// ===========================================================================
// Dormant mode
// ===========================================================================

static const WakeCause ALL_CAUSES[] = {
    WAKE_POWER_ON, WAKE_SOFTWARE_RESET, WAKE_CRASH,        WAKE_PIR,
    WAKE_BUTTON,   WAKE_TIMER,          WAKE_SPURIOUS_GPIO, WAKE_OTHER_SLEEP,
};

static DormantBootAction decide(bool flag, WakeCause cause, bool held, bool usb,
                                uint32_t wakeHoldMs = 0) {
    DormantBootInput in;
    in.flagActive = flag;
    in.kind = dormantBootKindFor(cause);
    in.buttonHeld = held;
    in.usb = usb;
    in.wakeHoldMs = wakeHoldMs;
    return decideDormantBoot(in);
}

void test_dormant_record_roundtrip(void) {
    for (uint8_t r = DORMANT_REASON_NONE; r <= DORMANT_REASON_OTHER; r++) {
        for (int a = 0; a < 2; a++) {
            DormantRecord in(a != 0, r);
            DormantRecord out = DormantRecord::decode(in.encode());
            TEST_ASSERT_EQUAL(in.active, out.active);
            TEST_ASSERT_EQUAL_UINT8(r, out.reason);
        }
    }
}

void test_dormant_record_garbage_is_not_dormant(void) {
    const uint32_t junk[] = {0, 0xFFFFFFFFUL, 0x00000101UL, 0xD0FF0101UL, 0x12345678UL,
                             0xD0000300UL};
    for (uint32_t raw : junk) {
        DormantRecord r = DormantRecord::decode(raw);
        TEST_ASSERT_FALSE(r.active);
        TEST_ASSERT_EQUAL_UINT8(DORMANT_REASON_NONE, r.reason);
    }
    TEST_ASSERT_FALSE(DormantRecord().active);  // never written
}

void test_dormant_reason_names(void) {
    TEST_ASSERT_EQUAL_STRING("None", dormantReasonName(DORMANT_REASON_NONE));
    TEST_ASSERT_EQUAL_STRING("Button hold", dormantReasonName(DORMANT_REASON_GESTURE));
    TEST_ASSERT_EQUAL_STRING("Home Assistant", dormantReasonName(DORMANT_REASON_HOME_ASSISTANT));
    TEST_ASSERT_EQUAL_STRING("Critical battery",
                             dormantReasonName(DORMANT_REASON_CRITICAL_BATTERY));
    TEST_ASSERT_EQUAL_STRING("Other", dormantReasonName(DORMANT_REASON_OTHER));
    TEST_ASSERT_EQUAL_STRING("Other", dormantReasonName(200));
}

void test_dormant_boot_kind_mapping(void) {
    TEST_ASSERT_EQUAL(DORMANT_BOOT_BUTTON_WAKE, dormantBootKindFor(WAKE_BUTTON));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_OTHER_WAKE, dormantBootKindFor(WAKE_PIR));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_OTHER_WAKE, dormantBootKindFor(WAKE_TIMER));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_OTHER_WAKE, dormantBootKindFor(WAKE_SPURIOUS_GPIO));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_OTHER_WAKE, dormantBootKindFor(WAKE_OTHER_SLEEP));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_RESET, dormantBootKindFor(WAKE_POWER_ON));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_RESET, dormantBootKindFor(WAKE_SOFTWARE_RESET));
    TEST_ASSERT_EQUAL(DORMANT_BOOT_RESET, dormantBootKindFor(WAKE_CRASH));
}

void test_dormant_flag_clear_always_normal(void) {
    for (WakeCause c : ALL_CAUSES) {
        for (int held = 0; held < 2; held++) {
            for (int usb = 0; usb < 2; usb++) {
                TEST_ASSERT_EQUAL(DORMANT_ACTION_NORMAL, decide(false, c, held, usb));
                TEST_ASSERT_EQUAL(DORMANT_ACTION_NORMAL, decide(false, c, held, usb, 1000));
            }
        }
    }
}

/// Full table: every wake cause x button x USB with the flag set.
void test_dormant_boot_decision_table(void) {
    for (WakeCause c : ALL_CAUSES) {
        for (int held = 0; held < 2; held++) {
            for (int usb = 0; usb < 2; usb++) {
                DormantBootAction expected;
                if (c == WAKE_BUTTON) {
                    expected = DORMANT_ACTION_WAKE;  // only the button wakes it
                } else if (c == WAKE_POWER_ON || c == WAKE_SOFTWARE_RESET || c == WAKE_CRASH) {
                    expected = held ? DORMANT_ACTION_WAKE : DORMANT_ACTION_REENTER;
                } else {
                    expected = DORMANT_ACTION_REENTER;  // PIR / timer / spurious / other
                }
                char msg[64];
                snprintf(msg, sizeof(msg), "cause %d held %d usb %d", c, held, usb);
                TEST_ASSERT_EQUAL_MESSAGE(expected, decide(true, c, held, usb), msg);
            }
        }
    }
}

void test_dormant_usb_never_wakes(void) {
    // Plugging USB into a flat battery = power-on reset with the flag set: stay off.
    TEST_ASSERT_EQUAL(DORMANT_ACTION_REENTER, decide(true, WAKE_POWER_ON, false, true));
    TEST_ASSERT_EQUAL(DORMANT_ACTION_REENTER, decide(true, WAKE_CRASH, false, true));  // brownout
    TEST_ASSERT_EQUAL(DORMANT_ACTION_REENTER, decide(true, WAKE_OTHER_SLEEP, false, true));
}

void test_dormant_wake_hold_option(void) {
    TEST_ASSERT_EQUAL(DORMANT_ACTION_VERIFY_HOLD, decide(true, WAKE_BUTTON, true, false, 1000));
    TEST_ASSERT_EQUAL(DORMANT_ACTION_VERIFY_HOLD, decide(true, WAKE_BUTTON, false, true, 1000));
    // Holding at power-on is already deliberate (and feeds the boot-hold resets).
    TEST_ASSERT_EQUAL(DORMANT_ACTION_WAKE, decide(true, WAKE_POWER_ON, true, false, 1000));
    TEST_ASSERT_EQUAL(DORMANT_ACTION_REENTER, decide(true, WAKE_PIR, false, false, 1000));
}

void test_dormant_critical_battery_record(void) {
    // Critical battery uses the same persisted state: flat battery -> charge -> button.
    DormantRecord rec(true, DORMANT_REASON_CRITICAL_BATTERY);
    DormantRecord back = DormantRecord::decode(rec.encode());
    TEST_ASSERT_TRUE(back.active);
    TEST_ASSERT_EQUAL_STRING("Critical battery", dormantReasonName(back.reason));
    TEST_ASSERT_EQUAL(DORMANT_ACTION_REENTER, decide(back.active, WAKE_POWER_ON, false, true));
    TEST_ASSERT_EQUAL(DORMANT_ACTION_WAKE, decide(back.active, WAKE_BUTTON, true, true));
    // After the wake the flag is cleared but the reason kept for "Last Shutdown".
    back.active = false;
    DormantRecord cleared = DormantRecord::decode(back.encode());
    TEST_ASSERT_FALSE(cleared.active);
    TEST_ASSERT_EQUAL_UINT8(DORMANT_REASON_CRITICAL_BATTERY, cleared.reason);
    TEST_ASSERT_EQUAL(DORMANT_ACTION_NORMAL, decide(cleared.active, WAKE_POWER_ON, false, false));
}

void test_wake_hold_confirmed(void) {
    WakeHoldVerifier v(1000);
    v.start(100);
    for (uint32_t t = 100; t < 1100; t += 5) {
        TEST_ASSERT_EQUAL(WakeHoldVerifier::PENDING, v.update(t, true));
    }
    TEST_ASSERT_EQUAL(WakeHoldVerifier::CONFIRMED, v.update(1100, true));
    TEST_ASSERT_EQUAL(WakeHoldVerifier::CONFIRMED, v.update(1200, false));  // latched
}

void test_wake_hold_early_release_rejected(void) {
    WakeHoldVerifier v(1000);
    v.start(0);
    v.update(0, true);
    v.update(300, true);
    TEST_ASSERT_EQUAL(WakeHoldVerifier::PENDING, v.update(310, false));  // debouncing
    TEST_ASSERT_EQUAL(WakeHoldVerifier::REJECTED, v.update(340, false));
}

void test_wake_hold_glitch_tolerated(void) {
    WakeHoldVerifier v(1000, 30);
    v.start(0);
    v.update(500, false);  // 10 ms bounce
    v.update(510, true);
    TEST_ASSERT_EQUAL(WakeHoldVerifier::PENDING, v.update(900, true));
    TEST_ASSERT_EQUAL(WakeHoldVerifier::CONFIRMED, v.update(1000, true));
}

void test_wake_hold_bag_bump(void) {
    // A bump: the button is already released when the check starts.
    WakeHoldVerifier v(1000);
    v.start(0);
    v.update(0, false);
    TEST_ASSERT_EQUAL(WakeHoldVerifier::REJECTED, v.update(30, false));
}

void test_wake_hold_zero_is_single_press(void) {
    WakeHoldVerifier v(0);
    v.start(0);
    TEST_ASSERT_EQUAL(WakeHoldVerifier::CONFIRMED, v.getResult());
    TEST_ASSERT_EQUAL(WakeHoldVerifier::CONFIRMED, v.update(0, false));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_battery_defaults_match_config_h);
    RUN_TEST(test_battery_unknown_until_min_samples);
    RUN_TEST(test_battery_normal);
    RUN_TEST(test_battery_critical_implies_low);
    RUN_TEST(test_battery_low_hysteresis);
    RUN_TEST(test_battery_critical_hysteresis);
    RUN_TEST(test_battery_nan_ignored);
    RUN_TEST(test_battery_implausible_low_ignored);
    RUN_TEST(test_battery_implausible_high_ignored);
    RUN_TEST(test_battery_single_critical_sample_not_confirmed);
    RUN_TEST(test_battery_three_consecutive_confirm);
    RUN_TEST(test_battery_good_reading_resets_count);
    RUN_TEST(test_battery_nan_resets_count);
    RUN_TEST(test_battery_usb_never_critical);
    RUN_TEST(test_battery_usb_plug_resets_count);
    RUN_TEST(test_battery_restore_needs_fresh_reading);
    RUN_TEST(test_battery_count_accumulates_across_sleeps);
    RUN_TEST(test_battery_restore_garbage_level);
    RUN_TEST(test_battery_state_change_reported);

    RUN_TEST(test_window_helper);
    RUN_TEST(test_scheduler_no_trigger_without_time);
    RUN_TEST(test_scheduler_no_trigger_outside_window);
    RUN_TEST(test_scheduler_no_trigger_recent_motion);
    RUN_TEST(test_scheduler_quiescence_boundary);
    RUN_TEST(test_scheduler_future_motion_is_recent);
    RUN_TEST(test_scheduler_note_motion_keeps_latest);
    RUN_TEST(test_scheduler_triggers_in_window);
    RUN_TEST(test_scheduler_no_trigger_while_recalibrating);
    RUN_TEST(test_scheduler_cooldown);
    RUN_TEST(test_scheduler_once_per_night);
    RUN_TEST(test_scheduler_once_per_night_wrapping_window);
    RUN_TEST(test_scheduler_bogus_future_recal_ignored);
    RUN_TEST(test_scheduler_previous_night_recal_allows);

    RUN_TEST(test_recal_no_power_pin);
    RUN_TEST(test_recal_initiate);
    RUN_TEST(test_recal_power_off_phase);
    RUN_TEST(test_recal_power_restore_and_warmup);
    RUN_TEST(test_recal_idempotent);
    RUN_TEST(test_recal_custom_timing_and_wrap);
    RUN_TEST(test_recal_restartable);

    RUN_TEST(test_dormant_record_roundtrip);
    RUN_TEST(test_dormant_record_garbage_is_not_dormant);
    RUN_TEST(test_dormant_reason_names);
    RUN_TEST(test_dormant_boot_kind_mapping);
    RUN_TEST(test_dormant_flag_clear_always_normal);
    RUN_TEST(test_dormant_boot_decision_table);
    RUN_TEST(test_dormant_usb_never_wakes);
    RUN_TEST(test_dormant_wake_hold_option);
    RUN_TEST(test_dormant_critical_battery_record);
    RUN_TEST(test_wake_hold_confirmed);
    RUN_TEST(test_wake_hold_early_release_rejected);
    RUN_TEST(test_wake_hold_glitch_tolerated);
    RUN_TEST(test_wake_hold_bag_bump);
    RUN_TEST(test_wake_hold_zero_is_single_press);

    return UNITY_END();
}
