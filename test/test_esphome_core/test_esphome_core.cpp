/**
 * @file test_esphome_core.cpp
 * @brief Native tests for the ESPHome stepaware core (esphome/components/stepaware/core).
 *
 * Cases are ported from test/test_state_machine, test/test_hal_button,
 * test/test_state_transitions.py and test/test_logic.py, but target the real core
 * classes instead of local mocks. Where the old mock deviated from the firmware
 * (e.g. warning surviving a mode change), the firmware behaviour is asserted.
 */
#include <unity.h>
#include <stdint.h>

#include "../../esphome/components/stepaware/core/step_button.h"
#include "../../esphome/components/stepaware/core/step_direction_detector.h"
#include "../../esphome/components/stepaware/core/step_led_pattern.h"
#include "../../esphome/components/stepaware/core/step_state_machine.h"

using namespace step_logic;

// ---------------------------------------------------------------------------
// Mock clock
// ---------------------------------------------------------------------------
static uint32_t g_now = 0;
static uint32_t mockClock() { return g_now; }
static void advance(uint32_t ms) { g_now += ms; }

// ---------------------------------------------------------------------------
// Mock matrix + listener
// ---------------------------------------------------------------------------
class MockMatrix : public StepMatrixDisplay {
public:
    enum Anim { NONE, ALERT, BATTERY_LOW, WIFI_EMBLEM };
    bool animating;
    bool ready;
    Anim anim;
    uint32_t animStart;
    uint32_t lastDuration;
    uint32_t alertStarts;
    uint32_t stops;
    uint32_t clears;
    uint32_t batteryLowStarts;
    uint32_t emblemStarts;
    int wifiShown;       // -1 never, 0 disconnected, 1 connected
    int batteryShown;    // -1 never, else percent
    const uint8_t* lastBitmap;
    uint8_t frame[8];

    MockMatrix()
        : animating(false), ready(true), anim(NONE), animStart(0), lastDuration(0),
          alertStarts(0), stops(0), clears(0), batteryLowStarts(0), emblemStarts(0),
          wifiShown(-1), batteryShown(-1), lastBitmap(nullptr) {
        blank();
    }
    void blank() {
        for (int i = 0; i < 8; i++) frame[i] = 0;
    }
    void startAnim(Anim a, uint32_t durationMs) {
        animating = true;
        anim = a;
        animStart = g_now;
        lastDuration = durationMs;
    }
    /// Like StepAwareMatrix::loop(): timed animations stop (and blank) on their own.
    void update() {
        if (animating && lastDuration > 0 && g_now - animStart >= lastDuration) {
            stopAnimation();
        }
    }
    void startMotionAlert(uint32_t durationMs) override {
        startAnim(ALERT, durationMs);
        alertStarts++;
    }
    void stopAnimation() override {
        if (animating) {
            blank();  // StepAwareMatrix::stopAnimation() clears
        }
        animating = false;
        anim = NONE;
        stops++;
    }
    void clear() override {
        blank();
        clears++;
    }
    bool isAnimating() const override { return animating; }
    bool isReady() const override { return ready; }
    void drawBitmap(const uint8_t* bitmap) override {
        lastBitmap = bitmap;
        for (int i = 0; i < 8; i++) frame[i] = bitmap[i];
    }
    void setPixel(uint8_t x, uint8_t y, bool on) override {
        uint8_t mask = static_cast<uint8_t>(0x80 >> x);
        if (on) frame[y] |= mask; else frame[y] &= static_cast<uint8_t>(~mask);
    }
    void showWifiStatus(bool connected) override {
        wifiShown = connected ? 1 : 0;
        lastBitmap = nullptr;
    }
    void showBatteryLevel(uint8_t percent) override {
        batteryShown = percent;
        lastBitmap = nullptr;
    }
    void startBatteryLow(uint32_t durationMs) override {
        startAnim(BATTERY_LOW, durationMs);
        batteryLowStarts++;
    }
    void startWifiEmblem(uint32_t durationMs) override {
        startAnim(WIFI_EMBLEM, durationMs);
        emblemStarts++;
    }
    bool pixel(uint8_t x, uint8_t y) const { return (frame[y] & (0x80 >> x)) != 0; }
};

static MockMatrix* g_matrix = nullptr;

class MockListener : public StepStateMachineListener {
public:
    uint32_t modeChanges;
    uint8_t lastNewMode;
    uint32_t warningOn;
    uint32_t warningOff;
    uint32_t rebootRequested;
    uint32_t rebootDue;
    uint32_t dormantRequested;
    MockListener()
        : modeChanges(0), lastNewMode(0), warningOn(0), warningOff(0),
          rebootRequested(0), rebootDue(0), dormantRequested(0) {}
    void onModeChanged(uint8_t, uint8_t newMode) override {
        modeChanges++;
        lastNewMode = newMode;
    }
    void onWarningChanged(bool active) override {
        if (active) warningOn++; else warningOff++;
    }
    void onRebootRequested() override { rebootRequested++; }
    void onRebootDue() override { rebootDue++; }
    void onDormantRequested() override { dormantRequested++; }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
static StepLedPattern* led = nullptr;
static StepButton* button = nullptr;
static StepDirectionDetector* dir = nullptr;
static StepStateMachine* sm = nullptr;
static MockListener* listener = nullptr;

static bool g_near = false;
static bool g_far = false;

void setUp(void) {
    g_now = 1000;  // non-zero like millis() after boot
    g_near = false;
    g_far = false;
    led = new StepLedPattern(mockClock);
    button = new StepButton(mockClock, 50, 1000);
    dir = new StepDirectionDetector(mockClock);
    sm = new StepStateMachine(mockClock);
    listener = new MockListener();
    sm->setHazardLed(led);
    sm->setButton(button);
    sm->setListener(listener);
    sm->setDirectionFilterEnabled(false);
}

void tearDown(void) {
    delete sm;
    delete dir;
    delete button;
    delete led;
    delete listener;
    sm = nullptr;
    dir = nullptr;
    button = nullptr;
    led = nullptr;
    listener = nullptr;
    g_matrix = nullptr;
}

/// One main-loop iteration: detector, then state machine with fused motion.
static void tick() {
    if (g_matrix) {
        g_matrix->update();  // main.cpp: ledMatrix->update() before the state machine
    }
    dir->update(g_far, g_near);
    sm->setSensorStates(g_near, g_far);
    sm->update(g_near || g_far);
}

static void attachMatrix(MockMatrix* matrix) {
    g_matrix = matrix;
    sm->setMatrix(matrix);
}

static void tickFor(uint32_t ms, uint32_t step = 10) {
    for (uint32_t t = 0; t < ms; t += step) {
        advance(step);
        tick();
    }
}

static void finishWarmup() {
    advance(PIR_WARMUP_TIME_MS);
    tick();
    TEST_ASSERT_TRUE(sm->isSensorReady());
}

static void motionPulse(bool* sensor) {
    *sensor = true;
    tick();
    *sensor = false;
    tick();
}

/// Press and let the 50 ms debounce confirm it (press time = now afterwards).
static void pressButton() {
    button->setRawState(true);
    tick();
    advance(50);
    tick();
}

/// Release (release starts now) and let the debounce confirm it.
static void releaseButton() {
    button->setRawState(false);
    tick();
    advance(50);
    tick();
}

/// Hold for exactly ms after the press is confirmed, then release.
static void holdAndRelease(uint32_t ms) {
    pressButton();
    tickFor(ms);
    releaseButton();
}

static void clickButton() {
    button->setRawState(true);
    tick();
    advance(60);
    tick();
    button->setRawState(false);
    tick();
    advance(60);
    tick();
}

// ===========================================================================
// State machine
// ===========================================================================

void test_sm_initialization(void) {
    TEST_ASSERT_FALSE(sm->isInitialized());
    TEST_ASSERT_TRUE(sm->begin(StepStateMachine::OFF));
    TEST_ASSERT_EQUAL(StepStateMachine::OFF, sm->getMode());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getModeChangeCount());
    TEST_ASSERT_FALSE(led->isOn());
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_sm_begin_default_mode_counts_change(void) {
    sm->begin();  // MOTION_DETECT (ConfigManager defaultMode = 2)
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());
    TEST_ASSERT_EQUAL_UINT32(1, listener->modeChanges);
}

