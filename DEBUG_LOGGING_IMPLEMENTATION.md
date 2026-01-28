# Debug Logging Implementation Plan

**Date**: 2026-01-27
**Purpose**: Add comprehensive persistent debug logging to diagnose sensor reading delays and configuration issues

## Overview

Implement a persistent debug logging system that:
1. **Stores logs to LittleFS** (survives reboots)
2. **Maintains 3 boot cycles** of logs with automatic rotation
3. **Limits filesystem usage** to max 30%
4. **Logs detailed diagnostics** of config, sensors, state machine, LED operations
5. **Provides download API** for remote diagnosis

## Architecture

### Components

#### 1. DebugLogger Class
**Location**: [include/debug_logger.h](include/debug_logger.h), [src/debug_logger.cpp](src/debug_logger.cpp)

**Features**:
- Log levels: VERBOSE, DEBUG, INFO, WARN, ERROR, NONE
- Log categories: BOOT, CONFIG, SENSOR, STATE, LED, WIFI, API, SYSTEM
- Boot cycle tracking
- Automatic log rotation
- Filesystem space management
- Auto-flush (every 5s or 20 writes)

**Log File Structure**:
```
/logs/boot_current.log  - Current session
/logs/boot_1.log        - Previous session
/logs/boot_2.log        - 2 sessions ago
/logs/boot_info.txt     - Boot cycle metadata
```

#### 2. Web API Endpoints
**Location**: [include/web_api.h](include/web_api.h), [src/web_api.cpp](src/web_api.cpp)

**New Endpoints**:
```
GET  /api/debug/logs          - List all log files with metadata
GET  /api/debug/logs/current  - Download current log
GET  /api/debug/logs/boot_1   - Download boot_1 log
GET  /api/debug/logs/boot_2   - Download boot_2 log
POST /api/debug/logs/clear    - Clear all logs
GET  /api/debug/config        - Get debug logger configuration
POST /api/debug/config        - Update debug logger configuration
```

## Implementation Steps

### Step 1: Create DebugLogger Class ✓

Files created:
- [include/debug_logger.h](include/debug_logger.h)
- [src/debug_logger.cpp](src/debug_logger.cpp)

### Step 2: Add Web API Endpoints

#### 2.1 Update web_api.h

Add to private section:
```cpp
/**
 * @brief GET /api/debug/logs - List all debug logs
 */
void handleGetDebugLogs(AsyncWebServerRequest* request);

/**
 * @brief GET /api/debug/logs/:file - Download specific log file
 */
void handleDownloadDebugLog(AsyncWebServerRequest* request);

/**
 * @brief POST /api/debug/logs/clear - Clear all debug logs
 */
void handleClearDebugLogs(AsyncWebServerRequest* request);

/**
 * @brief GET /api/debug/config - Get debug logger config
 */
void handleGetDebugConfig(AsyncWebServerRequest* request);

/**
 * @brief POST /api/debug/config - Update debug logger config
 */
void handlePostDebugConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
```

#### 2.2 Update web_api.cpp - Add Routes

In `WebAPI::begin()`:
```cpp
// Debug logging endpoints
m_server->on("/api/debug/logs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    this->handleGetDebugLogs(req);
});

m_server->on("/api/debug/logs/current", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/logs/boot_current.log", "text/plain");
});

m_server->on("/api/debug/logs/boot_1", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/logs/boot_1.log", "text/plain");
});

m_server->on("/api/debug/logs/boot_2", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/logs/boot_2.log", "text/plain");
});

m_server->on("/api/debug/logs/clear", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        this->handleClearDebugLogs(req);
    });

m_server->on("/api/debug/config", HTTP_GET, [this](AsyncWebServerRequest* req) {
    this->handleGetDebugConfig(req);
});

m_server->on("/api/debug/config", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        this->handlePostDebugConfig(req, data, len, index, total);
    });
```

#### 2.3 Implement Handlers

```cpp
void WebAPI::handleGetDebugLogs(AsyncWebServerRequest* request) {
    // Return JSON with list of log files and metadata
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"bootCycle\":%u,"
        "\"filesystemUsage\":%u,"
        "\"totalLogsSize\":%u,"
        "\"logs\":["
        "{\"name\":\"current\",\"size\":%u,\"path\":\"/logs/boot_current.log\"},"
        "{\"name\":\"boot_1\",\"size\":%u,\"path\":\"/logs/boot_1.log\"},"
        "{\"name\":\"boot_2\",\"size\":%u,\"path\":\"/logs/boot_2.log\"}"
        "]"
        "}",
        g_debugLogger.getBootCycle(),
        g_debugLogger.getFilesystemUsage(),
        g_debugLogger.getTotalLogsSize(),
        getFileSize("/logs/boot_current.log"),
        getFileSize("/logs/boot_1.log"),
        getFileSize("/logs/boot_2.log")
    );
    sendJSON(request, 200, json);
}

void WebAPI::handleClearDebugLogs(AsyncWebServerRequest* request) {
    g_debugLogger.clearAllLogs();
    sendJSON(request, 200, "{\"success\":true,\"message\":\"All logs cleared\"}");
}

void WebAPI::handleGetDebugConfig(AsyncWebServerRequest* request) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"level\":\"%s\",\"categoryMask\":%u}",
        DebugLogger::getLevelName(g_debugLogger.getLevel()),
        g_debugLogger.getCategoryMask()
    );
    sendJSON(request, 200, json);
}

void WebAPI::handlePostDebugConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Parse JSON and update debug logger config
    // Format: {"level":"DEBUG","categoryMask":255}
}
```

