# WiFi Manager Design Specification

## Overview

The WiFi Manager handles all WiFi connectivity for StepAware, including initial setup via Access Point (AP) mode, persistent connection management, automatic reconnection, and status monitoring.

## Design Goals

1. **Easy Setup**: First-time configuration via captive portal
2. **Reliability**: Automatic reconnection with exponential backoff
3. **Status Visibility**: Clear connection state reporting
4. **Power Efficiency**: Minimize WiFi power consumption
5. **Graceful Degradation**: System works without WiFi (reduced functionality)
6. **Configurability**: WiFi can be enabled/disabled via config

## Use Cases

### Use Case 1: First-Time Setup
```
1. User powers on device (no WiFi configured)
2. Device enters AP mode: "StepAware-Setup"
3. User connects phone to AP
4. Captive portal opens automatically
5. User enters WiFi credentials
6. Device saves config and reboots
7. Device connects to user's WiFi
```

### Use Case 2: Normal Operation
```
1. Device boots with saved WiFi credentials
2. Attempts connection to saved network
3. Connection successful → Start web server
4. User accesses dashboard via http://stepaware.local
```

### Use Case 3: Connection Lost
```
1. WiFi connection drops (router reboot, out of range)
2. Device detects disconnection
3. Waits 5 seconds, attempts reconnect
4. If fails, waits 10 seconds, tries again
5. Exponential backoff up to 60 seconds
6. After 10 failed attempts → Enter AP mode for reconfiguration
```

### Use Case 4: WiFi Disabled
```
1. User disables WiFi in config
2. Device operates in standalone mode
3. No web interface available
4. Button-only operation
5. Can re-enable WiFi via config file edit
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     WiFiManager                              │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ├─► Connection States
                        │   - DISABLED
                        │   - AP_MODE (setup)
                        │   - CONNECTING
                        │   - CONNECTED
                        │   - DISCONNECTED
                        │   - FAILED
                        │
                        ├─► AP Mode (Setup)
                        │   - SSID: "StepAware-XXXX"
                        │   - Password: Optional
                        │   - Captive Portal
                        │   - Config web page
                        │
                        ├─► Station Mode (Normal)
                        │   - Connect to user WiFi
                        │   - mDNS: stepaware.local
                        │   - Auto-reconnect
                        │   - Signal monitoring
                        │
                        ├─► Status Monitoring
                        │   - Connection state
                        │   - Signal strength (RSSI)
                        │   - IP address
                        │   - Uptime
                        │
                        └─► Power Management
                            - Sleep when idle
                            - Wake for connections
                            - Configurable power saving
```

## State Machine

```
                    ┌──────────────┐
                    │   DISABLED   │◄──── WiFi disabled in config
                    └──────┬───────┘
                           │ enable()
                           ▼
                    ┌──────────────┐
            ┌──────►│   AP_MODE    │◄──── No credentials or setup mode
            │       └──────┬───────┘
            │              │ credentials saved
            │              ▼
            │       ┌──────────────┐
            │       │  CONNECTING  │◄──── Attempting connection
            │       └──┬────────┬──┘
            │          │        │
            │ timeout  │ success│ failure
            │          │        │
            │          ▼        ▼
            │       ┌──────────────┐      ┌──────────────┐
            └───────│   CONNECTED  │      │ DISCONNECTED │
                    └──────┬───────┘      └──────┬───────┘
                           │ lost                 │ retry
                           └──────────────────────┘
```

## Interface Design

```cpp
class WiFiManager {
public:
    enum State {
        STATE_DISABLED,     // WiFi disabled
        STATE_AP_MODE,      // Access Point mode (setup)
        STATE_CONNECTING,   // Attempting to connect
        STATE_CONNECTED,    // Connected to WiFi
        STATE_DISCONNECTED, // Disconnected (will retry)
        STATE_FAILED        // Connection failed (max retries)
    };

    struct Config {
        bool enabled;                // WiFi enabled/disabled
        char ssid[64];               // WiFi SSID
        char password[64];           // WiFi password
        char hostname[32];           // mDNS hostname
        bool apModeOnFailure;        // Enter AP mode on connection failure
        uint32_t reconnectDelayMs;   // Initial reconnect delay
        uint8_t maxReconnectAttempts;// Max reconnect attempts before AP mode
        bool powerSaving;            // Enable WiFi power saving
    };

    struct Status {
        State state;
        int8_t rssi;                 // Signal strength (-100 to 0)
        IPAddress ip;                // IP address
        char ssid[64];               // Connected SSID
        uint32_t uptime;             // Connection uptime
        uint32_t reconnectCount;     // Reconnection count
        uint32_t failureCount;       // Consecutive failure count
    };

    bool begin(const Config* config = nullptr);
    void update();

    // Connection management
    bool connect();
    bool disconnect();
    bool reconnect();

    // AP mode
    bool startAPMode(const char* ssid = nullptr, const char* password = nullptr);
    bool stopAPMode();

    // Status
    State getState() const;
    const Status& getStatus() const;
    bool isConnected() const;
    int8_t getRSSI() const;
    IPAddress getIP() const;

    // Configuration
    bool setCredentials(const char* ssid, const char* password);
    bool saveConfig();
    bool loadConfig();

    // Callbacks
    void onConnected(void (*callback)());
    void onDisconnected(void (*callback)());
    void onAPMode(void (*callback)());

private:
    void handleStateDisabled();
    void handleStateAPMode();
    void handleStateConnecting();
    void handleStateConnected();
    void handleStateDisconnected();
    void handleStateFailed();

    void setupMDNS();
    void updateStatus();
    bool shouldReconnect();
    uint32_t getReconnectDelay();
};
```

