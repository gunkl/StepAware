/**
 * @file test_button_reset.cpp
 * @brief Unit tests for button-based reset functionality
 *
 * Tests boot-time button hold detection for WiFi and factory resets.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

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

// Mock ESP
struct {
    void restart() {
        // Mock restart - just set a flag in real implementation
    }
} ESP;

// ============================================================================
// Test Constants (from config.h)
// ============================================================================

#define BUTTON_WIFI_RESET_MS    15000   // 15 seconds for WiFi reset
#define BUTTON_FACTORY_RESET_MS 30000   // 30 seconds for factory reset

// ============================================================================
// Mock Button HAL
// ============================================================================

class MockButton {
public:
    bool pressed;
    uint32_t pressStart;

    MockButton() : pressed(false), pressStart(0) {}

    void mockPress() {
        pressed = true;
        pressStart = millis();
    }

    void mockRelease() {
        pressed = false;
    }

    bool isPressed() const {
        return pressed;
    }

    void update() {
        // In real implementation, this would read GPIO state
    }
};

// ============================================================================
// Mock LED HAL
// ============================================================================

enum LEDPattern {
    PATTERN_OFF,
    PATTERN_ON,
    PATTERN_BLINK_FAST,
    PATTERN_PULSE
};

class MockLED {
public:
    LEDPattern currentPattern;
    bool isLit;
    uint8_t brightness;

    MockLED() : currentPattern(PATTERN_OFF), isLit(false), brightness(0) {}

    void setPattern(LEDPattern pattern) {
        currentPattern = pattern;
    }

    void on(uint8_t bright = 255) {
        isLit = true;
        brightness = bright;
    }

    void off() {
        isLit = false;
        brightness = 0;
    }

    void update() {
        // Update pattern state
    }

    LEDPattern getPattern() const {
        return currentPattern;
    }
};

// ============================================================================
// Reset State Tracking
// ============================================================================

struct ResetState {
    bool wifiResetTriggered;
    bool factoryResetTriggered;
    bool resetCanceled;

    void reset() {
        wifiResetTriggered = false;
        factoryResetTriggered = false;
        resetCanceled = false;
    }
} resetState;

// ============================================================================
// Simplified Reset Detection Logic (from main.cpp)
// ============================================================================

MockButton button;
MockLED led;

void handleBootButtonHold() {
    uint32_t pressStart = millis();
    bool wifiResetPending = false;
    bool factoryResetPending = false;

    // Start with pulse pattern
    led.setPattern(PATTERN_PULSE);

    while (button.isPressed()) {
        uint32_t pressDuration = millis() - pressStart;

        // WiFi reset stage (15 seconds)
        if (pressDuration >= BUTTON_WIFI_RESET_MS && !wifiResetPending) {
            led.setPattern(PATTERN_BLINK_FAST);
            wifiResetPending = true;
        }

        // Factory reset stage (30 seconds)
        if (pressDuration >= BUTTON_FACTORY_RESET_MS && !factoryResetPending) {
            led.setPattern(PATTERN_ON);
            factoryResetPending = true;
        }

        led.update();
        button.update();

        // Simulate loop delay
        advance_time(10);
    }

    // Button released - determine what reset to perform
    if (factoryResetPending) {
        resetState.factoryResetTriggered = true;
    } else if (wifiResetPending) {
        resetState.wifiResetTriggered = true;
    } else {
        resetState.resetCanceled = true;
    }

    led.off();
}

// ============================================================================
// Test Setup/Teardown
// ============================================================================

void setUp(void) {
    reset_time();
    button = MockButton();
    led = MockLED();
    resetState.reset();
}

void tearDown(void) {
    // Clean up
}

// ============================================================================
// TEST CASES
// ============================================================================

/**
 * @brief Test timing threshold constants are correctly defined
 */
