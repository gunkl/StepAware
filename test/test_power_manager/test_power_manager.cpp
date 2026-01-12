/**
 * @file test_power_manager.cpp
 * @brief Unit tests for Power Manager
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

// Mock delay
void delay(unsigned long ms) {
    advance_time(ms);
}

// Power Manager states
enum PowerState {
    STATE_ACTIVE = 0,
    STATE_LIGHT_SLEEP = 1,
    STATE_DEEP_SLEEP = 2,
    STATE_LOW_BATTERY = 3,
    STATE_CRITICAL_BATTERY = 4,
    STATE_CHARGING = 5
};

// Simplified Power Manager for testing
class TestPowerManager {
private:
    PowerState state;
    float batteryVoltage;
    uint8_t batteryPercentage;
    bool charging;
    bool lowBattery;
    bool criticalBattery;
    uint32_t lastActivity;
    uint32_t startTime;
    uint32_t wakeCount;
    uint32_t deepSleepCount;
    uint32_t lightSleepTimeout;
    uint32_t deepSleepTimeout;
    float lowBatteryThreshold;
    float criticalBatteryThreshold;

    // Voltage filter
    static const uint8_t VOLTAGE_SAMPLES = 10;
    float voltageSamples[VOLTAGE_SAMPLES];
    uint8_t voltageSampleIndex;
    bool voltageSamplesFilled;

public:
    TestPowerManager()
        : state(STATE_ACTIVE)
        , batteryVoltage(3.8f)
        , batteryPercentage(50)
        , charging(false)
        , lowBattery(false)
        , criticalBattery(false)
        , lastActivity(0)
        , startTime(0)
        , wakeCount(0)
        , deepSleepCount(0)
        , lightSleepTimeout(30000)
        , deepSleepTimeout(300000)
        , lowBatteryThreshold(3.4f)
        , criticalBatteryThreshold(3.2f)
        , voltageSampleIndex(0)
        , voltageSamplesFilled(false)
    {
        for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
            voltageSamples[i] = 0.0f;
        }
    }

    void begin() {
        startTime = millis();
        lastActivity = millis();
        updateBatteryStatus();
    }

    void updateBatteryStatus() {
        // Add voltage to filter
        addVoltageSample(batteryVoltage);
        float filteredVoltage = getFilteredVoltage();

        // Calculate percentage
        batteryPercentage = calculateBatteryPercentage(filteredVoltage);

        // Update flags
        lowBattery = (filteredVoltage < lowBatteryThreshold);
        criticalBattery = (filteredVoltage < criticalBatteryThreshold);

        // Update state based on battery
        handlePowerState();
    }

    uint8_t calculateBatteryPercentage(float voltage) {
        if (voltage >= 4.2f) return 100;
        if (voltage <= 3.0f) return 0;

        if (voltage >= 3.7f) {
            return 50 + (uint8_t)(((voltage - 3.7f) / 0.5f) * 50.0f);
        } else {
            return (uint8_t)(((voltage - 3.0f) / 0.7f) * 50.0f);
        }
    }

    void addVoltageSample(float voltage) {
        voltageSamples[voltageSampleIndex] = voltage;
        voltageSampleIndex = (voltageSampleIndex + 1) % VOLTAGE_SAMPLES;
        if (voltageSampleIndex == 0) {
            voltageSamplesFilled = true;
        }
    }

    float getFilteredVoltage() {
        float sum = 0.0f;
        int count = voltageSamplesFilled ? VOLTAGE_SAMPLES : voltageSampleIndex;
        if (count == 0) return batteryVoltage;

        for (int i = 0; i < count; i++) {
            sum += voltageSamples[i];
        }
        return sum / count;
    }

    void update() {
        handlePowerState();
    }

    void handlePowerState() {
        switch (state) {
            case STATE_ACTIVE:
                if (criticalBattery && !charging) {
                    state = STATE_CRITICAL_BATTERY;
                } else if (lowBattery && !charging) {
                    state = STATE_LOW_BATTERY;
                } else if (charging) {
                    state = STATE_CHARGING;
                } else if (shouldEnterSleep()) {
                    enterLightSleep();
                }
                break;

            case STATE_LOW_BATTERY:
                if (charging) {
                    state = STATE_CHARGING;
                } else if (!lowBattery) {
                    state = STATE_ACTIVE;
                } else if (criticalBattery) {
                    state = STATE_CRITICAL_BATTERY;
                }
                break;

            case STATE_CRITICAL_BATTERY:
                if (charging) {
                    state = STATE_CHARGING;
                }
                break;

            case STATE_CHARGING:
                if (!charging) {
                    if (criticalBattery) {
                        state = STATE_CRITICAL_BATTERY;
                    } else if (lowBattery) {
                        state = STATE_LOW_BATTERY;
                    } else {
                        state = STATE_ACTIVE;
                    }
                }
                break;

            default:
                break;
        }
    }

    bool shouldEnterSleep() {
        uint32_t idleTime = millis() - lastActivity;
        return idleTime >= lightSleepTimeout;
    }

    void enterLightSleep() {
        state = STATE_LIGHT_SLEEP;
    }

    void enterDeepSleep() {
        state = STATE_DEEP_SLEEP;
        deepSleepCount++;
    }

    void wakeUp() {
        state = STATE_ACTIVE;
        wakeCount++;
        lastActivity = millis();
    }

    void recordActivity() {
        lastActivity = millis();
    }

    uint32_t getTimeSinceActivity() const {
        return millis() - lastActivity;
    }

    // Getters
    PowerState getState() const { return state; }
    float getBatteryVoltage() const { return const_cast<TestPowerManager*>(this)->getFilteredVoltage(); }
    uint8_t getBatteryPercentage() const { return batteryPercentage; }
    bool isCharging() const { return charging; }
    bool isBatteryLow() const { return lowBattery; }
    bool isBatteryCritical() const { return criticalBattery; }
    uint32_t getWakeCount() const { return wakeCount; }
    uint32_t getDeepSleepCount() const { return deepSleepCount; }

    // Setters for testing
    void setBatteryVoltage(float voltage) {
        batteryVoltage = voltage;
        // Fill the entire filter with the new voltage for immediate response
        for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
            voltageSamples[i] = voltage;
        }
        voltageSamplesFilled = true;
        updateBatteryStatus();
    }

    void addBatteryVoltageSample(float voltage) {
        batteryVoltage = voltage;
        updateBatteryStatus();
    }

    void setCharging(bool isCharging) {
        charging = isCharging;
        handlePowerState();
    }

    void reset() {
        state = STATE_ACTIVE;
        batteryVoltage = 3.8f;
        batteryPercentage = 50;
        charging = false;
        lowBattery = false;
        criticalBattery = false;
        lastActivity = 0;
        startTime = 0;
        wakeCount = 0;
        deepSleepCount = 0;
        voltageSampleIndex = 0;
        voltageSamplesFilled = false;
        for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
            voltageSamples[i] = 0.0f;
        }
    }
};

// Global test instance
TestPowerManager power;

// ============================================================================
// TEST CASES
// ============================================================================

void setUp(void) {
    reset_time();
    power.reset();
}

void tearDown(void) {
    // Clean up after each test
}

/**
 * @brief Test power manager initialization
 */