void test_sm_mode_cycling(void) {
    sm->begin(StepStateMachine::OFF);

    sm->cycleMode();
    TEST_ASSERT_EQUAL(StepStateMachine::CONTINUOUS_ON, sm->getMode());
    TEST_ASSERT_TRUE(led->isOn());
    TEST_ASSERT_EQUAL_UINT8(255, led->getLevel());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());

    sm->cycleMode();
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());
    TEST_ASSERT_FALSE(led->isOn());
    TEST_ASSERT_EQUAL_UINT32(2, sm->getModeChangeCount());

    sm->cycleMode();
    TEST_ASSERT_EQUAL(StepStateMachine::OFF, sm->getMode());
    TEST_ASSERT_FALSE(led->isOn());
    TEST_ASSERT_EQUAL_UINT32(3, sm->getModeChangeCount());
    TEST_ASSERT_EQUAL_UINT32(3, listener->modeChanges);
    TEST_ASSERT_EQUAL_UINT8(StepStateMachine::OFF, listener->lastNewMode);
}

void test_sm_set_mode_same_mode_is_noop(void) {
    sm->begin(StepStateMachine::OFF);
    sm->setMode(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());
    sm->setMode(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());
    sm->setMode(StepStateMachine::MOTION_DETECT);
    TEST_ASSERT_EQUAL_UINT32(2, sm->getModeChangeCount());
}

void test_sm_motion_triggers_warning_in_motion_mode(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    TEST_ASSERT_FALSE(sm->isWarningActive());

    g_near = true;
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_TRUE(led->isOn());  // blink starts in the ON phase
    TEST_ASSERT_EQUAL_UINT32(1, listener->warningOn);
}

void test_sm_motion_ignored_during_warmup(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    advance(PIR_WARMUP_TIME_MS - 100);
    g_near = true;
    tick();
    TEST_ASSERT_FALSE(sm->isSensorReady());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());  // counted
    TEST_ASSERT_FALSE(sm->isWarningActive());               // but no warning

    // Still high when warm-up completes: no new edge, no warning (as firmware)
    advance(200);
    tick();
    TEST_ASSERT_TRUE(sm->isSensorReady());
    TEST_ASSERT_FALSE(sm->isWarningActive());
}

void test_sm_motion_in_off_mode_no_warning(void) {
    sm->begin(StepStateMachine::OFF);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(led->isOn());
    // Firmware counts motion events in every mode
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());
}

void test_sm_motion_in_continuous_mode_no_warning(void) {
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    finishWarmup();
    motionPulse(&g_far);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_TRUE(led->isPatternActive());
}

void test_sm_continuous_led_blinks(void) {
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_TRUE(led->isOn());
    advance(500);
    tick();
    TEST_ASSERT_FALSE(led->isOn());
    advance(500);
    tick();
    TEST_ASSERT_TRUE(led->isOn());
}

void test_sm_warning_timeout(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    g_near = true;
    tick();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    advance(MOTION_WARNING_DURATION_MS - 1);
    tick();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    advance(1);
    tick();
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(led->isOn());
    TEST_ASSERT_EQUAL_UINT32(1, listener->warningOff);
}

void test_sm_configured_warning_duration(void) {
    sm->setMotionWarningDurationMs(15000);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    g_near = true;
    tick();
    advance(14000);
    tick();
    TEST_ASSERT_TRUE(sm->isWarningActive());
    advance(1000);
    tick();
    TEST_ASSERT_FALSE(sm->isWarningActive());
}

void test_sm_retrigger_restarts_warning(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());

    advance(4000);
    motionPulse(&g_near);
    TEST_ASSERT_EQUAL_UINT32(2, sm->getMotionEventCount());

    advance(4000);
    tick();
    TEST_ASSERT_TRUE(sm->isWarningActive());  // 8 s after first, 4 s after second
    TEST_ASSERT_EQUAL_UINT32(1, listener->warningOn);

    advance(1000);
    tick();
    TEST_ASSERT_FALSE(sm->isWarningActive());
}

void test_sm_mode_change_during_warning_stops_it(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(sm->isWarningActive());

    sm->setMode(StepStateMachine::OFF);  // exitMode() -> stopWarning()
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(led->isOn());
}

void test_sm_button_click_cycles_mode(void) {
    sm->begin(StepStateMachine::OFF);
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::CONTINUOUS_ON, sm->getMode());
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::OFF, sm->getMode());
    TEST_ASSERT_FALSE(sm->isRebootPending());
}

void test_sm_long_press_requests_reboot(void) {
    // Hold gestures act on release: 1 s .. < dormant threshold = reboot.
    sm->begin(StepStateMachine::MOTION_DETECT);
    TEST_ASSERT_FALSE(sm->isRebootPending());

    pressButton();
    TEST_ASSERT_TRUE(sm->isBusy());  // button held
    tickFor(1000);                   // long press: hold display only
    TEST_ASSERT_TRUE(sm->isHoldActive());
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(0, listener->rebootRequested);
    TEST_ASSERT_EQUAL(StepStateMachine::HOLD_REBOOT, sm->getHoldAction());
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());  // no mode change

    tickFor(1000);    // 2 s
    releaseButton();  // release = reboot
    TEST_ASSERT_FALSE(sm->isHoldActive());
    TEST_ASSERT_TRUE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(1, listener->rebootRequested);
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());  // no click
    TEST_ASSERT_EQUAL_UINT32(0, listener->rebootDue);

    advance(REBOOT_FEEDBACK_DURATION_MS);
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, listener->rebootDue);
    TEST_ASSERT_FALSE(sm->isRebootPending());
}

void test_sm_short_presses_never_reboot(void) {
    sm->begin(StepStateMachine::OFF);
    for (int i = 0; i < 6; i++) {
        clickButton();
        TEST_ASSERT_FALSE(sm->isRebootPending());
    }
    TEST_ASSERT_EQUAL(StepStateMachine::OFF, sm->getMode());
}

