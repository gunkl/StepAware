/**
 * @file test_pir_recal.cpp
 * @brief Unit tests for PIR power-cycle recalibration and RecalScheduler.
 *
 * Tests the HAL_PIR::recalibrate() state machine and the smart nightly
 * scheduler that triggers automatic recalibration during quiet overnight hours.
 */

#ifdef UNIT_TEST

// =========================================================================
// Mock Arduino shims (must be defined before any project headers)
// =========================================================================

#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

static unsigned long mock_millis_value = 0;
static unsigned long mock_micros_value = 0;
static uint8_t mock_pin_states[32] = {};  // Track output pin states

unsigned long millis() { return mock_millis_value; }
unsigned long micros() { return mock_micros_value; }

void advance_time(unsigned long ms) {
    mock_millis_value += ms;
    mock_micros_value += ms * 1000;
}

void reset_time() {
    mock_millis_value = 0;
    mock_micros_value = 0;
}

void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }

int digitalRead(uint8_t pin) {
    if (pin < 32) return mock_pin_states[pin];
    return 0;
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin < 32) mock_pin_states[pin] = value;
}

void delay(unsigned long ms) { advance_time(ms); }

struct MockSerial {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char*, ...) {}
} Serial;

// =========================================================================
// Stub out debug logger macros before including project headers
// =========================================================================

#define DEBUG_LOG_SENSOR(fmt, ...)
#define DEBUG_LOG_SYSTEM(fmt, ...)
#define DEBUG_LOG_BOOT(fmt, ...)
#define DEBUG_LOG_CONFIG(fmt, ...)
#define DEBUG_LOG_API(fmt, ...)
#define DEBUG_LOG_LED(fmt, ...)
#define DEBUG_LOG_WIFI(fmt, ...)
#define DEBUG_LOG_STATE(fmt, ...)

// g_logger is defined after the shadow logger.h is included (see below)

#endif // UNIT_TEST

// =========================================================================
// Include project headers and implementation
// =========================================================================

#include <unity.h>
#include "config.h"
#include "sensor_types.h"
#include "hal_motion_sensor.h"
#include "hal_pir.h"
#include "recal_scheduler.h"

// Include implementation files directly (test_build_src = no)
#include "../../src/hal_pir.cpp"
#include "../../src/recal_scheduler.cpp"

// Satisfy the extern Logger g_logger declared in the shadow logger.h
Logger g_logger;

// =========================================================================
// Test fixtures
// =========================================================================

static HAL_PIR* pirSensor = nullptr;

static void reset_pin_states() {
    memset(mock_pin_states, 0, sizeof(mock_pin_states));
}

void setUp(void) {
    reset_time();
    reset_pin_states();
    pirSensor = new HAL_PIR(1, true);  // Pin 1, mock mode
}

void tearDown(void) {
    delete pirSensor;
    pirSensor = nullptr;
}

// =========================================================================
// HAL_PIR Recalibration Tests
// =========================================================================

void test_recal_no_power_pin(void) {
    // No power pin assigned — recalibrate should fail
    pirSensor->begin();
    pirSensor->mockSetReady();
    TEST_ASSERT_TRUE(pirSensor->isReady());

    bool result = pirSensor->recalibrate();
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());
    TEST_ASSERT_TRUE(pirSensor->isReady());  // Unchanged
}

void test_recal_initiate(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();
    TEST_ASSERT_TRUE(pirSensor->isReady());

    bool result = pirSensor->recalibrate();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());
    TEST_ASSERT_FALSE(pirSensor->isReady());  // Cleared on initiate
}

void test_recal_power_off_phase(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();
    pirSensor->recalibrate();

    // Advance time but stay within power-off window
    advance_time(PIR_RECAL_POWER_OFF_MS - 1);
    pirSensor->update();

    // Still recalibrating — power-off phase not complete
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());
    TEST_ASSERT_FALSE(pirSensor->isReady());
}

void test_recal_power_restore_and_warmup_restart(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();
    pirSensor->recalibrate();

    // Complete power-off phase
    advance_time(PIR_RECAL_POWER_OFF_MS);
    pirSensor->update();

    // Power restored — recal flag cleared, warm-up restarted
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());
    TEST_ASSERT_FALSE(pirSensor->isReady());  // Warm-up just restarted

    // Advance through warm-up (minus 1ms)
    advance_time(PIR_WARMUP_TIME_MS - 1);
    pirSensor->update();
    TEST_ASSERT_FALSE(pirSensor->isReady());

    // Complete warm-up
    advance_time(1);
    pirSensor->update();
    TEST_ASSERT_TRUE(pirSensor->isReady());
}

void test_recal_idempotent(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();

    TEST_ASSERT_TRUE(pirSensor->recalibrate());
    // Call again while already recalibrating
    TEST_ASSERT_TRUE(pirSensor->recalibrate());  // Returns true (already in progress)
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());

    // Only one cycle runs — complete it normally
    advance_time(PIR_RECAL_POWER_OFF_MS);
    pirSensor->update();
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());
}

void test_recal_motion_cleared_after_restore(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();

    // Simulate motion before recal
    pirSensor->mockSetMotion(true);
    pirSensor->update();
    TEST_ASSERT_TRUE(pirSensor->motionDetected());

    // Trigger recal
    pirSensor->recalibrate();

    // Complete power-off phase
    advance_time(PIR_RECAL_POWER_OFF_MS);
    pirSensor->update();

    // Motion state cleared after power restore
    TEST_ASSERT_FALSE(pirSensor->motionDetected());
}

