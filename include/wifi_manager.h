#ifndef STEPAWARE_WIFI_MANAGER_H
#define STEPAWARE_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"

/**
 * @brief WiFi Manager for StepAware
 *
 * Manages WiFi connectivity including:
 * - First-time setup via Access Point mode
 * - Persistent connection management
 * - Automatic reconnection with exponential backoff
 * - Signal monitoring and status reporting
 * - mDNS (stepaware.local)
 * - Power saving modes
 *
 * Usage:
 * ```cpp
 * WiFiManager wifi;
 * wifi.begin();
 *
 * void loop() {
 *     wifi.update();  // Handle state transitions, reconnection
 * }
 * ```
 */
class WiFiManager {
public:
    /**
     * @brief WiFi connection states
     */
    enum State {
        STATE_DISABLED,     ///< WiFi disabled in configuration
        STATE_AP_MODE,      ///< Access Point mode (setup)
        STATE_CONNECTING,   ///< Attempting to connect to WiFi
        STATE_CONNECTED,    ///< Connected to WiFi
        STATE_DISCONNECTED, ///< Disconnected (will retry)
        STATE_FAILED        ///< Connection failed after max retries
    };

    /**
     * @brief WiFi configuration
     */
    struct Config {
        bool enabled;                ///< WiFi enabled/disabled
        char ssid[64];               ///< WiFi SSID to connect to
        char password[64];           ///< WiFi password
        char hostname[32];           ///< mDNS hostname (default: "stepaware")
        bool apModeOnFailure;        ///< Enter AP mode on connection failure
        uint32_t reconnectDelayMs;   ///< Initial reconnect delay (default: 5000ms)
        uint8_t maxReconnectAttempts;///< Max reconnect attempts before AP mode (default: 10)
        bool powerSaving;            ///< Enable WiFi power saving
        uint32_t connectionTimeout;  ///< Connection timeout (default: 30000ms)
    };

    /**
     * @brief WiFi status information
     */
    struct Status {
        State state;                 ///< Current connection state
        int8_t rssi;                 ///< Signal strength (-100 to 0 dBm)
        IPAddress ip;                ///< IP address (0.0.0.0 if not connected)
        char ssid[64];               ///< Connected SSID
        char apSSID[32];             ///< AP mode SSID (when in AP mode)
        uint32_t connectionUptime;   ///< Connection uptime in milliseconds
        uint32_t reconnectCount;     ///< Total reconnection count
        uint32_t failureCount;       ///< Consecutive failure count
    };

    /**
     * @brief Callback function types
     */
    typedef void (*ConnectedCallback)();
    typedef void (*DisconnectedCallback)();
    typedef void (*APModeCallback)();

    /**
     * @brief Construct WiFi Manager
     */
    WiFiManager();

    /**
     * @brief Destructor
     */
    ~WiFiManager();

    /**
     * @brief Initialize WiFi Manager
     *
     * @param config Optional configuration (uses defaults if nullptr)
     * @return True if initialization successful
     */
    bool begin(const Config* config = nullptr);

    /**
     * @brief Update WiFi state machine (call every loop)
     *
     * Handles state transitions, reconnection attempts, and status updates.
     * Must be called regularly for proper operation.
     */
    void update();

    /**
     * @brief Connect to WiFi network
     *
     * Attempts to connect using credentials from config.
     * If no credentials, enters AP mode for setup.
     *
     * @return True if connection initiated
     */
    bool connect();

    /**
     * @brief Disconnect from WiFi
     *
     * @return True if disconnect initiated
     */
    bool disconnect();

    /**
     * @brief Reconnect to WiFi
     *
     * Forces an immediate reconnection attempt.
     *
     * @return True if reconnect initiated
     */
    bool reconnect();

    /**
     * @brief Start Access Point mode
     *
     * Creates WiFi AP for initial setup or reconfiguration.
     *
     * @param ssid Optional SSID (auto-generated if nullptr)
     * @param password Optional password (open network if nullptr)
     * @return True if AP mode started
     */
    bool startAPMode(const char* ssid = nullptr, const char* password = nullptr);

    /**
     * @brief Stop Access Point mode
     *
     * @return True if AP mode stopped
     */
    bool stopAPMode();