void test_sm_direction_filter_requires_approach(void) {
    sm->setDirectionDetector(dir);
    sm->setDirectionFilterEnabled(true);
    dir->begin(false, false);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();

    // Near only: hand wave -> no warning
    motionPulse(&g_near);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());

    // Far then near (1 s later) -> approaching -> warning
    advance(1000);
    g_far = true;
    tick();
    TEST_ASSERT_FALSE(sm->isWarningActive());
    advance(1000);
    g_near = true;
    tick();
    TEST_ASSERT_TRUE(dir->isApproaching());
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());
}

void test_sm_direction_filter_disabled_any_pir_triggers(void) {
    sm->setDirectionDetector(dir);
    sm->setDirectionFilterEnabled(false);
    dir->begin(false, false);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(sm->isWarningActive());
}

void test_sm_toggling_filter_no_spurious_edge(void) {
    sm->setDirectionDetector(dir);
    sm->setDirectionFilterEnabled(true);
    dir->begin(false, false);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    g_near = true;  // near held, filtered out
    tick();
    sm->setDirectionFilterEnabled(false);
    tick();
    TEST_ASSERT_FALSE(sm->isWarningActive());
}

void test_sm_motion_wake_before_begin(void) {
    sm->handleMotionWake();
    TEST_ASSERT_TRUE(sm->isBusy());  // pending
    sm->begin(StepStateMachine::MOTION_DETECT);
    // Fires even though the PIR warm-up has not elapsed
    TEST_ASSERT_FALSE(sm->isSensorReady());
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_TRUE(led->isOn());
    TEST_ASSERT_TRUE(sm->isBusy());
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_sm_motion_wake_after_begin(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    advance(10);
    sm->handleMotionWake();
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_EQUAL_UINT32(g_now, sm->getLastActivityMs());
}

void test_sm_motion_wake_ignored_in_off_mode(void) {
    sm->handleMotionWake();
    sm->begin(StepStateMachine::OFF);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_sm_is_busy(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    TEST_ASSERT_FALSE(sm->isBusy());
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(sm->isBusy());
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_FALSE(sm->isBusy());
    sm->setMode(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_TRUE(sm->isBusy());
    sm->setMode(StepStateMachine::OFF);
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_sm_last_activity_on_motion(void) {
    sm->begin(StepStateMachine::OFF);
    finishWarmup();
    uint32_t before = sm->getLastActivityMs();
    advance(1234);
    motionPulse(&g_far);
    TEST_ASSERT_TRUE(sm->getLastActivityMs() > before);
    TEST_ASSERT_EQUAL_UINT32(g_now, sm->getLastActivityMs());
}

void test_sm_matrix_replaces_led_for_warning(void) {
    MockMatrix matrix;
    sm->setMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_EQUAL_UINT32(MOTION_WARNING_DURATION_MS, matrix.lastDuration);
    TEST_ASSERT_FALSE(led->isOn());  // original: matrix replaces hazard LED
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_FALSE(matrix.animating);
}

void test_sm_matrix_with_led_option(void) {
    MockMatrix matrix;
    sm->setMatrix(&matrix);
    sm->setHazardLedWithMatrix(true);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_TRUE(led->isOn());
}

void test_sm_matrix_continuous_self_heal(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_EQUAL_UINT32(0, matrix.lastDuration);  // loop forever
    matrix.animating = false;  // e.g. another animation ended it
    tick();
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_EQUAL_UINT32(2, matrix.alertStarts);
    sm->setMode(StepStateMachine::OFF);
    TEST_ASSERT_FALSE(matrix.animating);
}

// ===========================================================================
// Matrix UI (mode indicators, reboot bitmap, post-warning, pixels, emblem, battery)
// ===========================================================================

void test_ui_boot_in_off_shows_no_indicator(void) {
    // StateMachine::begin(): m_currentMode starts as OFF, so setMode(OFF) is a no-op.
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    TEST_ASSERT_FALSE(sm->isModeIndicatorActive());
    TEST_ASSERT_NULL(matrix.lastBitmap);
}

void test_ui_mode_indicator_bitmaps(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_MOTION, matrix.lastBitmap);  // boot shows it too
    TEST_ASSERT_TRUE(sm->isModeIndicatorActive());
    TEST_ASSERT_TRUE(sm->isBusy());

    sm->setMode(StepStateMachine::OFF);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_OFF, matrix.lastBitmap);
    TEST_ASSERT_EQUAL_HEX8(0x81, matrix.frame[0]);  // bold X corners
    sm->setMode(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_CONTINUOUS, matrix.lastBitmap);
    sm->setMode(StepStateMachine::MOTION_DETECT);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_MOTION, matrix.lastBitmap);
}

void test_ui_mode_indicator_expires_to_dark(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->setMode(StepStateMachine::OFF);
    tickFor(MODE_INDICATOR_DURATION_MS - 10);
    TEST_ASSERT_TRUE(sm->isModeIndicatorActive());
    TEST_ASSERT_EQUAL_HEX8(0x81, matrix.frame[0]);
    tickFor(10);
    TEST_ASSERT_FALSE(sm->isModeIndicatorActive());
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, matrix.frame[i]);
    }
    TEST_ASSERT_FALSE(matrix.animating);
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_ui_continuous_waits_for_indicator(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->setMode(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_FALSE(matrix.animating);  // indicator first, no arrow yet
    TEST_ASSERT_TRUE(led->isOn());        // hazard LED starts at once
    tickFor(MODE_INDICATOR_DURATION_MS - 10);
    TEST_ASSERT_EQUAL_UINT32(0, matrix.alertStarts);  // self-heal must not jump in
    tickFor(10);
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_EQUAL_UINT32(1, matrix.alertStarts);
    TEST_ASSERT_EQUAL_UINT32(0, matrix.lastDuration);
}

void test_ui_button_click_shows_next_indicator(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    tickFor(MODE_INDICATOR_DURATION_MS);
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::CONTINUOUS_ON, sm->getMode());
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_CONTINUOUS, matrix.lastBitmap);
}

void test_ui_matrix_not_ready_skips_indicator(void) {
    MockMatrix matrix;
    matrix.ready = false;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_FALSE(sm->isModeIndicatorActive());
    TEST_ASSERT_NULL(matrix.lastBitmap);
    TEST_ASSERT_EQUAL_UINT32(1, matrix.alertStarts);  // straight to the loop (retried)
}

void test_ui_reboot_bitmap_on_long_press(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_TRUE(matrix.animating);

    pressButton();
    tickFor(1000);  // long press: reboot icon + first countdown pixel
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_FALSE(matrix.animating);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_REBOOT, matrix.lastBitmap);
    TEST_ASSERT_TRUE(matrix.pixel(0, HOLD_PROGRESS_ROW));
    for (int y = 1; y < 8; y++) {
        TEST_ASSERT_EQUAL_HEX8(MODE_INDICATOR_REBOOT[y], matrix.frame[y]);
    }
    tickFor(500);
    releaseButton();  // 1.5 s: reboot
    TEST_ASSERT_TRUE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_HEX8(MODE_INDICATOR_REBOOT[0], matrix.frame[0]);  // bar gone
    tickFor(REBOOT_FEEDBACK_DURATION_MS - 100);
    // CONTINUOUS_ON self-heal must not cover the reboot bitmap
    TEST_ASSERT_FALSE(matrix.animating);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_REBOOT, matrix.lastBitmap);
    TEST_ASSERT_EQUAL_HEX8(MODE_INDICATOR_REBOOT[3], matrix.frame[3]);
    tickFor(200);
    TEST_ASSERT_EQUAL_UINT32(1, listener->rebootDue);
}