void test_recal_sensor_unreadable_during_power_off(void) {
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();
    pirSensor->recalibrate();

    // During power-off phase, update() returns early — event count unchanged
    uint32_t eventsBefore = pirSensor->getEventCount();
    pirSensor->mockSetMotion(true);  // Try to inject motion
    advance_time(100);
    pirSensor->update();  // Should return early (power off)

    TEST_ASSERT_EQUAL_UINT32(eventsBefore, pirSensor->getEventCount());
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());  // Still in power-off
}

// =========================================================================
// RecalScheduler Tests
// =========================================================================

// Mock time source for scheduler
static time_t mock_scheduler_time = 0;
static time_t mockTimeFunc() { return mock_scheduler_time; }

static RecalScheduler* scheduler = nullptr;

void setUp_scheduler(void) {
    reset_time();
    reset_pin_states();
    pirSensor = new HAL_PIR(1, true);
    pirSensor->setPowerPin(20);
    pirSensor->begin();
    pirSensor->mockSetReady();

    // Force UTC timezone for deterministic time tests
    static char tz_env[] = "TZ=UTC";
    putenv(tz_env);
    tzset();

    scheduler = new RecalScheduler(pirSensor);
    RecalScheduler::s_timeFunc = mockTimeFunc;
    scheduler->begin();
}

void tearDown_scheduler(void) {
    delete scheduler;
    scheduler = nullptr;
    delete pirSensor;
    pirSensor = nullptr;
    // Restore default time function
    RecalScheduler::s_timeFunc = nullptr;  // Will be reset by next begin()
}

void test_scheduler_no_trigger_without_ntp(void) {
    setUp_scheduler();

    // 3:30 AM UTC epoch (2024-01-15 03:30:00 UTC)
    mock_scheduler_time = 1705289400;
    // No motion ever (lastMotionMs = 0 satisfies quiescence)
    scheduler->update(false, 0);  // ntpSynced = false

    TEST_ASSERT_FALSE(scheduler->wasTriggered());
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());

    tearDown_scheduler();
}

void test_scheduler_no_trigger_outside_window(void) {
    setUp_scheduler();

    // 10:00 AM UTC epoch (2024-01-15 10:00:00 UTC)
    mock_scheduler_time = 1705312800;
    scheduler->update(true, 0);

    TEST_ASSERT_FALSE(scheduler->wasTriggered());
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());

    tearDown_scheduler();
}

void test_scheduler_no_trigger_recent_motion(void) {
    setUp_scheduler();

    // 3:30 AM UTC
    mock_scheduler_time = 1705289400;
    // Motion was 30 minutes ago (less than 1 hour quiescence)
    uint32_t lastMotion = mock_millis_value - 1800000;  // 30 min ago
    scheduler->update(true, lastMotion);

    TEST_ASSERT_FALSE(scheduler->wasTriggered());
    TEST_ASSERT_FALSE(pirSensor->isRecalibrating());

    tearDown_scheduler();
}

void test_scheduler_triggers_in_window(void) {
    setUp_scheduler();

    // 3:30 AM UTC (within 2-4 AM window; epoch 1705289400 = 2024-01-15 03:30:00 UTC)
    mock_scheduler_time = 1705289400;
    // No motion (lastMotionMs = 0)
    scheduler->update(true, 0);

    TEST_ASSERT_TRUE(scheduler->wasTriggered());
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());

    tearDown_scheduler();
}

void test_scheduler_cooldown_prevents_retriggering(void) {
    setUp_scheduler();

    // First trigger at 3:30 AM UTC
    mock_scheduler_time = 1705289400;
    scheduler->update(true, 0);
    TEST_ASSERT_TRUE(scheduler->wasTriggered());

    // Complete the recal cycle so sensor is no longer recalibrating
    advance_time(PIR_RECAL_POWER_OFF_MS);
    pirSensor->update();  // Power restored
    advance_time(PIR_WARMUP_TIME_MS);
    pirSensor->update();  // Warm-up complete

    // Try to trigger again immediately (still in cooldown)
    // wasTriggered is cleared on next update
    scheduler->update(true, 0);
    TEST_ASSERT_FALSE(scheduler->wasTriggered());

    // Advance past cooldown (2 hours)
    advance_time(7200001);
    scheduler->update(true, 0);
    TEST_ASSERT_TRUE(scheduler->wasTriggered());

    tearDown_scheduler();
}

void test_scheduler_no_trigger_while_already_recalibrating(void) {
    setUp_scheduler();

    // Manually start recalibration
    pirSensor->recalibrate();
    TEST_ASSERT_TRUE(pirSensor->isRecalibrating());

    // 3:30 AM UTC — all other conditions met
    mock_scheduler_time = 1705289400;
    scheduler->update(true, 0);

    // Should NOT trigger (already recalibrating)
    TEST_ASSERT_FALSE(scheduler->wasTriggered());

    tearDown_scheduler();
}

// =========================================================================
// Unity main
// =========================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // HAL_PIR recalibration tests
    RUN_TEST(test_recal_no_power_pin);
    RUN_TEST(test_recal_initiate);
    RUN_TEST(test_recal_power_off_phase);
    RUN_TEST(test_recal_power_restore_and_warmup_restart);
    RUN_TEST(test_recal_idempotent);
    RUN_TEST(test_recal_motion_cleared_after_restore);
    RUN_TEST(test_recal_sensor_unreadable_during_power_off);

    // RecalScheduler tests
    RUN_TEST(test_scheduler_no_trigger_without_ntp);
    RUN_TEST(test_scheduler_no_trigger_outside_window);
    RUN_TEST(test_scheduler_no_trigger_recent_motion);
    RUN_TEST(test_scheduler_triggers_in_window);
    RUN_TEST(test_scheduler_cooldown_prevents_retriggering);
    RUN_TEST(test_scheduler_no_trigger_while_already_recalibrating);

    return UNITY_END();
}
