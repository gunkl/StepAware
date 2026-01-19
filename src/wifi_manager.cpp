#include "wifi_manager.h"
#include "logger.h"

// Global WiFi manager instance
WiFiManager g_wifi;

WiFiManager::WiFiManager()
    : m_state(STATE_DISABLED)
    , m_initialized(false)
    , m_connectStartTime(0)
    , m_connectionStartTime(0)
    , m_lastReconnectAttempt(0)
    , m_lastStatusUpdate(0)
    , m_onConnected(nullptr)
    , m_onDisconnected(nullptr)
    , m_onAPMode(nullptr)
{
    // Initialize default configuration
    m_config.enabled = true;
    strncpy(m_config.ssid, "", sizeof(m_config.ssid));
    strncpy(m_config.password, "", sizeof(m_config.password));
    strncpy(m_config.hostname, "stepaware", sizeof(m_config.hostname));
    m_config.apModeOnFailure = true;
    m_config.reconnectDelayMs = 5000;
    m_config.maxReconnectAttempts = 10;
    m_config.powerSaving = false;
    m_config.connectionTimeout = 30000;

    // Initialize status
    m_status.state = STATE_DISABLED;
    m_status.rssi = 0;
    m_status.ip = IPAddress(0, 0, 0, 0);
    strncpy(m_status.ssid, "", sizeof(m_status.ssid));
    strncpy(m_status.apSSID, "", sizeof(m_status.apSSID));
    m_status.connectionUptime = 0;
    m_status.reconnectCount = 0;
    m_status.failureCount = 0;
}

WiFiManager::~WiFiManager() {
}

bool WiFiManager::begin(const Config* config) {
    if (m_initialized) {
        LOG_WARN("WiFi: Already initialized");
        return true;
    }

    // Apply custom configuration if provided
    if (config) {
        m_config = *config;
    }

    // Check if WiFi is enabled
    if (!m_config.enabled) {
        LOG_INFO("WiFi: Disabled in configuration");
        m_state = STATE_DISABLED;
        m_initialized = true;
        return true;
    }

    LOG_INFO("WiFi: Initializing");

#ifndef MOCK_MODE
    // Set WiFi mode
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(m_config.hostname);

    // Configure power saving
    setPowerSaving(m_config.powerSaving);
#endif

    m_initialized = true;

    // Attempt initial connection
    if (strlen(m_config.ssid) > 0) {
        LOG_INFO("WiFi: Credentials configured, connecting to %s", m_config.ssid);
        connect();
    } else {
        LOG_INFO("WiFi: No credentials configured, entering AP mode");
        startAPMode();
    }

    return true;
}

void WiFiManager::update() {
    if (!m_initialized || m_state == STATE_DISABLED) {
        return;
    }

    // Update status periodically
    if (millis() - m_lastStatusUpdate >= 1000) {
        updateStatus();
        m_lastStatusUpdate = millis();
    }

    // Handle current state
    switch (m_state) {
        case STATE_DISABLED:
            handleStateDisabled();
            break;

        case STATE_AP_MODE:
            handleStateAPMode();
            break;

        case STATE_CONNECTING:
            handleStateConnecting();
            break;

        case STATE_CONNECTED:
            handleStateConnected();
            break;

        case STATE_DISCONNECTED:
            handleStateDisconnected();
            break;

        case STATE_FAILED:
            handleStateFailed();
            break;
    }
}