void test_ui_reboot_disabled_shows_nothing(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->setRebootOnLongPress(false);
    sm->setDormantHoldMs(0);  // no gesture at all: legacy "notify only"
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    button->setRawState(true);
    tick();
    advance(60);
    tick();
    advance(1000);
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, listener->rebootRequested);
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_TRUE(matrix.lastBitmap != MODE_INDICATOR_REBOOT);
    button->setRawState(false);
    tickFor(REBOOT_FEEDBACK_DURATION_MS + 100);
    TEST_ASSERT_EQUAL_UINT32(0, listener->rebootDue);
}

void test_ui_post_warning_sequence(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    sm->setWifiConnected(true);
    sm->setBatteryState(false, 80);
    tick();
    matrix.stopAnimation();  // drop the WiFi emblem for this test

    motionPulse(&g_near);
    TEST_ASSERT_TRUE(matrix.animating);
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_WIFI, sm->getPostWarnPhase());
    TEST_ASSERT_EQUAL_INT(1, matrix.wifiShown);
    TEST_ASSERT_TRUE(sm->isBusy());

    tickFor(MATRIX_POST_WARN_WIFI_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_BATTERY, sm->getPostWarnPhase());
    TEST_ASSERT_EQUAL_INT(80, matrix.batteryShown);
    TEST_ASSERT_FALSE(matrix.animating);

    tickFor(MATRIX_POST_WARN_BATTERY_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_IDLE, sm->getPostWarnPhase());
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_ui_post_warning_low_battery_animates(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    sm->setBatteryState(false, 20);  // below 30 % but not flagged low
    motionPulse(&g_far);
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_EQUAL_INT(0, matrix.wifiShown);  // never connected
    tickFor(MATRIX_POST_WARN_WIFI_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_BATTERY, sm->getPostWarnPhase());
    TEST_ASSERT_TRUE(matrix.animating);
    TEST_ASSERT_EQUAL(MockMatrix::BATTERY_LOW, matrix.anim);
    TEST_ASSERT_EQUAL_UINT32(MATRIX_POST_WARN_BATTERY_MS, matrix.lastDuration);
    tickFor(MATRIX_POST_WARN_BATTERY_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_IDLE, sm->getPostWarnPhase());
    TEST_ASSERT_FALSE(matrix.animating);
}

void test_ui_post_warning_without_battery_skips_phase(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_WIFI, sm->getPostWarnPhase());
    tickFor(MATRIX_POST_WARN_WIFI_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_IDLE, sm->getPostWarnPhase());
    TEST_ASSERT_EQUAL_INT(-1, matrix.batteryShown);
    TEST_ASSERT_EQUAL_UINT32(0, matrix.batteryLowStarts);
}

void test_ui_no_post_warning_on_mode_change(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(sm->isWarningActive());
    sm->setMode(StepStateMachine::OFF);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_IDLE, sm->getPostWarnPhase());
    TEST_ASSERT_EQUAL_INT(-1, matrix.wifiShown);
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_OFF, matrix.lastBitmap);
}

void test_ui_retrigger_cancels_post_warning(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    tickFor(MOTION_WARNING_DURATION_MS);
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_WIFI, sm->getPostWarnPhase());
    motionPulse(&g_near);
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_EQUAL(StepStateMachine::POST_WARN_IDLE, sm->getPostWarnPhase());
    TEST_ASSERT_EQUAL(MockMatrix::ALERT, matrix.anim);
}

void test_ui_sensor_status_pixels(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    g_near = true;
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_FALSE(sm->isNearPixelLit());  // PIRs still warming up

    finishWarmup();
    TEST_ASSERT_TRUE(sm->isNearPixelLit());
    TEST_ASSERT_TRUE(matrix.pixel(7, 6));
    TEST_ASSERT_TRUE(matrix.pixel(7, 7));
    TEST_ASSERT_FALSE(matrix.pixel(7, 0));
    TEST_ASSERT_TRUE(sm->isBusy());  // lit pixel = display activity

    g_far = true;
    tick();
    TEST_ASSERT_TRUE(matrix.pixel(7, 0));
    TEST_ASSERT_TRUE(matrix.pixel(7, 1));

    g_near = false;
    g_far = false;
    tick();
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, matrix.frame[i]);
    }
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_ui_sensor_pixels_suspended_while_busy(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    finishWarmup();
    g_far = true;  // edge in OFF: no warning
    tick();
    TEST_ASSERT_TRUE(matrix.pixel(7, 0));
    sm->setMode(StepStateMachine::MOTION_DETECT);  // indicator replaces the frame
    tick();
    TEST_ASSERT_FALSE(sm->isFarPixelLit());
    TEST_ASSERT_FALSE(matrix.pixel(7, 0));
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_MOTION, matrix.lastBitmap);
    TEST_ASSERT_EQUAL_HEX8(MODE_INDICATOR_MOTION[0], matrix.frame[0]);
    tickFor(MODE_INDICATOR_DURATION_MS);  // indicator gone -> redraw held sensor
    TEST_ASSERT_TRUE(sm->isFarPixelLit());
    TEST_ASSERT_TRUE(matrix.pixel(7, 0));
}

void test_ui_sensor_pixels_can_be_disabled(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->setSensorStatusPixelsEnabled(false);
    sm->begin(StepStateMachine::OFF);
    finishWarmup();
    g_near = true;
    tick();
    TEST_ASSERT_FALSE(sm->isNearPixelLit());
    TEST_ASSERT_FALSE(matrix.pixel(7, 6));
}

void test_ui_wifi_emblem_once_per_boot(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->setWifiConnected(true);
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, matrix.emblemStarts);
    TEST_ASSERT_EQUAL_UINT32(MATRIX_WIFI_EMBLEM_MS, matrix.lastDuration);
    tickFor(MATRIX_WIFI_EMBLEM_MS);
    TEST_ASSERT_FALSE(matrix.animating);

    sm->setWifiConnected(false);
    tick();
    sm->setWifiConnected(true);
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, matrix.emblemStarts);  // reconnects don't repeat it
}

void test_ui_wifi_emblem_waits_for_indicator(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    sm->setWifiConnected(true);
    tick();
    TEST_ASSERT_EQUAL_UINT32(0, matrix.emblemStarts);  // mode indicator still up
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_EQUAL_UINT32(1, matrix.emblemStarts);
}

void test_ui_wifi_emblem_skipped_during_animation(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    sm->setWifiConnected(true);
    tick();
    TEST_ASSERT_EQUAL_UINT32(0, matrix.emblemStarts);
    TEST_ASSERT_TRUE(sm->isWifiEmblemShown());  // marked shown, never pops up late
    tickFor(MOTION_WARNING_DURATION_MS + 5000);
    TEST_ASSERT_EQUAL_UINT32(0, matrix.emblemStarts);
}

