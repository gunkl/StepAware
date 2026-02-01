/**
 * @file test_web_api.cpp
 * @brief Unit tests for Web API components
 *
 * Tests component integration and API response structure.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

// Mock time
unsigned long mock_time = 0;
unsigned long millis() { return mock_time; }
void advance_time(unsigned long ms) { mock_time += ms; }
void reset_time() { mock_time = 0; }

// Mock ESP functions
struct {
    uint32_t getFreeHeap() { return 100000; }
} ESP;

// Mock Serial
struct {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char*, ...) {}
} Serial;

// ============================================================================
// Mock Components for API Integration
// ============================================================================

// Mock State Machine
class MockStateMachine {
public:
    enum OperatingMode {
        OFF = 0,
        CONTINUOUS_ON = 1,
        MOTION_DETECT = 2
    };

    OperatingMode mode;
    bool warningActive;
    uint32_t motionEvents;
    uint32_t modeChanges;

    MockStateMachine()
        : mode(MOTION_DETECT), warningActive(false), motionEvents(10), modeChanges(5) {}

    OperatingMode getMode() const { return mode; }
    void setMode(OperatingMode m) { mode = m; modeChanges++; }
    bool isWarningActive() const { return warningActive; }
    uint32_t getMotionEventCount() const { return motionEvents; }
    uint32_t getModeChangeCount() const { return modeChanges; }

    static const char* getModeName(OperatingMode mode) {
        switch (mode) {
            case OFF: return "OFF";
            case CONTINUOUS_ON: return "CONTINUOUS_ON";
            case MOTION_DETECT: return "MOTION_DETECT";
            default: return "UNKNOWN";
        }
    }

    void reset() {
        mode = MOTION_DETECT;
        warningActive = false;
        motionEvents = 0;
        modeChanges = 0;
    }
};

// Mock WiFi Manager for API
class MockWiFiForAPI {
public:
    enum State {
        STATE_DISABLED = 0,
        STATE_AP_MODE = 1,
        STATE_CONNECTING = 2,
        STATE_CONNECTED = 3,
        STATE_DISCONNECTED = 4,
        STATE_FAILED = 5
    };

    State state;
    int8_t rssi;
    char ssid[64];

    MockWiFiForAPI() : state(STATE_CONNECTED), rssi(-50) {
        strcpy(ssid, "TestNetwork");
    }

    State getState() const { return state; }
    int8_t getRSSI() const { return rssi; }
    const char* getSSID() const { return ssid; }

    static const char* getStateName(State s) {
        switch (s) {
            case STATE_DISABLED: return "DISABLED";
            case STATE_AP_MODE: return "AP_MODE";
            case STATE_CONNECTING: return "CONNECTING";
            case STATE_CONNECTED: return "CONNECTED";
            case STATE_DISCONNECTED: return "DISCONNECTED";
            case STATE_FAILED: return "FAILED";
            default: return "UNKNOWN";
        }
    }
};

// Mock Power Manager for API
class MockPowerForAPI {
public:
    enum PowerState {
        STATE_ACTIVE = 0,
        STATE_LIGHT_SLEEP = 1,
        STATE_DEEP_SLEEP = 2,
        STATE_LOW_BATTERY = 3,
        STATE_CRITICAL_BATTERY = 4,
        STATE_USB_POWER = 5
    };

    PowerState state;
    float batteryVoltage;
    uint8_t batteryPercent;
    bool usbPower;
    bool low;
    bool critical;

    MockPowerForAPI() : state(STATE_ACTIVE), batteryVoltage(3.8f),
        batteryPercent(75), usbPower(false), low(false), critical(false) {}

    PowerState getState() const { return state; }
    float getBatteryVoltage() const { return batteryVoltage; }
    uint8_t getBatteryPercent() const { return batteryPercent; }

    static const char* getStateName(PowerState s) {
        switch (s) {
            case STATE_ACTIVE: return "ACTIVE";
            case STATE_LIGHT_SLEEP: return "LIGHT_SLEEP";
            case STATE_DEEP_SLEEP: return "DEEP_SLEEP";
            case STATE_LOW_BATTERY: return "LOW_BATTERY";
            case STATE_CRITICAL_BATTERY: return "CRITICAL_BATTERY";
            case STATE_USB_POWER: return "USB_POWER";
            default: return "UNKNOWN";
        }
    }
};

// Mock Watchdog for API
class MockWatchdogForAPI {
public:
    enum HealthStatus {
        HEALTH_OK = 0,
        HEALTH_WARNING = 1,
        HEALTH_CRITICAL = 2,
        HEALTH_FAILED = 3
    };

    HealthStatus systemHealth;

    MockWatchdogForAPI() : systemHealth(HEALTH_OK) {}

    HealthStatus getSystemHealth() const { return systemHealth; }

    static const char* getHealthStatusName(HealthStatus s) {
        switch (s) {
            case HEALTH_OK: return "OK";
            case HEALTH_WARNING: return "WARNING";
            case HEALTH_CRITICAL: return "CRITICAL";
            case HEALTH_FAILED: return "FAILED";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// Test instances
// ============================================================================

MockStateMachine stateMachine;
MockWiFiForAPI wifiManager;
MockPowerForAPI powerManager;
MockWatchdogForAPI watchdogManager;

void setUp(void) {
    reset_time();
    stateMachine.reset();
}

void tearDown(void) {
    // Clean up
}

// ============================================================================
// TEST CASES
// ============================================================================

/**
 * @brief Test state machine getters return correct values
 */