### Step 3: Add Logging Calls Throughout Codebase

#### 3.1 main.cpp - Boot Logging

```cpp
#include "debug_logger.h"

void setup() {
    // Initialize debug logger EARLY
    g_debugLogger.begin(DebugLogger::LEVEL_DEBUG, DebugLogger::CAT_ALL);

    DEBUG_LOG_BOOT("=== StepAware Starting ===");
    DEBUG_LOG_BOOT("Firmware: %s", FIRMWARE_VERSION);
    DEBUG_LOG_BOOT("Board: %s", BOARD_NAME);
    DEBUG_LOG_BOOT("Free Heap: %u bytes", ESP.getFreeHeap());

    // After config loaded
    g_debugLogger.logConfigDump();

    // ... existing setup code ...

    DEBUG_LOG_BOOT("=== Boot Complete ===");
}
```

#### 3.2 config_manager.cpp - Config Logging

```cpp
#include "debug_logger.h"

bool ConfigManager::fromJSON(const char* jsonStr) {
    DEBUG_LOG_CONFIG("Loading config from JSON (%u bytes)", strlen(jsonStr));

    // ... existing code ...

    if (success) {
        DEBUG_LOG_CONFIG("Config loaded successfully");
        DEBUG_LOG_CONFIG("Device: %s, Mode: %d", m_config.device.name, m_config.device.defaultMode);
        DEBUG_LOG_CONFIG("Sensors configured: %d", activeSensorCount);
    } else {
        DEBUG_LOG_CONFIG("Config load FAILED: %s", error);
    }
}

bool ConfigManager::save() {
    DEBUG_LOG_CONFIG("Saving config to %s", CONFIG_FILE);

    // ... existing code ...

    if (success) {
        DEBUG_LOG_CONFIG("Config saved successfully (%u bytes)", size);
    }
}
```

#### 3.3 sensor_manager.cpp - Sensor Logging

```cpp
#include "debug_logger.h"

void SensorManager::update() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_sensors[i] && m_sensors[i]->isReady()) {
            m_sensors[i]->update();

            // Log sensor readings (VERBOSE level - only if enabled)
            uint32_t dist = m_sensors[i]->getDistance();
            bool motion = m_sensors[i]->motionDetected();
            int8_t dir = (int8_t)m_sensors[i]->getDirection();

            g_debugLogger.logSensorReading(i, dist, motion, dir);

            // Log motion events (DEBUG level)
            if (motion && !m_lastMotionState[i]) {
                DEBUG_LOG_SENSOR("Slot %u: MOTION DETECTED (dist=%u mm)", i, dist);
            } else if (!motion && m_lastMotionState[i]) {
                DEBUG_LOG_SENSOR("Slot %u: Motion cleared", i);
            }

            m_lastMotionState[i] = motion;
        }
    }
}
```

#### 3.4 state_machine.cpp - State Logging

```cpp
#include "debug_logger.h"

void StateMachine::changeState(State newState, const char* reason) {
    const char* oldStateName = getStateName(m_currentState);
    const char* newStateName = getStateName(newState);

    g_debugLogger.logStateTransition(oldStateName, newStateName, reason);

    DEBUG_LOG_STATE("State change: %s -> %s (reason: %s)",
                    oldStateName, newStateName, reason);

    m_currentState = newState;
}
```

#### 3.5 hal_led.cpp - LED Logging

```cpp
#include "debug_logger.h"

void HAL_LED::setState(LEDState state) {
    const char* stateName = getStateName(state);
    uint8_t brightness = getBrightness();

    g_debugLogger.logLEDChange(stateName, brightness);

    DEBUG_LOG_LED("LED: %s (brightness: %u)", stateName, brightness);

    // ... existing code ...
}
```

#### 3.6 web_api.cpp - API Logging

```cpp
#include "debug_logger.h"

void WebAPI::handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    DEBUG_LOG_API("POST /api/config - %u bytes", len);

    // ... existing code ...

    if (success) {
        DEBUG_LOG_API("Config updated successfully");
        g_debugLogger.logConfigDump();  // Log new config
    } else {
        DEBUG_LOG_API("Config update FAILED: %s", error);
    }
}
```