void test_ui_battery_low_notice(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->setBatteryState(true, 20);
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, matrix.batteryLowStarts);
    TEST_ASSERT_EQUAL_UINT32(BATTERY_NOTICE_DURATION_MS, matrix.lastDuration);
    tickFor(BATTERY_NOTICE_DURATION_MS);
    TEST_ASSERT_FALSE(matrix.animating);
    sm->setBatteryState(true, 19);  // still low: no repeat
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, matrix.batteryLowStarts);
    sm->setBatteryState(false, 40);
    sm->setBatteryState(true, 25);  // new edge
    tick();
    TEST_ASSERT_EQUAL_UINT32(2, matrix.batteryLowStarts);
}

void test_ui_battery_low_notice_needs_low_percent(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->setBatteryState(true, 45);  // flagged low but >= 30 %: showBatteryStatus() no-op
    tick();
    TEST_ASSERT_EQUAL_UINT32(0, matrix.batteryLowStarts);
    sm->setBatteryState(false, StepStateMachine::BATTERY_PERCENT_UNKNOWN);
    sm->setBatteryState(true, StepStateMachine::BATTERY_PERCENT_UNKNOWN);  // unknown: show
    tick();
    TEST_ASSERT_EQUAL_UINT32(1, matrix.batteryLowStarts);
}

void test_ui_battery_low_deferred_behind_warning(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    motionPulse(&g_near);
    sm->setBatteryState(true, 10);
    tick();
    TEST_ASSERT_EQUAL_UINT32(0, matrix.batteryLowStarts);
    TEST_ASSERT_TRUE(sm->isBatteryNoticePending());
    tickFor(MOTION_WARNING_DURATION_MS);  // warning over, post-warning sequence runs
    TEST_ASSERT_EQUAL_UINT32(0, matrix.batteryLowStarts);
    tickFor(MATRIX_POST_WARN_WIFI_MS);    // battery phase: drain animation (part of it)
    TEST_ASSERT_EQUAL_UINT32(1, matrix.batteryLowStarts);
    tickFor(MATRIX_POST_WARN_BATTERY_MS + 20);  // sequence done -> deferred notice plays
    TEST_ASSERT_EQUAL_UINT32(2, matrix.batteryLowStarts);
    TEST_ASSERT_FALSE(sm->isBatteryNoticePending());
}

void test_ui_battery_low_in_continuous_shows_now(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_EQUAL(MockMatrix::ALERT, matrix.anim);
    sm->setBatteryState(true, 5);
    tick();
    TEST_ASSERT_EQUAL(MockMatrix::BATTERY_LOW, matrix.anim);
    tickFor(BATTERY_NOTICE_DURATION_MS + 20);
    TEST_ASSERT_EQUAL(MockMatrix::ALERT, matrix.anim);  // arrow loop self-heals
    TEST_ASSERT_EQUAL_UINT32(0, matrix.lastDuration);
}

void test_ui_battery_low_without_matrix_blinks_led(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    sm->setBatteryState(true, 10);
    tick();
    TEST_ASSERT_TRUE(led->isPatternActive());
    TEST_ASSERT_EQUAL(StepLedPattern::PATTERN_BLINK_SLOW, led->getPattern());
}

void test_ui_reset_display_tracking_redraws_pixels(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::OFF);
    finishWarmup();
    g_near = true;
    tick();
    TEST_ASSERT_TRUE(matrix.pixel(7, 6));
    matrix.clear();  // pre-sleep clear behind the core's back
    sm->resetDisplayTracking();
    tick();
    TEST_ASSERT_TRUE(matrix.pixel(7, 6));
}

// ===========================================================================
// LED pattern
// ===========================================================================

void test_led_warning_blink_timing(void) {
    led->startPattern(StepLedPattern::PATTERN_BLINK_WARNING, 0);
    TEST_ASSERT_TRUE(led->isOn());
    advance(499);
    led->update();
    TEST_ASSERT_TRUE(led->isOn());
    advance(1);
    led->update();
    TEST_ASSERT_FALSE(led->isOn());
    advance(500);
    led->update();
    TEST_ASSERT_TRUE(led->isOn());
}

void test_led_pattern_duration_expires(void) {
    led->startPattern(StepLedPattern::PATTERN_BLINK_FAST, 1000);
    advance(999);
    led->update();
    TEST_ASSERT_TRUE(led->isPatternActive());
    advance(1);
    led->update();
    TEST_ASSERT_FALSE(led->isPatternActive());
    TEST_ASSERT_EQUAL_UINT8(0, led->getLevel());
}

void test_led_brightness_and_level_changed(void) {
    TEST_ASSERT_TRUE(led->levelChanged());   // initial write
    TEST_ASSERT_FALSE(led->levelChanged());
    led->setOnBrightness(LED_BRIGHTNESS_MEDIUM);
    led->startPattern(StepLedPattern::PATTERN_BLINK_SLOW, 0);
    TEST_ASSERT_TRUE(led->levelChanged());
    TEST_ASSERT_EQUAL_UINT8(128, led->getLevel());
    led->setOnBrightness(LED_BRIGHTNESS_DIM);  // applies immediately while lit
    TEST_ASSERT_EQUAL_UINT8(20, led->getLevel());
    advance(1000);
    led->update();
    TEST_ASSERT_EQUAL_UINT8(0, led->getLevel());
}

void test_led_custom_intervals(void) {
    led->setBlinkIntervals(100, 200, 300);
    led->startPattern(StepLedPattern::PATTERN_BLINK_WARNING, 0);
    advance(300);
    led->update();
    TEST_ASSERT_FALSE(led->isOn());
    led->setCustomPattern(50, 150);
    led->startPattern(StepLedPattern::PATTERN_CUSTOM, 0);
    advance(50);
    led->update();
    TEST_ASSERT_FALSE(led->isOn());
    advance(149);
    led->update();
    TEST_ASSERT_FALSE(led->isOn());
    advance(1);
    led->update();
    TEST_ASSERT_TRUE(led->isOn());
}

void test_led_pulse_ramps(void) {
    led->startPattern(StepLedPattern::PATTERN_PULSE, 0);
    advance(500);
    led->update();
    uint8_t mid = led->getLevel();
    TEST_ASSERT_TRUE(mid > 100 && mid < 160);
    advance(500);
    led->update();
    TEST_ASSERT_TRUE(led->getLevel() >= 250);
    advance(1000);
    led->update();
    TEST_ASSERT_TRUE(led->getLevel() <= 5);
}

// ===========================================================================
// Button
// ===========================================================================

void test_button_debounce_rejects_bounce(void) {
    button->setRawState(true);
    button->update();
    advance(20);
    button->setRawState(false);
    button->update();
    advance(100);
    button->update();
    TEST_ASSERT_FALSE(button->isPressed());
    TEST_ASSERT_FALSE(button->hasQueuedEvents());
}

void test_button_click(void) {
    button->setRawState(true);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_TRUE(button->isPressed());
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_PRESSED));
    advance(100);
    button->setRawState(false);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_FALSE(button->isPressed());
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_CLICK));
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_RELEASED));
    TEST_ASSERT_EQUAL_UINT32(1, button->getClickCount());
}