void test_power_initialization(void) {
    power.begin();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
}

/**
 * @brief Test battery percentage calculation - full battery
 */
void test_battery_percentage_full(void) {
    power.setBatteryVoltage(4.2f);
    TEST_ASSERT_EQUAL(100, power.getBatteryPercentage());
}

/**
 * @brief Test battery percentage calculation - nominal
 */
void test_battery_percentage_nominal(void) {
    power.setBatteryVoltage(3.7f);
    TEST_ASSERT_EQUAL(50, power.getBatteryPercentage());
}

/**
 * @brief Test battery percentage calculation - low
 */
void test_battery_percentage_low(void) {
    power.setBatteryVoltage(3.4f);
    uint8_t percentage = power.getBatteryPercentage();
    TEST_ASSERT_TRUE(percentage >= 28 && percentage <= 30); // ~29%
}

/**
 * @brief Test battery percentage calculation - critical
 */
void test_battery_percentage_critical(void) {
    power.setBatteryVoltage(3.2f);
    uint8_t percentage = power.getBatteryPercentage();
    TEST_ASSERT_TRUE(percentage >= 13 && percentage <= 15); // ~14%
}

/**
 * @brief Test battery percentage calculation - empty
 */
void test_battery_percentage_empty(void) {
    power.setBatteryVoltage(3.0f);
    TEST_ASSERT_EQUAL(0, power.getBatteryPercentage());
}