### Step 4: Update platformio.ini

Add library dependency:
```ini
lib_deps =
    # ... existing deps ...
    LittleFS
```

### Step 5: Testing

#### 5.1 Basic Functionality
1. Upload firmware with debug logging
2. Reboot device 3 times
3. Check `/api/debug/logs` - should show 3 log files
4. Download `/api/debug/logs/current` - should contain detailed logs

#### 5.2 Sensor Diagnostics
1. Set log level to VERBOSE
2. Trigger sensor with hand
3. Download logs
4. **Verify**:
   - Sensor readings logged every update cycle
   - Distance values show immediate changes
   - Motion detection events logged
   - State transitions logged

#### 5.3 Config Diagnostics
1. Change config via web UI
2. Download logs
3. **Verify**:
   - Config JSON logged
   - Applied settings logged
   - Sensor window size changes logged

#### 5.4 Filesystem Management
1. Run device for extended period (fill logs)
2. Reboot multiple times
3. **Verify**:
   - Logs rotate correctly (oldest deleted)
   - Filesystem usage stays under 30%
   - No crashes from full filesystem

## Expected Output

### Example Log (boot_current.log)

```
=== BOOT CYCLE #5 ===
Timestamp: 1234 ms
Log Level: DEBUG
Categories: 0xFF
Free Heap: 245632 bytes
Filesystem: 12% used
==============================
[0000001234] [INFO   ] [BOOT  ] === StepAware Starting ===
[0000001245] [INFO   ] [BOOT  ] Firmware: v0.3.0
[0000001256] [INFO   ] [BOOT  ] Board: ESP32-C3-DevKit-Lipo
[0000001267] [INFO   ] [BOOT  ] Free Heap: 245632 bytes
[0000001278] [INFO   ] [CONFIG] Loading config from file /config.json
[0000001289] [INFO   ] [CONFIG] Config loaded successfully
[0000001290] [INFO   ] [CONFIG] Device: StepAware, Mode: 2
[0000001291] [INFO   ] [CONFIG] Sensors configured: 1
[0000001292] [INFO   ] [CONFIG] Sensor 0: Type=ULTRASONIC, Threshold=1500mm, Window=5
[0000002345] [INFO   ] [BOOT  ] Initializing sensors...
[0000002356] [INFO   ] [SENSOR] Slot 0: HC-SR04 initialized (trigger=2, echo=3)
[0000003456] [INFO   ] [STATE ] State change: INIT -> IDLE (reason: boot complete)
[0000003457] [INFO   ] [BOOT  ] === Boot Complete ===
[0000004567] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000004627] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000004687] [VERBOSE] [SENSOR] Slot 0: dist=3518 mm, motion=NO, dir=STATIONARY
[0000005234] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=NO, dir=APPROACHING
[0000005294] [VERBOSE] [SENSOR] Slot 0: dist=1800 mm, motion=NO, dir=APPROACHING
[0000005354] [VERBOSE] [SENSOR] Slot 0: dist=1500 mm, motion=NO, dir=APPROACHING
[0000005414] [DEBUG  ] [SENSOR] Slot 0: MOTION DETECTED (dist=1500 mm)
[0000005415] [INFO   ] [STATE ] State change: IDLE -> MOTION_DETECTED (reason: sensor 0 triggered)
[0000005416] [DEBUG  ] [LED   ] LED: WARNING (brightness: 255)
```

### API Response Examples

#### GET /api/debug/logs
```json
{
  "bootCycle": 5,
  "filesystemUsage": 12,
  "totalLogsSize": 45678,
  "logs": [
    {"name": "current", "size": 23456, "path": "/logs/boot_current.log"},
    {"name": "boot_1", "size": 12345, "path": "/logs/boot_1.log"},
    {"name": "boot_2", "size": 9877, "path": "/logs/boot_2.log"}
  ]
}
```

#### GET /api/debug/config
```json
{
  "level": "DEBUG",
  "categoryMask": 255
}
```

## Benefits

✅ **Persistent Across Reboots** - Diagnose issues that happen at boot
✅ **Historical Data** - Compare last 3 boot cycles
✅ **Detailed Sensor Logs** - See every reading, understand delays
✅ **Config Tracking** - Know exactly what settings are applied
✅ **Remote Diagnosis** - Download logs via API without serial connection
✅ **Space Management** - Automatic rotation prevents filesystem overflow
✅ **Minimal Performance Impact** - Auto-flush with batching
✅ **Filterable** - Category and level filtering reduces noise

## Next Steps

1. **Implement API endpoints** in web_api.cpp
2. **Add logging calls** throughout codebase
3. **Test on hardware** with various scenarios
4. **Analyze logs** to diagnose sensor delay issue
5. **Tune log levels** based on findings

---

**Status**: Implementation in progress
**Priority**: HIGH - Critical for diagnosing sensor issues
**Estimated Time**: 2-3 hours implementation + testing
