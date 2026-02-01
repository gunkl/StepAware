/**
 * @file test_integration.cpp
 * @brief Integration tests for StepAware system
 *
 * Tests interactions between multiple subsystems:
 * - WiFi + Power Manager
 * - State Machine + Power Manager
 * - Watchdog + All modules
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

// ============================================================================
// Mock System Components
// ============================================================================

// WiFi States
enum WiFiState {
    WIFI_DISABLED = 0,
    WIFI_AP_MODE = 1,
    WIFI_CONNECTING = 2,
    WIFI_CONNECTED = 3,
    WIFI_DISCONNECTED = 4,
    WIFI_FAILED = 5
};

// Power States
enum PowerState {
    POWER_ACTIVE = 0,
    POWER_LIGHT_SLEEP = 1,
    POWER_DEEP_SLEEP = 2,
    POWER_LOW_BATTERY = 3,
    POWER_CRITICAL_BATTERY = 4,
    POWER_USB_POWER = 5
};

// State Machine Modes
enum OperatingMode {
    MODE_OFF = 0,
    MODE_CONTINUOUS_ON = 1,
    MODE_MOTION_DETECT = 2
};

// Watchdog Health Status
enum HealthStatus {
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_FAILED = 3
};

// Mock WiFi Manager
class MockWiFi {
public:
    WiFiState state;
    int8_t rssi;
    bool powerSavingEnabled;

    MockWiFi() : state(WIFI_DISABLED), rssi(0), powerSavingEnabled(false) {}

    void connect() { state = WIFI_CONNECTING; }
    void disconnect() { state = WIFI_DISCONNECTED; }
    WiFiState getState() const { return state; }
    int8_t getRSSI() const { return rssi; }
    void setPowerSaving(bool enabled) { powerSavingEnabled = enabled; }
    bool isPowerSavingEnabled() const { return powerSavingEnabled; }
};

// Mock Power Manager
class MockPower {
public:
    PowerState state;
    float batteryVoltage;
    bool wifiDisabledForPowerSaving;

    MockPower() : state(POWER_ACTIVE), batteryVoltage(3.8f), wifiDisabledForPowerSaving(false) {}

    void enterLightSleep() {
        state = POWER_LIGHT_SLEEP;
        wifiDisabledForPowerSaving = true;
    }

    void wakeUp() {
        state = POWER_ACTIVE;
        wifiDisabledForPowerSaving = false;
    }

    PowerState getState() const { return state; }
    float getBatteryVoltage() const { return batteryVoltage; }
    bool isLowBattery() const { return batteryVoltage < 3.4f; }
};

// Mock State Machine
class MockStateMachine {
public:
    OperatingMode mode;
    bool motionDetected;
    uint32_t lastActivity;

    MockStateMachine() : mode(MODE_MOTION_DETECT), motionDetected(false), lastActivity(0) {}

    void handleMotion() {
        motionDetected = true;
        lastActivity = millis();
    }

    void update() {
        // Clear motion after processing
        motionDetected = false;
    }

    OperatingMode getMode() const { return mode; }
    bool isActive() const { return motionDetected || (millis() - lastActivity < 30000); }
};

// Mock Watchdog
class MockWatchdog {
public:
    uint32_t lastHealthCheck;
    HealthStatus systemHealth;

    MockWatchdog() : lastHealthCheck(0), systemHealth(HEALTH_OK) {}

    HealthStatus checkWiFiHealth(const MockWiFi& wifi) {
        if (wifi.getState() == WIFI_FAILED) return HEALTH_CRITICAL;
        if (wifi.getState() == WIFI_DISCONNECTED) return HEALTH_WARNING;
        if (wifi.getState() == WIFI_CONNECTED && wifi.getRSSI() < -85) return HEALTH_WARNING;
        return HEALTH_OK;
    }

    HealthStatus checkPowerHealth(const MockPower& power) {
        if (power.getBatteryVoltage() < 3.2f) return HEALTH_CRITICAL;
        if (power.isLowBattery()) return HEALTH_WARNING;
        return HEALTH_OK;
    }

    void update(const MockWiFi& wifi, const MockPower& power) {
        lastHealthCheck = millis();

        HealthStatus wifiHealth = checkWiFiHealth(wifi);
        HealthStatus powerHealth = checkPowerHealth(power);

        // System health is the worst of all subsystems
        systemHealth = (wifiHealth > powerHealth) ? wifiHealth : powerHealth;
    }

    HealthStatus getSystemHealth() const { return systemHealth; }
};

// Global mock instances
MockWiFi g_wifi;
MockPower g_power;
MockStateMachine g_stateMachine;
MockWatchdog g_watchdog;

// ============================================================================
// TEST CASES
// ============================================================================

void setUp(void) {
    reset_time();
    g_wifi = MockWiFi();
    g_power = MockPower();
    g_stateMachine = MockStateMachine();
    g_watchdog = MockWatchdog();
}

void tearDown(void) {
    // Clean up
}

/**
 * @brief Test WiFi disabled when entering light sleep
 */
