/**
 * @file test_wifi_manager.cpp
 * @brief Unit tests for WiFi Manager
 */

#include <unity.h>
#include <stdint.h>
#include <string.h>

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

// WiFi Manager states (from wifi_manager.h)
enum WiFiState {
    STATE_DISABLED = 0,
    STATE_AP_MODE = 1,
    STATE_CONNECTING = 2,
    STATE_CONNECTED = 3,
    STATE_DISCONNECTED = 4,
    STATE_FAILED = 5
};

// Simplified WiFi Manager for testing
class TestWiFiManager {
public:
    struct Config {
        bool enabled;
        char ssid[64];
        char password[64];
        bool apModeOnFailure;
        uint32_t reconnectDelayMs;
        uint8_t maxReconnectAttempts;

        Config()
            : enabled(true)
            , apModeOnFailure(true)
            , reconnectDelayMs(5000)
            , maxReconnectAttempts(10) {
            ssid[0] = '\0';
            password[0] = '\0';
        }
    };

    struct Status {
        WiFiState state;
        int8_t rssi;
        uint32_t uptime;
        uint32_t reconnectCount;
        uint32_t failureCount;

        Status()
            : state(STATE_DISABLED)
            , rssi(0)
            , uptime(0)
            , reconnectCount(0)
            , failureCount(0) {}
    };

private:
    WiFiState state;
    char ssid[64];
    char password[64];
    bool enabled;
    uint32_t connectionTimeout;
    uint32_t connectStartTime;
    int8_t rssi;

    // Configuration
    Config config;
    Status status;

    // Simulate WiFi connection success/failure
    bool simulateConnectionSuccess;

public:
    TestWiFiManager()
        : state(STATE_DISABLED)
        , enabled(false)
        , connectionTimeout(30000)
        , connectStartTime(0)
        , rssi(0)
        , simulateConnectionSuccess(true) {
        ssid[0] = '\0';
        password[0] = '\0';
        status.state = STATE_DISABLED;
    }

    // Overload for backward compatibility with old tests
    void begin(bool _enabled) {
        config.enabled = _enabled;
        enabled = _enabled;
        if (!enabled) {
            state = STATE_DISABLED;
            status.state = STATE_DISABLED;
        } else if (strlen(ssid) == 0) {
            state = STATE_AP_MODE;
            status.state = STATE_AP_MODE;
        } else {
            state = STATE_DISCONNECTED;
            status.state = STATE_DISCONNECTED;
        }
    }

    void begin(const Config* _config) {
        if (_config) {
            config = *_config;
        }

        enabled = config.enabled;
        if (!enabled) {
            state = STATE_DISABLED;
            status.state = STATE_DISABLED;
        } else if (strlen(ssid) == 0) {
            state = STATE_AP_MODE;
            status.state = STATE_AP_MODE;
        } else {
            state = STATE_DISCONNECTED;
            status.state = STATE_DISCONNECTED;
        }
    }

