/**
 * @file test_state_machine.cpp
 * @brief Unit tests for state machine logic
 */

#include <unity.h>
#include <stdint.h>
#include <cstring>

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
    bool rebootPending;
    bool modeIndicatorShown;
    OperatingMode lastIndicatorMode;

    // Sensor status display
    bool sensorTriggered[4];
    uint8_t mockFrame[8];
    bool sensorStatusDisplay[4];
    uint8_t sensorDistanceZone[4];
    bool sensorActive[4];
    bool lastSensorDisplayState[4];
    bool lastMatrixWasAnimating;
    bool matrixAnimating;

public:
    TestStateMachine()
        : mode(OFF), motion_events(0), mode_changes(0),
          led_on(false), warning_active(false), warning_end_time(0),
          rebootPending(false), modeIndicatorShown(false), lastIndicatorMode(OFF),
          lastMatrixWasAnimating(false), matrixAnimating(false) {
        memset(sensorTriggered, 0, sizeof(sensorTriggered));
        memset(mockFrame, 0, sizeof(mockFrame));
        memset(sensorStatusDisplay, 0, sizeof(sensorStatusDisplay));
        memset(sensorDistanceZone, 0, sizeof(sensorDistanceZone));
        memset(sensorActive, 0, sizeof(sensorActive));
        memset(lastSensorDisplayState, 0, sizeof(lastSensorDisplayState));
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
        modeIndicatorShown = true;
        lastIndicatorMode = mode;
    }

    void handleLongPress() {
        rebootPending = true;
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

    void configureSensor(uint8_t slot, uint8_t zone, bool statusDisplay) {
        if (slot < 4) {
            sensorActive[slot] = true;
            sensorDistanceZone[slot] = zone;
            sensorStatusDisplay[slot] = statusDisplay;
        }
    }

    void setSensorTriggered(uint8_t slot, bool triggered) {
        if (slot < 4) {
            sensorTriggered[slot] = triggered;
        }
    }

    void setMatrixAnimating(bool animating) {
        matrixAnimating = animating;
    }

    // Mirrors StateMachine::updateSensorStatusLEDs() logic using mock frame buffer
    // Bit layout: MSB (bit 7) = x=0 (leftmost), LSB (bit 0) = x=7 (rightmost)
    void updateSensorStatusLEDs() {
        bool matrixBusy = matrixAnimating || rebootPending;

        if (lastMatrixWasAnimating && !matrixBusy) {
            memset(lastSensorDisplayState, 0, sizeof(lastSensorDisplayState));
        }
        lastMatrixWasAnimating = matrixBusy;

        if (matrixBusy) return;

        for (uint8_t i = 0; i < 4; i++) {
            if (!sensorActive[i] || !sensorStatusDisplay[i] ||
                (sensorDistanceZone[i] != 1 && sensorDistanceZone[i] != 2)) {
                continue;
            }

            uint8_t y1, y2;
            if (sensorDistanceZone[i] == 1) {  // Near: bottom-right
                y1 = 6; y2 = 7;
            } else {                            // Far:  top-right
                y1 = 0; y2 = 1;
            }

            bool currentMotion = sensorTriggered[i];

            if (currentMotion != lastSensorDisplayState[i]) {
                // x=7 → bit 0 (LSB)
                if (currentMotion) {
                    mockFrame[y1] |=  (1 << 0);
                    mockFrame[y2] |=  (1 << 0);
                } else {
                    mockFrame[y1] &= ~(1 << 0);
                    mockFrame[y2] &= ~(1 << 0);
                }
                lastSensorDisplayState[i] = currentMotion;
            }
        }
    }

    bool getPixel(uint8_t x, uint8_t y) const {
        if (x > 7 || y > 7) return false;
        // bit (7-x): MSB = x=0, LSB = x=7
        return (mockFrame[y] & (1 << (7 - x))) != 0;
    }

    void update() {
        // Check if warning expired
        if (warning_active && millis() >= warning_end_time) {
            warning_active = false;
        }
        updateSensorStatusLEDs();
    }

    OperatingMode getMode() const { return mode; }
    uint32_t getMotionEventCount() const { return motion_events; }
    uint32_t getModeChangeCount() const { return mode_changes; }
    bool isLedOn() const { return led_on || warning_active; }
    bool isWarningActive() const { return warning_active; }
    bool isRebootPending() const { return rebootPending; }
    bool isModeIndicatorShown() const { return modeIndicatorShown; }
    OperatingMode getLastIndicatorMode() const { return lastIndicatorMode; }

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

void test_long_press_triggers_reboot(void) {
    // Reboot should not be pending at startup
    TEST_ASSERT_FALSE(sm->isRebootPending());

    // A long press sets the reboot-pending flag
    sm->handleLongPress();
    TEST_ASSERT_TRUE(sm->isRebootPending());
}

void test_mode_indicator_on_cycle(void) {
    // No indicator has been shown yet
    TEST_ASSERT_FALSE(sm->isModeIndicatorShown());

    // Cycle OFF -> CONTINUOUS_ON: indicator shown for CONTINUOUS_ON
    sm->cycleMode();
    TEST_ASSERT_TRUE(sm->isModeIndicatorShown());
    TEST_ASSERT_EQUAL(CONTINUOUS_ON, sm->getLastIndicatorMode());

    // Cycle CONTINUOUS_ON -> MOTION_DETECT: indicator shown for MOTION_DETECT
    sm->cycleMode();
    TEST_ASSERT_TRUE(sm->isModeIndicatorShown());
    TEST_ASSERT_EQUAL(MOTION_DETECT, sm->getLastIndicatorMode());

    // Cycle MOTION_DETECT -> OFF: indicator shown for OFF
    sm->cycleMode();
    TEST_ASSERT_TRUE(sm->isModeIndicatorShown());
    TEST_ASSERT_EQUAL(OFF, sm->getLastIndicatorMode());
}

void test_short_press_does_not_reboot(void) {
    // Reboot should not be pending at startup
    TEST_ASSERT_FALSE(sm->isRebootPending());

    // Cycle through all three modes (three short presses)
    sm->cycleMode(); // OFF -> CONTINUOUS_ON
    TEST_ASSERT_FALSE(sm->isRebootPending());

    sm->cycleMode(); // CONTINUOUS_ON -> MOTION_DETECT
    TEST_ASSERT_FALSE(sm->isRebootPending());

    sm->cycleMode(); // MOTION_DETECT -> OFF
    TEST_ASSERT_FALSE(sm->isRebootPending());

    // Full second cycle to be thorough
    sm->cycleMode();
    sm->cycleMode();
    sm->cycleMode();
    TEST_ASSERT_FALSE(sm->isRebootPending());
}

void test_sensor_status_display(void) {
    // Configure slot 0 as Near PIR with status display on
    sm->configureSensor(0, 1, true);   // zone=1 (Near), statusDisplay=true
    // Configure slot 1 as Far PIR with status display on
    sm->configureSensor(1, 2, true);   // zone=2 (Far), statusDisplay=true

    // --- Trigger Near sensor (slot 0) ---
    sm->setSensorTriggered(0, true);
    sm->update();

    // Bottom-right pixels (7,6) and (7,7) should be ON
    TEST_ASSERT_TRUE(sm->getPixel(7, 6));
    TEST_ASSERT_TRUE(sm->getPixel(7, 7));
    // Top-right pixels (7,0) and (7,1) should still be OFF
    TEST_ASSERT_FALSE(sm->getPixel(7, 0));
    TEST_ASSERT_FALSE(sm->getPixel(7, 1));

    // --- Trigger Far sensor (slot 1) ---
    sm->setSensorTriggered(1, true);
    sm->update();

    // Top-right pixels should now be ON
    TEST_ASSERT_TRUE(sm->getPixel(7, 0));
    TEST_ASSERT_TRUE(sm->getPixel(7, 1));
    // Bottom-right still ON from slot 0
    TEST_ASSERT_TRUE(sm->getPixel(7, 6));
    TEST_ASSERT_TRUE(sm->getPixel(7, 7));

    // --- Clear Near sensor (slot 0) ---
    sm->setSensorTriggered(0, false);
    sm->update();

    // Bottom-right should be OFF
    TEST_ASSERT_FALSE(sm->getPixel(7, 6));
    TEST_ASSERT_FALSE(sm->getPixel(7, 7));
    // Top-right still ON (slot 1 still triggered)
    TEST_ASSERT_TRUE(sm->getPixel(7, 0));
    TEST_ASSERT_TRUE(sm->getPixel(7, 1));

    // --- Clear Far sensor (slot 1) ---
    sm->setSensorTriggered(1, false);
    sm->update();

    // Both corners OFF
    TEST_ASSERT_FALSE(sm->getPixel(7, 0));
    TEST_ASSERT_FALSE(sm->getPixel(7, 1));
    TEST_ASSERT_FALSE(sm->getPixel(7, 6));
    TEST_ASSERT_FALSE(sm->getPixel(7, 7));
}

void test_sensor_status_suppressed_during_animation(void) {
    sm->configureSensor(0, 1, true);   // Near sensor with status display

    // Sensor is triggered while matrix is animating
    sm->setSensorTriggered(0, true);
    sm->setMatrixAnimating(true);
    sm->update();

    // LEDs should NOT be drawn while animating
    TEST_ASSERT_FALSE(sm->getPixel(7, 6));
    TEST_ASSERT_FALSE(sm->getPixel(7, 7));

    // Animation ends — status LEDs should appear on next update
    sm->setMatrixAnimating(false);
    sm->update();

    TEST_ASSERT_TRUE(sm->getPixel(7, 6));
    TEST_ASSERT_TRUE(sm->getPixel(7, 7));
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
    RUN_TEST(test_long_press_triggers_reboot);
    RUN_TEST(test_mode_indicator_on_cycle);
    RUN_TEST(test_short_press_does_not_reboot);
    RUN_TEST(test_sensor_status_display);
    RUN_TEST(test_sensor_status_suppressed_during_animation);

    return UNITY_END();
}