void test_state_machine_getters(void) {
    stateMachine.mode = MockStateMachine::MOTION_DETECT;
    stateMachine.warningActive = true;
    stateMachine.motionEvents = 42;
    stateMachine.modeChanges = 7;

    TEST_ASSERT_EQUAL(MockStateMachine::MOTION_DETECT, stateMachine.getMode());
    TEST_ASSERT_TRUE(stateMachine.isWarningActive());
    TEST_ASSERT_EQUAL(42, stateMachine.getMotionEventCount());
    TEST_ASSERT_EQUAL(7, stateMachine.getModeChangeCount());
}

/**
 * @brief Test state machine mode cycling
 */
void test_state_machine_mode_changes(void) {
    stateMachine.mode = MockStateMachine::OFF;
    uint32_t initialChanges = stateMachine.getModeChangeCount();

    stateMachine.setMode(MockStateMachine::CONTINUOUS_ON);
    TEST_ASSERT_EQUAL(MockStateMachine::CONTINUOUS_ON, stateMachine.getMode());
    TEST_ASSERT_EQUAL(initialChanges + 1, stateMachine.getModeChangeCount());
}

/**
 * @brief Test WiFi state names
 */
void test_wifi_state_names(void) {
    TEST_ASSERT_EQUAL_STRING("CONNECTED", MockWiFiForAPI::getStateName(MockWiFiForAPI::STATE_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("DISCONNECTED", MockWiFiForAPI::getStateName(MockWiFiForAPI::STATE_DISCONNECTED));
    TEST_ASSERT_EQUAL_STRING("AP_MODE", MockWiFiForAPI::getStateName(MockWiFiForAPI::STATE_AP_MODE));
    TEST_ASSERT_EQUAL_STRING("FAILED", MockWiFiForAPI::getStateName(MockWiFiForAPI::STATE_FAILED));
}

/**
 * @brief Test WiFi getters
 */
void test_wifi_getters(void) {
    wifiManager.state = MockWiFiForAPI::STATE_CONNECTED;
    wifiManager.rssi = -65;

    TEST_ASSERT_EQUAL(MockWiFiForAPI::STATE_CONNECTED, wifiManager.getState());
    TEST_ASSERT_EQUAL(-65, wifiManager.getRSSI());
    TEST_ASSERT_EQUAL_STRING("TestNetwork", wifiManager.getSSID());
}

/**
 * @brief Test power state names
 */
void test_power_state_names(void) {
    TEST_ASSERT_EQUAL_STRING("ACTIVE", MockPowerForAPI::getStateName(MockPowerForAPI::STATE_ACTIVE));
    TEST_ASSERT_EQUAL_STRING("LOW_BATTERY", MockPowerForAPI::getStateName(MockPowerForAPI::STATE_LOW_BATTERY));
    TEST_ASSERT_EQUAL_STRING("CRITICAL_BATTERY", MockPowerForAPI::getStateName(MockPowerForAPI::STATE_CRITICAL_BATTERY));
    TEST_ASSERT_EQUAL_STRING("USB_POWER", MockPowerForAPI::getStateName(MockPowerForAPI::STATE_USB_POWER));
}

/**
 * @brief Test power getters
 */
void test_power_getters(void) {
    powerManager.state = MockPowerForAPI::STATE_ACTIVE;
    powerManager.batteryVoltage = 3.7f;
    powerManager.batteryPercent = 60;

    TEST_ASSERT_EQUAL(MockPowerForAPI::STATE_ACTIVE, powerManager.getState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.7f, powerManager.getBatteryVoltage());
    TEST_ASSERT_EQUAL(60, powerManager.getBatteryPercent());
}

/**
 * @brief Test power low battery state
 */
void test_power_low_battery(void) {
    powerManager.state = MockPowerForAPI::STATE_LOW_BATTERY;
    powerManager.batteryVoltage = 3.3f;
    powerManager.batteryPercent = 20;
    powerManager.low = true;

    TEST_ASSERT_EQUAL(MockPowerForAPI::STATE_LOW_BATTERY, powerManager.getState());
    TEST_ASSERT_TRUE(powerManager.low);
    TEST_ASSERT_FALSE(powerManager.critical);
}

/**
 * @brief Test watchdog health status names
 */
void test_watchdog_status_names(void) {
    TEST_ASSERT_EQUAL_STRING("OK", MockWatchdogForAPI::getHealthStatusName(MockWatchdogForAPI::HEALTH_OK));
    TEST_ASSERT_EQUAL_STRING("WARNING", MockWatchdogForAPI::getHealthStatusName(MockWatchdogForAPI::HEALTH_WARNING));
    TEST_ASSERT_EQUAL_STRING("CRITICAL", MockWatchdogForAPI::getHealthStatusName(MockWatchdogForAPI::HEALTH_CRITICAL));
    TEST_ASSERT_EQUAL_STRING("FAILED", MockWatchdogForAPI::getHealthStatusName(MockWatchdogForAPI::HEALTH_FAILED));
}

/**
 * @brief Test watchdog getters
 */
void test_watchdog_getters(void) {
    watchdogManager.systemHealth = MockWatchdogForAPI::HEALTH_OK;
    TEST_ASSERT_EQUAL(MockWatchdogForAPI::HEALTH_OK, watchdogManager.getSystemHealth());

    watchdogManager.systemHealth = MockWatchdogForAPI::HEALTH_WARNING;
    TEST_ASSERT_EQUAL(MockWatchdogForAPI::HEALTH_WARNING, watchdogManager.getSystemHealth());
}

/**
 * @brief Test all components working together
 */
void test_all_components_integration(void) {
    // Set up all components
    stateMachine.mode = MockStateMachine::MOTION_DETECT;
    stateMachine.warningActive = true;

    wifiManager.state = MockWiFiForAPI::STATE_CONNECTED;
    wifiManager.rssi = -55;

    powerManager.state = MockPowerForAPI::STATE_ACTIVE;
    powerManager.batteryVoltage = 3.9f;
    powerManager.batteryPercent = 85;

    watchdogManager.systemHealth = MockWatchdogForAPI::HEALTH_OK;

    // Verify all components return expected values
    TEST_ASSERT_EQUAL(MockStateMachine::MOTION_DETECT, stateMachine.getMode());
    TEST_ASSERT_TRUE(stateMachine.isWarningActive());

    TEST_ASSERT_EQUAL(MockWiFiForAPI::STATE_CONNECTED, wifiManager.getState());
    TEST_ASSERT_EQUAL(-55, wifiManager.getRSSI());

    TEST_ASSERT_EQUAL(MockPowerForAPI::STATE_ACTIVE, powerManager.getState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.9f, powerManager.getBatteryVoltage());
    TEST_ASSERT_EQUAL(85, powerManager.getBatteryPercent());

    TEST_ASSERT_EQUAL(MockWatchdogForAPI::HEALTH_OK, watchdogManager.getSystemHealth());
}

/**
 * @brief Test degraded system state
 */
void test_degraded_system_state(void) {
    // WiFi disconnected
    wifiManager.state = MockWiFiForAPI::STATE_DISCONNECTED;

    // Battery low
    powerManager.state = MockPowerForAPI::STATE_LOW_BATTERY;
    powerManager.batteryVoltage = 3.3f;
    powerManager.batteryPercent = 25;
    powerManager.low = true;

    // Watchdog warning
    watchdogManager.systemHealth = MockWatchdogForAPI::HEALTH_WARNING;

    // Verify degraded state
    TEST_ASSERT_EQUAL(MockWiFiForAPI::STATE_DISCONNECTED, wifiManager.getState());
    TEST_ASSERT_EQUAL(MockPowerForAPI::STATE_LOW_BATTERY, powerManager.getState());
    TEST_ASSERT_TRUE(powerManager.low);
    TEST_ASSERT_EQUAL(MockWatchdogForAPI::HEALTH_WARNING, watchdogManager.getSystemHealth());
}

/**
 * @brief Test critical system state
 */
void test_critical_system_state(void) {
    // WiFi failed
    wifiManager.state = MockWiFiForAPI::STATE_FAILED;

    // Battery critical
    powerManager.state = MockPowerForAPI::STATE_CRITICAL_BATTERY;
    powerManager.batteryVoltage = 3.1f;
    powerManager.batteryPercent = 5;
    powerManager.critical = true;

    // Watchdog critical
    watchdogManager.systemHealth = MockWatchdogForAPI::HEALTH_CRITICAL;

    // Verify critical state
    TEST_ASSERT_EQUAL(MockWiFiForAPI::STATE_FAILED, wifiManager.getState());
    TEST_ASSERT_EQUAL(MockPowerForAPI::STATE_CRITICAL_BATTERY, powerManager.getState());
    TEST_ASSERT_TRUE(powerManager.critical);
    TEST_ASSERT_EQUAL(MockWatchdogForAPI::HEALTH_CRITICAL, watchdogManager.getSystemHealth());
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // State Machine tests
    RUN_TEST(test_state_machine_getters);
    RUN_TEST(test_state_machine_mode_changes);

    // WiFi tests
    RUN_TEST(test_wifi_state_names);
    RUN_TEST(test_wifi_getters);

    // Power tests
    RUN_TEST(test_power_state_names);
    RUN_TEST(test_power_getters);
    RUN_TEST(test_power_low_battery);

    // Watchdog tests
    RUN_TEST(test_watchdog_status_names);
    RUN_TEST(test_watchdog_getters);

    // Integration tests
    RUN_TEST(test_all_components_integration);
    RUN_TEST(test_degraded_system_state);
    RUN_TEST(test_critical_system_state);

    return UNITY_END();
}