## AP Mode Setup

### SSID Generation
```cpp
// Format: StepAware-XXXX
// XXXX = Last 4 digits of MAC address
String generateAPSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "StepAware-%02X%02X",
             mac[4], mac[5]);
    return String(ssid);
}
```

### Captive Portal
```html
<!-- Automatically served on any HTTP request in AP mode -->
<!DOCTYPE html>
<html>
<head>
    <title>StepAware Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
    <h1>StepAware WiFi Setup</h1>
    <form action="/setup" method="POST">
        <label>WiFi Network:</label>
        <select name="ssid">
            <!-- Populated with scan results -->
        </select>
        <label>Password:</label>
        <input type="password" name="password">
        <button type="submit">Connect</button>
    </form>
</body>
</html>
```

### Setup Flow
```
1. User connects to "StepAware-XXXX"
2. Phone auto-opens captive portal
3. Device serves setup page
4. User selects WiFi network from scan
5. User enters password
6. POST to /setup with credentials
7. Device validates and saves config
8. Device reboots to apply
9. Device connects to user WiFi
```

## Connection Management

### Initial Connection
```cpp
bool WiFiManager::connect() {
    if (!m_config.enabled || strlen(m_config.ssid) == 0) {
        return startAPMode();
    }

    LOG_INFO("WiFi: Connecting to %s", m_config.ssid);
    m_state = STATE_CONNECTING;
    m_connectStartTime = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(m_config.ssid, m_config.password);

    return true;
}
```

### Reconnection Strategy
```cpp
// Exponential backoff: 5s, 10s, 20s, 40s, 60s (max)
uint32_t WiFiManager::getReconnectDelay() {
    uint32_t delay = m_config.reconnectDelayMs << m_status.failureCount;
    return min(delay, 60000);  // Cap at 60 seconds
}

bool WiFiManager::shouldReconnect() {
    // Don't reconnect if disabled or in AP mode
    if (m_state == STATE_DISABLED || m_state == STATE_AP_MODE) {
        return false;
    }

    // Check if max attempts reached
    if (m_status.failureCount >= m_config.maxReconnectAttempts) {
        LOG_ERROR("WiFi: Max reconnect attempts reached, entering AP mode");
        return false;  // Will trigger AP mode
    }

    // Check if enough time has passed
    uint32_t elapsed = millis() - m_lastReconnectAttempt;
    return elapsed >= getReconnectDelay();
}
```

### Connection Timeout
```cpp
void WiFiManager::handleStateConnecting() {
    if (WiFi.status() == WL_CONNECTED) {
        m_state = STATE_CONNECTED;
        m_status.failureCount = 0;
        m_status.reconnectCount++;
        updateStatus();
        setupMDNS();

        LOG_INFO("WiFi: Connected! IP: %s", WiFi.localIP().toString().c_str());

        if (m_onConnected) {
            m_onConnected();
        }
    } else {
        // Check timeout (30 seconds)
        if (millis() - m_connectStartTime > 30000) {
            LOG_WARN("WiFi: Connection timeout");
            m_state = STATE_DISCONNECTED;
            m_status.failureCount++;
            WiFi.disconnect();

            if (m_onDisconnected) {
                m_onDisconnected();
            }
        }
    }
}
```

## mDNS Setup

```cpp
void WiFiManager::setupMDNS() {
    if (!MDNS.begin(m_config.hostname)) {
        LOG_ERROR("WiFi: mDNS failed to start");
        return;
    }

    MDNS.addService("http", "tcp", 80);
    LOG_INFO("WiFi: mDNS started - http://%s.local", m_config.hostname);
}
```

## Signal Monitoring

```cpp
void WiFiManager::updateStatus() {
    if (m_state == STATE_CONNECTED) {
        m_status.rssi = WiFi.RSSI();
        m_status.ip = WiFi.localIP();
        strncpy(m_status.ssid, WiFi.SSID().c_str(), sizeof(m_status.ssid));
        m_status.uptime = millis() - m_connectionStartTime;

        // Warn if signal weak
        if (m_status.rssi < -80) {
            LOG_WARN("WiFi: Weak signal (%d dBm)", m_status.rssi);
        }
    }
}
```