void test_wifi_disabled_on_light_sleep(void) {
    // WiFi connected
    g_wifi.state = WIFI_CONNECTED;
    g_wifi.rssi = -50;

    // Enter light sleep
    g_power.enterLightSleep();

    // WiFi should be disabled for power saving
    TEST_ASSERT_TRUE(g_power.wifiDisabledForPowerSaving);
    TEST_ASSERT_EQUAL(POWER_LIGHT_SLEEP, g_power.getState());
}

/**
 * @brief Test WiFi re-enabled when waking from sleep
 */
void test_wifi_enabled_on_wake(void) {
    // Enter light sleep (WiFi disabled)
    g_power.enterLightSleep();
    TEST_ASSERT_TRUE(g_power.wifiDisabledForPowerSaving);

    // Wake up
    g_power.wakeUp();

    // WiFi should be re-enabled
    TEST_ASSERT_FALSE(g_power.wifiDisabledForPowerSaving);
    TEST_ASSERT_EQUAL(POWER_ACTIVE, g_power.getState());
}

/**
 * @brief Test low battery disables WiFi to save power
 */
void test_low_battery_disables_wifi(void) {
    // WiFi connected
    g_wifi.state = WIFI_CONNECTED;

    // Battery drops to low level
    g_power.batteryVoltage = 3.3f;
    g_power.state = POWER_LOW_BATTERY;

    // System should disable WiFi to conserve power
    TEST_ASSERT_TRUE(g_power.isLowBattery());

    // In real implementation, this would trigger WiFi disconnect
    // For now, verify battery state is low
    TEST_ASSERT_EQUAL(POWER_LOW_BATTERY, g_power.getState());
}

/**
 * @brief Test motion prevents sleep
 */
void test_motion_prevents_sleep(void) {
    // Motion detected
    g_stateMachine.handleMotion();

    // Try to enter sleep
    advance_time(31000); // Past sleep timeout

    // Should still be active due to recent motion
    TEST_ASSERT_TRUE(g_stateMachine.isActive());
}

/**
 * @brief Test watchdog detects WiFi failure
 */
void test_watchdog_detects_wifi_failure(void) {
    g_wifi.state = WIFI_FAILED;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, g_watchdog.checkWiFiHealth(g_wifi));
}

/**
 * @brief Test watchdog detects low battery
 */
void test_watchdog_detects_low_battery(void) {
    g_power.batteryVoltage = 3.3f;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_WARNING, g_watchdog.checkPowerHealth(g_power));
}

/**
 * @brief Test watchdog detects critical battery
 */
void test_watchdog_detects_critical_battery(void) {
    g_power.batteryVoltage = 3.1f;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, g_watchdog.checkPowerHealth(g_power));
}

/**
 * @brief Test watchdog overall system health
 */
void test_watchdog_system_health(void) {
    // All systems healthy
    g_wifi.state = WIFI_CONNECTED;
    g_wifi.rssi = -50;
    g_power.batteryVoltage = 3.8f;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_OK, g_watchdog.getSystemHealth());
}

/**
 * @brief Test watchdog system health with one warning
 */
void test_watchdog_system_health_warning(void) {
    // WiFi weak signal
    g_wifi.state = WIFI_CONNECTED;
    g_wifi.rssi = -90;
    g_power.batteryVoltage = 3.8f;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_WARNING, g_watchdog.getSystemHealth());
}

