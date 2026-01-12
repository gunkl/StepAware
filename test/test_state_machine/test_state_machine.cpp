/**
 * @file test_state_machine.cpp
 * @brief Unit tests for state machine logic
 */

#include <unity.h>
#include <stdint.h>

// Mock time
unsigned long mock_time = 0;
unsigned long millis() { return mock_time; }
void advance_time(unsigned long ms) { mock_time += ms; }
void reset_time() { mock_time = 0; }

// Mock Serial
struct {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char*, ...) {}
} Serial;

// Simple state machine for testing
enum OperatingMode {
    OFF = 0,
    CONTINUOUS_ON = 1,
    MOTION_DETECT = 2
};

class TestStateMachine {
private:
    OperatingMode mode;
    uint32_t motion_events;
    uint32_t mode_changes;
    bool led_on;
    bool warning_active;
    unsigned long warning_end_time;

public:
    TestStateMachine()
        : mode(OFF), motion_events(0), mode_changes(0),
          led_on(false), warning_active(false), warning_end_time(0) {
    }

    void cycleMode() {
        switch (mode) {
            case OFF:
                mode = CONTINUOUS_ON;
                led_on = true;
                break;
            case CONTINUOUS_ON:
                mode = MOTION_DETECT;
                led_on = false;
                break;
            case MOTION_DETECT:
                mode = OFF;
                led_on = false;
                break;
        }
        mode_changes++;
    }

    void setMode(OperatingMode new_mode) {
        if (mode != new_mode) {
            mode = new_mode;
            mode_changes++;

            // Update LED based on mode
            if (mode == CONTINUOUS_ON) {
                led_on = true;
            } else {
                led_on = false;
            }
        }
    }

    void handleMotion() {
        if (mode == MOTION_DETECT) {
            motion_events++;
            warning_active = true;
            warning_end_time = millis() + 15000; // 15 second warning
        }
    }

    void update() {
        // Check if warning expired
        if (warning_active && millis() >= warning_end_time) {
            warning_active = false;
        }
    }

    OperatingMode getMode() const { return mode; }
    uint32_t getMotionEventCount() const { return motion_events; }
    uint32_t getModeChangeCount() const { return mode_changes; }
    bool isLedOn() const { return led_on || warning_active; }
    bool isWarningActive() const { return warning_active; }

    void reset() {
        motion_events = 0;
        mode_changes = 0;
    }
};

TestStateMachine* sm = nullptr;

void setUp(void) {
    reset_time();
    sm = new TestStateMachine();
}

void tearDown(void) {
    delete sm;
    sm = nullptr;
}

void test_state_machine_initialization(void) {
    TEST_ASSERT_NOT_NULL(sm);
    TEST_ASSERT_EQUAL(OFF, sm->getMode());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getModeChangeCount());
    TEST_ASSERT_FALSE(sm->isLedOn());
}

void test_mode_cycling(void) {
    // Start at OFF
    TEST_ASSERT_EQUAL(OFF, sm->getMode());

    // Cycle to CONTINUOUS_ON
    sm->cycleMode();
    TEST_ASSERT_EQUAL(CONTINUOUS_ON, sm->getMode());
    TEST_ASSERT_TRUE(sm->isLedOn());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());

    // Cycle to MOTION_DETECT
    sm->cycleMode();
    TEST_ASSERT_EQUAL(MOTION_DETECT, sm->getMode());
    TEST_ASSERT_FALSE(sm->isLedOn());
    TEST_ASSERT_EQUAL_UINT32(2, sm->getModeChangeCount());

    // Cycle back to OFF
    sm->cycleMode();
    TEST_ASSERT_EQUAL(OFF, sm->getMode());
    TEST_ASSERT_FALSE(sm->isLedOn());
    TEST_ASSERT_EQUAL_UINT32(3, sm->getModeChangeCount());
}