## Power Management Integration

```cpp
// WiFi power saving modes
void WiFiManager::setPowerSaving(bool enabled) {
    if (enabled) {
        WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Minimum power saving
        LOG_INFO("WiFi: Power saving enabled");
    } else {
        WiFi.setSleep(WIFI_PS_NONE);  // No power saving (best performance)
        LOG_INFO("WiFi: Power saving disabled");
    }
}
```

## Configuration Storage

```json
// Part of main config.json
{
    "wifi": {
        "enabled": true,
        "ssid": "MyNetwork",
        "password": "MyPassword123",
        "hostname": "stepaware",
        "apModeOnFailure": true,
        "reconnectDelayMs": 5000,
        "maxReconnectAttempts": 10,
        "powerSaving": false
    }
}
```

## API Endpoints

### GET /api/wifi/status
```json
{
    "state": "CONNECTED",
    "ssid": "MyNetwork",
    "rssi": -45,
    "ip": "192.168.1.100",
    "uptime": 3600000,
    "reconnectCount": 2,
    "failureCount": 0
}
```

### POST /api/wifi/scan
```json
{
    "networks": [
        {
            "ssid": "MyNetwork",
            "rssi": -45,
            "encryption": "WPA2"
        },
        {
            "ssid": "NeighborNetwork",
            "rssi": -72,
            "encryption": "WPA2"
        }
    ]
}
```

### POST /api/wifi/connect
```json
{
    "ssid": "MyNetwork",
    "password": "MyPassword123"
}
```

### POST /api/wifi/disconnect
```json
{
    "success": true
}
```

## Watchdog Integration

```cpp
WatchdogManager::HealthStatus checkWiFiHealth(const char** message) {
    WiFiManager::State state = g_wifi.getState();

    switch (state) {
        case WiFiManager::STATE_CONNECTED:
            // Check signal strength
            if (g_wifi.getRSSI() < -85) {
                *message = "Weak signal";
                return WatchdogManager::HEALTH_WARNING;
            }
            return WatchdogManager::HEALTH_OK;

        case WiFiManager::STATE_CONNECTING:
            return WatchdogManager::HEALTH_OK;  // Normal

        case WiFiManager::STATE_DISCONNECTED:
            *message = "Disconnected, will retry";
            return WatchdogManager::HEALTH_WARNING;

        case WiFiManager::STATE_FAILED:
            *message = "Connection failed";
            return WatchdogManager::HEALTH_CRITICAL;

        case WiFiManager::STATE_AP_MODE:
            return WatchdogManager::HEALTH_OK;  // Normal for setup

        case WiFiManager::STATE_DISABLED:
            return WatchdogManager::HEALTH_OK;  // Intentional

        default:
            return WatchdogManager::HEALTH_FAILED;
    }
}

bool recoverWiFi(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            // Try reconnecting
            return g_wifi.reconnect();

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            // Restart WiFi subsystem
            g_wifi.disconnect();
            delay(1000);
            return g_wifi.connect();

        default:
            return false;
    }
}
```

## Mock Mode Support

```cpp
#ifdef MOCK_MODE
class MockWiFiManager : public WiFiManager {
public:
    // Simulate WiFi states without actual hardware
    void simulateConnect() { m_state = STATE_CONNECTED; }
    void simulateDisconnect() { m_state = STATE_DISCONNECTED; }
    void setMockRSSI(int8_t rssi) { m_status.rssi = rssi; }
};
#endif
```

## Testing Strategy

### Unit Tests
- State transitions
- Reconnection logic
- Exponential backoff
- AP mode activation
- Configuration save/load

### Integration Tests
- Connect to mock AP
- Handle disconnection
- Recover from failures
- mDNS resolution
- Captive portal flow

### Hardware Tests
- Real WiFi connection
- Signal strength monitoring
- Roaming between APs
- Power consumption
- Long-term stability

## Security Considerations

1. **Password Storage**: Encrypted in SPIFFS
2. **AP Mode Security**: Optional password protection
3. **Captive Portal**: No authentication (temporary)
4. **mDNS**: Local network only
5. **HTTPS**: Future enhancement

## Performance

- **Memory**: ~4KB RAM
- **Flash**: ~15KB code
- **Connection Time**: 3-10 seconds (typical)
- **Reconnect Time**: 5-60 seconds (with backoff)
- **Power**: ~80mA when active, <1mA in sleep

## Benefits

1. **User-Friendly**: Easy first-time setup
2. **Reliable**: Automatic recovery from failures
3. **Efficient**: Minimal power consumption
4. **Flexible**: Can be disabled if not needed
5. **Monitored**: Integrated with watchdog

---

**Last Updated**: 2026-01-12
**Version**: 1.0
**Status**: Design Specification