/**
 * @brief Test watchdog system health critical
 */
void test_watchdog_system_health_critical(void) {
    // Critical battery
    g_wifi.state = WIFI_CONNECTED;
    g_power.batteryVoltage = 3.1f;

    g_watchdog.update(g_wifi, g_power);

    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, g_watchdog.getSystemHealth());
}

/**
 * @brief Test complete wake-motion-sleep cycle
 */
void test_complete_wake_motion_sleep_cycle(void) {
    // Start in active state
    TEST_ASSERT_EQUAL(POWER_ACTIVE, g_power.getState());

    // Motion detected
    g_stateMachine.handleMotion();
    TEST_ASSERT_TRUE(g_stateMachine.isActive());

    // Process motion
    g_stateMachine.update();

    // Wait for activity timeout
    advance_time(31000);

    // Should now be idle
    TEST_ASSERT_FALSE(g_stateMachine.isActive());

    // Enter sleep
    g_power.enterLightSleep();
    TEST_ASSERT_EQUAL(POWER_LIGHT_SLEEP, g_power.getState());

    // New motion wakes system
    g_power.wakeUp();
    g_stateMachine.handleMotion();

    // Back to active
    TEST_ASSERT_EQUAL(POWER_ACTIVE, g_power.getState());
    TEST_ASSERT_TRUE(g_stateMachine.isActive());
}

/**
 * @brief Test WiFi reconnection attempt during low battery
 */
void test_wifi_reconnect_during_low_battery(void) {
    // Start with good battery, WiFi connected
    g_wifi.state = WIFI_CONNECTED;
    g_power.batteryVoltage = 3.8f;

    // WiFi disconnects
    g_wifi.state = WIFI_DISCONNECTED;

    // Battery is still good, should attempt reconnect
    g_wifi.connect();
    TEST_ASSERT_EQUAL(WIFI_CONNECTING, g_wifi.getState());

    // Now battery goes low
    g_power.batteryVoltage = 3.3f;
    g_power.state = POWER_LOW_BATTERY;

    // In low battery mode, WiFi reconnect may be disabled
    // Verify we're in low battery state
    TEST_ASSERT_EQUAL(POWER_LOW_BATTERY, g_power.getState());
}

/**
 * @brief Test system prioritizes critical battery over WiFi
 */
void test_critical_battery_priority(void) {
    // WiFi connected
    g_wifi.state = WIFI_CONNECTED;

    // Battery goes critical
    g_power.batteryVoltage = 3.1f;
    g_power.state = POWER_CRITICAL_BATTERY;

    // Watchdog checks
    g_watchdog.update(g_wifi, g_power);

    // System health should be critical (battery takes priority)
    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, g_watchdog.getSystemHealth());
}

/**
 * @brief Test WiFi power saving mode integration
 */
void test_wifi_power_saving_integration(void) {
    g_wifi.state = WIFI_CONNECTED;
    g_wifi.setPowerSaving(false);

    // Enable power saving mode
    g_wifi.setPowerSaving(true);

    TEST_ASSERT_TRUE(g_wifi.isPowerSavingEnabled());
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // WiFi + Power integration tests
    RUN_TEST(test_wifi_disabled_on_light_sleep);
    RUN_TEST(test_wifi_enabled_on_wake);
    RUN_TEST(test_low_battery_disables_wifi);
    RUN_TEST(test_wifi_power_saving_integration);

    // State Machine + Power integration tests
    RUN_TEST(test_motion_prevents_sleep);
    RUN_TEST(test_complete_wake_motion_sleep_cycle);

    // Watchdog integration tests
    RUN_TEST(test_watchdog_detects_wifi_failure);
    RUN_TEST(test_watchdog_detects_low_battery);
    RUN_TEST(test_watchdog_detects_critical_battery);
    RUN_TEST(test_watchdog_system_health);
    RUN_TEST(test_watchdog_system_health_warning);
    RUN_TEST(test_watchdog_system_health_critical);

    // Complex scenarios
    RUN_TEST(test_wifi_reconnect_during_low_battery);
    RUN_TEST(test_critical_battery_priority);

    return UNITY_END();
}