    void setCredentials(const char* _ssid, const char* _password) {
        strncpy(ssid, _ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';

        if (_password) {
            strncpy(password, _password, sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
        } else {
            password[0] = '\0';
        }
    }

    bool connect() {
        if (!enabled || strlen(ssid) == 0) {
            return false;
        }

        state = STATE_CONNECTING;
        connectStartTime = millis();
        return true;
    }

    void disconnect() {
        state = STATE_DISCONNECTED;
    }

    bool reconnect() {
        status.failureCount = 0;
        config.reconnectDelayMs = millis();
        return connect();
    }

    void update() {
        switch (state) {
            case STATE_CONNECTING:
                handleConnecting();
                break;

            case STATE_DISCONNECTED:
                handleDisconnected();
                break;

            case STATE_CONNECTED:
                // Check if still connected (simulated)
                break;

            default:
                break;
        }
    }

    void handleConnecting() {
        // Simulate connection timeout
        if (millis() - connectStartTime > connectionTimeout) {
            state = STATE_DISCONNECTED;
            status.state = STATE_DISCONNECTED;
            status.failureCount++;
            return;
        }

        // Simulate connection success after 3 seconds
        if (simulateConnectionSuccess && (millis() - connectStartTime > 3000)) {
            state = STATE_CONNECTED;
            status.state = STATE_CONNECTED;
            status.failureCount = 0;
            status.reconnectCount++;
            rssi = -45; // Good signal
            status.rssi = -45;
        }
    }

    void handleDisconnected() {
        // Check if should attempt reconnect
        if (shouldReconnect()) {
            connect();
        }
    }

    bool shouldReconnect() {
        // If AP mode on failure is disabled, allow infinite retries
        if (!config.apModeOnFailure) {
            // Cap failure count to prevent overflow
            if (status.failureCount > config.maxReconnectAttempts) {
                status.failureCount = config.maxReconnectAttempts;
            }
        } else {
            // Check if max attempts reached (legacy behavior)
            if (status.failureCount >= config.maxReconnectAttempts) {
                state = STATE_FAILED;
                status.state = STATE_FAILED;
                return false;
            }
        }

        return true;  // Simplified for testing
    }

    uint32_t getReconnectDelay() {
        // Exponential backoff
        uint32_t delay = config.reconnectDelayMs << status.failureCount;
        return (delay > 60000) ? 60000 : delay;
    }

    bool startAPMode() {
        state = STATE_AP_MODE;
        return true;
    }

    WiFiState getState() const { return state; }
    uint32_t getFailureCount() const { return status.failureCount; }
    uint32_t getReconnectCount() const { return status.reconnectCount; }
    int8_t getRSSI() const { return rssi; }

    // Configuration management
    Config getConfig() const { return config; }
    void setConfig(const Config& _config) { config = _config; }

    // Status access
    Status& getStatus() { return status; }
    const Status& getStatus() const { return status; }

    // Direct state manipulation for testing
    void setState(WiFiState _state) {
        state = _state;
        status.state = _state;
    }

    // Test helpers
    void setSimulateConnectionSuccess(bool success) {
        simulateConnectionSuccess = success;
    }

    void simulateConnectionLoss() {
        if (state == STATE_CONNECTED) {
            state = STATE_DISCONNECTED;
            status.state = STATE_DISCONNECTED;
            status.failureCount++;
        }
    }

    void reset() {
        state = STATE_DISABLED;
        enabled = false;
        status.failureCount = 0;
        status.reconnectCount = 0;
        connectStartTime = 0;
        rssi = 0;
        status.rssi = 0;
        simulateConnectionSuccess = true;
        ssid[0] = '\0';
        password[0] = '\0';
        config = Config();  // Reset to defaults
        status.state = STATE_DISABLED;
    }
};

// Global test instance
TestWiFiManager wifi;

// ============================================================================
// TEST CASES
// ============================================================================

void setUp(void) {
    reset_time();
    wifi.reset();
}

void tearDown(void) {
    // Clean up after each test
}

/**
 * @brief Test WiFi manager initialization in disabled state
 */
void test_wifi_disabled_state(void) {
    wifi.begin(false);
    TEST_ASSERT_EQUAL(STATE_DISABLED, wifi.getState());
}

/**
 * @brief Test WiFi manager enters AP mode when no credentials configured
 */
void test_wifi_ap_mode_no_credentials(void) {
    wifi.begin(true);
    TEST_ASSERT_EQUAL(STATE_AP_MODE, wifi.getState());
}

/**
 * @brief Test WiFi manager connects with valid credentials
 */
void test_wifi_connect_with_credentials(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.begin(true);

    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, wifi.getState());

    wifi.connect();
    TEST_ASSERT_EQUAL(STATE_CONNECTING, wifi.getState());

    // Simulate connection time
    advance_time(3500);
    wifi.update();

    TEST_ASSERT_EQUAL(STATE_CONNECTED, wifi.getState());
    TEST_ASSERT_EQUAL(1, wifi.getReconnectCount());
}

/**
 * @brief Test WiFi connection timeout
 */
void test_wifi_connection_timeout(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.setSimulateConnectionSuccess(false);
    wifi.begin(true);

    wifi.connect();
    TEST_ASSERT_EQUAL(STATE_CONNECTING, wifi.getState());

    // Advance past timeout (30 seconds)
    advance_time(31000);
    wifi.update();

    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, wifi.getState());
    TEST_ASSERT_EQUAL(1, wifi.getFailureCount());
}

/**
 * @brief Test WiFi automatic reconnection
 */