bool WiFiManager::connect() {
    if (!m_config.enabled) {
        LOG_ERROR("WiFi: Cannot connect, WiFi disabled");
        return false;
    }

    if (strlen(m_config.ssid) == 0) {
        LOG_WARN("WiFi: No SSID configured, entering AP mode");
        return startAPMode();
    }

#ifndef MOCK_MODE
    LOG_INFO("WiFi: Connecting to %s", m_config.ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(m_config.ssid, m_config.password);

    m_connectStartTime = millis();
    setState(STATE_CONNECTING);
#else
    // Mock mode: instant connection
    setState(STATE_CONNECTED);
    m_status.ip = IPAddress(192, 168, 1, 100);
    m_status.rssi = -45;
    strncpy(m_status.ssid, m_config.ssid, sizeof(m_status.ssid));
#endif

    return true;
}

bool WiFiManager::disconnect() {
    LOG_INFO("WiFi: Disconnecting");

#ifndef MOCK_MODE
    WiFi.disconnect();
#endif

    setState(STATE_DISCONNECTED);
    return true;
}

bool WiFiManager::reconnect() {
    LOG_INFO("WiFi: Reconnecting");

    m_status.failureCount = 0;
    m_lastReconnectAttempt = millis();

    return connect();
}

bool WiFiManager::startAPMode(const char* ssid, const char* password) {
    String apSSID = ssid ? String(ssid) : generateAPSSID();

    LOG_INFO("WiFi: Starting AP mode (SSID: %s)", apSSID.c_str());

    strncpy(m_status.apSSID, apSSID.c_str(), sizeof(m_status.apSSID));

#ifndef MOCK_MODE
    WiFi.mode(WIFI_AP);

    if (password && strlen(password) > 0) {
        WiFi.softAP(apSSID.c_str(), password);
    } else {
        WiFi.softAP(apSSID.c_str());  // Open network
    }

    IPAddress apIP = WiFi.softAPIP();
    LOG_INFO("WiFi: AP mode started, IP: %s", apIP.toString().c_str());

    m_status.ip = apIP;
#else
    // Mock mode
    m_status.ip = IPAddress(192, 168, 4, 1);
#endif

    setState(STATE_AP_MODE);

    if (m_onAPMode) {
        m_onAPMode();
    }

    return true;
}

bool WiFiManager::stopAPMode() {
    if (m_state != STATE_AP_MODE) {
        return false;
    }

    LOG_INFO("WiFi: Stopping AP mode");

#ifndef MOCK_MODE
    WiFi.softAPdisconnect(true);
#endif

    setState(STATE_DISCONNECTED);
    return true;
}

bool WiFiManager::setCredentials(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) {
        LOG_ERROR("WiFi: Invalid SSID");
        return false;
    }

    strncpy(m_config.ssid, ssid, sizeof(m_config.ssid) - 1);
    m_config.ssid[sizeof(m_config.ssid) - 1] = '\0';

    if (password) {
        strncpy(m_config.password, password, sizeof(m_config.password) - 1);
        m_config.password[sizeof(m_config.password) - 1] = '\0';
    } else {
        m_config.password[0] = '\0';
    }

    LOG_INFO("WiFi: Credentials updated for %s", m_config.ssid);

    return true;
}

int WiFiManager::scanNetworks(String* networks, int maxNetworks) {
#ifndef MOCK_MODE
    int n = WiFi.scanNetworks();

    if (n == 0) {
        LOG_INFO("WiFi: No networks found");
        return 0;
    }

    int count = min(n, maxNetworks);
    for (int i = 0; i < count; i++) {
        networks[i] = WiFi.SSID(i);
    }

    LOG_INFO("WiFi: Found %d networks", n);
    return count;
#else
    // Mock mode: return fake networks
    if (maxNetworks > 0) networks[0] = "MockNetwork1";
    if (maxNetworks > 1) networks[1] = "MockNetwork2";
    return min(2, maxNetworks);
#endif
}

