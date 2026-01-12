/**
 * @file test_wifi_watchdog.cpp
 * @brief Unit tests for WiFi Manager watchdog health checks
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

// WiFi Manager states
enum WiFiState {
    STATE_DISABLED = 0,
    STATE_AP_MODE = 1,
    STATE_CONNECTING = 2,
    STATE_CONNECTED = 3,
    STATE_DISCONNECTED = 4,
    STATE_FAILED = 5
};

// Watchdog health status
enum HealthStatus {
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_FAILED = 3
};

// Recovery actions
enum RecoveryAction {
    RECOVERY_NONE = 0,
    RECOVERY_SOFT = 1,
    RECOVERY_MODULE_RESTART = 2,
    RECOVERY_SYSTEM_REBOOT = 3
};

// Mock WiFi Manager
class MockWiFiManager {
private:
    WiFiState state;
    int8_t rssi;

public:
    MockWiFiManager() : state(STATE_DISABLED), rssi(0) {}

    WiFiState getState() const { return state; }
    int8_t getRSSI() const { return rssi; }

    void setState(WiFiState s) { state = s; }
    void setRSSI(int8_t r) { rssi = r; }

    void disconnect() { state = STATE_DISCONNECTED; }
    bool reconnect() {
        state = STATE_CONNECTING;
        return true;
    }
    bool connect() {
        state = STATE_CONNECTING;
        return true;
    }

    void reset() {
        state = STATE_DISABLED;
        rssi = 0;
    }
};

MockWiFiManager g_wifi;

// Health check function (simplified from watchdog_health_checks.cpp)
HealthStatus checkWiFiHealth(const char** message) {
    WiFiState state = g_wifi.getState();

    switch (state) {
        case STATE_CONNECTED:
            // Check signal strength
            if (g_wifi.getRSSI() < -85) {
                if (message) *message = "Weak signal";
                return HEALTH_WARNING;
            }
            return HEALTH_OK;

        case STATE_CONNECTING:
            return HEALTH_OK;  // Normal transitional state

        case STATE_DISCONNECTED:
            if (message) *message = "Disconnected, will retry";
            return HEALTH_WARNING;

        case STATE_FAILED:
            if (message) *message = "Connection failed";
            return HEALTH_CRITICAL;

        case STATE_AP_MODE:
            return HEALTH_OK;  // Normal for setup

        case STATE_DISABLED:
            return HEALTH_OK;  // Intentional

        default:
            return HEALTH_FAILED;
    }
}

// Recovery function (simplified from watchdog_health_checks.cpp)
bool recoverWiFi(RecoveryAction action) {
    switch (action) {
        case RECOVERY_SOFT:
            // Try reconnecting
            return g_wifi.reconnect();

        case RECOVERY_MODULE_RESTART:
            // Restart WiFi subsystem
            g_wifi.disconnect();
            advance_time(1000);
            return g_wifi.connect();

        default:
            return false;
    }
}

// ============================================================================
// TEST CASES
// ============================================================================

void setUp(void) {
    reset_time();
    g_wifi.reset();
}

void tearDown(void) {
    // Clean up after each test
}

/**
 * @brief Test WiFi health check - disabled state
 */
void test_wifi_health_disabled(void) {
    g_wifi.setState(STATE_DISABLED);

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_OK, status);
}

/**
 * @brief Test WiFi health check - AP mode
 */
void test_wifi_health_ap_mode(void) {
    g_wifi.setState(STATE_AP_MODE);

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_OK, status);
}

/**
 * @brief Test WiFi health check - connecting state
 */
void test_wifi_health_connecting(void) {
    g_wifi.setState(STATE_CONNECTING);

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_OK, status);
}

/**
 * @brief Test WiFi health check - connected with good signal
 */
void test_wifi_health_connected_good_signal(void) {
    g_wifi.setState(STATE_CONNECTED);
    g_wifi.setRSSI(-45); // Good signal

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_OK, status);
}

/**
 * @brief Test WiFi health check - connected with weak signal
 */
void test_wifi_health_connected_weak_signal(void) {
    g_wifi.setState(STATE_CONNECTED);
    g_wifi.setRSSI(-90); // Weak signal (< -85)

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_WARNING, status);
    TEST_ASSERT_NOT_NULL(message);
}

/**
 * @brief Test WiFi health check - disconnected state
 */
void test_wifi_health_disconnected(void) {
    g_wifi.setState(STATE_DISCONNECTED);

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_WARNING, status);
    TEST_ASSERT_NOT_NULL(message);
}