void test_set_mode_directly(void) {
    // Set to CONTINUOUS_ON
    sm->setMode(CONTINUOUS_ON);
    TEST_ASSERT_EQUAL(CONTINUOUS_ON, sm->getMode());
    TEST_ASSERT_TRUE(sm->isLedOn());
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());

    // Set to same mode - should not increment counter
    sm->setMode(CONTINUOUS_ON);
    TEST_ASSERT_EQUAL_UINT32(1, sm->getModeChangeCount());

    // Set to different mode
    sm->setMode(MOTION_DETECT);
    TEST_ASSERT_EQUAL(MOTION_DETECT, sm->getMode());
    TEST_ASSERT_EQUAL_UINT32(2, sm->getModeChangeCount());
}

void test_motion_detection_in_motion_mode(void) {
    // Set to MOTION_DETECT mode
    sm->setMode(MOTION_DETECT);
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
    TEST_ASSERT_FALSE(sm->isWarningActive());

    // Trigger motion
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());
    TEST_ASSERT_TRUE(sm->isWarningActive());
    TEST_ASSERT_TRUE(sm->isLedOn()); // LED should be on during warning
}

void test_motion_detection_in_off_mode(void) {
    // Set to OFF mode
    sm->setMode(OFF);

    // Trigger motion - should be ignored
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
    TEST_ASSERT_FALSE(sm->isWarningActive());
}

void test_motion_detection_in_continuous_mode(void) {
    // Set to CONTINUOUS_ON mode
    sm->setMode(CONTINUOUS_ON);

    // Trigger motion - should be ignored (LED always on anyway)
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_TRUE(sm->isLedOn()); // LED on due to CONTINUOUS_ON, not warning
}

void test_warning_timeout(void) {
    // Set to MOTION_DETECT and trigger motion
    sm->setMode(MOTION_DETECT);
    sm->handleMotion();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    // Update at 5 seconds - warning still active
    advance_time(5000);
    sm->update();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    // Update at 10 seconds - warning still active
    advance_time(5000);
    sm->update();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    // Update at 15 seconds - warning should expire
    advance_time(5000);
    sm->update();
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(sm->isLedOn());
}

void test_multiple_motion_events(void) {
    sm->setMode(MOTION_DETECT);

    // First motion
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(1, sm->getMotionEventCount());

    advance_time(1000);

    // Second motion (before first warning expires)
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(2, sm->getMotionEventCount());

    advance_time(16000); // Total > 15s from first motion
    sm->update();

    // Third motion (after warning expired)
    sm->handleMotion();
    TEST_ASSERT_EQUAL_UINT32(3, sm->getMotionEventCount());
    TEST_ASSERT_TRUE(sm->isWarningActive());
}

void test_reset_counters(void) {
    // Generate some events
    sm->cycleMode();
    sm->setMode(MOTION_DETECT);
    sm->handleMotion();

    TEST_ASSERT_GREATER_THAN(0, sm->getModeChangeCount());
    TEST_ASSERT_GREATER_THAN(0, sm->getMotionEventCount());

    // Reset
    sm->reset();
    TEST_ASSERT_EQUAL_UINT32(0, sm->getModeChangeCount());
    TEST_ASSERT_EQUAL_UINT32(0, sm->getMotionEventCount());
}

void test_mode_change_during_warning(void) {
    // Start in MOTION_DETECT, trigger warning
    sm->setMode(MOTION_DETECT);
    sm->handleMotion();
    TEST_ASSERT_TRUE(sm->isWarningActive());

    // Change to OFF during warning
    sm->setMode(OFF);
    TEST_ASSERT_EQUAL(OFF, sm->getMode());

    // Warning should still be active (bug or feature?)
    // This tests current behavior - may want to change this
    TEST_ASSERT_TRUE(sm->isWarningActive());

    // After timeout, warning should clear
    advance_time(16000);
    sm->update();
    TEST_ASSERT_FALSE(sm->isWarningActive());
    TEST_ASSERT_FALSE(sm->isLedOn());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_state_machine_initialization);
    RUN_TEST(test_mode_cycling);
    RUN_TEST(test_set_mode_directly);
    RUN_TEST(test_motion_detection_in_motion_mode);
    RUN_TEST(test_motion_detection_in_off_mode);
    RUN_TEST(test_motion_detection_in_continuous_mode);
    RUN_TEST(test_warning_timeout);
    RUN_TEST(test_multiple_motion_events);
    RUN_TEST(test_reset_counters);
    RUN_TEST(test_mode_change_during_warning);

    return UNITY_END();
}