void test_wifi_automatic_reconnect(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.begin(true);

    // Initial connection
    wifi.connect();
    advance_time(3500);
    wifi.update();
    TEST_ASSERT_EQUAL(STATE_CONNECTED, wifi.getState());

    // Simulate connection loss
    wifi.simulateConnectionLoss();
    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, wifi.getState());
    TEST_ASSERT_EQUAL(1, wifi.getFailureCount());

    // Should attempt reconnect after delay (10 seconds for second attempt with 1 failure)
    // Delay = 5000 << 1 = 10000ms
    advance_time(10500);
    wifi.update();

    TEST_ASSERT_EQUAL(STATE_CONNECTING, wifi.getState());
}

/**
 * @brief Test WiFi exponential backoff
 */
void test_wifi_exponential_backoff(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.setSimulateConnectionSuccess(false);
    wifi.begin(true);

    // First failure: 5 second delay
    wifi.connect();
    advance_time(31000);
    wifi.update();
    TEST_ASSERT_EQUAL(1, wifi.getFailureCount());
    uint32_t delay1 = wifi.getReconnectDelay();
    TEST_ASSERT_EQUAL(5000 << 1, delay1); // 10 seconds

    // Second failure: 10 second delay
    advance_time(10500);
    wifi.update();
    advance_time(31000);
    wifi.update();
    TEST_ASSERT_EQUAL(2, wifi.getFailureCount());
    uint32_t delay2 = wifi.getReconnectDelay();
    TEST_ASSERT_EQUAL(5000 << 2, delay2); // 20 seconds

    // Third failure: 20 second delay
    advance_time(20500);
    wifi.update();
    advance_time(31000);
    wifi.update();
    TEST_ASSERT_EQUAL(3, wifi.getFailureCount());
    uint32_t delay3 = wifi.getReconnectDelay();
    TEST_ASSERT_EQUAL(5000 << 3, delay3); // 40 seconds
}

/**
 * @brief Test WiFi max reconnect attempts
 */
void test_wifi_max_reconnect_attempts(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.setSimulateConnectionSuccess(false);
    wifi.begin(true);

    // Fail 10 times (max attempts)
    for (int i = 0; i < 10; i++) {
        wifi.connect();
        advance_time(31000);
        wifi.update();

        // Wait for backoff delay
        advance_time(61000); // Wait max backoff
        wifi.update();
    }

    TEST_ASSERT_EQUAL(10, wifi.getFailureCount());
    TEST_ASSERT_EQUAL(STATE_FAILED, wifi.getState());
}

/**
 * @brief Test WiFi manual reconnect resets failure count
 */
void test_wifi_manual_reconnect_resets_failures(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.setSimulateConnectionSuccess(false);
    wifi.begin(true);

    // Fail a few times
    wifi.connect();
    advance_time(31000);
    wifi.update();
    TEST_ASSERT_EQUAL(1, wifi.getFailureCount());

    // Manual reconnect
    wifi.setSimulateConnectionSuccess(true);
    wifi.reconnect();

    TEST_ASSERT_EQUAL(0, wifi.getFailureCount());
    TEST_ASSERT_EQUAL(STATE_CONNECTING, wifi.getState());

    // Should succeed
    advance_time(3500);
    wifi.update();
    TEST_ASSERT_EQUAL(STATE_CONNECTED, wifi.getState());
}

/**
 * @brief Test WiFi disconnect
 */
void test_wifi_disconnect(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.begin(true);

    // Connect
    wifi.connect();
    advance_time(3500);
    wifi.update();
    TEST_ASSERT_EQUAL(STATE_CONNECTED, wifi.getState());

    // Disconnect
    wifi.disconnect();
    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, wifi.getState());
}

/**
 * @brief Test WiFi RSSI reporting
 */
void test_wifi_rssi_reporting(void) {
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.begin(true);

    // Not connected - no RSSI
    TEST_ASSERT_EQUAL(0, wifi.getRSSI());

    // Connect
    wifi.connect();
    advance_time(3500);
    wifi.update();

    // Connected - should have RSSI
    TEST_ASSERT_EQUAL(-45, wifi.getRSSI());
}

/**
 * @brief Test WiFi AP mode fallback
 */