/**
 * @brief Test WiFi health check - failed state
 */
void test_wifi_health_failed(void) {
    g_wifi.setState(STATE_FAILED);

    const char* message = nullptr;
    HealthStatus status = checkWiFiHealth(&message);

    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, status);
    TEST_ASSERT_NOT_NULL(message);
}

/**
 * @brief Test WiFi soft recovery
 */
void test_wifi_soft_recovery(void) {
    g_wifi.setState(STATE_DISCONNECTED);

    bool result = recoverWiFi(RECOVERY_SOFT);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(STATE_CONNECTING, g_wifi.getState());
}

/**
 * @brief Test WiFi module restart recovery
 */
void test_wifi_module_restart_recovery(void) {
    g_wifi.setState(STATE_CONNECTED);

    bool result = recoverWiFi(RECOVERY_MODULE_RESTART);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(STATE_CONNECTING, g_wifi.getState());
}

/**
 * @brief Test WiFi recovery with unsupported action
 */
void test_wifi_recovery_unsupported_action(void) {
    g_wifi.setState(STATE_FAILED);

    bool result = recoverWiFi(RECOVERY_SYSTEM_REBOOT);

    TEST_ASSERT_FALSE(result);
}

/**
 * @brief Test signal strength thresholds
 */
void test_wifi_signal_strength_thresholds(void) {
    g_wifi.setState(STATE_CONNECTED);

    // Test at threshold boundary (-85 dBm)
    g_wifi.setRSSI(-85);
    const char* msg1 = nullptr;
    HealthStatus status1 = checkWiFiHealth(&msg1);
    TEST_ASSERT_EQUAL(HEALTH_OK, status1); // -85 is OK

    // Test just below threshold (-86 dBm)
    g_wifi.setRSSI(-86);
    const char* msg2 = nullptr;
    HealthStatus status2 = checkWiFiHealth(&msg2);
    TEST_ASSERT_EQUAL(HEALTH_WARNING, status2); // -86 is warning

    // Test strong signal (-30 dBm)
    g_wifi.setRSSI(-30);
    const char* msg3 = nullptr;
    HealthStatus status3 = checkWiFiHealth(&msg3);
    TEST_ASSERT_EQUAL(HEALTH_OK, status3);
}

/**
 * @brief Test health check state transitions
 */
void test_wifi_health_state_transitions(void) {
    const char* message = nullptr;

    // Start disabled
    g_wifi.setState(STATE_DISABLED);
    TEST_ASSERT_EQUAL(HEALTH_OK, checkWiFiHealth(&message));

    // Transition to connecting
    g_wifi.setState(STATE_CONNECTING);
    TEST_ASSERT_EQUAL(HEALTH_OK, checkWiFiHealth(&message));

    // Transition to connected
    g_wifi.setState(STATE_CONNECTED);
    g_wifi.setRSSI(-50);
    TEST_ASSERT_EQUAL(HEALTH_OK, checkWiFiHealth(&message));

    // Transition to disconnected
    g_wifi.setState(STATE_DISCONNECTED);
    TEST_ASSERT_EQUAL(HEALTH_WARNING, checkWiFiHealth(&message));

    // Transition to failed
    g_wifi.setState(STATE_FAILED);
    TEST_ASSERT_EQUAL(HEALTH_CRITICAL, checkWiFiHealth(&message));

    // Transition to AP mode
    g_wifi.setState(STATE_AP_MODE);
    TEST_ASSERT_EQUAL(HEALTH_OK, checkWiFiHealth(&message));
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Health check tests
    RUN_TEST(test_wifi_health_disabled);
    RUN_TEST(test_wifi_health_ap_mode);
    RUN_TEST(test_wifi_health_connecting);
    RUN_TEST(test_wifi_health_connected_good_signal);
    RUN_TEST(test_wifi_health_connected_weak_signal);
    RUN_TEST(test_wifi_health_disconnected);
    RUN_TEST(test_wifi_health_failed);

    // Recovery tests
    RUN_TEST(test_wifi_soft_recovery);
    RUN_TEST(test_wifi_module_restart_recovery);
    RUN_TEST(test_wifi_recovery_unsupported_action);

    // Edge case tests
    RUN_TEST(test_wifi_signal_strength_thresholds);
    RUN_TEST(test_wifi_health_state_transitions);

    return UNITY_END();
}