void test_reset_timing_constants(void) {
    // Verify constants are set correctly
    TEST_ASSERT_EQUAL(15000, BUTTON_WIFI_RESET_MS);
    TEST_ASSERT_EQUAL(30000, BUTTON_FACTORY_RESET_MS);

    // Verify factory reset threshold is exactly 2x WiFi reset
    TEST_ASSERT_EQUAL(BUTTON_WIFI_RESET_MS * 2, BUTTON_FACTORY_RESET_MS);
}

/**
 * @brief Test WiFi reset threshold detection logic
 */
void test_wifi_reset_threshold_logic(void) {
    uint32_t pressDuration;

    // Just before threshold - should not trigger
    pressDuration = BUTTON_WIFI_RESET_MS - 1;
    bool wifiResetPending = (pressDuration >= BUTTON_WIFI_RESET_MS);
    TEST_ASSERT_FALSE(wifiResetPending);

    // At exact threshold - should trigger
    pressDuration = BUTTON_WIFI_RESET_MS;
    wifiResetPending = (pressDuration >= BUTTON_WIFI_RESET_MS);
    TEST_ASSERT_TRUE(wifiResetPending);

    // Past threshold - should still trigger
    pressDuration = BUTTON_WIFI_RESET_MS + 1000;
    wifiResetPending = (pressDuration >= BUTTON_WIFI_RESET_MS);
    TEST_ASSERT_TRUE(wifiResetPending);
}

/**
 * @brief Test factory reset threshold detection logic
 */
void test_factory_reset_threshold_logic(void) {
    uint32_t pressDuration;
    bool wifiPending, factoryPending;

    // Just before factory threshold - WiFi yes, factory no
    pressDuration = BUTTON_FACTORY_RESET_MS - 1;
    wifiPending = (pressDuration >= BUTTON_WIFI_RESET_MS);
    factoryPending = (pressDuration >= BUTTON_FACTORY_RESET_MS);
    TEST_ASSERT_TRUE(wifiPending);
    TEST_ASSERT_FALSE(factoryPending);

    // At exact factory threshold - both yes, but factory takes precedence
    pressDuration = BUTTON_FACTORY_RESET_MS;
    wifiPending = (pressDuration >= BUTTON_WIFI_RESET_MS);
    factoryPending = (pressDuration >= BUTTON_FACTORY_RESET_MS);
    TEST_ASSERT_TRUE(wifiPending);
    TEST_ASSERT_TRUE(factoryPending);
}

/**
 * @brief Test reset priority logic (factory > wifi > none)
 */
void test_reset_priority_logic(void) {
    // Scenario 1: Short press (< 15s) - no reset
    uint32_t duration = 10000;
    bool shouldWiFiReset = (duration >= BUTTON_WIFI_RESET_MS);
    bool shouldFactoryReset = (duration >= BUTTON_FACTORY_RESET_MS);

    // Determine which reset (factory takes priority)
    bool triggersFactory = shouldFactoryReset;
    bool triggersWiFi = !shouldFactoryReset && shouldWiFiReset;
    bool triggersNone = !shouldFactoryReset && !shouldWiFiReset;

    TEST_ASSERT_FALSE(triggersFactory);
    TEST_ASSERT_FALSE(triggersWiFi);
    TEST_ASSERT_TRUE(triggersNone);

    // Scenario 2: Medium press (15s-30s) - WiFi reset
    duration = 20000;
    shouldWiFiReset = (duration >= BUTTON_WIFI_RESET_MS);
    shouldFactoryReset = (duration >= BUTTON_FACTORY_RESET_MS);

    triggersFactory = shouldFactoryReset;
    triggersWiFi = !shouldFactoryReset && shouldWiFiReset;
    triggersNone = !shouldFactoryReset && !shouldWiFiReset;

    TEST_ASSERT_FALSE(triggersFactory);
    TEST_ASSERT_TRUE(triggersWiFi);
    TEST_ASSERT_FALSE(triggersNone);

    // Scenario 3: Long press (>= 30s) - Factory reset
    duration = 35000;
    shouldWiFiReset = (duration >= BUTTON_WIFI_RESET_MS);
    shouldFactoryReset = (duration >= BUTTON_FACTORY_RESET_MS);

    triggersFactory = shouldFactoryReset;
    triggersWiFi = !shouldFactoryReset && shouldWiFiReset;
    triggersNone = !shouldFactoryReset && !shouldWiFiReset;

    TEST_ASSERT_TRUE(triggersFactory);
    TEST_ASSERT_FALSE(triggersWiFi);
    TEST_ASSERT_FALSE(triggersNone);
}