void test_button_long_press(void) {
    button->setRawState(true);
    button->update();
    advance(50);
    button->update();
    advance(999);
    button->update();
    TEST_ASSERT_FALSE(button->hasEvent(StepButton::EVENT_LONG_PRESS));
    advance(1);
    button->update();
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_LONG_PRESS));
    TEST_ASSERT_EQUAL_UINT32(1000, button->getPressedDuration());
    button->setRawState(false);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_FALSE(button->hasEvent(StepButton::EVENT_CLICK));  // no click after long
    TEST_ASSERT_EQUAL_UINT32(0, button->getClickCount());
}

void test_button_zero_debounce(void) {
    button->setDebounceMs(0);  // as used by the ESPHome glue
    button->setRawState(true);
    button->update();
    button->update();
    TEST_ASSERT_TRUE(button->isPressed());
    button->setRawState(false);
    button->update();
    button->update();
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_CLICK));
}

void test_button_multiple_clicks(void) {
    for (int i = 0; i < 3; i++) {
        button->setRawState(true);
        button->update();
        advance(60);
        button->update();
        button->setRawState(false);
        button->update();
        advance(60);
        button->update();
    }
    TEST_ASSERT_EQUAL_UINT32(3, button->getClickCount());
    button->resetClickCount();
    TEST_ASSERT_EQUAL_UINT32(0, button->getClickCount());
}

// ===========================================================================
// Direction detector
// ===========================================================================

void test_dir_approaching(void) {
    dir->begin(false, false);
    dir->update(true, false);
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_FAR_ONLY, dir->getCurrentState());
    advance(800);
    dir->update(true, true);
    TEST_ASSERT_TRUE(dir->isApproaching());
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_APPROACHING, dir->getCurrentState());
    TEST_ASSERT_EQUAL_UINT32(1, dir->getApproachingCount());
    advance(100);
    TEST_ASSERT_EQUAL_UINT32(100, dir->getDirectionConfidenceMs());
    dir->update(false, false);
    TEST_ASSERT_FALSE(dir->isApproaching());
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_IDLE, dir->getCurrentState());
}

void test_dir_near_first_is_not_approaching(void) {
    dir->begin(false, false);
    dir->update(false, true);
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_NEAR_ONLY, dir->getCurrentState());
    TEST_ASSERT_EQUAL_UINT32(1, dir->getUnknownCount());
    dir->update(false, false);
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_IDLE, dir->getCurrentState());
    TEST_ASSERT_FALSE(dir->isApproaching());
}

void test_dir_simultaneous_is_unknown(void) {
    dir->begin(false, false);
    dir->update(true, false);
    advance(100);  // < 150 ms
    dir->update(true, true);
    TEST_ASSERT_FALSE(dir->isApproaching());
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_BOTH_ACTIVE, dir->getCurrentState());
}

void test_dir_far_clear_latched_issue51(void) {
    dir->begin(false, false);
    dir->update(true, false);
    advance(1000);
    dir->update(false, false);  // far cleared: stays FAR_ONLY
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_FAR_ONLY, dir->getCurrentState());
    advance(1000);
    dir->update(false, true);   // near within window
    TEST_ASSERT_TRUE(dir->isApproaching());
}

void test_dir_far_clear_window_expires(void) {
    dir->begin(false, false);
    dir->update(true, false);
    advance(100);
    dir->update(false, false);
    advance(DIR_CONFIRMATION_WINDOW_MS);
    dir->update(false, false);
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_IDLE, dir->getCurrentState());
}

void test_dir_near_outside_window(void) {
    dir->begin(false, false);
    dir->update(true, false);
    advance(DIR_CONFIRMATION_WINDOW_MS + 1);
    dir->update(true, true);
    TEST_ASSERT_FALSE(dir->isApproaching());
}

void test_dir_pattern_timeout(void) {
    dir->begin(false, false);
    dir->update(true, false);
    advance(DIR_PATTERN_TIMEOUT_MS + 1);
    dir->update(true, false);
    TEST_ASSERT_EQUAL(StepDirectionDetector::DIR_IDLE, dir->getCurrentState());
}

void test_dir_leaving(void) {
    dir->begin(false, false);
    dir->update(false, true);
    advance(700);
    dir->update(true, true);
    TEST_ASSERT_TRUE(dir->isLeaving());
    TEST_ASSERT_FALSE(dir->isApproaching());
    TEST_ASSERT_EQUAL_UINT32(1, dir->getLeavingCount());
    dir->update(false, false);
    TEST_ASSERT_FALSE(dir->isLeaving());
}

void test_dir_seed_far_trigger(void) {
    dir->begin(false, false);
    dir->seedFarTrigger(500);
    dir->update(true, true);  // far still high (no new edge), near rises
    TEST_ASSERT_TRUE(dir->isApproaching());
}


// ===========================================================================
// Hold gestures: reboot / dormant on release, wake-press suppression
// ===========================================================================

void test_gesture_click_still_instant(void) {
    sm->begin(StepStateMachine::OFF);
    holdAndRelease(900);  // under the long-press time: plain click
    TEST_ASSERT_EQUAL(StepStateMachine::CONTINUOUS_ON, sm->getMode());
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_FALSE(sm->isHoldActive());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
}

void test_gesture_release_just_before_threshold_reboots(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    holdAndRelease(DORMANT_HOLD_MS - 10);
    TEST_ASSERT_TRUE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    TEST_ASSERT_FALSE(sm->isShutdownPending());
}

void test_gesture_release_at_2_to_4_s_reboots(void) {
    for (uint32_t ms = 2000; ms <= 4000; ms += 1000) {
        tearDown();
        setUp();
        sm->begin(StepStateMachine::MOTION_DETECT);
        holdAndRelease(ms);
        TEST_ASSERT_TRUE(sm->isRebootPending());
        TEST_ASSERT_EQUAL_UINT32(1, listener->rebootRequested);
        TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    }
}

void test_gesture_hold_5s_release_requests_dormant(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    pressButton();
    tickFor(DORMANT_HOLD_MS);
    // Still held at the threshold: power-off icon, LED solid, nothing requested yet.
    TEST_ASSERT_EQUAL(StepStateMachine::HOLD_DORMANT, sm->getHoldAction());
    TEST_ASSERT_EQUAL_PTR(POWER_OFF_ICON, matrix.lastBitmap);
    TEST_ASSERT_EQUAL(StepLedPattern::PATTERN_ON, led->getPattern());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    tickFor(3000);  // holding longer changes nothing
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);

    releaseButton();
    TEST_ASSERT_EQUAL_UINT32(1, listener->dormantRequested);
    TEST_ASSERT_EQUAL_UINT32(0, listener->rebootRequested);
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_TRUE(sm->isShutdownPending());
    TEST_ASSERT_TRUE(sm->isBusy());
    TEST_ASSERT_EQUAL_PTR(POWER_OFF_ICON, matrix.lastBitmap);
    for (int y = 0; y < 8; y++) {
        TEST_ASSERT_EQUAL_HEX8(POWER_OFF_ICON[y], matrix.frame[y]);
    }
    TEST_ASSERT_FALSE(led->isPatternActive());  // LED off for the shutdown
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());  // no click
}

