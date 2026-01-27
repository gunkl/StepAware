#include "web_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "watchdog_manager.h"
#include "hal_ledmatrix_8x8.h"
#include "sensor_manager.h"
#include <ArduinoJson.h>
#if !MOCK_HARDWARE
#include <LittleFS.h>  // For animation uploads and user content, NOT for web UI
#endif

WebAPI::WebAPI(AsyncWebServer* server, StateMachine* stateMachine, ConfigManager* config)
    : m_server(server)
    , m_stateMachine(stateMachine)
    , m_config(config)
    , m_wifi(nullptr)
    , m_power(nullptr)
    , m_watchdog(nullptr)
    , m_ledMatrix(nullptr)
    , m_sensorManager(nullptr)
    , m_corsEnabled(true)
{
}

WebAPI::~WebAPI() {
}

bool WebAPI::begin() {
    if (!m_server || !m_stateMachine || !m_config) {
        LOG_ERROR("WebAPI: Invalid parameters");
        return false;
    }

    LOG_INFO("WebAPI: Registering endpoints");

    // Root endpoint - inline dashboard UI
    // NOTE: We use inline HTML (buildDashboardHTML) instead of filesystem-based UI
    // because it includes all features (multi-sensor, LED matrix, animations) and
    // is simpler to deploy (no filesystem upload required).
    m_server->on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleRoot(req);
    });

    // GET endpoints
    m_server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetStatus(req);
    });

    m_server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetConfig(req);
    });

    m_server->on("/api/mode", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetMode(req);
    });

    m_server->on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetLogs(req);
    });

    m_server->on("/api/version", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetVersion(req);
    });

    // POST endpoints (with body handler)
    m_server->on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePostConfig(req, data, len, index, total);
        }
    );

    m_server->on("/api/mode", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePostMode(req, data, len, index, total);
        }
    );

    m_server->on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* req) {
        this->handlePostReset(req);
    });

    m_server->on("/api/sensors", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePostSensors(req, data, len, index, total);
        }
    );

    m_server->on("/api/displays", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetDisplays(req);
    });

    m_server->on("/api/displays", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePostDisplays(req, data, len, index, total);
        }
    );

    // OPTIONS for CORS preflight
    m_server->on("/api/status", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/config", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/mode", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/logs", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/sensors", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/displays", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });

    // Custom Animation API (Issue #12 Phase 2)
    // Register more specific routes BEFORE general routes

    // GET /api/animations/template?type=MOTION_ALERT - download animation template
    m_server->on("/api/animations/template", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetAnimationTemplate(req);
    });

    m_server->on("/api/animations", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetAnimations(req);
    });

    m_server->on("/api/animations/play", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePlayAnimation(req, data, len, index, total);
        }
    );

    m_server->on("/api/animations/stop", HTTP_POST, [this](AsyncWebServerRequest* req) {
        this->handleStopAnimation(req);
    });

    m_server->on("/api/animations/builtin", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handlePlayBuiltInAnimation(req, data, len, index, total);
        }
    );

    m_server->on("/api/animations/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        [this](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            this->handleUploadAnimation(req, filename, index, data, len, final);
        },
        nullptr
    );

    m_server->on("/api/animations/assign", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            this->handleAssignAnimation(req, data, len, index, total);
        }
    );

    m_server->on("/api/animations/assignments", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetAssignments(req);
    });

    // DELETE /api/animations/:name - dynamic route handler
    m_server->on("/api/animations/*", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        this->handleDeleteAnimation(req);
    });

    // OPTIONS for animation endpoints
    m_server->on("/api/animations", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/animations/play", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/animations/stop", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/animations/upload", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/animations/builtin", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });

    LOG_INFO("WebAPI: ✓ Endpoints registered");
    return true;
}

void WebAPI::setCORSEnabled(bool enabled) {
    m_corsEnabled = enabled;
}

void WebAPI::setWiFiManager(WiFiManager* wifi) {
    m_wifi = wifi;
}

void WebAPI::setPowerManager(PowerManager* power) {
    m_power = power;
}

void WebAPI::setWatchdogManager(WatchdogManager* watchdog) {
    m_watchdog = watchdog;
}

void WebAPI::setLEDMatrix(HAL_LEDMatrix_8x8* ledMatrix) {
    m_ledMatrix = ledMatrix;
}

void WebAPI::setSensorManager(SensorManager* sensorManager) {
    m_sensorManager = sensorManager;
}

void WebAPI::handleGetStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> doc;

    // System info
    doc["uptime"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();

    // State Machine
    JsonObject stateMachineObj = doc.createNestedObject("stateMachine");
    stateMachineObj["mode"] = m_stateMachine->getMode();
    stateMachineObj["modeName"] = StateMachine::getModeName(m_stateMachine->getMode());
    stateMachineObj["warningActive"] = m_stateMachine->isWarningActive();
    stateMachineObj["motionEvents"] = m_stateMachine->getMotionEventCount();
    stateMachineObj["modeChanges"] = m_stateMachine->getModeChangeCount();

    // WiFi Manager (if available)
    if (m_wifi) {
        JsonObject wifiObj = doc.createNestedObject("wifi");
        wifiObj["state"] = m_wifi->getState();
        wifiObj["stateName"] = WiFiManager::getStateName(m_wifi->getState());
        wifiObj["rssi"] = m_wifi->getRSSI();

        const WiFiManager::Status& wifiStatus = m_wifi->getStatus();
        wifiObj["ssid"] = wifiStatus.ssid;
        wifiObj["ipAddress"] = wifiStatus.ip.toString();
        wifiObj["failures"] = wifiStatus.failureCount;
        wifiObj["reconnects"] = wifiStatus.reconnectCount;
        wifiObj["uptime"] = wifiStatus.connectionUptime;
    }

    // Power Manager (if available)
    if (m_power) {
        JsonObject powerObj = doc.createNestedObject("power");
        powerObj["state"] = m_power->getState();
        powerObj["stateName"] = PowerManager::getStateName(m_power->getState());

        const PowerManager::BatteryStatus& battery = m_power->getBatteryStatus();
        powerObj["batteryVoltage"] = battery.voltage;
        powerObj["batteryPercent"] = battery.percentage;
        powerObj["charging"] = battery.charging;
        powerObj["low"] = battery.low;
        powerObj["critical"] = battery.critical;

        const PowerManager::PowerStats& powerStats = m_power->getStats();
        powerObj["activeTime"] = powerStats.activeTime;
        powerObj["sleepTime"] = powerStats.sleepTime;
        powerObj["wakeCount"] = powerStats.wakeCount;
    }

    // Watchdog Manager (if available)
    if (m_watchdog) {
        JsonObject watchdogObj = doc.createNestedObject("watchdog");
        watchdogObj["systemHealth"] = m_watchdog->getSystemHealth();
        watchdogObj["healthName"] = WatchdogManager::getHealthStatusName(m_watchdog->getSystemHealth());
    }

    // Serialize to string
    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleGetConfig(AsyncWebServerRequest* request) {
    char buffer[2048];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
}

void WebAPI::handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Null-terminate data
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }

    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    // Parse and validate
    if (!m_config->fromJSON(jsonStr)) {
        free(jsonStr);
        sendError(request, 400, m_config->getLastError());
        return;
    }

    // Save to SPIFFS
    if (!m_config->save()) {
        free(jsonStr);
        sendError(request, 500, "Failed to save configuration");
        return;
    }

    free(jsonStr);

    // Return updated config
    char buffer[2048];
    m_config->toJSON(buffer, sizeof(buffer));
    sendJSON(request, 200, buffer);

    LOG_INFO("Config updated via API");
}

void WebAPI::handlePostSensors(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Null-terminate data
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }

    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    // Parse JSON sensor array
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        free(jsonStr);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Get current config
    ConfigManager::Config currentConfig = m_config->getConfig();

    // Clear all sensor slots first
    for (int i = 0; i < 4; i++) {
        currentConfig.sensors[i].active = false;
    }

    // Parse and update sensor slots
    JsonArray sensorsArray = doc.as<JsonArray>();
    for (JsonObject sensorObj : sensorsArray) {
        uint8_t slot = sensorObj["slot"] | 0;
        if (slot >= 4) continue;  // Skip invalid slots

        currentConfig.sensors[slot].active = true;
        currentConfig.sensors[slot].enabled = sensorObj["enabled"] | true;
        strlcpy(currentConfig.sensors[slot].name, sensorObj["name"] | "", 32);
        currentConfig.sensors[slot].type = (SensorType)(sensorObj["type"] | 0);
        currentConfig.sensors[slot].primaryPin = sensorObj["primaryPin"] | 0;
        currentConfig.sensors[slot].secondaryPin = sensorObj["secondaryPin"] | 0;
        currentConfig.sensors[slot].isPrimary = sensorObj["isPrimary"] | false;
        currentConfig.sensors[slot].detectionThreshold = sensorObj["detectionThreshold"] | 0;
        currentConfig.sensors[slot].debounceMs = sensorObj["debounceMs"] | 100;
        currentConfig.sensors[slot].warmupMs = sensorObj["warmupMs"] | 0;
        currentConfig.sensors[slot].enableDirectionDetection = sensorObj["enableDirectionDetection"] | false;
        currentConfig.sensors[slot].sampleWindowSize = sensorObj["sampleWindowSize"] | 5;
        currentConfig.sensors[slot].sampleRateMs = sensorObj["sampleRateMs"] | 60;
    }

    free(jsonStr);

    // Update and save config
    if (!m_config->setConfig(currentConfig)) {
        sendError(request, 400, m_config->getLastError());
        return;
    }

    if (!m_config->save()) {
        sendError(request, 500, "Failed to save sensor configuration");
        return;
    }

    // Apply configuration changes to live sensors (if sensor manager available)
    if (m_sensorManager) {
        for (uint8_t i = 0; i < 4; i++) {
            HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
            if (sensor && currentConfig.sensors[i].active) {
                // Apply threshold changes
                if (currentConfig.sensors[i].detectionThreshold > 0) {
                    sensor->setDetectionThreshold(currentConfig.sensors[i].detectionThreshold);
                    LOG_INFO("Applied threshold %u mm to sensor %u",
                             currentConfig.sensors[i].detectionThreshold, i);
                }

                // Apply direction detection setting
                sensor->setDirectionDetection(currentConfig.sensors[i].enableDirectionDetection);

                // Apply window size changes
                if (currentConfig.sensors[i].sampleWindowSize > 0) {
                    sensor->setSampleWindowSize(currentConfig.sensors[i].sampleWindowSize);
                    LOG_INFO("Applied window size %u to sensor %u",
                             currentConfig.sensors[i].sampleWindowSize, i);
                }

                // Apply distance range if sensor supports it
                const SensorCapabilities& caps = sensor->getCapabilities();
                if (caps.supportsDistanceMeasurement) {
                    sensor->setDistanceRange(caps.minDetectionDistance, caps.maxDetectionDistance);
                }
            }
        }
        LOG_INFO("Sensor configuration applied to live sensors");
    }

    // Return updated sensor configuration
    char buffer[4096];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
    LOG_INFO("Sensor configuration saved successfully");
}