/**
 * @brief Test low battery detection
 */
void test_low_battery_detection(void) {
    power.begin();

    // Start with good battery
    power.setBatteryVoltage(3.8f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
    TEST_ASSERT_FALSE(power.isBatteryLow());

    // Drop to low battery
    power.setBatteryVoltage(3.3f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_LOW_BATTERY, power.getState());
    TEST_ASSERT_TRUE(power.isBatteryLow());
}

/**
 * @brief Test critical battery detection
 */
void test_critical_battery_detection(void) {
    power.begin();

    // Start with good battery
    power.setBatteryVoltage(3.8f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());

    // Drop to critical battery
    power.setBatteryVoltage(3.1f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CRITICAL_BATTERY, power.getState());
    TEST_ASSERT_TRUE(power.isBatteryCritical());
}

/**
 * @brief Test charging detection
 */
void test_charging_detection(void) {
    power.begin();

    // Start not charging
    power.setCharging(false);
    power.update();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());

    // Start charging
    power.setCharging(true);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CHARGING, power.getState());
    TEST_ASSERT_TRUE(power.isCharging());
}

/**
 * @brief Test charging overrides low battery
 */
void test_charging_overrides_low_battery(void) {
    power.begin();

    // Low battery
    power.setBatteryVoltage(3.3f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_LOW_BATTERY, power.getState());

    // Plug in charger
    power.setCharging(true);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CHARGING, power.getState());
}

/**
 * @brief Test charging overrides critical battery
 */
void test_charging_overrides_critical_battery(void) {
    power.begin();

    // Critical battery
    power.setBatteryVoltage(3.1f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CRITICAL_BATTERY, power.getState());

    // Plug in charger
    power.setCharging(true);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CHARGING, power.getState());
}

/**
 * @brief Test idle timeout triggers light sleep
 */
void test_idle_timeout_light_sleep(void) {
    power.begin();

    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());

    // Advance time past light sleep timeout
    advance_time(31000); // 31 seconds
    power.update();

    TEST_ASSERT_EQUAL(STATE_LIGHT_SLEEP, power.getState());
}

/**
 * @brief Test activity recording resets idle timer
 */
void test_activity_resets_idle_timer(void) {
    power.begin();

    // Advance time but keep recording activity
    advance_time(15000); // 15 seconds
    power.recordActivity();
    advance_time(15000); // Another 15 seconds (30 total, but activity at 15s)
    power.update();

    // Should still be active (only 15s since last activity)
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
}

/**
 * @brief Test wake from sleep
 */
void test_wake_from_sleep(void) {
    power.begin();

    // Enter sleep
    advance_time(31000);
    power.update();
    TEST_ASSERT_EQUAL(STATE_LIGHT_SLEEP, power.getState());

    // Wake up
    power.wakeUp();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
    TEST_ASSERT_EQUAL(1, power.getWakeCount());
}

/**
 * @brief Test deep sleep counter
 */