void test_gesture_countdown_bar(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    pressButton();
    tickFor(1000);
    TEST_ASSERT_EQUAL_UINT8(1, sm->getHoldProgress());
    tickFor(2000);  // 3 s: halfway between 1 s and 5 s
    TEST_ASSERT_EQUAL_UINT8(5, sm->getHoldProgress());
    for (uint8_t x = 0; x < 8; x++) {
        TEST_ASSERT_EQUAL(x < 5, matrix.pixel(x, HOLD_PROGRESS_ROW));
    }
    tickFor(1900);  // 4.9 s: full bar
    TEST_ASSERT_EQUAL_UINT8(8, sm->getHoldProgress());
    TEST_ASSERT_EQUAL_HEX8(0xFF, matrix.frame[HOLD_PROGRESS_ROW]);
    tickFor(100);  // 5 s: icon switches
    TEST_ASSERT_EQUAL_PTR(POWER_OFF_ICON, matrix.lastBitmap);
    TEST_ASSERT_EQUAL_HEX8(POWER_OFF_ICON[0], matrix.frame[0]);
}

void test_gesture_led_feedback_without_matrix(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    pressButton();
    tickFor(1000);
    TEST_ASSERT_EQUAL(StepLedPattern::PATTERN_BLINK_FAST, led->getPattern());
    tickFor(DORMANT_HOLD_MS - 1000);
    TEST_ASSERT_EQUAL(StepLedPattern::PATTERN_ON, led->getPattern());
    releaseButton();
    TEST_ASSERT_EQUAL_UINT32(1, listener->dormantRequested);
    TEST_ASSERT_FALSE(led->isPatternActive());
}

void test_gesture_configurable_threshold(void) {
    sm->setDormantHoldMs(3000);
    sm->begin(StepStateMachine::MOTION_DETECT);
    holdAndRelease(3000);
    TEST_ASSERT_EQUAL_UINT32(1, listener->dormantRequested);
    TEST_ASSERT_FALSE(sm->isRebootPending());
}

void test_gesture_dormant_disabled_long_hold_reboots(void) {
    sm->setDormantHoldMs(0);
    TEST_ASSERT_FALSE(sm->isDormantGestureEnabled());
    sm->begin(StepStateMachine::MOTION_DETECT);
    holdAndRelease(8000);
    TEST_ASSERT_TRUE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
}

void test_gesture_threshold_not_above_long_press_disables(void) {
    sm->setDormantHoldMs(1000);  // == long press: meaningless, treated as disabled
    TEST_ASSERT_FALSE(sm->isDormantGestureEnabled());
}

void test_gesture_reboot_disabled_dormant_enabled(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->setRebootOnLongPress(false);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);

    pressButton();
    tickFor(1000);
    TEST_ASSERT_EQUAL(StepStateMachine::HOLD_CANCEL, sm->getHoldAction());
    TEST_ASSERT_TRUE(matrix.lastBitmap != MODE_INDICATOR_REBOOT);
    TEST_ASSERT_TRUE(matrix.pixel(0, HOLD_PROGRESS_ROW));  // countdown still shown
    tickFor(1000);
    releaseButton();  // 2 s: cancel
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    TEST_ASSERT_EQUAL_UINT32(1, listener->rebootRequested);  // "disabled" notice
    TEST_ASSERT_FALSE(matrix.pixel(0, HOLD_PROGRESS_ROW));   // cleared
    TEST_ASSERT_FALSE(sm->isBusy());

    holdAndRelease(6000);
    TEST_ASSERT_EQUAL_UINT32(1, listener->dormantRequested);
}

void test_gesture_hold_blocks_warning_and_selfheal(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    tickFor(MODE_INDICATOR_DURATION_MS);
    TEST_ASSERT_TRUE(matrix.animating);
    pressButton();
    tickFor(1500);
    TEST_ASSERT_FALSE(matrix.animating);  // CONTINUOUS self-heal held off
    TEST_ASSERT_EQUAL_PTR(MODE_INDICATOR_REBOOT, matrix.lastBitmap);

    tearDown();
    setUp();
    MockMatrix m2;
    attachMatrix(&m2);
    sm->begin(StepStateMachine::MOTION_DETECT);
    finishWarmup();
    pressButton();
    tickFor(1500);
    motionPulse(&g_near);
    TEST_ASSERT_FALSE(sm->isWarningActive());  // hold display not replaced by the alert
}

void test_shutdown_display_and_timeout(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::CONTINUOUS_ON);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->showShutdown();  // HA "Shut Down"
    TEST_ASSERT_TRUE(sm->isShutdownPending());
    TEST_ASSERT_EQUAL_PTR(POWER_OFF_ICON, matrix.lastBitmap);
    TEST_ASSERT_FALSE(matrix.animating);
    tickFor(2000);
    TEST_ASSERT_FALSE(matrix.animating);  // self-heal does not cover the icon
    TEST_ASSERT_TRUE(sm->isBusy());

    // Clicks while shutting down do not change (and persist) the mode.
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::CONTINUOUS_ON, sm->getMode());

    tickFor(SHUTDOWN_DISPLAY_TIMEOUT_MS);  // device never slept: give the display back
    TEST_ASSERT_FALSE(sm->isShutdownPending());
    tickFor(100);
    TEST_ASSERT_TRUE(matrix.animating);
}

void test_shutdown_cancel(void) {
    MockMatrix matrix;
    attachMatrix(&matrix);
    sm->begin(StepStateMachine::MOTION_DETECT);
    tickFor(MODE_INDICATOR_DURATION_MS);
    sm->showShutdown();
    sm->cancelShutdown();
    TEST_ASSERT_FALSE(sm->isShutdownPending());
    TEST_ASSERT_EQUAL_HEX8(0, matrix.frame[0]);
    tick();
    TEST_ASSERT_FALSE(sm->isBusy());
}

void test_wake_press_does_not_click(void) {
    // Dormant wake: the waking press is still held when the core starts.
    sm->begin(StepStateMachine::MOTION_DETECT);
    button->setRawState(true);  // already pressed at boot
    button->suppressNextPress(1500);
    tick();
    advance(50);
    tick();
    TEST_ASSERT_TRUE(sm->isBusy());  // held: still keeps the device awake
    tickFor(300);
    releaseButton();
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());
    TEST_ASSERT_EQUAL_UINT32(0, listener->modeChanges - 1);  // only begin()
    // The next press is a normal click again.
    clickButton();
    TEST_ASSERT_EQUAL(StepStateMachine::OFF, sm->getMode());
}

void test_wake_press_held_long_is_not_a_gesture(void) {
    sm->begin(StepStateMachine::MOTION_DETECT);
    pressButton();
    button->suppressNextPress(1500);  // suppression applied to a press in progress
    tickFor(7000);
    releaseButton();
    TEST_ASSERT_FALSE(sm->isRebootPending());
    TEST_ASSERT_EQUAL_UINT32(0, listener->dormantRequested);
    TEST_ASSERT_EQUAL(StepStateMachine::MOTION_DETECT, sm->getMode());
}

void test_button_long_release_event(void) {
    button->setRawState(true);
    button->update();
    advance(50);
    button->update();
    advance(2500);
    button->update();
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_LONG_PRESS));
    button->setRawState(false);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_LONG_RELEASE));
    TEST_ASSERT_FALSE(button->hasEvent(StepButton::EVENT_CLICK));
    TEST_ASSERT_EQUAL_UINT32(2500, button->getLastPressDurationMs());
}