void WebAPI::handleGetDisplays(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> doc;

    // Get current config
    const ConfigManager::Config& config = m_config->getConfig();

    // Build displays array
    JsonArray displaysArray = doc.createNestedArray("displays");
    for (int i = 0; i < 2; i++) {
        if (config.displays[i].active) {
            JsonObject displayObj = displaysArray.createNestedObject();
            displayObj["slot"] = i;
            displayObj["name"] = config.displays[i].name;
            displayObj["type"] = config.displays[i].type;
            displayObj["i2cAddress"] = config.displays[i].i2cAddress;
            displayObj["sdaPin"] = config.displays[i].sdaPin;
            displayObj["sclPin"] = config.displays[i].sclPin;
            displayObj["enabled"] = config.displays[i].enabled;
            displayObj["brightness"] = config.displays[i].brightness;
            displayObj["rotation"] = config.displays[i].rotation;
            displayObj["useForStatus"] = config.displays[i].useForStatus;
        }
    }

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handlePostDisplays(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Null-terminate data
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }

    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    // Parse JSON display array
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        free(jsonStr);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Get current config
    ConfigManager::Config currentConfig = m_config->getConfig();

    // Clear all display slots first
    for (int i = 0; i < 2; i++) {
        currentConfig.displays[i].active = false;
    }

    // Parse and update display slots
    JsonArray displaysArray = doc.as<JsonArray>();
    for (JsonObject displayObj : displaysArray) {
        uint8_t slot = displayObj["slot"] | 0;
        if (slot >= 2) continue;  // Skip invalid slots

        currentConfig.displays[slot].active = true;
        currentConfig.displays[slot].enabled = displayObj["enabled"] | true;
        strlcpy(currentConfig.displays[slot].name, displayObj["name"] | "", 32);
        currentConfig.displays[slot].type = (DisplayType)(displayObj["type"] | 0);
        currentConfig.displays[slot].i2cAddress = displayObj["i2cAddress"] | MATRIX_I2C_ADDRESS;
        currentConfig.displays[slot].sdaPin = displayObj["sdaPin"] | I2C_SDA_PIN;
        currentConfig.displays[slot].sclPin = displayObj["sclPin"] | I2C_SCL_PIN;
        currentConfig.displays[slot].brightness = displayObj["brightness"] | MATRIX_BRIGHTNESS_DEFAULT;
        currentConfig.displays[slot].rotation = displayObj["rotation"] | 0;
        currentConfig.displays[slot].useForStatus = displayObj["useForStatus"] | true;
    }

    free(jsonStr);

    // Update and save config
    if (!m_config->setConfig(currentConfig)) {
        sendError(request, 400, m_config->getLastError());
        return;
    }

    if (!m_config->save()) {
        sendError(request, 500, "Failed to save display configuration");
        return;
    }

    // Return updated display configuration
    char buffer[4096];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
    LOG_INFO("Display configuration saved successfully");
}

// ============================================================================
// Custom Animation API Handlers (Issue #12 Phase 2)
// ============================================================================

void WebAPI::handleGetAnimations(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> doc;
    JsonArray animationsArray = doc.createNestedArray("animations");

    // If LED matrix is not available, return empty list
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        doc["count"] = 0;
        doc["maxAnimations"] = 8;
        doc["available"] = false;
        String json;
        serializeJson(doc, json);
        sendJSON(request, 200, json.c_str());
        return;
    }

    uint8_t count = m_ledMatrix->getCustomAnimationCount();
    doc["count"] = count;
    doc["maxAnimations"] = 8;
    doc["available"] = true;

    // Note: We cannot iterate over custom animations directly as they are private.
    // This is a simplified response - in a full implementation, we would need
    // to add a getCustomAnimationInfo() method to HAL_LEDMatrix_8x8

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleUploadAnimation(AsyncWebServerRequest* request, const String& filename,
                                   size_t index, uint8_t* data, size_t len, bool final) {
    // Check if LED matrix is available
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        if (final) {
            sendError(request, 503, "LED Matrix not available");
        }
        return;
    }

    // We need to accumulate the file data as it comes in chunks
    // For now, we'll implement a simplified version that writes to LittleFS
    #if !MOCK_HARDWARE
    static File uploadFile;

    if (index == 0) {
        // First chunk - open file for writing
        String filepath = "/animations/" + filename;
        uploadFile = LittleFS.open(filepath, "w");
        if (!uploadFile) {
            if (final) {
                sendError(request, 500, "Failed to create file");
            }
            return;
        }
    }

    if (len) {
        // Write chunk to file
        uploadFile.write(data, len);
    }

    if (final) {
        // Last chunk - close file and load animation
        if (uploadFile) {
            uploadFile.close();
        }

        String filepath = "/animations/" + filename;
        bool loaded = m_ledMatrix->loadCustomAnimation(filepath.c_str());

        if (loaded) {
            StaticJsonDocument<256> doc;
            doc["success"] = true;
            doc["name"] = filename;
            doc["filepath"] = filepath;
            String json;
            serializeJson(doc, json);
            sendJSON(request, 200, json.c_str());
        } else {
            sendError(request, 400, "Failed to load animation");
        }
    }
    #else
    // Mock mode - just acknowledge upload
    if (final) {
        StaticJsonDocument<256> doc;
        doc["success"] = true;
        doc["name"] = filename;
        doc["message"] = "Mock mode - animation not actually loaded";
        String json;
        serializeJson(doc, json);
        sendJSON(request, 200, json.c_str());
    }
    #endif
}

void WebAPI::handlePlayAnimation(AsyncWebServerRequest* request, uint8_t* data,
                                 size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Check if LED matrix is available
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        sendError(request, 503, "LED Matrix not available");
        return;
    }

    // Parse JSON body
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }

    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        free(jsonStr);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* name = doc["name"];
    uint32_t duration = doc["duration"] | 0;

    free(jsonStr);

    if (!name || strlen(name) == 0) {
        sendError(request, 400, "Animation name required");
        return;
    }

    // Play the animation
    bool started = m_ledMatrix->playCustomAnimation(name, duration);

    if (started) {
        StaticJsonDocument<256> response;
        response["success"] = true;
        response["animation"] = name;
        response["duration"] = duration;
        String json;
        serializeJson(response, json);
        sendJSON(request, 200, json.c_str());
    } else {
        sendError(request, 404, "Animation not found");
    }
}

void WebAPI::handlePlayBuiltInAnimation(AsyncWebServerRequest* request, uint8_t* data,
                                        size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Check if LED matrix is available
    if (!m_ledMatrix) {
        LOG_WARN("WebAPI: Built-in animation request but LED Matrix pointer is null");
        sendError(request, 503, "LED Matrix not configured");
        return;
    }

    if (!m_ledMatrix->isReady()) {
        LOG_WARN("WebAPI: Built-in animation request but LED Matrix not ready");
        sendError(request, 503, "LED Matrix not initialized");
        return;
    }

    // Parse JSON body
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }

    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        free(jsonStr);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* type = doc["type"];
    uint32_t duration = doc["duration"] | 0;

    free(jsonStr);

    if (!type || strlen(type) == 0) {
        sendError(request, 400, "Animation type required");
        return;
    }

    // Map animation type string to enum
    HAL_LEDMatrix_8x8::AnimationPattern pattern = HAL_LEDMatrix_8x8::ANIM_NONE;

    if (strcmp(type, "MOTION_ALERT") == 0) {
        pattern = HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT;
    } else if (strcmp(type, "BATTERY_LOW") == 0) {
        pattern = HAL_LEDMatrix_8x8::ANIM_BATTERY_LOW;
    } else if (strcmp(type, "BOOT_STATUS") == 0) {
        pattern = HAL_LEDMatrix_8x8::ANIM_BOOT_STATUS;
    } else if (strcmp(type, "WIFI_CONNECTED") == 0) {
        pattern = HAL_LEDMatrix_8x8::ANIM_WIFI_CONNECTED;
    } else {
        sendError(request, 400, "Unknown animation type");
        return;
    }

    // Start the animation
    m_ledMatrix->startAnimation(pattern, duration);

    StaticJsonDocument<256> response;
    response["success"] = true;
    response["type"] = type;
    response["duration"] = duration;
    String json;
    serializeJson(response, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleStopAnimation(AsyncWebServerRequest* request) {
    // Check if LED matrix is available
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        sendError(request, 503, "LED Matrix not available");
        return;
    }

    m_ledMatrix->stopAnimation();

    StaticJsonDocument<128> doc;
    doc["success"] = true;
    doc["message"] = "Animation stopped";
    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleDeleteAnimation(AsyncWebServerRequest* request) {
    // Check if LED matrix is available
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        sendError(request, 503, "LED Matrix not available");
        return;
    }

    // Extract animation name from URL path
    // URL format: /api/animations/AnimationName
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash == -1 || lastSlash == path.length() - 1) {
        sendError(request, 400, "Animation name required");
        return;
    }

    String name = path.substring(lastSlash + 1);

    // Currently, we don't have a method to delete individual animations,
    // only clearCustomAnimations() which deletes all.
    // This would require adding a deleteCustomAnimation(name) method to HAL_LEDMatrix_8x8

    sendError(request, 501, "Delete individual animation not yet implemented - use clearCustomAnimations() to remove all");
}

void WebAPI::handleGetAnimationTemplate(AsyncWebServerRequest* request) {
    // Extract animation type from query parameter
    // URL format: /api/animations/template?type=MOTION_ALERT
    LOG_INFO("Template request received for URL: %s", request->url().c_str());

    if (!request->hasParam("type")) {
        LOG_ERROR("Template request: No 'type' query parameter");
        sendError(request, 400, "Animation type required (use ?type=MOTION_ALERT)");
        return;
    }

    String animType = request->getParam("type")->value();
    LOG_INFO("Generating template for animation type: %s", animType.c_str());
    String templateContent;

    // Generate template based on animation type
    if (animType == "MOTION_ALERT") {
        templateContent = "# Motion Alert Animation Template\n";
        templateContent += "# Flash + scrolling arrow effect\n";
        templateContent += "name=MotionAlert\n";
        templateContent += "loop=false\n\n";
        templateContent += "# Flash frame (all on)\n";
        templateContent += "frame=11111111,11111111,11111111,11111111,11111111,11111111,11111111,11111111,200\n\n";
        templateContent += "# Flash frame (all off)\n";
        templateContent += "frame=00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,200\n\n";
        templateContent += "# Arrow pointing up\n";
        templateContent += "frame=00011000,00111100,01111110,11111111,00011000,00011000,00011000,00011000,150\n";

    } else if (animType == "BATTERY_LOW") {
        templateContent = "# Battery Low Animation Template\n";
        templateContent += "# Display battery percentage\n";
        templateContent += "name=BatteryLow\n";
        templateContent += "loop=true\n\n";
        templateContent += "# Battery outline\n";
        templateContent += "frame=01111110,01000010,01000010,01000010,01000010,01000010,01111110,00011000,500\n\n";
        templateContent += "# Empty battery\n";
        templateContent += "frame=01111110,01000010,01000010,01000010,01000010,01000010,01111110,00000000,500\n";

    } else if (animType == "BOOT_STATUS") {
        templateContent = "# Boot Status Animation Template\n";
        templateContent += "# Startup sequence\n";
        templateContent += "name=BootStatus\n";
        templateContent += "loop=false\n\n";
        templateContent += "# Expanding square\n";
        templateContent += "frame=00000000,00000000,00000000,00011000,00011000,00000000,00000000,00000000,100\n";
        templateContent += "frame=00000000,00000000,00111100,00100100,00100100,00111100,00000000,00000000,100\n";
        templateContent += "frame=00000000,01111110,01000010,01000010,01000010,01000010,01111110,00000000,100\n";
        templateContent += "frame=11111111,10000001,10000001,10000001,10000001,10000001,10000001,11111111,100\n";

    } else if (animType == "WIFI_CONNECTED") {
        templateContent = "# WiFi Connected Animation Template\n";
        templateContent += "# Checkmark symbol\n";
        templateContent += "name=WiFiConnected\n";
        templateContent += "loop=false\n\n";
        templateContent += "# Checkmark\n";
        templateContent += "frame=00000000,00000001,00000011,10000110,11001100,01111000,00110000,00000000,500\n";
        templateContent += "# With box\n";
        templateContent += "frame=11111111,10000001,10000011,10000110,11001100,01111000,00110001,11111111,500\n";

    } else {
        sendError(request, 404, "Unknown animation type");
        return;
    }

    // Set headers for file download
    String filename = animType;
    filename.toLowerCase();
    filename += "_template.txt";

    AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", templateContent);
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    response->addHeader("Cache-Control", "no-cache");

    if (m_corsEnabled) {
        response->addHeader("Access-Control-Allow-Origin", "*");
    }

    request->send(response);
    LOG_INFO("Sent animation template: %s", animType.c_str());
}