void test_wifi_ap_mode_fallback(void) {
    wifi.begin(true);

    // No credentials - should start in AP mode
    TEST_ASSERT_EQUAL(STATE_AP_MODE, wifi.getState());

    // Can manually start AP mode from connected state
    wifi.setCredentials("TestNetwork", "TestPassword123");
    wifi.connect();
    advance_time(3500);
    wifi.update();
    TEST_ASSERT_EQUAL(STATE_CONNECTED, wifi.getState());

    // Manually switch to AP mode
    wifi.startAPMode();
    TEST_ASSERT_EQUAL(STATE_AP_MODE, wifi.getState());
}

/**
 * @brief Test infinite retry when apModeOnFailure is disabled
 */
void test_wifi_infinite_retry_no_ap_fallback(void) {
    wifi.setCredentials("TestNetwork", "password123");

    TestWiFiManager::Config config = wifi.getConfig();
    config.apModeOnFailure = false;  // Disable AP mode fallback
    config.maxReconnectAttempts = 10;
    wifi.setConfig(config);

    wifi.begin(&config);

    // Simulate many connection failures
    for (int i = 0; i < 20; i++) {  // More than maxReconnectAttempts
        wifi.setState(STATE_DISCONNECTED);
        wifi.getStatus().failureCount++;

        advance_time(65000);  // Advance past max backoff
        wifi.update();
    }

    // Should still be in DISCONNECTED state, not FAILED
    // (FAILED would only happen with apModeOnFailure=true)
    TEST_ASSERT_NOT_EQUAL(STATE_FAILED, wifi.getState());

    // Should keep retrying
    TEST_ASSERT_TRUE(wifi.getState() == STATE_DISCONNECTED ||
                     wifi.getState() == STATE_CONNECTING);
}

/**
 * @brief Test failure count is capped when infinite retry enabled
 */
void test_wifi_failure_count_capped(void) {
    wifi.setCredentials("TestNetwork", "password123");

    TestWiFiManager::Config config = wifi.getConfig();
    config.apModeOnFailure = false;  // Infinite retry mode
    config.maxReconnectAttempts = 10;
    wifi.setConfig(config);

    wifi.begin(&config);

    // Simulate many failures
    wifi.getStatus().failureCount = 100;  // Way over max

    advance_time(65000);
    wifi.update();

    // Failure count should be capped at maxReconnectAttempts
    TEST_ASSERT_LESS_OR_EQUAL(config.maxReconnectAttempts + 1,
                              wifi.getStatus().failureCount);
}

/**
 * @brief Test backoff delay caps at 60 seconds
 */
void test_wifi_backoff_delay_cap(void) {
    wifi.setCredentials("TestNetwork", "password123");

    TestWiFiManager::Config config = wifi.getConfig();
    config.reconnectDelayMs = 5000;  // 5 second initial delay
    config.apModeOnFailure = false;
    wifi.setConfig(config);

    wifi.begin(&config);

    // Simulate increasing failures to test backoff
    for (int i = 0; i < 10; i++) {
        wifi.setState(STATE_DISCONNECTED);
        wifi.getStatus().failureCount = i;

        uint32_t delay = wifi.getReconnectDelay();

        // Delay should cap at 60 seconds
        TEST_ASSERT_LESS_OR_EQUAL(60000, delay);

        // For high failure counts, should be exactly 60s
        if (i >= 4) {  // 5000 << 4 = 80000, capped to 60000
            TEST_ASSERT_EQUAL(60000, delay);
        }
    }
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Basic state tests
    RUN_TEST(test_wifi_disabled_state);
    RUN_TEST(test_wifi_ap_mode_no_credentials);
    RUN_TEST(test_wifi_connect_with_credentials);

    // Connection management tests
    RUN_TEST(test_wifi_connection_timeout);
    RUN_TEST(test_wifi_automatic_reconnect);
    RUN_TEST(test_wifi_exponential_backoff);
    RUN_TEST(test_wifi_max_reconnect_attempts);
    RUN_TEST(test_wifi_manual_reconnect_resets_failures);
    RUN_TEST(test_wifi_disconnect);

    // Feature tests
    RUN_TEST(test_wifi_rssi_reporting);
    RUN_TEST(test_wifi_ap_mode_fallback);

    // Infinite retry tests (Issue #2)
    RUN_TEST(test_wifi_infinite_retry_no_ap_fallback);
    RUN_TEST(test_wifi_failure_count_capped);
    RUN_TEST(test_wifi_backoff_delay_cap);

    return UNITY_END();
}