void test_button_suppress_window_lapses(void) {
    button->suppressNextPress(1000);
    TEST_ASSERT_TRUE(button->isSuppressing());
    advance(1500);
    button->update();  // no press in the window
    TEST_ASSERT_FALSE(button->isSuppressing());
    button->setRawState(true);
    button->update();
    advance(50);
    button->update();
    button->setRawState(false);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_TRUE(button->hasEvent(StepButton::EVENT_CLICK));
    TEST_ASSERT_EQUAL_UINT32(1, button->getClickCount());
}

void test_button_suppress_swallows_everything(void) {
    button->suppressNextPress(1000);
    advance(200);
    button->setRawState(true);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_TRUE(button->isPressed());
    advance(2000);
    button->update();
    button->setRawState(false);
    button->update();
    advance(50);
    button->update();
    TEST_ASSERT_FALSE(button->hasQueuedEvents());
    TEST_ASSERT_EQUAL_UINT32(0, button->getClickCount());
    TEST_ASSERT_FALSE(button->isSuppressing());
}

int main(int, char**) {
    UNITY_BEGIN();
    // State machine
    RUN_TEST(test_sm_initialization);
    RUN_TEST(test_sm_begin_default_mode_counts_change);
    RUN_TEST(test_sm_mode_cycling);
    RUN_TEST(test_sm_set_mode_same_mode_is_noop);
    RUN_TEST(test_sm_motion_triggers_warning_in_motion_mode);
    RUN_TEST(test_sm_motion_ignored_during_warmup);
    RUN_TEST(test_sm_motion_in_off_mode_no_warning);
    RUN_TEST(test_sm_motion_in_continuous_mode_no_warning);
    RUN_TEST(test_sm_continuous_led_blinks);
    RUN_TEST(test_sm_warning_timeout);
    RUN_TEST(test_sm_configured_warning_duration);
    RUN_TEST(test_sm_retrigger_restarts_warning);
    RUN_TEST(test_sm_mode_change_during_warning_stops_it);
    RUN_TEST(test_sm_button_click_cycles_mode);
    RUN_TEST(test_sm_long_press_requests_reboot);
    RUN_TEST(test_sm_short_presses_never_reboot);
    RUN_TEST(test_sm_direction_filter_requires_approach);
    RUN_TEST(test_sm_direction_filter_disabled_any_pir_triggers);
    RUN_TEST(test_sm_toggling_filter_no_spurious_edge);
    RUN_TEST(test_sm_motion_wake_before_begin);
    RUN_TEST(test_sm_motion_wake_after_begin);
    RUN_TEST(test_sm_motion_wake_ignored_in_off_mode);
    RUN_TEST(test_sm_is_busy);
    RUN_TEST(test_sm_last_activity_on_motion);
    RUN_TEST(test_sm_matrix_replaces_led_for_warning);
    RUN_TEST(test_sm_matrix_with_led_option);
    RUN_TEST(test_sm_matrix_continuous_self_heal);
    // Matrix UI
    RUN_TEST(test_ui_mode_indicator_bitmaps);
    RUN_TEST(test_ui_boot_in_off_shows_no_indicator);
    RUN_TEST(test_ui_mode_indicator_expires_to_dark);
    RUN_TEST(test_ui_continuous_waits_for_indicator);
    RUN_TEST(test_ui_button_click_shows_next_indicator);
    RUN_TEST(test_ui_matrix_not_ready_skips_indicator);
    RUN_TEST(test_ui_reboot_bitmap_on_long_press);
    RUN_TEST(test_ui_reboot_disabled_shows_nothing);
    RUN_TEST(test_ui_post_warning_sequence);
    RUN_TEST(test_ui_post_warning_low_battery_animates);
    RUN_TEST(test_ui_post_warning_without_battery_skips_phase);
    RUN_TEST(test_ui_no_post_warning_on_mode_change);
    RUN_TEST(test_ui_retrigger_cancels_post_warning);
    RUN_TEST(test_ui_sensor_status_pixels);
    RUN_TEST(test_ui_sensor_pixels_suspended_while_busy);
    RUN_TEST(test_ui_sensor_pixels_can_be_disabled);
    RUN_TEST(test_ui_wifi_emblem_once_per_boot);
    RUN_TEST(test_ui_wifi_emblem_waits_for_indicator);
    RUN_TEST(test_ui_wifi_emblem_skipped_during_animation);
    RUN_TEST(test_ui_battery_low_notice);
    RUN_TEST(test_ui_battery_low_notice_needs_low_percent);
    RUN_TEST(test_ui_battery_low_deferred_behind_warning);
    RUN_TEST(test_ui_battery_low_in_continuous_shows_now);
    RUN_TEST(test_ui_battery_low_without_matrix_blinks_led);
    RUN_TEST(test_ui_reset_display_tracking_redraws_pixels);
    // LED
    RUN_TEST(test_led_warning_blink_timing);
    RUN_TEST(test_led_pattern_duration_expires);
    RUN_TEST(test_led_brightness_and_level_changed);
    RUN_TEST(test_led_custom_intervals);
    RUN_TEST(test_led_pulse_ramps);
    // Button
    RUN_TEST(test_button_debounce_rejects_bounce);
    RUN_TEST(test_button_click);
    RUN_TEST(test_button_long_press);
    RUN_TEST(test_button_zero_debounce);
    RUN_TEST(test_button_multiple_clicks);
    // Direction detector
    RUN_TEST(test_dir_approaching);
    RUN_TEST(test_dir_near_first_is_not_approaching);
    RUN_TEST(test_dir_simultaneous_is_unknown);
    RUN_TEST(test_dir_far_clear_latched_issue51);
    RUN_TEST(test_dir_far_clear_window_expires);
    RUN_TEST(test_dir_near_outside_window);
    RUN_TEST(test_dir_pattern_timeout);
    RUN_TEST(test_dir_leaving);
    RUN_TEST(test_dir_seed_far_trigger);
    // Hold gestures / dormant
    RUN_TEST(test_gesture_click_still_instant);
    RUN_TEST(test_gesture_release_just_before_threshold_reboots);
    RUN_TEST(test_gesture_release_at_2_to_4_s_reboots);
    RUN_TEST(test_gesture_hold_5s_release_requests_dormant);
    RUN_TEST(test_gesture_countdown_bar);
    RUN_TEST(test_gesture_led_feedback_without_matrix);
    RUN_TEST(test_gesture_configurable_threshold);
    RUN_TEST(test_gesture_dormant_disabled_long_hold_reboots);
    RUN_TEST(test_gesture_threshold_not_above_long_press_disables);
    RUN_TEST(test_gesture_reboot_disabled_dormant_enabled);
    RUN_TEST(test_gesture_hold_blocks_warning_and_selfheal);
    RUN_TEST(test_shutdown_display_and_timeout);
    RUN_TEST(test_shutdown_cancel);
    RUN_TEST(test_wake_press_does_not_click);
    RUN_TEST(test_wake_press_held_long_is_not_a_gesture);
    RUN_TEST(test_button_long_release_event);
    RUN_TEST(test_button_suppress_window_lapses);
    RUN_TEST(test_button_suppress_swallows_everything);
    return UNITY_END();
}