void WebAPI::handleAssignAnimation(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Parse JSON body
    char* jsonStr = (char*)malloc(total + 1);
    if (!jsonStr) {
        sendError(request, 500, "Out of memory");
        return;
    }
    memcpy(jsonStr, data, total);
    jsonStr[total] = '\0';

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    free(jsonStr);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* functionKey = doc["function"];
    const char* type = doc["type"];  // "builtin" or "custom"
    const char* animation = doc["animation"];

    if (!functionKey || !type || !animation) {
        sendError(request, 400, "Missing required fields");
        return;
    }

    // Store assignment in config
    // For now, we'll use a simple in-memory storage
    // TODO: Persist to ConfigManager

    StaticJsonDocument<256> response;
    response["success"] = true;
    response["function"] = functionKey;
    response["type"] = type;
    response["animation"] = animation;

    String json;
    serializeJson(response, json);
    sendJSON(request, 200, json.c_str());

    LOG_INFO("Assigned %s animation '%s' to function '%s'", type, animation, functionKey);
}

void WebAPI::handleGetAssignments(AsyncWebServerRequest* request) {
    // Return current animation assignments
    // For now, return default built-in assignments
    // TODO: Load from ConfigManager

    StaticJsonDocument<1024> doc;

    JsonObject motionAlert = doc.createNestedObject("motion-alert");
    motionAlert["type"] = "builtin";
    motionAlert["name"] = "MOTION_ALERT";

    JsonObject batteryLow = doc.createNestedObject("battery-low");
    batteryLow["type"] = "builtin";
    batteryLow["name"] = "BATTERY_LOW";

    JsonObject bootStatus = doc.createNestedObject("boot-status");
    bootStatus["type"] = "builtin";
    bootStatus["name"] = "BOOT_STATUS";

    JsonObject wifiConnected = doc.createNestedObject("wifi-connected");
    wifiConnected["type"] = "builtin";
    wifiConnected["name"] = "WIFI_CONNECTED";

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleGetMode(AsyncWebServerRequest* request) {
    StaticJsonDocument<128> doc;

    doc["mode"] = m_stateMachine->getMode();
    doc["modeName"] = StateMachine::getModeName(m_stateMachine->getMode());

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handlePostMode(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process when all data received
    if (index + len != total) {
        return;
    }

    // Parse JSON
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, total);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Get mode from request
    if (!doc.containsKey("mode")) {
        sendError(request, 400, "Missing 'mode' field");
        return;
    }

    int mode = doc["mode"];

    // Validate mode
    if (mode < StateMachine::OFF || mode > StateMachine::MOTION_DETECT) {
        sendError(request, 400, "Invalid mode value");
        return;
    }

    // Set mode
    m_stateMachine->setMode((StateMachine::OperatingMode)mode);

    LOG_INFO("Mode changed to %s via API", StateMachine::getModeName((StateMachine::OperatingMode)mode));

    // Return new mode
    handleGetMode(request);
}

void WebAPI::handleGetLogs(AsyncWebServerRequest* request) {
    StaticJsonDocument<8192> doc;
    JsonArray logs = doc.createNestedArray("logs");

    uint32_t count = g_logger.getEntryCount();

    // Get limit from query parameter, default to 100
    uint32_t maxEntries = 100;
    if (request->hasParam("limit")) {
        maxEntries = request->getParam("limit")->value().toInt();
        if (maxEntries > 200) maxEntries = 200;  // Cap at 200 to prevent memory issues
    }

    uint32_t startIndex = count > maxEntries ? count - maxEntries : 0;

    for (uint32_t i = startIndex; i < count; i++) {
        Logger::LogEntry entry;
        if (g_logger.getEntry(i, entry)) {
            JsonObject logObj = logs.createNestedObject();
            logObj["timestamp"] = entry.timestamp;
            logObj["level"] = entry.level;
            logObj["levelName"] = Logger::getLevelName((Logger::LogLevel)entry.level);
            logObj["message"] = entry.message;
        }
    }

    doc["count"] = count;
    doc["returned"] = logs.size();

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handlePostReset(AsyncWebServerRequest* request) {
    LOG_WARN("Factory reset requested via API");

    // Reset configuration
    if (!m_config->reset(true)) {
        sendError(request, 500, "Failed to reset configuration");
        return;
    }

    // Reset state machine counters
    // m_stateMachine->resetCounters();

    StaticJsonDocument<128> doc;
    doc["success"] = true;
    doc["message"] = "Configuration reset to factory defaults";

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());

    LOG_INFO("Factory reset complete");
}

void WebAPI::handleGetVersion(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;

    doc["firmware"] = FIRMWARE_NAME;
    doc["version"] = FIRMWARE_VERSION;
    doc["buildDate"] = BUILD_DATE;
    doc["buildTime"] = BUILD_TIME;

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleOptions(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    if (m_corsEnabled) {
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    }
    request->send(response);
}

void WebAPI::sendJSON(AsyncWebServerRequest* request, int code, const char* json) {
    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
    if (m_corsEnabled) {
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    }
    request->send(response);
}

void WebAPI::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    StaticJsonDocument<256> doc;
    doc["error"] = message;
    doc["code"] = code;

    String json;
    serializeJson(doc, json);

    sendJSON(request, code, json.c_str());
}

void WebAPI::handleRoot(AsyncWebServerRequest* request) {
    // Serve inline HTML dashboard
    // Note: We use inline HTML instead of filesystem-based UI because:
    // 1. All features are implemented (multi-sensor, LED matrix, animations)
    // 2. Simpler deployment (no filesystem upload step)
    // 3. No LittleFS mount/filesystem issues
    // 4. For single-developer embedded projects, reflashing is acceptable
    String html = buildDashboardHTML();
    request->send(200, "text/html", html);
}

String WebAPI::buildDashboardHTML() {
    String html;
    html.reserve(16384);  // Larger allocation for enhanced UI

    // HTML Head
    html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">";
    html += "<title>StepAware Dashboard</title><style>";

    // Base styles
    html += "*{margin:0;padding:0;box-sizing:border-box;}";
    html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
    html += "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}";
    html += ".container{max-width:1200px;margin:0 auto;}";
    html += ".card{background:white;border-radius:12px;padding:24px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2);}";
    html += "h1{color:white;margin-bottom:24px;font-size:2.5em;text-align:center;text-shadow:2px 2px 4px rgba(0,0,0,0.2);}";
    html += "h2{color:#333;margin-bottom:16px;font-size:1.5em;border-bottom:2px solid #667eea;padding-bottom:8px;}";
    html += "h3{color:#555;margin:16px 0 8px;font-size:1.2em;}";

    // Tab navigation
    html += ".tabs{display:flex;gap:8px;margin-bottom:20px;border-bottom:2px solid rgba(255,255,255,0.3);}";
    html += ".tab{background:rgba(255,255,255,0.2);color:white;border:none;padding:12px 24px;cursor:pointer;";
    html += "border-radius:8px 8px 0 0;font-size:1em;font-weight:600;transition:all 0.3s;}";
    html += ".tab:hover{background:rgba(255,255,255,0.3);}";
    html += ".tab.active{background:white;color:#667eea;}";
    html += ".tab-content{display:none;}";
    html += ".tab-content.active{display:block;}";

    // Sticky status bar
    html += ".status-bar{background:white;border-radius:8px;padding:12px 16px;margin-bottom:20px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}";
    html += ".status-compact{display:flex;gap:24px;align-items:center;flex-wrap:wrap;justify-content:space-between;}";
    html += ".status-item-compact{display:flex;align-items:center;gap:8px;}";
    html += ".status-icon{width:8px;height:8px;border-radius:50%;background:#667eea;flex-shrink:0;}";
    html += ".status-icon.warning{background:#f59e0b;animation:pulse 2s infinite;}";
    html += ".status-icon.active{background:#10b981;}";
    html += ".status-icon.inactive{background:#6b7280;}";
    html += "@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.5;}}";
    html += ".status-label-compact{font-size:0.75em;color:#666;text-transform:uppercase;letter-spacing:0.5px;}";
    html += ".status-value-compact{font-size:0.95em;font-weight:600;color:#333;}";

    // Buttons
    html += ".mode-buttons{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-top:16px;}";
    html += ".btn{padding:16px;border:none;border-radius:8px;font-size:1em;font-weight:600;cursor:pointer;transition:all 0.3s;color:white;}";
    html += ".btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.15);}";
    html += ".btn-off{background:#6b7280;}.btn-off:hover{background:#4b5563;}";
    html += ".btn-always{background:#10b981;}.btn-always:hover{background:#059669;}";
    html += ".btn-motion{background:#3b82f6;}.btn-motion:hover{background:#2563eb;}";
    html += ".btn-primary{background:#667eea;}.btn-primary:hover{background:#5568d3;}";
    html += ".btn-success{background:#10b981;}.btn-success:hover{background:#059669;}";
    html += ".btn-warning{background:#f59e0b;color:white;}.btn-warning:hover{background:#d97706;}";
    html += ".btn-secondary{background:#6b7280;color:white;}.btn-secondary:hover{background:#4b5563;}";
    html += ".btn-danger{background:#ef4444;color:white;}.btn-danger:hover{background:#dc2626;}";
    html += ".btn.active{box-shadow:0 0 0 3px rgba(102,126,234,0.5);}";
    html += ".btn-small{padding:8px 16px;font-size:0.9em;}";
    html += ".btn-sm{padding:6px 12px;font-size:0.85em;color:white;min-width:70px;line-height:1.5;text-align:center;display:inline-block;vertical-align:middle;}";

    // Badges
    html += ".badge{display:inline-block;padding:4px 12px;border-radius:12px;font-size:0.85em;font-weight:600;}";
    html += ".badge-success{background:#d1fae5;color:#065f46;}";
    html += ".badge-warning{background:#fef3c7;color:#92400e;}";
    html += ".badge-error{background:#fee2e2;color:#991b1b;}";
    html += ".badge-info{background:#dbeafe;color:#1e40af;}";

    // Forms
    html += ".form-group{margin-bottom:16px;}";
    html += ".form-label{display:block;margin-bottom:4px;color:#555;font-weight:600;font-size:0.9em;}";
    html += ".form-input,.form-select{width:100%;padding:10px;border:2px solid #e5e7eb;border-radius:6px;font-size:1em;}";
    html += ".form-input:focus,.form-select:focus{outline:none;border-color:#667eea;}";
    html += ".form-help{font-size:0.85em;color:#6b7280;margin-top:4px;}";
    html += ".form-row{display:grid;grid-template-columns:1fr 1fr;gap:16px;}";

    // Logs
    html += "#log-viewer{background:#1e293b;color:#e2e8f0;padding:16px;border-radius:8px;font-family:monospace;";
    html += "font-size:0.85em;height:800px;overflow-y:auto;line-height:1.6;resize:vertical;}";
    html += ".log-entry{margin-bottom:4px;}";
    html += ".log-entry.hidden{display:none;}";
    html += ".log-info{color:#60a5fa;}.log-warn{color:#fbbf24;}.log-error{color:#f87171;}";
    html += "#log-status{margin-bottom:12px;padding:8px;background:#f3f4f6;border-radius:6px;text-align:center;}";
    html += ".log-controls{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap;align-items:center;}";
    html += ".log-search{flex:1;min-width:200px;padding:8px;border:2px solid #e5e7eb;border-radius:6px;}";
    html += ".log-filter-group{display:flex;gap:4px;}";
    html += ".log-filter-btn{padding:6px 12px;border:2px solid #e5e7eb;background:white;border-radius:6px;cursor:pointer;font-size:0.85em;transition:all 0.2s;}";
    html += ".log-filter-btn.active{background:#667eea;color:white;border-color:#667eea;}";
    html += ".log-filter-btn:hover{border-color:#667eea;}";

    // Misc
    html += "#wifi-status{display:flex;align-items:center;gap:8px;flex-wrap:wrap;}";
    html += ".config-grid{display:grid;gap:16px;}";
    html += ".save-indicator{display:none;padding:12px;background:#d1fae5;color:#065f46;border-radius:6px;margin-top:16px;text-align:center;font-weight:600;}";
    html += ".save-indicator.show{display:block;}";

    // Sensor cards
    html += ".sensor-card{border:2px solid #e5e7eb;border-radius:8px;padding:16px;margin-bottom:16px;position:relative;}";
    html += ".sensor-card.disabled{opacity:0.6;background:#f9fafb;}";
    html += ".sensor-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;}";
    html += ".sensor-title{font-weight:600;font-size:1.1em;color:#333;}";
    html += ".sensor-actions{display:flex;gap:8px;}";
    html += ".pin-info{background:#f3f4f6;padding:8px 12px;border-radius:6px;font-family:monospace;font-size:0.9em;margin:8px 0;}";
    html += ".pin-label{color:#6b7280;font-size:0.85em;}";
    html += ".pin-value{color:#667eea;font-weight:600;}";
    html += ".sensor-type-badge{display:inline-block;padding:4px 8px;background:#dbeafe;color:#1e40af;border-radius:4px;font-size:0.85em;font-weight:600;margin-bottom:8px;}";
    html += ".btn-remove{background:#ef4444;}.btn-remove:hover{background:#dc2626;}";
    html += ".btn-toggle{background:#6b7280;}.btn-toggle.active{background:#10b981;}";

    html += "</style></head><body><div class=\"container\">";
    html += "<h1>&#128680; StepAware Dashboard</h1>";

    // Sticky Status Bar
    html += "<div class=\"status-bar\"><div class=\"status-compact\">";
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\" id=\"mode-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">Mode</div><div class=\"status-value-compact\" id=\"mode-display\">...</div></div></div>";
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\" id=\"warning-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">Warning</div><div class=\"status-value-compact\" id=\"warning-status\">--</div></div></div>";
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">Motion</div><div class=\"status-value-compact\" id=\"motion-events\">0</div></div></div>";
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">Uptime</div><div class=\"status-value-compact\" id=\"uptime\">--</div></div></div>";
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\" id=\"wifi-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">WiFi</div><div class=\"status-value-compact\" id=\"wifi-status-compact\">--</div></div></div>";
    html += "</div></div>";

    // Tab Navigation
    html += "<div class=\"tabs\">";
    html += "<button class=\"tab active\" onclick=\"showTab('status')\">Status</button>";
    html += "<button class=\"tab\" onclick=\"showTab('hardware')\">Hardware</button>";
    html += "<button class=\"tab\" onclick=\"showTab('config')\">Configuration</button>";
    html += "<button class=\"tab\" onclick=\"showTab('logs')\">Logs</button>";
    html += "</div>";

    // STATUS TAB
    html += "<div id=\"status-tab\" class=\"tab-content active\">";

    html += "<div class=\"card\"><h2>Control Panel</h2><div class=\"mode-buttons\">";
    html += "<button class=\"btn btn-off\" onclick=\"setMode(0)\" id=\"btn-0\">OFF</button>";
    html += "<button class=\"btn btn-always\" onclick=\"setMode(1)\" id=\"btn-1\">ALWAYS ON</button>";
    html += "<button class=\"btn btn-motion\" onclick=\"setMode(2)\" id=\"btn-2\">MOTION DETECT</button>";
    html += "</div></div>";

    html += "<div class=\"card\"><h2>Network Details</h2><div id=\"wifi-details-full\">";
    html += "<p>Loading WiFi information...</p>";
    html += "</div></div>";

    html += "</div>"; // End status tab

    // HARDWARE TAB
    html += "<div id=\"hardware-tab\" class=\"tab-content\">";
    html += "<div class=\"card\"><h2>Sensor Configuration</h2>";
    html += "<p class=\"form-help\" style=\"margin-bottom:16px;\">Configure up to 4 motion sensors. Each sensor can be enabled/disabled independently.</p>";

    html += "<div id=\"sensors-list\"></div>";

    html += "<button class=\"btn btn-primary\" style=\"margin-top:16px;\" onclick=\"addSensor()\">+ Add Sensor</button>";

    html += "</div>"; // End sensors section

    // DISPLAYS SECTION
    html += "<div class=\"card\" style=\"margin-top:24px;\">";
    html += "<h2>LED Matrix Display</h2>";
    html += "<p class=\"form-help\" style=\"margin-bottom:16px;\">Configure 8x8 LED matrix for enhanced visual feedback.</p>";

    html += "<div id=\"displays-list\"></div>";

    // Active animation assignments (shown after display configuration)
    html += "<div id=\"active-animations-panel\" style=\"display:none;margin-top:12px;padding:10px;background:#f8fafc;border-radius:6px;border:1px solid #e2e8f0;\">";
    html += "<div style=\"font-weight:600;font-size:0.9em;margin-bottom:6px;color:#1e293b;\">Active Assignments</div>";
    html += "<div style=\"display:grid;gap:4px;font-size:0.85em;\">";

    // Motion Alert assignment
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:4px 6px;background:white;border-radius:3px;\">";
    html += "<span style=\"color:#64748b;\">Motion Alert:</span>";
    html += "<span id=\"anim-motion-alert\" style=\"font-weight:600;color:#333;\">Built-in</span>";
    html += "</div>";

    // Battery Low assignment
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:4px 6px;background:white;border-radius:3px;\">";
    html += "<span style=\"color:#64748b;\">Battery Low:</span>";
    html += "<span id=\"anim-battery-low\" style=\"font-weight:600;color:#333;\">Built-in</span>";
    html += "</div>";

    // Boot Status assignment
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:4px 6px;background:white;border-radius:3px;\">";
    html += "<span style=\"color:#64748b;\">Boot Status:</span>";
    html += "<span id=\"anim-boot-status\" style=\"font-weight:600;color:#333;\">Built-in</span>";
    html += "</div>";

    // WiFi Connected assignment
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:4px 6px;background:white;border-radius:3px;\">";
    html += "<span style=\"color:#64748b;\">WiFi:</span>";
    html += "<span id=\"anim-wifi-connected\" style=\"font-weight:600;color:#333;\">Built-in</span>";
    html += "</div>";

    html += "</div></div>";

    html += "<button class=\"btn btn-primary\" style=\"margin-top:16px;\" onclick=\"addDisplay()\">+ Add Display</button>";
    html += "</div>"; // End displays section

    // ANIMATIONS SECTION
    html += "<div class=\"card\" style=\"margin-top:24px;\">";
    html += "<h2>Animation Library</h2>";
    html += "<p class=\"form-help\" style=\"margin-bottom:16px;\">Manage and assign animations to different system functions.</p>";

    // Built-in animations - dropdown with actions
    html += "<div style=\"margin-bottom:20px;\">";
    html += "<h3 style=\"font-size:1.1em;margin-bottom:12px;color:#1e293b;\">Built-In Animations</h3>";
    html += "<div style=\"display:grid;grid-template-columns:1fr auto auto auto;gap:8px;align-items:center;padding:12px;background:#f8fafc;border-radius:6px;\">";
    html += "<select id=\"builtin-animation-select\" class=\"form-select\" style=\"font-size:0.9em;\">";
    html += "<option value=\"MOTION_ALERT\">Motion Alert - Flash + scroll arrow</option>";
    html += "<option value=\"BATTERY_LOW\">Battery Low - Battery percentage</option>";
    html += "<option value=\"BOOT_STATUS\">Boot Status - Startup animation</option>";
    html += "<option value=\"WIFI_CONNECTED\">WiFi Connected - Checkmark</option>";
    html += "</select>";
    html += "<button class=\"btn btn-sm btn-primary\" onclick=\"playSelectedBuiltIn()\" title=\"Play animation\" style=\"width:36px;padding:8px;\">▶</button>";
    html += "<button class=\"btn btn-sm btn-secondary\" onclick=\"downloadSelectedTemplate()\" title=\"Download as template\" style=\"width:36px;padding:8px;\">⬇</button>";
    html += "<button class=\"btn btn-sm btn-success\" onclick=\"assignSelectedBuiltIn()\" title=\"Assign to function\" style=\"width:36px;padding:8px;\">✓</button>";
    html += "</div>";
    html += "</div>";

    // Custom animations list
    html += "<div style=\"margin-bottom:16px;\">";
    html += "<h3 style=\"font-size:1.1em;margin-bottom:12px;color:#1e293b;\">Custom Animations</h3>";
    html += "<div id=\"animations-list\"></div>";
    html += "</div>";

    // Upload section
    html += "<div style=\"border:2px dashed #cbd5e1;border-radius:8px;padding:16px;background:#f8fafc;margin-bottom:16px;\">";
    html += "<div style=\"font-weight:600;margin-bottom:8px;\">Upload Custom Animation</div>";
    html += "<div style=\"display:flex;gap:8px;align-items:center;\">";
    html += "<input type=\"file\" id=\"animation-file-input\" accept=\".txt\" style=\"flex:1;\">";
    html += "<button class=\"btn btn-success btn-small\" onclick=\"uploadAnimation()\">Upload</button>";
    html += "</div>";
    html += "<div class=\"form-help\" style=\"margin-top:8px;\">Upload .txt animation files. See <a href=\"#\" onclick=\"showAnimationHelp();return false;\" style=\"color:#667eea;\">format guide</a>.</div>";
    html += "</div>";

    // Test controls
    html += "<div style=\"padding:16px;background:#f1f5f9;border-radius:8px;\">";
    html += "<div style=\"font-weight:600;margin-bottom:12px;\">Test & Control</div>";
    html += "<div style=\"display:flex;gap:8px;align-items:center;flex-wrap:wrap;\">";
    html += "<button class=\"btn btn-secondary btn-small\" onclick=\"stopAnimation()\" style=\"min-width:100px;\">⏹ Stop All</button>";
    html += "<label style=\"font-size:0.85em;color:#64748b;margin-left:auto;\">Duration (ms):</label>";
    html += "<input type=\"number\" id=\"test-duration\" value=\"5000\" min=\"0\" step=\"1000\" style=\"width:100px;padding:6px;border:1px solid #cbd5e1;border-radius:4px;\">";
    html += "<span class=\"form-help\" style=\"margin:0;\">(0 = loop forever)</span>";
    html += "</div>";
    html += "</div>";

    html += "</div>"; // End animations section

    html += "</div>"; // End hardware tab

    // CONFIG TAB
    html += "<div id=\"config-tab\" class=\"tab-content\">";
    html += "<div class=\"card\"><h2>Device Configuration</h2>";
    html += "<form id=\"config-form\" onsubmit=\"saveConfig(event)\">";

    html += "<h3>Device Settings</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Device Name</label>";
    html += "<input type=\"text\" id=\"cfg-deviceName\" class=\"form-input\" maxlength=\"31\"></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Default Mode</label>";
    html += "<select id=\"cfg-defaultMode\" class=\"form-select\"><option value=\"0\">OFF</option>";
    html += "<option value=\"1\">ALWAYS ON</option><option value=\"2\">MOTION DETECT</option></select></div>";
    html += "</div>";

    html += "<h3>WiFi Settings</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">SSID</label>";
    html += "<input type=\"text\" id=\"cfg-wifiSSID\" class=\"form-input\" maxlength=\"31\"></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Password</label>";
    html += "<input type=\"password\" id=\"cfg-wifiPassword\" class=\"form-input\" maxlength=\"63\"></div>";
    html += "</div>";

    html += "<h3>Motion Detection</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Warning Duration (seconds)</label>";
    html += "<input type=\"number\" id=\"cfg-motionWarningDuration\" class=\"form-input\" min=\"1\" max=\"600\">";
    html += "<div class=\"form-help\">Time to display warning after motion detected</div></div>";
    html += "</div>";

    html += "<h3>LED Settings</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Full Brightness (0-255)</label>";
    html += "<input type=\"number\" id=\"cfg-ledBrightnessFull\" class=\"form-input\" min=\"0\" max=\"255\" value=\"255\">";
    html += "<div class=\"form-help\">LED brightness when fully on</div></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Dim Brightness (0-255)</label>";
    html += "<input type=\"number\" id=\"cfg-ledBrightnessDim\" class=\"form-input\" min=\"0\" max=\"255\" value=\"50\">";
    html += "<div class=\"form-help\">LED brightness when dimmed</div></div>";
    html += "</div>";

    html += "<h3>Logging</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Log Level</label>";
    html += "<select id=\"cfg-logLevel\" class=\"form-select\">";
    html += "<option value=\"0\">ERROR</option>";
    html += "<option value=\"1\">WARN</option>";
    html += "<option value=\"2\">INFO</option>";
    html += "<option value=\"3\">DEBUG</option>";
    html += "<option value=\"4\">VERBOSE</option>";
    html += "</select>";
    html += "<div class=\"form-help\">Higher levels include more detail but use more memory</div></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Power Saving</label>";
    html += "<select id=\"cfg-powerSaving\" class=\"form-select\">";
    html += "<option value=\"0\">Disabled</option>";
    html += "<option value=\"1\">Enabled</option>";
    html += "</select>";
    html += "<div class=\"form-help\">Enable to reduce power consumption</div></div>";
    html += "</div>";

    html += "<div style=\"margin-top:24px;\">";
    html += "<button type=\"submit\" class=\"btn btn-primary\">Save Configuration</button>";
    html += "<button type=\"button\" class=\"btn btn-off btn-small\" style=\"margin-left:12px;\" onclick=\"loadConfig()\">Reload</button>";
    html += "</div>";
    html += "<div id=\"save-indicator\" class=\"save-indicator\">Configuration saved successfully!</div>";
    html += "</form></div></div>";

    // LOGS TAB
    html += "<div id=\"logs-tab\" class=\"tab-content\">";
    html += "<div class=\"card\"><h2>System Logs</h2>";
    html += "<div id=\"log-status\">Fetching logs...</div>";
    html += "<div class=\"log-controls\">";
    html += "<input type=\"text\" id=\"log-search\" class=\"log-search\" placeholder=\"Search logs...\" oninput=\"filterLogs()\">";
    html += "<div class=\"log-filter-group\">";
    html += "<button class=\"log-filter-btn active\" data-level=\"all\" onclick=\"setLogFilter('all')\">All</button>";
    html += "<button class=\"log-filter-btn\" data-level=\"ERROR\" onclick=\"setLogFilter('ERROR')\">Error</button>";
    html += "<button class=\"log-filter-btn\" data-level=\"WARN\" onclick=\"setLogFilter('WARN')\">Warn</button>";
    html += "<button class=\"log-filter-btn\" data-level=\"INFO\" onclick=\"setLogFilter('INFO')\">Info</button>";
    html += "</div></div>";
    html += "<div id=\"log-viewer\"></div>";
    html += "<div style=\"margin-top:12px;\">";
    html += "<button class=\"btn btn-primary btn-small\" onclick=\"fetchLogs()\">Refresh</button>";
    html += "<button class=\"btn btn-off btn-small\" style=\"margin-left:8px;\" onclick=\"clearLogView()\">Clear View</button>";
    html += "</div></div></div>";

    html += "</div>"; // End container

    // JavaScript
    html += "<script>";

    // Tab switching
    html += "function showTab(tab){";
    html += "document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));";
    html += "document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));";
    html += "event.target.classList.add('active');";
    html += "document.getElementById(tab+'-tab').classList.add('active');";
    html += "if(tab==='config')loadConfig();";
    html += "if(tab==='hardware'){loadSensors();loadDisplays();loadAnimations();}";
    html += "if(tab==='logs')fetchLogs();";
    html += "}";

    // Status fetching
    html += "let currentMode=-1;";
    html += "async function fetchStatus(){";
    html += "try{const res=await fetch('/api/status');const data=await res.json();";
    html += "currentMode=data.stateMachine.mode;";

    // Update compact status bar
    html += "document.getElementById('mode-display').textContent=data.stateMachine.modeName;";
    html += "const modeIcon=document.getElementById('mode-icon');";
    html += "modeIcon.className='status-icon '+(data.stateMachine.mode===0?'inactive':data.stateMachine.mode===1?'active':'');";

    html += "const warningEl=document.getElementById('warning-status');";
    html += "const warningIcon=document.getElementById('warning-icon');";
    html += "if(data.stateMachine.warningActive){warningEl.textContent='Active';warningIcon.className='status-icon warning';}";
    html += "else{warningEl.textContent='Idle';warningIcon.className='status-icon inactive';}";

    html += "document.getElementById('motion-events').textContent=data.stateMachine.motionEvents;";

    html += "const uptime=Math.floor(data.uptime/1000);";
    html += "const hours=Math.floor(uptime/3600);const mins=Math.floor((uptime%3600)/60);";
    html += "document.getElementById('uptime').textContent=hours+'h '+mins+'m';";

    // WiFi compact status
    html += "const wifiIcon=document.getElementById('wifi-icon');";
    html += "const wifiCompact=document.getElementById('wifi-status-compact');";
    html += "if(data.wifi){";
    html += "if(data.wifi.state===3){wifiCompact.textContent=data.wifi.ssid;wifiIcon.className='status-icon active';}";
    html += "else if(data.wifi.state===2){wifiCompact.textContent='Connecting';wifiIcon.className='status-icon warning';}";
    html += "else{wifiCompact.textContent='Disconnected';wifiIcon.className='status-icon inactive';}}";

    // Update detailed WiFi info on Status tab
    html += "if(document.getElementById('status-tab').classList.contains('active')){";
    html += "const detailsFull=document.getElementById('wifi-details-full');";
    html += "if(data.wifi && data.wifi.state===3){";
    html += "detailsFull.innerHTML='<p><strong>SSID:</strong> '+data.wifi.ssid+'</p>';";
    html += "detailsFull.innerHTML+='<p><strong>IP Address:</strong> '+data.wifi.ipAddress+'</p>';";
    html += "detailsFull.innerHTML+='<p><strong>Signal Strength:</strong> '+data.wifi.rssi+' dBm</p>';";
    html += "detailsFull.innerHTML+='<p><strong>Status:</strong> <span class=\"badge badge-success\">Connected</span></p>';}";
    html += "else if(data.wifi && data.wifi.state===2){detailsFull.innerHTML='<p>Connecting to WiFi...</p>';}";
    html += "else{detailsFull.innerHTML='<p>WiFi disconnected or disabled</p>';}}";

    // Update mode buttons
    html += "for(let i=0;i<=2;i++){const btn=document.getElementById('btn-'+i);";
    html += "if(i===currentMode)btn.classList.add('active');else btn.classList.remove('active');}";
    html += "}catch(e){console.error('Status fetch error:',e);}}";

    html += "async function setMode(mode){";
    html += "try{const res=await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({mode:mode})});if(res.ok)fetchStatus();}catch(e){}}";

    // Config loading
    html += "let currentConfig={};";
    html += "async function loadConfig(){";
    html += "try{const res=await fetch('/api/config');const cfg=await res.json();";
    html += "currentConfig=cfg;";
    html += "document.getElementById('cfg-deviceName').value=cfg.device?.name||'';";
    html += "document.getElementById('cfg-defaultMode').value=cfg.device?.defaultMode||0;";
    html += "document.getElementById('cfg-wifiSSID').value=cfg.wifi?.ssid||'';";
    html += "if(cfg.wifi?.password&&cfg.wifi.password.length>0){";
    html += "document.getElementById('cfg-wifiPassword').value='';";
    html += "document.getElementById('cfg-wifiPassword').placeholder='••••••••';}";
    html += "else{document.getElementById('cfg-wifiPassword').value='';document.getElementById('cfg-wifiPassword').placeholder='';}";
    html += "document.getElementById('cfg-motionWarningDuration').value=Math.round((cfg.motion?.warningDuration||30000)/1000);";
    html += "document.getElementById('cfg-ledBrightnessFull').value=(cfg.led?.brightnessFull!==undefined)?cfg.led.brightnessFull:255;";
    html += "document.getElementById('cfg-ledBrightnessDim').value=(cfg.led?.brightnessDim!==undefined)?cfg.led.brightnessDim:50;";
    html += "document.getElementById('cfg-logLevel').value=cfg.logging?.level||2;";
    html += "document.getElementById('cfg-powerSaving').value=cfg.power?.savingEnabled?1:0;";
    html += "}catch(e){console.error('Config load error:',e);}}";

    // Config saving
    html += "async function saveConfig(e){";
    html += "e.preventDefault();";
    html += "const pwdField=document.getElementById('cfg-wifiPassword');";
    html += "const cfg=JSON.parse(JSON.stringify(currentConfig));";
    html += "cfg.device=cfg.device||{};";
    html += "cfg.device.name=document.getElementById('cfg-deviceName').value;";
    html += "cfg.device.defaultMode=parseInt(document.getElementById('cfg-defaultMode').value);";
    html += "cfg.wifi=cfg.wifi||{};";
    html += "cfg.wifi.ssid=document.getElementById('cfg-wifiSSID').value;";
    html += "cfg.wifi.enabled=true;";
    html += "if(pwdField.value.length>0){cfg.wifi.password=pwdField.value;}";
    html += "cfg.motion=cfg.motion||{};";
    html += "cfg.motion.warningDuration=parseInt(document.getElementById('cfg-motionWarningDuration').value)*1000;";
    html += "cfg.led=cfg.led||{};";
    html += "cfg.led.brightnessFull=parseInt(document.getElementById('cfg-ledBrightnessFull').value);";
    html += "cfg.led.brightnessDim=parseInt(document.getElementById('cfg-ledBrightnessDim').value);";
    html += "cfg.logging=cfg.logging||{};";
    html += "cfg.logging.level=parseInt(document.getElementById('cfg-logLevel').value);";
    html += "cfg.power=cfg.power||{};";
    html += "cfg.power.savingEnabled=parseInt(document.getElementById('cfg-powerSaving').value)===1;";
    html += "try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify(cfg)});if(res.ok){";
    html += "document.getElementById('save-indicator').classList.add('show');";
    html += "setTimeout(()=>document.getElementById('save-indicator').classList.remove('show'),3000);";
    html += "loadConfig();}else{alert('Failed to save configuration');}}catch(e){alert('Error: '+e.message);}}";

    // Log fetching and filtering
    html += "let currentFilter='all';";
    html += "async function fetchLogs(){";
    html += "try{document.getElementById('log-status').textContent='Fetching logs...';";
    html += "const res=await fetch('/api/logs?limit=100');const data=await res.json();";
    html += "const viewer=document.getElementById('log-viewer');viewer.innerHTML='';";
    html += "if(data.logs && data.logs.length>0){";
    html += "data.logs.forEach(log=>{const div=document.createElement('div');div.className='log-entry';";
    html += "div.setAttribute('data-level',log.levelName);";
    html += "div.setAttribute('data-message',log.message.toLowerCase());";
    html += "let cls='log-info';";
    html += "if(log.levelName==='WARN')cls='log-warn';";
    html += "if(log.levelName==='ERROR')cls='log-error';";
    html += "const time=new Date(log.timestamp).toLocaleTimeString();";
    html += "const msg='['+time+'] ['+log.levelName+'] '+log.message;";
    html += "div.innerHTML='<span class=\"'+cls+'\">'+msg+'</span>';viewer.appendChild(div);});";
    html += "filterLogs();";
    html += "document.getElementById('log-status').textContent='Showing '+data.logs.length+' of '+data.count+' logs - Last updated: '+new Date().toLocaleTimeString();";
    html += "}else{viewer.innerHTML='<div style=\"color:#94a3b8;\">No logs available. Check log level in Configuration tab.</div>';";
    html += "document.getElementById('log-status').textContent='No logs found';}}";
    html += "catch(e){document.getElementById('log-status').textContent='Error fetching logs: '+e.message;console.error(e);}}";
    html += "function setLogFilter(level){";
    html += "currentFilter=level;";
    html += "document.querySelectorAll('.log-filter-btn').forEach(btn=>{";
    html += "btn.classList.toggle('active',btn.getAttribute('data-level')===level);});";
    html += "filterLogs();}";
    html += "function filterLogs(){";
    html += "const search=document.getElementById('log-search').value.toLowerCase();";
    html += "const entries=document.querySelectorAll('.log-entry');";
    html += "let visibleCount=0;";
    html += "entries.forEach(entry=>{";
    html += "const level=entry.getAttribute('data-level');";
    html += "const message=entry.getAttribute('data-message')||'';";
    html += "const matchesFilter=(currentFilter==='all'||level===currentFilter);";
    html += "const matchesSearch=(search===''||message.includes(search));";
    html += "const visible=matchesFilter&&matchesSearch;";
    html += "entry.classList.toggle('hidden',!visible);";
    html += "if(visible)visibleCount++;});";
    html += "const total=entries.length;";
    html += "if(total>0){const status=document.getElementById('log-status');";
    html += "const baseText=status.textContent.split('-')[0];";
    html += "status.textContent=baseText+'- Showing '+visibleCount+' of '+total+' logs';}}";

    html += "function clearLogView(){document.getElementById('log-viewer').innerHTML='';}";

    // Hardware Tab - Sensor Management
    html += "let sensorSlots=[null,null,null,null];";
    html += "const SENSOR_TYPES={PIR:{name:'PIR Motion',pins:1,config:['warmup','debounce']},";
    html += "IR:{name:'IR Beam-Break',pins:1,config:['debounce']},";
    html += "ULTRASONIC:{name:'Ultrasonic (HC-SR04)',pins:2,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']},";
    html += "ULTRASONIC_GROVE:{name:'Ultrasonic (Grove)',pins:1,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}};";

    // Load sensors from configuration
    html += "async function loadSensors(){";
    html += "try{const res=await fetch('/api/config');if(!res.ok)return;";
    html += "const cfg=await res.json();";
    html += "sensorSlots=[null,null,null,null];";
    html += "if(cfg.sensors&&Array.isArray(cfg.sensors)){";
    html += "cfg.sensors.forEach(s=>{if(s.slot>=0&&s.slot<4){sensorSlots[s.slot]=s;}});";
    html += "renderSensors();}}catch(e){console.error('Failed to load sensors:',e);}}";

    // Render all sensor cards
    html += "function renderSensors(){";
    html += "const container=document.getElementById('sensors-list');container.innerHTML='';";
    html += "sensorSlots.forEach((sensor,idx)=>{";
    html += "if(sensor!==null){container.appendChild(createSensorCard(sensor,idx));}});";
    html += "if(sensorSlots.filter(s=>s!==null).length===0){";
    html += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:20px;\">No sensors configured. Click \"Add Sensor\" to get started.</p>';}}";

    // Create sensor card element
    html += "function createSensorCard(sensor,slotIdx){";
    html += "const card=document.createElement('div');";
    html += "card.className='sensor-card'+(sensor.enabled?'':' disabled');";
    html += "let html='';";

    // Header with badge, title, and buttons on one line
    html += "html+='<div class=\"sensor-header\">';";
    html += "html+='<div style=\"display:flex;align-items:center;gap:10px;\">';";
    html += "html+='<span class=\"badge badge-'+(sensor.type===0?'success':sensor.type===1?'info':'primary')+'\">'+";
    html += "(sensor.type===0?'PIR':sensor.type===1?'IR':sensor.type===4?'GROVE':'HC-SR04')+'</span>';";
    html += "html+='<span class=\"sensor-title\">Slot '+slotIdx+': '+(sensor.name||'Unnamed Sensor')+'</span></div>';";
    html += "html+='<div class=\"sensor-actions\">';";
    html += "html+='<button class=\"btn btn-sm btn-'+(sensor.enabled?'warning':'success')+'\" onclick=\"toggleSensor('+slotIdx+')\">'+(sensor.enabled?'Disable':'Enable')+'</button>';";
    html += "html+='<button class=\"btn btn-sm btn-secondary\" onclick=\"editSensor('+slotIdx+')\">Edit</button>';";
    html += "html+='<button class=\"btn btn-sm btn-danger\" onclick=\"removeSensor('+slotIdx+')\">Remove</button>';";
    html += "html+='</div></div>';";

    // Compact horizontal layout for pin and config info
    html += "html+='<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px;\">';";

    // Wiring diagram column
    html += "html+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>';";
    html += "html+='<div style=\"line-height:1.6;\">';";

    // PIR/IR wiring
    html += "if(sensor.type===0||sensor.type===1){";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor OUT → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';}";

    // Ultrasonic HC-SR04 wiring (4-pin)
    html += "else if(sensor.type===2){";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">5V</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor TRIG → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor ECHO → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.secondaryPin+'</span></div>';}";

    // Grove Ultrasonic wiring (3-pin)
    html += "else if(sensor.type===4){";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove VCC (Red) → <span style=\"color:#dc2626;font-weight:600;\">3.3V/5V</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove GND (Black) → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove SIG (Yellow) → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';}";

    html += "html+='</div></div>';";

    // Configuration column
    html += "html+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>';";
    html += "html+='<div style=\"line-height:1.6;\">';";
    html += "if(sensor.type===0){";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warmup:</span> <span>'+(sensor.warmupMs/1000)+'s</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Debounce:</span> <span>'+sensor.debounceMs+'ms</span></div>';}";
    html += "else if(sensor.type===2||sensor.type===4){";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Type:</span> <span>'+(sensor.type===2?'HC-SR04 (4-pin)':'Grove (3-pin)')+'</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Max Range:</span> <span>'+(sensor.maxDetectionDistance||3000)+'mm</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warn At:</span> <span>'+sensor.detectionThreshold+'mm</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Direction:</span> <span>'+(sensor.enableDirectionDetection?'Enabled':'Disabled')+'</span></div>';";
    html += "if(sensor.enableDirectionDetection){";
    html += "const dirMode=(sensor.directionTriggerMode===0?'Approaching':sensor.directionTriggerMode===1?'Receding':'Both');";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Trigger:</span> <span>'+dirMode+'</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Samples:</span> <span>'+sensor.rapidSampleCount+' @ '+sensor.rapidSampleMs+'ms</span></div>';}}";
    html += "html+='</div></div>';";

    html += "html+='</div>';";  // Close grid

    html += "card.innerHTML=html;";
    html += "return card;}";

    // Add new sensor
    html += "function addSensor(){";
    html += "const freeSlot=sensorSlots.findIndex(s=>s===null);";
    html += "if(freeSlot===-1){alert('Maximum 4 sensors allowed. Remove a sensor first.');return;}";
    html += "const type=prompt('Select sensor type:\\n0 = PIR Motion\\n1 = IR Beam-Break\\n2 = Ultrasonic (HC-SR04 4-pin)\\n4 = Ultrasonic (Grove 3-pin)','0');";
    html += "if(type===null)return;";
    html += "const typeNum=parseInt(type);";
    html += "if(typeNum<0||typeNum>4||typeNum===3){alert('Invalid sensor type');return;}";
    html += "const name=prompt('Enter sensor name:','Sensor '+(freeSlot+1));";
    html += "if(!name)return;";
    html += "const pin=parseInt(prompt('Enter primary pin (GPIO number):','5'));";
    html += "if(isNaN(pin)||pin<0||pin>48){alert('Invalid pin number');return;}";
    html += "const sensor={type:typeNum,name:name,primaryPin:pin,enabled:true,isPrimary:freeSlot===0,";
    html += "warmupMs:60000,debounceMs:100,detectionThreshold:1500,maxDetectionDistance:3000,enableDirectionDetection:true,";
    html += "directionTriggerMode:0,rapidSampleCount:5,rapidSampleMs:200};";
    html += "if(typeNum===2){";
    html += "const echoPin=parseInt(prompt('Enter echo pin for HC-SR04 (GPIO number):','9'));";
    html += "if(isNaN(echoPin)||echoPin<0||echoPin>48){alert('Invalid echo pin');return;}";
    html += "sensor.secondaryPin=echoPin;}";
    html += "if(typeNum===4){sensor.secondaryPin=0;}";
    html += "sensorSlots[freeSlot]=sensor;renderSensors();saveSensors();}";

    // Remove sensor
    html += "function removeSensor(slotIdx){";
    html += "if(!confirm('Remove sensor from slot '+slotIdx+'?'))return;";
    html += "sensorSlots[slotIdx]=null;renderSensors();saveSensors();}";

    // Toggle sensor enabled/disabled
    html += "function toggleSensor(slotIdx){";
    html += "if(sensorSlots[slotIdx]){";
    html += "sensorSlots[slotIdx].enabled=!sensorSlots[slotIdx].enabled;";
    html += "renderSensors();saveSensors();}}";

    // Edit sensor
    html += "function editSensor(slotIdx){";
    html += "const sensor=sensorSlots[slotIdx];if(!sensor)return;";
    html += "const newName=prompt('Sensor name:',sensor.name);";
    html += "if(newName!==null&&newName.length>0){sensor.name=newName;}";
    html += "if(sensor.type===0){";
    html += "const warmup=parseInt(prompt('PIR warmup time (seconds):',sensor.warmupMs/1000));";
    html += "if(!isNaN(warmup)&&warmup>=1&&warmup<=120)sensor.warmupMs=warmup*1000;";
    html += "const debounce=parseInt(prompt('Debounce time (ms):',sensor.debounceMs));";
    html += "if(!isNaN(debounce)&&debounce>=10&&debounce<=1000)sensor.debounceMs=debounce;}";
    html += "else if(sensor.type===2||sensor.type===4){";
    html += "const maxDist=parseInt(prompt('Max detection distance (mm)\\nSensor starts detecting at this range:',sensor.maxDetectionDistance||3000));";
    html += "if(!isNaN(maxDist)&&maxDist>=100)sensor.maxDetectionDistance=maxDist;";
    html += "const warnDist=parseInt(prompt('Warning trigger distance (mm)\\nWarning activates when person is within:',sensor.detectionThreshold||1500));";
    html += "if(!isNaN(warnDist)&&warnDist>=10)sensor.detectionThreshold=warnDist;";
    html += "const dirStr=prompt('Enable direction detection? (yes/no):',(sensor.enableDirectionDetection?'yes':'no'));";
    html += "if(dirStr!==null){sensor.enableDirectionDetection=(dirStr.toLowerCase()==='yes'||dirStr==='1');}";
    html += "if(sensor.enableDirectionDetection){";
    html += "const dirMode=prompt('Trigger on:\\n0=Approaching (walking towards)\\n1=Receding (walking away)\\n2=Both directions',sensor.directionTriggerMode||0);";
    html += "if(dirMode!==null&&!isNaN(parseInt(dirMode))){sensor.directionTriggerMode=parseInt(dirMode);}";
    html += "const samples=parseInt(prompt('Rapid sample count (2-20):',sensor.rapidSampleCount||5));";
    html += "if(!isNaN(samples)&&samples>=2&&samples<=20)sensor.rapidSampleCount=samples;";
    html += "const interval=parseInt(prompt('Sample interval ms (50-1000):',sensor.rapidSampleMs||200));";
    html += "if(!isNaN(interval)&&interval>=50&&interval<=1000)sensor.rapidSampleMs=interval;}}";
    html += "renderSensors();saveSensors();}";

    // Save sensors to backend
    html += "async function saveSensors(){";
    html += "try{";
    html += "const activeSensors=sensorSlots.map((s,idx)=>s?{...s,slot:idx}:null).filter(s=>s!==null);";
    html += "const res=await fetch('/api/sensors',{method:'POST',";
    html += "headers:{'Content-Type':'application/json'},body:JSON.stringify(activeSensors)});";
    html += "if(res.ok){";
    html += "const data=await res.json();";
    html += "console.log('Sensors saved:',data);";
    html += "}else{";
    html += "const err=await res.text();";
    html += "console.error('Save failed:',err);";
    html += "alert('Failed to save sensor configuration: '+err);}}";
    html += "catch(e){console.error('Save error:',e);alert('Error saving sensors: '+e.message);}}";

    // === DISPLAY MANAGEMENT ===
    html += "let displaySlots=[null,null];";

    // Load displays from backend
    html += "async function loadDisplays(){";
    html += "try{";
    html += "const res=await fetch('/api/displays');";
    html += "if(res.ok){";
    html += "const data=await res.json();";
    html += "if(data.displays&&Array.isArray(data.displays)){";
    html += "data.displays.forEach(d=>{if(d.slot>=0&&d.slot<2){displaySlots[d.slot]=d;}});";
    html += "renderDisplays();}}";
    html += "}catch(e){console.error('Load displays error:',e);}}";

    // Render displays list
    html += "function renderDisplays(){";
    html += "const container=document.getElementById('displays-list');";
    html += "if(!container)return;";
    html += "container.innerHTML='';";
    html += "let anyDisplay=false;";
    html += "for(let i=0;i<displaySlots.length;i++){";
    html += "if(displaySlots[i]){anyDisplay=true;const card=createDisplayCard(displaySlots[i],i);container.appendChild(card);}}";
    html += "if(!anyDisplay){";
    html += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:20px;\">No displays configured. Click \"Add Display\" to get started.</p>';}}";

    // Create display card
    html += "function createDisplayCard(display,slotIdx){";
    html += "const card=document.createElement('div');";
    html += "card.className='sensor-card';";
    html += "card.style.cssText='border:1px solid #e2e8f0;border-radius:8px;padding:16px;margin-bottom:12px;background:#fff;';";
    html += "let content='';";
    html += "content+='<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;\">';";
    html += "content+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html += "const typeName=display.type===1?'8x8 Matrix':'LED';";
    html += "const typeColor=display.type===1?'#3b82f6':'#10b981';";
    html += "content+='<span style=\"background:'+typeColor+';color:white;padding:4px 8px;border-radius:4px;font-size:0.75em;font-weight:600;\">'+typeName+'</span>';";
    html += "content+='<span style=\"font-weight:600;\">Slot '+slotIdx+': '+display.name+'</span>';";
    html += "content+='</div>';";
    html += "content+='<div style=\"display:flex;gap:8px;\">';";
    html += "content+='<button class=\"btn btn-sm btn-'+(display.enabled?'warning':'success')+'\" onclick=\"toggleDisplay('+slotIdx+')\">'+(display.enabled?'Disable':'Enable')+'</button>';";
    html += "content+='<button class=\"btn btn-sm btn-secondary\" onclick=\"editDisplay('+slotIdx+')\">Edit</button>';";
    html += "content+='<button class=\"btn btn-sm btn-danger\" onclick=\"removeDisplay('+slotIdx+')\">Remove</button>';";
    html += "content+='</div></div>';";
    html += "content+='<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:16px;\">';";
    html += "content+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>';";
    html += "content+='<div style=\"line-height:1.6;\">';";
    html += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>';";
    html += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix SDA → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+display.sdaPin+'</span></div>';";
    html += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix SCL → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+display.sclPin+'</span></div>';";
    html += "content+='</div></div>';";
    html += "content+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>';";
    html += "content+='<div style=\"line-height:1.6;\">';";
    html += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">I2C Address:</span> <span>0x'+display.i2cAddress.toString(16).toUpperCase()+'</span></div>';";
    html += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Brightness:</span> <span>'+display.brightness+'/15</span></div>';";
    html += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Rotation:</span> <span>'+(display.rotation*90)+'°</span></div>';";
    html += "content+='</div></div></div>';";
    html += "card.innerHTML=content;";
    html += "return card;}";

    // Add display
    html += "function addDisplay(){";
    html += "let slot=-1;";
    html += "for(let i=0;i<2;i++){if(!displaySlots[i]){slot=i;break;}}";
    html += "if(slot===-1){alert('Maximum 2 displays reached');return;}";
    html += "const name=prompt('Display name:','8x8 Matrix');";
    html += "if(!name)return;";
    html += "const newDisplay={slot:slot,name:name,type:1,i2cAddress:0x70,sdaPin:7,sclPin:10,enabled:true,brightness:5,rotation:0,useForStatus:true};";
    html += "displaySlots[slot]=newDisplay;";
    html += "renderDisplays();";
    html += "saveDisplays();}";

    // Remove display
    html += "function removeDisplay(slotIdx){";
    html += "if(!confirm('Remove this display?'))return;";
    html += "displaySlots[slotIdx]=null;";
    html += "renderDisplays();";
    html += "saveDisplays();}";

    // Toggle display
    html += "function toggleDisplay(slotIdx){";
    html += "if(displaySlots[slotIdx]){";
    html += "displaySlots[slotIdx].enabled=!displaySlots[slotIdx].enabled;";
    html += "renderDisplays();";
    html += "saveDisplays();}}";

    // Edit display
    html += "function editDisplay(slotIdx){";
    html += "const display=displaySlots[slotIdx];";
    html += "if(!display)return;";
    html += "const name=prompt('Display name:',display.name);";
    html += "if(name){display.name=name;}";
    html += "const brightness=prompt('Brightness (0-15):',display.brightness);";
    html += "if(brightness){display.brightness=parseInt(brightness)||5;}";
    html += "const rotation=prompt('Rotation (0,1,2,3):',display.rotation);";
    html += "if(rotation){display.rotation=parseInt(rotation)||0;}";
    html += "renderDisplays();";
    html += "saveDisplays();}";

    // Save displays to backend
    html += "async function saveDisplays(){";
    html += "try{";
    html += "const activeDisplays=displaySlots.filter(d=>d!==null);";
    html += "const res=await fetch('/api/displays',{method:'POST',";
    html += "headers:{'Content-Type':'application/json'},body:JSON.stringify(activeDisplays)});";
    html += "if(res.ok){";
    html += "const data=await res.json();";
    html += "console.log('Displays saved:',data);";
    html += "}else{";
    html += "const err=await res.text();";
    html += "console.error('Save failed:',err);";
    html += "alert('Failed to save display configuration: '+err);}}";
    html += "catch(e){console.error('Save error:',e);alert('Error saving displays: '+e.message);}}";

    // ========================================================================
    // Custom Animation Management (Issue #12 Phase 2)
    // ========================================================================

    // Load animations list
    html += "async function loadAnimations(){";
    html += "try{";
    html += "const res=await fetch('/api/animations');";
    html += "if(res.ok){";
    html += "const data=await res.json();";
    html += "renderAnimations(data.animations||[]);";
    html += "updateAnimationSelect(data.animations||[]);";
    html += "}else{console.error('Failed to load animations');}}";
    html += "catch(e){console.error('Load animations error:',e);}}";

    // Render animations list
    html += "function renderAnimations(animations){";
    html += "const container=document.getElementById('animations-list');";
    html += "if(!container)return;";
    html += "if(animations.length===0){";
    html += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:12px;background:#f8fafc;border-radius:4px;\">No custom animations loaded. Upload an animation file to get started.</p>';";
    html += "return;}";
    html += "let html='<div style=\"display:grid;gap:8px;\">';";
    html += "animations.forEach((anim,idx)=>{";
    html += "html+='<div style=\"display:flex;justify-content:space-between;align-items:center;padding:12px;background:#fff;border:1px solid #e2e8f0;border-radius:6px;\">';";
    html += "html+='<div style=\"flex:1;\">';";
    html += "html+='<div style=\"font-weight:600;color:#1e293b;\">'+anim.name+'</div>';";
    html += "html+='<div style=\"font-size:0.8em;color:#64748b;margin-top:2px;\">'+anim.frameCount+' frames';";
    html += "if(anim.loop)html+=' • Looping';";
    html += "html+='</div></div>';";
    html += "html+='<div style=\"display:flex;gap:6px;\">';";
    html += "html+='<button class=\"btn btn-sm btn-primary\" onclick=\"playCustomAnimation(\\''+anim.name+'\\')\" title=\"Play animation\" style=\"width:36px;padding:8px;\">▶</button>';";
    html += "html+='<button class=\"btn btn-sm btn-success\" onclick=\"assignCustomAnimation(\\''+anim.name+'\\')\" title=\"Assign to function\" style=\"width:36px;padding:8px;\">✓</button>';";
    html += "html+='<button class=\"btn btn-sm btn-danger\" onclick=\"deleteAnimation(\\''+anim.name+'\\')\" title=\"Remove from memory\" style=\"width:36px;padding:8px;\">×</button>';";
    html += "html+='</div></div>';});";
    html += "html+='</div>';";
    html += "container.innerHTML=html;}";

    // Update animation select dropdown
    html += "function updateAnimationSelect(animations){";
    html += "const select=document.getElementById('test-animation-select');";
    html += "if(!select)return;";
    html += "select.innerHTML='<option value=\"\">Select animation...</option>';";
    html += "animations.forEach(anim=>{";
    html += "const opt=document.createElement('option');";
    html += "opt.value=anim.name;";
    html += "opt.textContent=anim.name+' ('+anim.frameCount+' frames)';";
    html += "select.appendChild(opt);});}";

    // Upload animation file
    html += "async function uploadAnimation(){";
    html += "const fileInput=document.getElementById('animation-file-input');";
    html += "if(!fileInput||!fileInput.files||fileInput.files.length===0){";
    html += "alert('Please select a file first');return;}";
    html += "const file=fileInput.files[0];";
    html += "if(!file.name.endsWith('.txt')){";
    html += "alert('Please select a .txt animation file');return;}";
    html += "const formData=new FormData();";
    html += "formData.append('file',file);";
    html += "try{";
    html += "const res=await fetch('/api/animations/upload',{method:'POST',body:formData});";
    html += "if(res.ok){";
    html += "const data=await res.json();";
    html += "alert('Animation uploaded successfully: '+data.name);";
    html += "fileInput.value='';";
    html += "loadAnimations();";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Upload failed: '+err);}}";
    html += "catch(e){alert('Upload error: '+e.message);}}";

    // Play built-in animation
    html += "async function playBuiltInAnimation(animType,duration){";
    html += "try{";
    html += "const res=await fetch('/api/animations/builtin',{";
    html += "method:'POST',";
    html += "headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({type:animType,duration:duration||0})});";
    html += "if(res.ok){";
    html += "console.log('Playing built-in animation: '+animType);";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Failed to play animation: '+err);}}";
    html += "catch(e){alert('Error playing animation: '+e.message);}}";

    // Play animation from test controls
    html += "function playTestAnimation(){";
    html += "const select=document.getElementById('test-animation-select');";
    html += "const duration=parseInt(document.getElementById('test-duration').value)||0;";
    html += "if(!select||!select.value){alert('Select an animation first');return;}";
    html += "playAnimation(select.value,duration);}";

    // Play specific custom animation
    html += "async function playAnimation(name,duration){";
    html += "try{";
    html += "const res=await fetch('/api/animations/play',{";
    html += "method:'POST',";
    html += "headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({name:name,duration:duration||0})});";
    html += "if(res.ok){";
    html += "console.log('Playing animation: '+name);";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Failed to play animation: '+err);}}";
    html += "catch(e){alert('Error playing animation: '+e.message);}}";

    // Stop current animation
    html += "async function stopAnimation(){";
    html += "try{";
    html += "const res=await fetch('/api/animations/stop',{method:'POST'});";
    html += "if(res.ok){console.log('Animation stopped');}";
    html += "}catch(e){console.error('Error stopping animation:',e);}}";

    // Delete animation from memory
    html += "async function deleteAnimation(name){";
    html += "if(!confirm('Remove \"'+name+'\" from memory?\\n\\nThis will free memory but you can re-upload the file anytime.'))return;";
    html += "try{";
    html += "const res=await fetch('/api/animations/'+encodeURIComponent(name),{method:'DELETE'});";
    html += "if(res.ok){";
    html += "alert('Animation removed from memory');";
    html += "loadAnimations();";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Failed to delete: '+err);}}";
    html += "catch(e){alert('Delete error: '+e.message);}}";

    // Show animation format help
    html += "function showAnimationHelp(){";
    html += "alert('Animation File Format:\\n\\n'+";
    html += "'name=MyAnimation\\n'+";
    html += "'loop=true\\n'+";
    html += "'frame=11111111,10000001,...,100\\n\\n'+";
    html += "'• Each frame: 8 binary bytes + delay (ms)\\n'+";
    html += "'• Max 16 frames per animation\\n'+";
    html += "'• Max 8 animations loaded at once\\n\\n'+";
    html += "'See /data/animations/README.md for examples');}";

    // Download built-in animation as template
    html += "async function downloadTemplate(animType){";
    html += "try{";
    html += "console.log('Downloading template for:',animType);";
    html += "const res=await fetch('/api/animations/template?type='+animType);";
    html += "console.log('Fetch complete, status:',res.status);";
    html += "if(res.ok){";
    html += "const text=await res.text();";
    html += "const blob=new Blob([text],{type:'text/plain'});";
    html += "const url=URL.createObjectURL(blob);";
    html += "const a=document.createElement('a');";
    html += "a.href=url;";
    html += "a.download=animType.toLowerCase()+'_template.txt';";
    html += "document.body.appendChild(a);";
    html += "a.click();";
    html += "document.body.removeChild(a);";
    html += "URL.revokeObjectURL(url);";
    html += "console.log('Download triggered');}else{";
    html += "const err=await res.text();";
    html += "console.error('Download failed:',res.status,err);";
    html += "alert('Failed to download template: '+res.status);}}";
    html += "catch(e){";
    html += "console.error('Download error:',e);";
    html += "alert('Download error: '+e.message);}}";

    // Play selected built-in animation from dropdown
    html += "function playSelectedBuiltIn(){";
    html += "const select=document.getElementById('builtin-animation-select');";
    html += "if(!select||!select.value)return;";
    html += "const duration=parseInt(document.getElementById('test-duration').value)||5000;";
    html += "playBuiltInAnimation(select.value,duration);}";

    // Download selected animation as template
    html += "function downloadSelectedTemplate(){";
    html += "const select=document.getElementById('builtin-animation-select');";
    html += "if(!select||!select.value)return;";
    html += "downloadTemplate(select.value);}";

    // Assign selected built-in animation to a function
    html += "function assignSelectedBuiltIn(){";
    html += "const select=document.getElementById('builtin-animation-select');";
    html += "if(!select||!select.value)return;";
    html += "const functions=['motion-alert','battery-low','boot-status','wifi-connected'];";
    html += "const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];";
    html += "let message='Assign \"'+select.selectedOptions[0].text+'\" to which function?\\n\\n';";
    html += "for(let i=0;i<functions.length;i++){";
    html += "message+=(i+1)+'. '+functionNames[i]+'\\n';}";
    html += "const choice=prompt(message,'1');";
    html += "if(!choice)return;";
    html += "const idx=parseInt(choice)-1;";
    html += "if(idx>=0&&idx<functions.length){";
    html += "assignAnimation(functions[idx],'builtin',select.value);}}";

    // Assign animation to a function
    html += "async function assignAnimation(functionKey,type,animName){";
    html += "try{";
    html += "const res=await fetch('/api/animations/assign',{";
    html += "method:'POST',";
    html += "headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({function:functionKey,type:type,animation:animName})});";
    html += "if(res.ok){";
    html += "updateActiveAnimations();";
    html += "alert('Animation assigned successfully');}";
    html += "else{";
    html += "const err=await res.text();";
    html += "alert('Failed to assign animation: '+err);}}";
    html += "catch(e){alert('Assignment error: '+e.message);}}";

    // Update active animations display
    html += "function updateActiveAnimations(){";
    html += "fetch('/api/animations/assignments')";
    html += ".then(res=>res.json())";
    html += ".then(data=>{";
    html += "const panel=document.getElementById('active-animations-panel');";
    html += "if(panel){panel.style.display='block';}";
    html += "if(data['motion-alert']){";
    html += "const elem=document.getElementById('anim-motion-alert');";
    html += "if(elem)elem.textContent=data['motion-alert'].type==='builtin'?'Built-in: '+data['motion-alert'].name:data['motion-alert'].name;}";
    html += "if(data['battery-low']){";
    html += "const elem=document.getElementById('anim-battery-low');";
    html += "if(elem)elem.textContent=data['battery-low'].type==='builtin'?'Built-in: '+data['battery-low'].name:data['battery-low'].name;}";
    html += "if(data['boot-status']){";
    html += "const elem=document.getElementById('anim-boot-status');";
    html += "if(elem)elem.textContent=data['boot-status'].type==='builtin'?'Built-in: '+data['boot-status'].name:data['boot-status'].name;}";
    html += "if(data['wifi-connected']){";
    html += "const elem=document.getElementById('anim-wifi-connected');";
    html += "if(elem)elem.textContent=data['wifi-connected'].type==='builtin'?'Built-in: '+data['wifi-connected'].name:data['wifi-connected'].name;}";
    html += "})";
    html += ".catch(e=>console.error('Failed to load assignments:',e));}";

    // Play custom animation with duration from input
    html += "function playCustomAnimation(name){";
    html += "const duration=parseInt(document.getElementById('test-duration').value)||5000;";
    html += "playAnimation(name,duration);}";

    // Assign custom animation to a function
    html += "function assignCustomAnimation(name){";
    html += "const functions=['motion-alert','battery-low','boot-status','wifi-connected'];";
    html += "const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];";
    html += "let message='Assign \"'+name+'\" to which function?\\n\\n';";
    html += "for(let i=0;i<functions.length;i++){";
    html += "message+=(i+1)+'. '+functionNames[i]+'\\n';}";
    html += "const choice=prompt(message,'1');";
    html += "if(!choice)return;";
    html += "const idx=parseInt(choice)-1;";
    html += "if(idx>=0&&idx<functions.length){";
    html += "assignAnimation(functions[idx],'custom',name);}}";

    // Auto-refresh status and logs
    html += "fetchStatus();";
    html += "setInterval(fetchStatus,2000);";
    html += "setInterval(()=>{";
    html += "if(document.getElementById('logs-tab').classList.contains('active')){fetchLogs();}";
    html += "},5000);";

    // Load animation assignments on page load
    html += "updateActiveAnimations();";

    html += "</script></body></html>";

    return html;
}