    /**
     * @brief Get current connection state
     *
     * @return Current state
     */
    State getState() const { return m_state; }

    /**
     * @brief Get WiFi status
     *
     * @return Status information
     */
    const Status& getStatus() const { return m_status; }

    /**
     * @brief Check if connected to WiFi
     *
     * @return True if connected
     */
    bool isConnected() const { return m_state == STATE_CONNECTED; }

    /**
     * @brief Get signal strength
     *
     * @return RSSI in dBm (-100 to 0, higher is better)
     */
    int8_t getRSSI() const { return m_status.rssi; }

    /**
     * @brief Get IP address
     *
     * @return IP address (0.0.0.0 if not connected)
     */
    IPAddress getIP() const { return m_status.ip; }

    /**
     * @brief Set WiFi credentials
     *
     * Updates configuration with new credentials.
     * Does not automatically connect - call connect() after.
     *
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @return True if credentials set
     */
    bool setCredentials(const char* ssid, const char* password);

    /**
     * @brief Scan for available WiFi networks
     *
     * @param networks Output array for scan results
     * @param maxNetworks Maximum networks to return
     * @return Number of networks found
     */
    int scanNetworks(String* networks, int maxNetworks);

    /**
     * @brief Register connected callback
     *
     * Called when WiFi connection is established.
     *
     * @param callback Callback function
     */
    void onConnected(ConnectedCallback callback) { m_onConnected = callback; }

    /**
     * @brief Register disconnected callback
     *
     * Called when WiFi connection is lost.
     *
     * @param callback Callback function
     */
    void onDisconnected(DisconnectedCallback callback) { m_onDisconnected = callback; }

    /**
     * @brief Register AP mode callback
     *
     * Called when entering AP mode.
     *
     * @param callback Callback function
     */
    void onAPMode(APModeCallback callback) { m_onAPMode = callback; }

    /**
     * @brief Get state name string
     *
     * @param state State to convert
     * @return State name
     */
    static const char* getStateName(State state);

private:
    Config m_config;                  ///< WiFi configuration
    Status m_status;                  ///< WiFi status
    State m_state;                    ///< Current state
    bool m_initialized;               ///< Initialization flag

    uint32_t m_connectStartTime;      ///< Connection attempt start time
    uint32_t m_connectionStartTime;   ///< Successful connection start time
    uint32_t m_lastReconnectAttempt;  ///< Last reconnect attempt time
    uint32_t m_lastStatusUpdate;      ///< Last status update time

    ConnectedCallback m_onConnected;       ///< Connected callback
    DisconnectedCallback m_onDisconnected; ///< Disconnected callback
    APModeCallback m_onAPMode;             ///< AP mode callback

    /**
     * @brief State machine handlers
     */
    void handleStateDisabled();
    void handleStateAPMode();
    void handleStateConnecting();
    void handleStateConnected();
    void handleStateDisconnected();
    void handleStateFailed();

    /**
     * @brief Set up mDNS responder
     *
     * Enables access via http://hostname.local
     */
    void setupMDNS();

    /**
     * @brief Update status information
     *
     * Updates RSSI, IP, uptime, etc.
     */
    void updateStatus();

    /**
     * @brief Check if should attempt reconnection
     *
     * Implements exponential backoff logic.
     *
     * @return True if should reconnect now
     */
    bool shouldReconnect();

    /**
     * @brief Get reconnect delay
     *
     * Calculates delay with exponential backoff.
     *
     * @return Delay in milliseconds
     */
    uint32_t getReconnectDelay();

    /**
     * @brief Generate AP mode SSID
     *
     * Format: StepAware-XXXX (XXXX = last 4 hex digits of MAC)
     *
     * @return Generated SSID
     */
    String generateAPSSID();

    /**
     * @brief Set WiFi power saving mode
     *
     * @param enabled True to enable power saving
     */
    void setPowerSaving(bool enabled);

    /**
     * @brief Transition to new state
     *
     * @param newState State to transition to
     */
    void setState(State newState);
};

// Global WiFi manager instance
extern WiFiManager g_wifi;

#endif // STEPAWARE_WIFI_MANAGER_H