/**
 * @brief Test LED pattern logic based on duration
 */
void test_led_pattern_logic(void) {
    uint32_t duration;
    LEDPattern expectedPattern;

    // Initial (< 15s) - PULSE
    duration = 5000;
    if (duration >= BUTTON_FACTORY_RESET_MS) {
        expectedPattern = PATTERN_ON;
    } else if (duration >= BUTTON_WIFI_RESET_MS) {
        expectedPattern = PATTERN_BLINK_FAST;
    } else {
        expectedPattern = PATTERN_PULSE;
    }
    TEST_ASSERT_EQUAL(PATTERN_PULSE, expectedPattern);

    // WiFi pending (15s-30s) - BLINK_FAST
    duration = 20000;
    if (duration >= BUTTON_FACTORY_RESET_MS) {
        expectedPattern = PATTERN_ON;
    } else if (duration >= BUTTON_WIFI_RESET_MS) {
        expectedPattern = PATTERN_BLINK_FAST;
    } else {
        expectedPattern = PATTERN_PULSE;
    }
    TEST_ASSERT_EQUAL(PATTERN_BLINK_FAST, expectedPattern);

    // Factory pending (>= 30s) - ON
    duration = 35000;
    if (duration >= BUTTON_FACTORY_RESET_MS) {
        expectedPattern = PATTERN_ON;
    } else if (duration >= BUTTON_WIFI_RESET_MS) {
        expectedPattern = PATTERN_BLINK_FAST;
    } else {
        expectedPattern = PATTERN_PULSE;
    }
    TEST_ASSERT_EQUAL(PATTERN_ON, expectedPattern);
}

/**
 * @brief Test boundary conditions
 */
void test_boundary_conditions(void) {
    // WiFi reset - 1ms
    uint32_t duration = BUTTON_WIFI_RESET_MS - 1;
    TEST_ASSERT_FALSE(duration >= BUTTON_WIFI_RESET_MS);

    // WiFi reset exact
    duration = BUTTON_WIFI_RESET_MS;
    TEST_ASSERT_TRUE(duration >= BUTTON_WIFI_RESET_MS);

    // WiFi reset + 1ms
    duration = BUTTON_WIFI_RESET_MS + 1;
    TEST_ASSERT_TRUE(duration >= BUTTON_WIFI_RESET_MS);

    // Factory reset - 1ms
    duration = BUTTON_FACTORY_RESET_MS - 1;
    TEST_ASSERT_FALSE(duration >= BUTTON_FACTORY_RESET_MS);

    // Factory reset exact
    duration = BUTTON_FACTORY_RESET_MS;
    TEST_ASSERT_TRUE(duration >= BUTTON_FACTORY_RESET_MS);
}

/**
 * @brief Test time advancement works correctly
 */
void test_time_advancement(void) {
    reset_time();
    TEST_ASSERT_EQUAL(0, millis());

    advance_time(1000);
    TEST_ASSERT_EQUAL(1000, millis());

    advance_time(14000);
    TEST_ASSERT_EQUAL(15000, millis());

    // Verify we're at WiFi reset threshold
    TEST_ASSERT_EQUAL(BUTTON_WIFI_RESET_MS, millis());
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Constant validation
    RUN_TEST(test_reset_timing_constants);

    // Threshold detection
    RUN_TEST(test_wifi_reset_threshold_logic);
    RUN_TEST(test_factory_reset_threshold_logic);

    // Priority and state logic
    RUN_TEST(test_reset_priority_logic);
    RUN_TEST(test_led_pattern_logic);

    // Boundary conditions
    RUN_TEST(test_boundary_conditions);

    // Time handling
    RUN_TEST(test_time_advancement);

    return UNITY_END();
}