void test_deep_sleep_counter(void) {
    power.begin();

    TEST_ASSERT_EQUAL(0, power.getDeepSleepCount());

    power.enterDeepSleep();
    TEST_ASSERT_EQUAL(1, power.getDeepSleepCount());

    power.wakeUp();
    power.enterDeepSleep();
    TEST_ASSERT_EQUAL(2, power.getDeepSleepCount());
}

/**
 * @brief Test voltage filtering (moving average)
 */
void test_voltage_filtering(void) {
    power.begin();

    // Add noisy samples
    for (int i = 0; i < 10; i++) {
        float voltage = 3.7f + (i % 2 ? 0.1f : -0.1f); // Oscillate between 3.6 and 3.8
        power.addBatteryVoltageSample(voltage);
    }

    // Filtered voltage should be close to 3.7V
    float filtered = power.getBatteryVoltage();
    TEST_ASSERT_TRUE(filtered >= 3.65f && filtered <= 3.75f);
}

/**
 * @brief Test time since activity
 */
void test_time_since_activity(void) {
    power.begin();

    TEST_ASSERT_EQUAL(0, power.getTimeSinceActivity());

    advance_time(5000);
    TEST_ASSERT_EQUAL(5000, power.getTimeSinceActivity());

    power.recordActivity();
    TEST_ASSERT_EQUAL(0, power.getTimeSinceActivity());
}

/**
 * @brief Test battery recovery from low to normal
 */
void test_battery_recovery_low_to_normal(void) {
    power.begin();

    // Enter low battery state
    power.setBatteryVoltage(3.3f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_LOW_BATTERY, power.getState());

    // Battery recovers (e.g., after charging)
    power.setBatteryVoltage(3.8f);
    power.update();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
}

/**
 * @brief Test state transitions from charging back to normal
 */
void test_charging_to_normal_transition(void) {
    power.begin();

    // Start charging
    power.setCharging(true);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CHARGING, power.getState());

    // Unplug with good battery
    power.setBatteryVoltage(3.8f);
    power.setCharging(false);
    power.update();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, power.getState());
}

/**
 * @brief Test unplugging with low battery returns to low battery state
 */
void test_charging_to_low_battery_transition(void) {
    power.begin();

    // Charging with low battery
    power.setBatteryVoltage(3.3f);
    power.setCharging(true);
    power.update();
    TEST_ASSERT_EQUAL(STATE_CHARGING, power.getState());

    // Unplug while still low
    power.setCharging(false);
    power.update();
    TEST_ASSERT_EQUAL(STATE_LOW_BATTERY, power.getState());
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Initialization tests
    RUN_TEST(test_power_initialization);

    // Battery percentage tests
    RUN_TEST(test_battery_percentage_full);
    RUN_TEST(test_battery_percentage_nominal);
    RUN_TEST(test_battery_percentage_low);
    RUN_TEST(test_battery_percentage_critical);
    RUN_TEST(test_battery_percentage_empty);

    // Battery state tests
    RUN_TEST(test_low_battery_detection);
    RUN_TEST(test_critical_battery_detection);
    RUN_TEST(test_charging_detection);
    RUN_TEST(test_charging_overrides_low_battery);
    RUN_TEST(test_charging_overrides_critical_battery);

    // Sleep management tests
    RUN_TEST(test_idle_timeout_light_sleep);
    RUN_TEST(test_activity_resets_idle_timer);
    RUN_TEST(test_wake_from_sleep);
    RUN_TEST(test_deep_sleep_counter);

    // Filtering and utility tests
    RUN_TEST(test_voltage_filtering);
    RUN_TEST(test_time_since_activity);

    // State transition tests
    RUN_TEST(test_battery_recovery_low_to_normal);
    RUN_TEST(test_charging_to_normal_transition);
    RUN_TEST(test_charging_to_low_battery_transition);

    return UNITY_END();
}