const char* WiFiManager::getStateName(State state) {
    switch (state) {
        case STATE_DISABLED: return "DISABLED";
        case STATE_AP_MODE: return "AP_MODE";
        case STATE_CONNECTING: return "CONNECTING";
        case STATE_CONNECTED: return "CONNECTED";
        case STATE_DISCONNECTED: return "DISCONNECTED";
        case STATE_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

void WiFiManager::handleStateDisabled() {
    // Nothing to do
}

void WiFiManager::handleStateAPMode() {
    // AP mode is steady state, wait for configuration
}

void WiFiManager::handleStateConnecting() {
#ifndef MOCK_MODE
    if (WiFi.status() == WL_CONNECTED) {
        // Connection successful
        m_connectionStartTime = millis();
        m_status.failureCount = 0;
        m_status.reconnectCount++;

        updateStatus();
        setupMDNS();

        LOG_INFO("WiFi: Connected! IP: %s, RSSI: %d dBm",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());

        setState(STATE_CONNECTED);

        if (m_onConnected) {
            m_onConnected();
        }
    } else {
        // Check timeout
        if (millis() - m_connectStartTime > m_config.connectionTimeout) {
            LOG_WARN("WiFi: Connection timeout to %s", m_config.ssid);

            WiFi.disconnect();
            m_status.failureCount++;
            m_lastReconnectAttempt = millis();  // Set reconnect timer on failure

            setState(STATE_DISCONNECTED);

            if (m_onDisconnected) {
                m_onDisconnected();
            }
        }
    }
#endif
}

void WiFiManager::handleStateConnected() {
#ifndef MOCK_MODE
    // Check if still connected
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("WiFi: Connection lost");

        m_status.failureCount++;
        m_lastReconnectAttempt = millis();  // Set reconnect timer on connection loss

        setState(STATE_DISCONNECTED);

        if (m_onDisconnected) {
            m_onDisconnected();
        }
    }
#endif
}

void WiFiManager::handleStateDisconnected() {
    if (shouldReconnect()) {
        LOG_INFO("WiFi: Attempting reconnect (failure count: %u)", m_status.failureCount);

        connect();
    }
}

void WiFiManager::handleStateFailed() {
    // Max reconnect attempts reached
    if (m_config.apModeOnFailure) {
        LOG_ERROR("WiFi: Connection failed after %u attempts, entering AP mode", m_status.failureCount);
        startAPMode();
    }
}

void WiFiManager::setupMDNS() {
#ifndef MOCK_MODE
    if (!MDNS.begin(m_config.hostname)) {
        LOG_ERROR("WiFi: mDNS failed to start");
        return;
    }

    MDNS.addService("http", "tcp", 80);
    LOG_INFO("WiFi: mDNS started - http://%s.local", m_config.hostname);
#else
    LOG_INFO("WiFi: mDNS (mock) - http://%s.local", m_config.hostname);
#endif
}

void WiFiManager::updateStatus() {
    m_status.state = m_state;

    if (m_state == STATE_CONNECTED) {
#ifndef MOCK_MODE
        m_status.rssi = WiFi.RSSI();
        m_status.ip = WiFi.localIP();
        strncpy(m_status.ssid, WiFi.SSID().c_str(), sizeof(m_status.ssid));
        m_status.connectionUptime = millis() - m_connectionStartTime;

        // Warn if signal weak
        if (m_status.rssi < -80) {
            LOG_WARN("WiFi: Weak signal (%d dBm)", m_status.rssi);
        }
#else
        m_status.connectionUptime = millis() - m_connectionStartTime;
#endif
    }
}

bool WiFiManager::shouldReconnect() {
    // Don't reconnect if disabled or in AP mode
    if (m_state == STATE_DISABLED || m_state == STATE_AP_MODE) {
        return false;
    }

    // Check if max attempts reached
    if (m_status.failureCount >= m_config.maxReconnectAttempts) {
        setState(STATE_FAILED);
        return false;
    }

    // Check if enough time has passed
    uint32_t elapsed = millis() - m_lastReconnectAttempt;
    return elapsed >= getReconnectDelay();
}

uint32_t WiFiManager::getReconnectDelay() {
    // Exponential backoff: 5s, 10s, 20s, 40s, 60s (max)
    uint32_t delay = m_config.reconnectDelayMs << m_status.failureCount;
    return (delay > 60000U) ? 60000U : delay;  // Cap at 60 seconds
}

String WiFiManager::generateAPSSID() {
#ifndef MOCK_MODE
    uint8_t mac[6];
    WiFi.macAddress(mac);

    char ssid[32];
    snprintf(ssid, sizeof(ssid), "StepAware-%02X%02X", mac[4], mac[5]);

    return String(ssid);
#else
    return String("StepAware-MOCK");
#endif
}

void WiFiManager::setPowerSaving(bool enabled) {
#ifndef MOCK_MODE
    if (enabled) {
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        LOG_INFO("WiFi: Power saving enabled");
    } else {
        WiFi.setSleep(WIFI_PS_NONE);
        LOG_INFO("WiFi: Power saving disabled (best performance)");
    }
#endif
}

void WiFiManager::setState(State newState) {
    if (m_state != newState) {
        LOG_INFO("WiFi: State %s -> %s", getStateName(m_state), getStateName(newState));
        m_state = newState;
        m_status.state = newState;
    }
}

void WiFiManager::updateConfig(const Config& config) {
    bool wasEnabled = m_config.enabled;
    bool wasConnected = (m_state == STATE_CONNECTED);

    // Update configuration
    m_config = config;

    LOG_INFO("WiFi: Configuration updated (enabled=%s, ssid=%s)",
             m_config.enabled ? "yes" : "no", m_config.ssid);

    if (!m_config.enabled && wasEnabled) {
        // WiFi was disabled
        LOG_INFO("WiFi: Disabling");
        disconnect();
        setState(STATE_DISABLED);
    } else if (m_config.enabled && !wasEnabled) {
        // WiFi was enabled
        LOG_INFO("WiFi: Enabling");
        m_status.failureCount = 0;
        if (strlen(m_config.ssid) > 0) {
            connect();
        } else {
            LOG_WARN("WiFi: No SSID configured, entering AP mode");
            startAPMode();
        }
    } else if (m_config.enabled && wasConnected) {
        // Check if SSID changed while connected
        if (strcmp(m_status.ssid, m_config.ssid) != 0) {
            LOG_INFO("WiFi: SSID changed, reconnecting");
            disconnect();
            m_status.failureCount = 0;
            connect();
        }
    }
}

void WiFiManager::setEnabled(bool enabled) {
    if (enabled == m_config.enabled) {
        return;  // No change
    }

    m_config.enabled = enabled;

    if (enabled) {
        LOG_INFO("WiFi: Enabling");
        m_status.failureCount = 0;
        if (strlen(m_config.ssid) > 0) {
            connect();
        } else {
            LOG_WARN("WiFi: No SSID configured, entering AP mode");
            startAPMode();
        }
    } else {
        LOG_INFO("WiFi: Disabling");
        disconnect();
        setState(STATE_DISABLED);
    }
}
