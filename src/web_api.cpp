#include "web_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "watchdog_manager.h"
#include "hal_ledmatrix_8x8.h"
#include "hal_ultrasonic.h"
#include "hal_ultrasonic_grove.h"
#include "sensor_manager.h"
#include "direction_detector.h"
#include "debug_logger.h"
#include "ota_manager.h"
#include <ArduinoJson.h>
#include <map>
#if !MOCK_HARDWARE
#include <LittleFS.h>  // For animation uploads and user content, NOT for web UI
#include <esp_core_dump.h>
#include <esp_partition.h>
#endif

// Static map to track request buffers for chunked POST body handling
// Using std::map instead of request->_tempObject to avoid heap corruption
static std::map<AsyncWebServerRequest*, char*> s_requestBuffers;

// Static buffer for HTML dashboard (single-user device)
static String g_htmlResponseBuffer;
static unsigned long g_lastHTMLBuildTime = 0;
static bool g_htmlResponseInProgress = false;

// Helper functions for request buffer management
static char* allocateRequestBuffer(AsyncWebServerRequest* request, size_t size) {
    char* buffer = (char*)malloc(size + 1);
    if (buffer) {
        s_requestBuffers[request] = buffer;
        DEBUG_LOG_API("Allocated request buffer: req=%p, size=%u", (void*)request, size);
    }
    return buffer;
}

static char* getRequestBuffer(AsyncWebServerRequest* request) {
    auto it = s_requestBuffers.find(request);
    return (it != s_requestBuffers.end()) ? it->second : nullptr;
}

static void freeRequestBuffer(AsyncWebServerRequest* request) {
    auto it = s_requestBuffers.find(request);
    if (it != s_requestBuffers.end()) {
        DEBUG_LOG_API("Freeing request buffer: req=%p", (void*)request);
        free(it->second);
        s_requestBuffers.erase(it);
    }
}

WebAPI::WebAPI(AsyncWebServer* server, StateMachine* stateMachine, ConfigManager* config)
    : m_server(server)
    , m_stateMachine(stateMachine)
    , m_config(config)
    , m_wifi(nullptr)
    , m_power(nullptr)
    , m_watchdog(nullptr)
    , m_ledMatrix(nullptr)
    , m_sensorManager(nullptr)
    , m_directionDetector(nullptr)
    , m_otaManager(nullptr)
    , m_corsEnabled(true)
    , m_logWebSocket(nullptr)
    , m_maxWebSocketClients(3)
{
}

WebAPI::~WebAPI() {
    // Clean up OTA manager
    if (m_otaManager) {
        delete m_otaManager;
        m_otaManager = nullptr;
    }
}

bool WebAPI::begin() {
    if (!m_server || !m_stateMachine || !m_config) {
        DEBUG_LOG_API("WebAPI: Invalid parameters");
        return false;
    }

    DEBUG_LOG_API("WebAPI: Registering endpoints");

    // Root endpoint - inline dashboard UI
    // NOTE: We use inline HTML (buildDashboardHTML) instead of filesystem-based UI
    // because it includes all features (multi-sensor, LED matrix, animations) and
    // is simpler to deploy (no filesystem upload required).
    m_server->on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleRoot(req);
    });

    // Live logs popup window
    m_server->on("/live-logs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleLiveLogs(req);
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

    m_server->on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* req) {
        this->handlePostReboot(req);
    });

    // CRITICAL: Register specific routes BEFORE general routes
    // POST /api/sensors/:slot/errorrate - trigger error rate check for a sensor
    m_server->on("/api/sensors/0/errorrate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 0 ERROR RATE POST HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/1/errorrate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 1 ERROR RATE POST HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/2/errorrate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 2 ERROR RATE POST HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/3/errorrate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 3 ERROR RATE POST HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });

    // Also register GET handlers for manual browser testing
    m_server->on("/api/sensors/0/errorrate", HTTP_GET, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 0 ERROR RATE GET HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/1/errorrate", HTTP_GET, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 1 ERROR RATE GET HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/2/errorrate", HTTP_GET, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 2 ERROR RATE GET HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });
    m_server->on("/api/sensors/3/errorrate", HTTP_GET, [this](AsyncWebServerRequest* req) {
        DEBUG_LOG_API("WebAPI: *** SLOT 3 ERROR RATE GET HANDLER CALLED ***");
        this->handleCheckSensorErrorRate(req);
    });

    DEBUG_LOG_API("WebAPI: Registered error rate routes (GET+POST) for slots 0-3");

    // NOW register the general /api/sensors routes (AFTER the specific ones)
    m_server->on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetSensors(req);
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

    // Debug logging endpoints
    // IMPORTANT: Register more specific routes BEFORE general routes
    // to ensure ESPAsyncWebServer matches them correctly

    m_server->on("/api/debug/logs/current", HTTP_GET, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        req->send(LittleFS, "/logs/boot_current.log", "text/plain");
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
    });

    m_server->on("/api/debug/logs/boot_1", HTTP_GET, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        req->send(LittleFS, "/logs/boot_1.log", "text/plain");
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
    });

    m_server->on("/api/debug/logs/boot_2", HTTP_GET, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        req->send(LittleFS, "/logs/boot_2.log", "text/plain");
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
    });

    // DELETE endpoints for individual log files
    m_server->on("/api/debug/logs/current", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        // Don't delete current log - it's active, just clear it
        if (LittleFS.exists("/logs/boot_current.log")) {
            File f = LittleFS.open("/logs/boot_current.log", "w");
            if (f) {
                f.close();
                this->sendJSON(req, 200, "{\"success\":true,\"message\":\"Current log cleared\"}");
            } else {
                this->sendError(req, 500, "Failed to clear log file");
            }
        } else {
            this->sendError(req, 404, "Log not found");
        }
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
    });

    m_server->on("/api/debug/logs/boot_1", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        if (LittleFS.exists("/logs/boot_1.log")) {
            if (LittleFS.remove("/logs/boot_1.log")) {
                this->sendJSON(req, 200, "{\"success\":true,\"message\":\"Log deleted\"}");
            } else {
                this->sendError(req, 500, "Failed to delete log file");
            }
        } else {
            this->sendError(req, 404, "Log not found");
        }
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
    });

    m_server->on("/api/debug/logs/boot_2", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
        if (LittleFS.exists("/logs/boot_2.log")) {
            if (LittleFS.remove("/logs/boot_2.log")) {
                this->sendJSON(req, 200, "{\"success\":true,\"message\":\"Log deleted\"}");
            } else {
                this->sendError(req, 500, "Failed to delete log file");
            }
        } else {
            this->sendError(req, 404, "Log not found");
        }
#else
        this->sendError(req, 501, "Not available in mock mode");
#endif
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

    // General metadata endpoint - MUST be registered AFTER specific routes
    m_server->on("/api/debug/logs", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetDebugLogs(req);
    });

    // OPTIONS for debug endpoints
    m_server->on("/api/debug/logs", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/debug/config", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });

    // OTA (Over-The-Air) firmware update endpoints
    // Initialize OTA Manager
    m_otaManager = new OTAManager();
    if (m_otaManager) {
        m_otaManager->begin();
    }

    // OTA upload endpoint (chunked file upload handler)
    m_server->on("/api/ota/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        [this](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            this->handleOTAUpload(req, filename, index, data, len, final);
        },
        nullptr
    );

    // OTA status endpoint
    m_server->on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetOTAStatus(req);
    });

    // Coredump download endpoint
    m_server->on("/api/ota/coredump", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetCoredump(req);
    });

    // Coredump clear endpoint
    m_server->on("/api/ota/coredump", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        this->handleClearCoredump(req);
    });

    // OPTIONS for OTA endpoints
    m_server->on("/api/ota/upload", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/ota/status", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });
    m_server->on("/api/ota/coredump", HTTP_OPTIONS, [this](AsyncWebServerRequest* req) {
        this->handleOptions(req);
    });

    // Add catch-all handler for debugging unmatched routes
    m_server->onNotFound([](AsyncWebServerRequest *request) {
        DEBUG_LOG_API("WebAPI: Unmatched request - Method: %s, URL: %s",
                 request->methodToString(), request->url().c_str());
        request->send(404, "text/plain", "Not Found: " + request->url());
    });

    // Create WebSocket for log streaming
    m_logWebSocket = new AsyncWebSocket("/ws/logs");
    m_logWebSocket->onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                                    AwsEventType t, void* a, uint8_t* d, size_t l) {
        this->handleLogWebSocketEvent(s, c, t, a, d, l);
    });
    m_server->addHandler(m_logWebSocket);
    DEBUG_LOG_API("WebSocket /ws/logs registered");

    DEBUG_LOG_API("WebAPI: ✓ Endpoints registered");
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

void WebAPI::setDirectionDetector(DirectionDetector* directionDetector) {
    m_directionDetector = directionDetector;
}

void WebAPI::handleGetStatus(AsyncWebServerRequest* request) {
    DEBUG_LOG_API("GET /api/status");
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

    // Direction Detector (if available and enabled)
    if (m_directionDetector) {
        const ConfigManager::DirectionDetectorConfig& dirCfg = m_config->getConfig().directionDetector;
        JsonObject dirObj = doc.createNestedObject("direction");
        dirObj["enabled"] = dirCfg.enabled;

        if (dirCfg.enabled) {
            // Current direction state
            dirObj["current"] = m_directionDetector->isApproaching() ? "APPROACHING" : "UNKNOWN";
            dirObj["confirmed"] = m_directionDetector->isDirectionConfirmed();
            dirObj["confidenceMs"] = m_directionDetector->getDirectionConfidenceMs();
            dirObj["state"] = m_directionDetector->getStateName();

            // Sensor states
            dirObj["farSensorActive"] = m_directionDetector->getFarSensorState();
            dirObj["nearSensorActive"] = m_directionDetector->getNearSensorState();

            // Statistics
            JsonObject statsObj = dirObj.createNestedObject("statistics");
            statsObj["approaching"] = m_directionDetector->getApproachingCount();
            statsObj["unknown"] = m_directionDetector->getUnknownCount();

            // Configuration
            JsonObject configObj = dirObj.createNestedObject("config");
            configObj["farSensorSlot"] = dirCfg.farSensorSlot;
            configObj["nearSensorSlot"] = dirCfg.nearSensorSlot;
            configObj["confirmationWindowMs"] = dirCfg.confirmationWindowMs;
            configObj["simultaneousThresholdMs"] = dirCfg.simultaneousThresholdMs;
            configObj["patternTimeoutMs"] = dirCfg.patternTimeoutMs;
            configObj["triggerOnApproaching"] = dirCfg.triggerOnApproaching;
        }
    }

    // Serialize to string
    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleGetConfig(AsyncWebServerRequest* request) {
    DEBUG_LOG_API("GET /api/config");

    // Note: This endpoint returns CONFIGURATION only, not runtime status.
    // Sensor error rates are NOT included here. They would need a separate
    // /api/sensors endpoint that queries live sensor status.
    DEBUG_LOG_API("WebAPI: Returning sensor configuration (error rates not included - "
              "use POST /api/sensors/:slot/errorrate to trigger test)");

    char buffer[2048];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
}

void WebAPI::handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    DEBUG_LOG_API("POST /api/config - chunk: index=%u, len=%u, total=%u", index, len, total);

    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            DEBUG_LOG_API("Config update FAILED: Out of memory");
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            DEBUG_LOG_API("Request disconnected, cleaning up buffer");
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    DEBUG_LOG_API("POST /api/config - complete: %u bytes", total);

    if (!requestBuffer) {
        DEBUG_LOG_API("Config update FAILED: Buffer is null");
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    char* jsonStr = requestBuffer;
    jsonStr[total] = '\0';

    // Log received JSON for debugging (first 500 chars)
    DEBUG_LOG_API("Received JSON (first 500 chars): %.500s", jsonStr);
    if (total > 500) {
        DEBUG_LOG_API("... (total %u bytes, truncated for logging)", total);
    }

    // Parse and validate
    if (!m_config->fromJSON(jsonStr)) {
        DEBUG_LOG_API("Config update FAILED: %s", m_config->getLastError());
        DEBUG_LOG_API("Failed JSON: %s", jsonStr);  // Log full JSON on error
        freeRequestBuffer(request);
        sendError(request, 400, m_config->getLastError());
        return;
    }

    // Save to SPIFFS
    DEBUG_LOG_API("=== Saving Full Config via API ===");
    if (!m_config->save()) {
        DEBUG_LOG_API("Config update FAILED: Cannot save to filesystem");
        DEBUG_LOG_API("=== Full Config Save Failed ===");
        freeRequestBuffer(request);
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    DEBUG_LOG_API("=== Full Config Saved Successfully ===");

    freeRequestBuffer(request);

    // Apply log level from config to both loggers
    const ConfigManager::Config& cfg = m_config->getConfig();
    DebugLogger::LogLevel debugLevel = static_cast<DebugLogger::LogLevel>(cfg.logLevel);
    g_debugLogger.setLevel(debugLevel);

    // Also update regular Logger level
    Logger::LogLevel loggerLevel = static_cast<Logger::LogLevel>(cfg.logLevel);
    g_logger.setLevel(loggerLevel);

    DEBUG_LOG_API("Log level updated to %u from config", cfg.logLevel);

    // Return updated config
    char buffer[2048];
    m_config->toJSON(buffer, sizeof(buffer));
    sendJSON(request, 200, buffer);

    DEBUG_LOG_API("Config updated via API");
    DEBUG_LOG_API("Config updated successfully");
    g_debugLogger.logConfigDump();
}

void WebAPI::handleGetSensors(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> doc;

    // Get current config
    const ConfigManager::Config& config = m_config->getConfig();

    // Build sensors array
    JsonArray sensorsArray = doc.createNestedArray("sensors");
    for (int i = 0; i < 4; i++) {
        if (config.sensors[i].active) {
            JsonObject sensorObj = sensorsArray.createNestedObject();
            sensorObj["slot"] = i;
            sensorObj["name"] = config.sensors[i].name;
            sensorObj["type"] = config.sensors[i].type;
            sensorObj["enabled"] = config.sensors[i].enabled;
            sensorObj["primaryPin"] = config.sensors[i].primaryPin;
            sensorObj["secondaryPin"] = config.sensors[i].secondaryPin;
            sensorObj["isPrimary"] = config.sensors[i].isPrimary;
            sensorObj["detectionThreshold"] = config.sensors[i].detectionThreshold;
            sensorObj["maxDetectionDistance"] = config.sensors[i].maxDetectionDistance;
            sensorObj["debounceMs"] = config.sensors[i].debounceMs;
            sensorObj["warmupMs"] = config.sensors[i].warmupMs;
            sensorObj["enableDirectionDetection"] = config.sensors[i].enableDirectionDetection;
            sensorObj["directionTriggerMode"] = config.sensors[i].directionTriggerMode;
            sensorObj["directionSensitivity"] = config.sensors[i].directionSensitivity;
            sensorObj["sampleWindowSize"] = config.sensors[i].sampleWindowSize;
            sensorObj["sampleRateMs"] = config.sensors[i].sampleRateMs;

            // Add runtime error rate info if sensor manager is available
            if (m_sensorManager) {
                HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
                if (sensor) {
                    SensorType sensorType = sensor->getSensorType();

                    // Only ultrasonic sensors support error rate monitoring
                    if (sensorType == SENSOR_TYPE_ULTRASONIC) {
                        HAL_Ultrasonic* ultrasonicSensor = static_cast<HAL_Ultrasonic*>(sensor);
                        float errorRate = ultrasonicSensor->getErrorRate();
                        sensorObj["errorRate"] = errorRate;
                        sensorObj["errorRateAvailable"] = ultrasonicSensor->isErrorRateAvailable();
                        DEBUG_LOG_API("WebAPI: Sensor slot %d (HC-SR04) error rate: %.1f%% (available: %d)",
                                 i, errorRate, ultrasonicSensor->isErrorRateAvailable());
                    } else if (sensorType == SENSOR_TYPE_ULTRASONIC_GROVE) {
                        HAL_Ultrasonic_Grove* groveSensor = static_cast<HAL_Ultrasonic_Grove*>(sensor);
                        float errorRate = groveSensor->getErrorRate();
                        sensorObj["errorRate"] = errorRate;
                        sensorObj["errorRateAvailable"] = groveSensor->isErrorRateAvailable();
                        DEBUG_LOG_API("WebAPI: Sensor slot %d (Grove) error rate: %.1f%% (available: %d)",
                                 i, errorRate, groveSensor->isErrorRateAvailable());
                    }
                }
            }
        }
    }

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handlePostSensors(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    char* jsonStr = requestBuffer;
    jsonStr[total] = '\0';

    // Parse JSON sensor array
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        freeRequestBuffer(request);
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
        currentConfig.sensors[slot].directionTriggerMode = sensorObj["directionTriggerMode"] | 0;
        currentConfig.sensors[slot].directionSensitivity = sensorObj["directionSensitivity"] | 0;
        currentConfig.sensors[slot].sampleWindowSize = sensorObj["sampleWindowSize"] | 5;
        currentConfig.sensors[slot].sampleRateMs = sensorObj["sampleRateMs"] | 60;
        currentConfig.sensors[slot].maxDetectionDistance = sensorObj["maxDetectionDistance"] | 3000;
        currentConfig.sensors[slot].distanceZone = sensorObj["distanceZone"] | 0;
    }

    freeRequestBuffer(request);

    // Update and save config
    if (!m_config->setConfig(currentConfig)) {
        sendError(request, 400, m_config->getLastError());
        return;
    }

    // Auto-configure direction detector based on sensor distance zones
    m_config->autoConfigureDirectionDetector();

    // Log sensor configuration being saved via API (VERBOSE)
    DEBUG_LOG_API("=== Saving Sensor Config via API ===");
    for (int i = 0; i < 4; i++) {
        if (currentConfig.sensors[i].active) {
            DEBUG_LOG_API("Sensor[%d]: %s, type=%d, enabled=%s, directionMode=%u, threshold=%umm, maxDist=%umm",
                        i, currentConfig.sensors[i].name, currentConfig.sensors[i].type,
                        currentConfig.sensors[i].enabled ? "yes" : "no",
                        currentConfig.sensors[i].directionTriggerMode,
                        currentConfig.sensors[i].detectionThreshold,
                        currentConfig.sensors[i].maxDetectionDistance);
            DEBUG_LOG_API("  directionEnabled=%s, sensitivity=%u, window=%u, sampleRate=%ums",
                        currentConfig.sensors[i].enableDirectionDetection ? "yes" : "no",
                        currentConfig.sensors[i].directionSensitivity,
                        currentConfig.sensors[i].sampleWindowSize,
                        currentConfig.sensors[i].sampleRateMs);
        }
    }

    if (!m_config->save()) {
        sendError(request, 500, "Failed to save sensor configuration");
        DEBUG_LOG_API("=== Sensor Config Save Failed ===");
        return;
    }
    DEBUG_LOG_API("=== Sensor Config Saved Successfully ===");

    // Apply configuration changes to live sensors (if sensor manager available)
    if (m_sensorManager) {
        for (uint8_t i = 0; i < 4; i++) {
            HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
            if (sensor && currentConfig.sensors[i].active) {
                // Apply threshold changes
                if (currentConfig.sensors[i].detectionThreshold > 0) {
                    sensor->setDetectionThreshold(currentConfig.sensors[i].detectionThreshold);
                    DEBUG_LOG_API("Applied threshold %u mm to sensor %u",
                             currentConfig.sensors[i].detectionThreshold, i);
                }

                // Apply direction detection setting (base interface method)
                sensor->setDirectionDetection(currentConfig.sensors[i].enableDirectionDetection);
                DEBUG_LOG_API("Applied direction detection %s to sensor %u",
                         currentConfig.sensors[i].enableDirectionDetection ? "enabled" : "disabled", i);

                // Apply ultrasonic-specific settings (requires cast)
                SensorType sensorType = sensor->getSensorType();
                if (sensorType == SENSOR_TYPE_ULTRASONIC) {
                    HAL_Ultrasonic* ultrasonicSensor = static_cast<HAL_Ultrasonic*>(sensor);

                    // Apply direction trigger mode (0=approaching, 1=receding, 2=both)
                    ultrasonicSensor->setDirectionTriggerMode(currentConfig.sensors[i].directionTriggerMode);
                    DEBUG_LOG_API("Applied direction trigger mode %u to sensor %u",
                             currentConfig.sensors[i].directionTriggerMode, i);

                    // Apply direction sensitivity
                    if (currentConfig.sensors[i].directionSensitivity >= 0) {
                        ultrasonicSensor->setDirectionSensitivity(currentConfig.sensors[i].directionSensitivity);
                        DEBUG_LOG_API("Applied direction sensitivity %u mm to sensor %u",
                                 currentConfig.sensors[i].directionSensitivity, i);
                    }

                    // Apply sample rate interval
                    if (currentConfig.sensors[i].sampleRateMs > 0) {
                        ultrasonicSensor->setMeasurementInterval(currentConfig.sensors[i].sampleRateMs);
                        DEBUG_LOG_API("Applied sample interval %u ms to sensor %u",
                                 currentConfig.sensors[i].sampleRateMs, i);
                    }
                } else if (sensorType == SENSOR_TYPE_ULTRASONIC_GROVE) {
                    HAL_Ultrasonic_Grove* groveSensor = static_cast<HAL_Ultrasonic_Grove*>(sensor);

                    // Apply direction trigger mode (0=approaching, 1=receding, 2=both)
                    groveSensor->setDirectionTriggerMode(currentConfig.sensors[i].directionTriggerMode);
                    DEBUG_LOG_API("Applied direction trigger mode %u to sensor %u",
                             currentConfig.sensors[i].directionTriggerMode, i);

                    // Apply direction sensitivity
                    if (currentConfig.sensors[i].directionSensitivity >= 0) {
                        groveSensor->setDirectionSensitivity(currentConfig.sensors[i].directionSensitivity);
                        DEBUG_LOG_API("Applied direction sensitivity %u mm to sensor %u",
                                 currentConfig.sensors[i].directionSensitivity, i);
                    }

                    // Apply sample rate interval
                    if (currentConfig.sensors[i].sampleRateMs > 0) {
                        groveSensor->setMeasurementInterval(currentConfig.sensors[i].sampleRateMs);
                        DEBUG_LOG_API("Applied sample interval %u ms to sensor %u",
                                 currentConfig.sensors[i].sampleRateMs, i);
                    }
                }

                // Apply window size changes
                if (currentConfig.sensors[i].sampleWindowSize > 0) {
                    sensor->setSampleWindowSize(currentConfig.sensors[i].sampleWindowSize);
                    DEBUG_LOG_API("Applied window size %u to sensor %u",
                             currentConfig.sensors[i].sampleWindowSize, i);
                }

                // Apply distance range if sensor supports it
                const SensorCapabilities& caps = sensor->getCapabilities();
                if (caps.supportsDistanceMeasurement) {
                    uint32_t maxDist = currentConfig.sensors[i].maxDetectionDistance;
                    if (maxDist == 0) maxDist = caps.maxDetectionDistance;  // Use capability default if not set
                    sensor->setDistanceRange(caps.minDetectionDistance, maxDist);
                    DEBUG_LOG_API("Applied distance range %u-%u mm to sensor %u",
                             caps.minDetectionDistance, maxDist, i);
                }
            }
        }
        DEBUG_LOG_API("Sensor configuration applied to live sensors");
    }

    // Return updated sensor configuration
    char buffer[4096];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
    DEBUG_LOG_API("Sensor configuration saved successfully");
}

void WebAPI::handleCheckSensorErrorRate(AsyncWebServerRequest* request) {
    DEBUG_LOG_API("WebAPI: ========== handleGetSensorErrorRate() ENTERED ==========");
    DEBUG_LOG_API("WebAPI: Request URL: %s", request->url().c_str());
    DEBUG_LOG_API("WebAPI: Request Method: %s", request->methodToString());

    // Extract slot number from URL path parameter
    // URL format: /api/sensors/:slot/errorrate (e.g., /api/sensors/0/errorrate)
    String url = request->url();
    int slotStartIdx = url.indexOf("/sensors/") + 9;  // Length of "/sensors/"
    int slotEndIdx = url.indexOf("/", slotStartIdx);
    String slotStr = url.substring(slotStartIdx, slotEndIdx);
    uint8_t slot = slotStr.toInt();

    DEBUG_LOG_API("WebAPI: Error rate requested for sensor slot %d", slot);

    if (slot >= 4) {
        DEBUG_LOG_API("WebAPI: Invalid sensor slot %d (must be 0-3)", slot);
        sendError(request, 400, "Invalid sensor slot");
        return;
    }

    // Check if sensor manager is available
    if (!m_sensorManager) {
        DEBUG_LOG_API("WebAPI: Sensor manager not available");
        sendError(request, 500, "Sensor manager not available");
        return;
    }

    // Get sensor
    HAL_MotionSensor* sensor = m_sensorManager->getSensor(slot);
    if (!sensor) {
        DEBUG_LOG_API("WebAPI: No sensor found in slot %d", slot);
        sendError(request, 404, "Sensor not found in slot");
        return;
    }

    // Check if sensor supports error rate monitoring
    SensorType sensorType = sensor->getSensorType();
    const char* sensorTypeName = getSensorTypeName(sensorType);
    DEBUG_LOG_API("WebAPI: Sensor type in slot %d: %s (type=%d)", slot, sensorTypeName, sensorType);

    if (sensorType != SENSOR_TYPE_ULTRASONIC && sensorType != SENSOR_TYPE_ULTRASONIC_GROVE) {
        DEBUG_LOG_API("WebAPI: Sensor type %s does not support error rate monitoring", sensorTypeName);
        sendError(request, 400, "Sensor type does not support error rate monitoring");
        return;
    }

    // Get current error rate from rolling buffer
    float errorRate = -1.0f;

    if (sensorType == SENSOR_TYPE_ULTRASONIC) {
        HAL_Ultrasonic* ultrasonicSensor = static_cast<HAL_Ultrasonic*>(sensor);
        errorRate = ultrasonicSensor->getErrorRate();
        DEBUG_LOG_API("WebAPI: HC-SR04 sensor - errorRate=%.1f%%", errorRate);
    } else if (sensorType == SENSOR_TYPE_ULTRASONIC_GROVE) {
        HAL_Ultrasonic_Grove* groveSensor = static_cast<HAL_Ultrasonic_Grove*>(sensor);
        errorRate = groveSensor->getErrorRate();
        DEBUG_LOG_API("WebAPI: Grove sensor - errorRate=%.1f%%", errorRate);
    }

    DEBUG_LOG_API("WebAPI: Error rate for slot %d: %.1f%%", slot, errorRate);

    // Build response
    StaticJsonDocument<256> doc;
    doc["slot"] = slot;
    doc["errorRate"] = errorRate;

    if (errorRate < 0) {
        doc["message"] = "Error rate not yet available - collecting first 100 samples at boot";
        doc["status"] = "unavailable";
        DEBUG_LOG_API("WebAPI: Error rate unavailable (warmup phase)");
    } else if (errorRate < 5.0f) {
        doc["message"] = "Sensor health: Excellent";
        doc["status"] = "excellent";
        DEBUG_LOG_API("WebAPI: Sensor health: Excellent (%.1f%% error rate)", errorRate);
    } else if (errorRate < 15.0f) {
        doc["message"] = "Sensor health: Fair - consider checking wiring or environment";
        doc["status"] = "fair";
        DEBUG_LOG_API("WebAPI: Sensor health: Fair (%.1f%% error rate)", errorRate);
    } else {
        doc["message"] = "Sensor health: Poor - check wiring, power, and sensor orientation";
        doc["status"] = "poor";
        DEBUG_LOG_API("WebAPI: Sensor health: Poor (%.1f%% error rate)", errorRate);
    }

    String json;
    serializeJson(doc, json);
    DEBUG_LOG_API("WebAPI: Sending error rate response: %s", json.c_str());
    DEBUG_LOG_API("WebAPI: ========== handleGetSensorErrorRate() EXITING ==========");
    sendJSON(request, 200, json.c_str());
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

            // Add error rate if LED matrix is available and initialized
            if (m_ledMatrix && config.displays[i].type == 1) {  // Type 1 = 8x8 Matrix
                float errorRate = m_ledMatrix->getErrorRate();
                displayObj["errorRate"] = errorRate;
                displayObj["errorRateAvailable"] = m_ledMatrix->isErrorRateAvailable();
                displayObj["transactionCount"] = m_ledMatrix->getTransactionCount();
                DEBUG_LOG_API("WebAPI: Display slot %d (%s) error rate: %.1f%% (ready: %d, txCount: %u)",
                         i, config.displays[i].name, errorRate, m_ledMatrix->isReady(),
                         m_ledMatrix->getTransactionCount());
            } else {
                DEBUG_LOG_API("WebAPI: Display slot %d - no error rate available (ledMatrix: %d, type: %d)",
                          i, (m_ledMatrix != nullptr), config.displays[i].type);
            }
        }
    }

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
}

void WebAPI::handlePostDisplays(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    char* jsonStr = requestBuffer;
    jsonStr[total] = '\0';

    // Parse JSON display array
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        freeRequestBuffer(request);
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
        currentConfig.displays[slot].rotation = displayObj["rotation"] | MATRIX_ROTATION;
        currentConfig.displays[slot].useForStatus = displayObj["useForStatus"] | true;
    }

    freeRequestBuffer(request);

    // Update and save config
    if (!m_config->setConfig(currentConfig)) {
        sendError(request, 400, m_config->getLastError());
        return;
    }

    // Log display configuration being saved via API (VERBOSE)
    DEBUG_LOG_API("=== Saving Display Config via API ===");
    for (int i = 0; i < 2; i++) {
        if (currentConfig.displays[i].active) {
            DEBUG_LOG_API("Display[%d]: %s, type=%d, enabled=%s, i2c=0x%02X, brightness=%u",
                        i, currentConfig.displays[i].name, currentConfig.displays[i].type,
                        currentConfig.displays[i].enabled ? "yes" : "no",
                        currentConfig.displays[i].i2cAddress,
                        currentConfig.displays[i].brightness);
            DEBUG_LOG_API("  rotation=%u, useForStatus=%s, I2C pins: SDA=%u, SCL=%u",
                        currentConfig.displays[i].rotation,
                        currentConfig.displays[i].useForStatus ? "yes" : "no",
                        currentConfig.displays[i].sdaPin,
                        currentConfig.displays[i].sclPin);
        }
    }

    if (!m_config->save()) {
        sendError(request, 500, "Failed to save display configuration");
        DEBUG_LOG_API("=== Display Config Save Failed ===");
        return;
    }
    DEBUG_LOG_API("=== Display Config Saved Successfully ===");

    // Return updated display configuration
    char buffer[4096];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
    DEBUG_LOG_API("Display configuration saved successfully");
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
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    // Check if LED matrix is available
    if (!m_ledMatrix || !m_ledMatrix->isReady()) {
        freeRequestBuffer(request);
        sendError(request, 503, "LED Matrix not available");
        return;
    }

    requestBuffer[total] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, requestBuffer);

    if (error) {
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* name = doc["name"];
    uint32_t duration = doc["duration"] | 0;

    if (!name || strlen(name) == 0) {
        freeRequestBuffer(request);
        sendError(request, 400, "Animation name required");
        return;
    }

    // Play the animation
    bool started = m_ledMatrix->playCustomAnimation(name, duration);

    freeRequestBuffer(request);

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
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    // Check if LED matrix is available
    if (!m_ledMatrix) {
        DEBUG_LOG_API("WebAPI: Built-in animation request but LED Matrix pointer is null");
        freeRequestBuffer(request);
        sendError(request, 503, "LED Matrix not configured");
        return;
    }

    if (!m_ledMatrix->isReady()) {
        DEBUG_LOG_API("WebAPI: Built-in animation request but LED Matrix not ready");
        freeRequestBuffer(request);
        sendError(request, 503, "LED Matrix not initialized");
        return;
    }

    requestBuffer[total] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, requestBuffer);

    if (error) {
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* type = doc["type"];
    uint32_t duration = doc["duration"] | 0;

    if (!type || strlen(type) == 0) {
        freeRequestBuffer(request);
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
        freeRequestBuffer(request);
        sendError(request, 400, "Unknown animation type");
        return;
    }

    // Start the animation
    m_ledMatrix->startAnimation(pattern, duration);

    freeRequestBuffer(request);

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
    DEBUG_LOG_API("Template request received for URL: %s", request->url().c_str());

    if (!request->hasParam("type")) {
        DEBUG_LOG_API("Template request: No 'type' query parameter");
        sendError(request, 400, "Animation type required (use ?type=MOTION_ALERT)");
        return;
    }

    String animType = request->getParam("type")->value();
    DEBUG_LOG_API("Generating template for animation type: %s", animType.c_str());
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
    DEBUG_LOG_API("Sent animation template: %s", animType.c_str());
}

void WebAPI::handleAssignAnimation(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    requestBuffer[total] = '\0';

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, requestBuffer);

    if (error) {
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    const char* functionKey = doc["function"];
    const char* type = doc["type"];  // "builtin" or "custom"
    const char* animation = doc["animation"];

    if (!functionKey || !type || !animation) {
        freeRequestBuffer(request);
        sendError(request, 400, "Missing required fields");
        return;
    }

    // Store assignment in config
    // For now, we'll use a simple in-memory storage
    // TODO: Persist to ConfigManager

    freeRequestBuffer(request);

    StaticJsonDocument<256> response;
    response["success"] = true;
    response["function"] = functionKey;
    response["type"] = type;
    response["animation"] = animation;

    String json;
    serializeJson(response, json);
    sendJSON(request, 200, json.c_str());

    DEBUG_LOG_API("Assigned %s animation '%s' to function '%s'", type, animation, functionKey);
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
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    DEBUG_LOG_API("POST /api/mode - %u bytes", total);

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    requestBuffer[total] = '\0';

    // Parse JSON
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, requestBuffer);

    if (error) {
        DEBUG_LOG_API("Mode change FAILED: Invalid JSON");
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Get mode from request
    if (!doc.containsKey("mode")) {
        DEBUG_LOG_API("Mode change FAILED: Missing mode field");
        freeRequestBuffer(request);
        sendError(request, 400, "Missing 'mode' field");
        return;
    }

    int mode = doc["mode"];

    // Validate mode
    if (mode < StateMachine::OFF || mode > StateMachine::MOTION_DETECT) {
        DEBUG_LOG_API("Mode change FAILED: Invalid mode value %d", mode);
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid mode value");
        return;
    }

    // Set mode
    m_stateMachine->setMode((StateMachine::OperatingMode)mode);

    DEBUG_LOG_API("Mode changed to %s via API", StateMachine::getModeName((StateMachine::OperatingMode)mode));
    DEBUG_LOG_API("Mode changed to %s", StateMachine::getModeName((StateMachine::OperatingMode)mode));

    freeRequestBuffer(request);

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
    DEBUG_LOG_API("Factory reset requested via API");

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

    DEBUG_LOG_API("Factory reset complete");
}

void WebAPI::handlePostReboot(AsyncWebServerRequest* request) {
    DEBUG_LOG_API("Device reboot requested via API");
    DEBUG_LOG_API("Reboot request received");

    StaticJsonDocument<128> doc;
    doc["success"] = true;
    doc["message"] = "Device rebooting...";

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());

    DEBUG_LOG_API("Rebooting device in 1 second...");
    DEBUG_LOG_SYSTEM("Device reboot initiated via web API");
    g_debugLogger.flush();  // Flush logs before reboot

    // Delay to allow response to be sent, then reboot
    delay(1000);
    ESP.restart();
}

void WebAPI::handleGetVersion(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;

    doc["firmware"] = FIRMWARE_NAME;
    doc["version"] = FIRMWARE_VERSION;
    doc["buildNumber"] = BUILD_NUMBER;
    doc["buildDate"] = BUILD_DATE;
    doc["buildTime"] = BUILD_TIME;

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleOTAUpload(AsyncWebServerRequest* request, const String& filename,
                             size_t index, uint8_t* data, size_t len, bool final) {
    // Check if OTA manager is available
    if (!m_otaManager) {
        if (final) {
            sendError(request, 503, "OTA Manager not available");
        }
        return;
    }

    // First chunk - start upload
    if (index == 0) {
        size_t totalSize = request->contentLength();
        LOG_INFO("OTA: Starting firmware upload - size: %u bytes", totalSize);

        if (!m_otaManager->handleUploadStart(totalSize)) {
            OTAManager::Status status = m_otaManager->getStatus();
            LOG_ERROR("OTA: Upload start failed - %s", status.errorMessage);
            if (final) {
                sendError(request, 400, status.errorMessage);
            }
            return;
        }
    }

    // Write chunk
    if (len > 0) {
        if (!m_otaManager->handleUploadChunk(data, len)) {
            OTAManager::Status status = m_otaManager->getStatus();
            LOG_ERROR("OTA: Chunk write failed - %s", status.errorMessage);
            if (final) {
                sendError(request, 500, status.errorMessage);
            }
            return;
        }
    }

    // Final chunk - complete upload
    if (final) {
        if (m_otaManager->handleUploadComplete()) {
            LOG_INFO("OTA: Firmware upload completed successfully");

            StaticJsonDocument<256> doc;
            doc["success"] = true;
            doc["message"] = "Firmware uploaded successfully. Rebooting...";
            doc["bytesWritten"] = m_otaManager->getStatus().bytesWritten;

            String json;
            serializeJson(doc, json);
            sendJSON(request, 200, json.c_str());

            // Reboot after a short delay to allow response to be sent
            LOG_INFO("OTA: Rebooting in 2 seconds to apply new firmware...");
            delay(2000);
            ESP.restart();
        } else {
            OTAManager::Status status = m_otaManager->getStatus();
            LOG_ERROR("OTA: Upload completion failed - %s", status.errorMessage);
            sendError(request, 500, status.errorMessage);
        }
    }
}

void WebAPI::handleGetOTAStatus(AsyncWebServerRequest* request) {
    if (!m_otaManager) {
        sendError(request, 503, "OTA Manager not available");
        return;
    }

    OTAManager::Status status = m_otaManager->getStatus();
    StaticJsonDocument<512> doc;

    doc["inProgress"] = status.inProgress;
    doc["bytesWritten"] = status.bytesWritten;
    doc["totalSize"] = status.totalSize;
    doc["progressPercent"] = status.progressPercent;
    doc["errorMessage"] = status.errorMessage;
    doc["maxFirmwareSize"] = m_otaManager->getMaxFirmwareSize();
    doc["currentPartition"] = m_otaManager->getCurrentPartition();
    doc["currentVersion"] = String(FIRMWARE_VERSION) + " (build " + String(BUILD_NUMBER) + ")";

    String json;
    serializeJson(doc, json);

    sendJSON(request, 200, json.c_str());
}

void WebAPI::handleGetCoredump(AsyncWebServerRequest* request) {
#if !MOCK_HARDWARE
    LOG_INFO("Coredump download requested");

    // Check if coredump exists
    esp_err_t err = esp_core_dump_image_check();
    if (err != ESP_OK) {
        LOG_WARN("No coredump found or coredump corrupted (error: %d)", err);
        sendError(request, 404, "No core dump available");
        return;
    }

    // Get coredump address and size
    size_t coredump_addr = 0;
    size_t coredump_size = 0;
    err = esp_core_dump_image_get(&coredump_addr, &coredump_size);
    if (err != ESP_OK || coredump_size == 0) {
        LOG_ERROR("Failed to get coredump info (error: %d)", err);
        sendError(request, 500, "Failed to read core dump info");
        return;
    }

    LOG_INFO("Coredump found: addr=0x%x, size=%u bytes", coredump_addr, coredump_size);

    // Find coredump partition to read data
    const esp_partition_t* coredump_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
        NULL
    );

    if (coredump_partition == NULL) {
        LOG_ERROR("Coredump partition not found");
        sendError(request, 500, "Core dump partition not found");
        return;
    }

    // Allocate buffer for coredump
    uint8_t* buffer = (uint8_t*)malloc(coredump_size);
    if (!buffer) {
        LOG_ERROR("Failed to allocate %u bytes for coredump", coredump_size);
        sendError(request, 500, "Insufficient memory");
        return;
    }

    // Read coredump data from partition
    err = esp_partition_read(coredump_partition, 0, buffer, coredump_size);
    if (err != ESP_OK) {
        LOG_ERROR("Failed to read coredump data (error: %d)", err);
        free(buffer);
        sendError(request, 500, "Failed to read core dump data");
        return;
    }

    LOG_INFO("Coredump read successfully, sending to client...");

    // Send coredump as binary download
    AsyncWebServerResponse* response = request->beginResponse(
        "application/octet-stream",
        coredump_size,
        [buffer, coredump_size](uint8_t *dest, size_t maxLen, size_t index) -> size_t {
            if (index >= coredump_size) {
                free((void*)buffer);  // Free when done
                return 0;
            }

            size_t remaining = coredump_size - index;
            size_t toSend = (remaining < maxLen) ? remaining : maxLen;
            memcpy(dest, buffer + index, toSend);
            return toSend;
        }
    );

    response->addHeader("Content-Disposition", "attachment; filename=coredump.elf");
    request->send(response);

    LOG_INFO("Coredump download started");
#else
    sendError(request, 501, "Coredump not available in mock mode");
#endif
}

void WebAPI::handleClearCoredump(AsyncWebServerRequest* request) {
#if !MOCK_HARDWARE
    LOG_INFO("Coredump clear requested");

    // Find coredump partition
    const esp_partition_t* coredump_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
        NULL
    );

    if (coredump_partition == NULL) {
        LOG_ERROR("Coredump partition not found");
        sendError(request, 404, "Core dump partition not found");
        return;
    }

    // Erase coredump partition
    esp_err_t err = esp_partition_erase_range(coredump_partition, 0, coredump_partition->size);
    if (err != ESP_OK) {
        LOG_ERROR("Failed to erase coredump partition (error: %d)", err);
        sendError(request, 500, "Failed to clear core dump");
        return;
    }

    LOG_INFO("Coredump partition cleared successfully");

    StaticJsonDocument<128> doc;
    doc["success"] = true;
    doc["message"] = "Core dump cleared";

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());
#else
    sendError(request, 501, "Coredump not available in mock mode");
#endif
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
    unsigned long now = millis();

    // Check if a response is in progress (with 3 second timeout for safety)
    if (g_htmlResponseInProgress && (now - g_lastHTMLBuildTime) < 3000) {
        LOG_WARN("Dashboard request rejected - response in progress");
        request->send(503, "text/plain", "Dashboard busy, please retry");
        return;
    }

    // Mark response as in progress
    g_htmlResponseInProgress = true;
    g_lastHTMLBuildTime = now;

    // Build HTML only if not already cached
    // Note: We use inline HTML instead of filesystem-based UI because:
    // 1. All features are implemented (multi-sensor, LED matrix, animations)
    // 2. Simpler deployment (no filesystem upload step)
    // 3. No LittleFS mount/filesystem issues
    // 4. For single-developer embedded projects, reflashing is acceptable
    if (g_htmlResponseBuffer.length() == 0 || !g_htmlResponseBuffer.endsWith("</html>")) {
        LOG_INFO("Building dashboard HTML...");
        g_htmlResponseBuffer = buildDashboardHTML();

        // Verify build succeeded
        if (g_htmlResponseBuffer.length() == 0 || !g_htmlResponseBuffer.endsWith("</html>")) {
            LOG_ERROR("Dashboard HTML build failed - truncated or empty");
            g_htmlResponseInProgress = false;
            request->send(500, "text/plain", "Dashboard build failed - low memory");
            return;
        }

        LOG_INFO("Dashboard HTML built: %u bytes, free heap: %u",
                 g_htmlResponseBuffer.length(), ESP.getFreeHeap());
    } else {
        LOG_DEBUG("Using cached dashboard HTML (%u bytes)", g_htmlResponseBuffer.length());
    }

    size_t len = g_htmlResponseBuffer.length();
    const char* htmlPtr = g_htmlResponseBuffer.c_str();

    // Send using chunked response callback (reads directly from static buffer)
    AsyncWebServerResponse *response = request->beginResponse(
        "text/html", len,
        [htmlPtr, len](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= len) {
                g_htmlResponseInProgress = false;  // Mark as complete
                return 0;
            }

            size_t remaining = len - index;
            size_t toSend = (remaining < maxLen) ? remaining : maxLen;
            memcpy(buffer, htmlPtr + index, toSend);

            return toSend;
        }
    );

    response->addHeader("Cache-Control", "no-cache");
    request->send(response);

    LOG_INFO("Dashboard HTML response started (chunked, %u bytes)", len);
}

void WebAPI::handleLiveLogs(AsyncWebServerRequest* request) {
    // Live logs viewer in popup window with filtering
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>";
    html += "<title>StepAware Live Logs</title><style>";
    html += "body{font-family:monospace;margin:0;padding:10px;background:#1e1e1e;color:#d4d4d4;}";
    html += "h1{font-size:16px;margin:0 0 10px 0;color:#fff;}";
    html += "#status{padding:5px;background:#333;border-radius:3px;margin-bottom:5px;font-size:12px;}";
    html += ".badge{display:inline-block;padding:2px 6px;border-radius:3px;font-size:11px;font-weight:bold;}";
    html += ".badge-success{background:#28a745;color:white;}";
    html += ".badge-error{background:#dc3545;color:white;}";
    html += ".badge-warning{background:#ffa500;color:white;}";

    // Filter controls styling
    html += ".filters{padding:8px;background:#2a2a2a;border-radius:3px;margin-bottom:5px;font-size:11px;}";
    html += ".filter-btn{display:inline-block;padding:4px 8px;margin:2px;border:1px solid #555;background:#333;";
    html += "color:#aaa;border-radius:3px;cursor:pointer;font-size:11px;font-weight:bold;}";
    html += ".filter-btn.active{border-color:#4CAF50;background:#4CAF50;color:#fff;}";
    html += ".filter-btn:hover{background:#444;}";
    html += ".filter-btn.active:hover{background:#45a049;}";
    html += "label{margin-left:10px;font-size:11px;}";
    html += "input[type='checkbox']{margin-right:3px;}";

    html += "#logs{height:calc(100vh - 140px);overflow-y:auto;background:#000;padding:10px;border:1px solid #444;}";
    html += ".log-entry{padding:2px 0;font-size:12px;line-height:1.4;}";
    html += ".log-entry.hidden{display:none;}";
    html += ".log-verbose{color:#808080;}.log-debug{color:#00d4ff;}.log-info{color:#d4d4d4;}";
    html += ".log-warn{color:#ffa500;}.log-error{color:#ff4444;font-weight:bold;}";
    html += "</style></head><body>";

    html += "<h1>&#128680; StepAware Live Logs</h1>";
    html += "<div id='status'><span class='badge badge-error' id='ws-badge'>Connecting...</span> ";
    html += "<span id='count'>0 logs</span></div>";

    // Filter controls
    html += "<div class='filters'>";
    html += "<span style='margin-right:8px;'>Level:</span>";
    html += "<button class='filter-btn active' data-level='0' onclick='toggleLevel(this)'>VERBOSE</button>";
    html += "<button class='filter-btn active' data-level='1' onclick='toggleLevel(this)'>DEBUG</button>";
    html += "<button class='filter-btn active' data-level='2' onclick='toggleLevel(this)'>INFO</button>";
    html += "<button class='filter-btn active' data-level='3' onclick='toggleLevel(this)'>WARN</button>";
    html += "<button class='filter-btn active' data-level='4' onclick='toggleLevel(this)'>ERROR</button>";
    html += "<label><input type='checkbox' id='auto-scroll' checked> Auto-scroll</label>";
    html += "</div>";

    html += "<div id='logs'></div>";

    // JavaScript
    html += "<script>";
    html += "let ws,count=0,autoScroll=true;";
    html += "let activeFilters=new Set([0,1,2,3,4]);";

    // Toggle level filter
    html += "function toggleLevel(btn){";
    html += "const level=parseInt(btn.dataset.level);";
    html += "if(activeFilters.has(level)){";
    html += "activeFilters.delete(level);btn.classList.remove('active');}else{";
    html += "activeFilters.add(level);btn.classList.add('active');}";
    html += "document.querySelectorAll('.log-entry').forEach(e=>{";
    html += "const lvl=parseInt(e.dataset.level);";
    html += "if(activeFilters.has(lvl))e.classList.remove('hidden');";
    html += "else e.classList.add('hidden');});";
    html += "}";

    // Auto-scroll handler
    html += "document.getElementById('auto-scroll').addEventListener('change',e=>{";
    html += "autoScroll=e.target.checked;});";

    // WebSocket connection
    html += "function connect(){";
    html += "const proto=location.protocol==='https:'?'wss:':'ws:';";
    html += "ws=new WebSocket(proto+'//'+location.host+'/ws/logs');";
    html += "ws.onopen=()=>{document.getElementById('ws-badge').textContent='Connected';";
    html += "document.getElementById('ws-badge').className='badge badge-success';};";
    html += "ws.onmessage=(e)=>{";
    html += "const data=JSON.parse(e.data);";
    html += "const div=document.createElement('div');";
    html += "div.className='log-entry log-'+data.levelName.toLowerCase().trim();";
    html += "div.dataset.level=data.level;";
    html += "div.dataset.source=data.source||'logger';";
    html += "const ms=data.ts;const s=Math.floor(ms/1000);const m=Math.floor(s/60);const h=Math.floor(m/60);";
    html += "const ts=String(h).padStart(2,'0')+':'+String(m%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0')+'.'+String(ms%1000).padStart(3,'0');";
    html += "div.textContent='['+ts+'] ['+data.levelName+'] '+data.msg;";

    // Apply filters to new entry
    html += "if(!activeFilters.has(parseInt(data.level)))div.classList.add('hidden');";

    html += "document.getElementById('logs').appendChild(div);";
    html += "count++;document.getElementById('count').textContent=count+' logs';";
    html += "if(autoScroll)div.scrollIntoView();};";
    html += "ws.onerror=()=>{document.getElementById('ws-badge').textContent='Error';";
    html += "document.getElementById('ws-badge').className='badge badge-error';};";
    html += "ws.onclose=()=>{document.getElementById('ws-badge').textContent='Disconnected';";
    html += "document.getElementById('ws-badge').className='badge badge-error';setTimeout(connect,3000);};";
    html += "}";
    html += "connect();";
    html += "</script></body></html>";

    request->send(200, "text/html", html);
}

String WebAPI::buildDashboardHTML() {
    LOG_DEBUG("buildDashboardHTML() starting, free heap: %u", ESP.getFreeHeap());
    String html;
    // Reserve 60KB - conservative to avoid allocation failures on ESP32-C3
    // Actual HTML is ~65KB, but String will auto-grow if needed
    html.reserve(61440);
    LOG_DEBUG("Reserved 60KB for HTML buffer");

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

    // Logs - Real-time streaming viewer
    html += ".log-viewer{font-family:'Courier New',monospace;font-size:12px;background:#1e1e1e;color:#d4d4d4;";
    html += "padding:10px;height:400px;overflow-y:auto;border:1px solid #444;border-radius:4px;margin-top:10px;}";
    html += ".log-entry{padding:2px 0;white-space:pre-wrap;word-wrap:break-word;font-size:11px;}";
    html += ".log-entry.hidden{display:none;}";
    html += ".log-verbose{color:#808080;}";
    html += ".log-debug{color:#00d4ff;}";
    html += ".log-info{color:#d4d4d4;}";
    html += ".log-warn{color:#ffa500;}";
    html += ".log-error{color:#ff4444;font-weight:bold;}";
    html += ".log-header{margin-bottom:10px;display:flex;align-items:center;}";
    html += ".log-controls{display:flex;align-items:center;margin-bottom:10px;}";
    html += ".log-filter-buttons{display:inline-flex;gap:4px;}";
    html += ".filter-btn{padding:4px 8px;border:1px solid #666;background:#333;color:#fff;cursor:pointer;";
    html += "font-size:11px;border-radius:3px;}";
    html += ".filter-btn.active{background:#007bff;border-color:#0056b3;}";
    html += ".filter-btn:hover{background:#555;}";
    html += ".filter-btn.active:hover{background:#0056b3;}";

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
    html += "<button class=\"tab\" onclick=\"window.open('/live-logs','logs','width=900,height=600')\">Live Logs</button>";
    html += "<button class=\"tab\" onclick=\"showTab('firmware')\">Firmware</button>";
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

    html += "<div class=\"card\"><h2>System</h2>";
    html += "<div style=\"display:flex;justify-content:center;\">";
    html += "<button class=\"btn btn-danger\" onclick=\"rebootDevice()\" style=\"width:50%;max-width:200px;font-weight:600;\">Reboot Device</button>";
    html += "</div>";
    html += "<p class=\"form-help\" style=\"margin-top:8px;margin-bottom:0;text-align:center;\">Restart the ESP32 device. The web interface will be unavailable for ~10 seconds.</p>";
    html += "</div>";

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

    html += "<h3>Direction Detection (Dual-PIR)</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Simultaneous Threshold (ms)</label>";
    html += "<input type=\"number\" id=\"cfg-dirSimultaneousThreshold\" class=\"form-input\" min=\"50\" max=\"2000\">";
    html += "<div class=\"form-help\">If both sensors trigger within this time, it's considered simultaneous (not directional). Lower = more sensitive to direction. Typical: 100-200ms</div></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Confirmation Window (ms)</label>";
    html += "<input type=\"number\" id=\"cfg-dirConfirmationWindow\" class=\"form-input\" min=\"1000\" max=\"10000\">";
    html += "<div class=\"form-help\">Maximum time between sensor triggers to detect direction. Higher = works for slower movement. Typical: 3000-5000ms</div></div>";
    html += "</div>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Pattern Timeout (ms)</label>";
    html += "<input type=\"number\" id=\"cfg-dirPatternTimeout\" class=\"form-input\" min=\"5000\" max=\"30000\">";
    html += "<div class=\"form-help\">How long to wait for pattern completion before resetting. Typical: 10000ms</div></div>";
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
    html += "<option value=\"0\">VERBOSE</option>";
    html += "<option value=\"1\">DEBUG</option>";
    html += "<option value=\"2\">INFO</option>";
    html += "<option value=\"3\">WARN</option>";
    html += "<option value=\"4\">ERROR</option>";
    html += "<option value=\"5\">NONE</option>";
    html += "</select>";
    html += "<div class=\"form-help\">Higher levels log less detail. VERBOSE logs everything (battery intensive)</div></div>";
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

    // Real-time System Logs Card
    html += "<div class=\"card\"><h2>System Logs</h2>";
    html += "<div class=\"log-header\">";
    html += "<span id=\"ws-status\" class=\"badge badge-info\">Connecting...</span>";
    html += "<label style=\"margin-left:10px;\">";
    html += "<input type=\"checkbox\" id=\"auto-scroll\" checked> Auto-scroll";
    html += "</label>";
    html += "</div>";
    html += "<div class=\"log-controls\">";
    html += "<input type=\"text\" id=\"log-search\" placeholder=\"Search logs...\" ";
    html += "onkeyup=\"filterLogs()\" style=\"width:200px;margin-right:10px;\">";
    html += "<div class=\"log-filter-buttons\">";
    html += "<button class=\"filter-btn active\" data-level=\"0\" onclick=\"toggleLevelFilter(this)\">VERBOSE</button>";
    html += "<button class=\"filter-btn active\" data-level=\"1\" onclick=\"toggleLevelFilter(this)\">DEBUG</button>";
    html += "<button class=\"filter-btn active\" data-level=\"2\" onclick=\"toggleLevelFilter(this)\">INFO</button>";
    html += "<button class=\"filter-btn active\" data-level=\"3\" onclick=\"toggleLevelFilter(this)\">WARN</button>";
    html += "<button class=\"filter-btn active\" data-level=\"4\" onclick=\"toggleLevelFilter(this)\">ERROR</button>";
    html += "</div>";
    html += "<button class=\"btn btn-sm\" onclick=\"clearLogViewer()\" style=\"margin-left:10px;\">Clear Display</button>";
    html += "</div>";
    html += "<div id=\"log-viewer\" class=\"log-viewer\"></div>";
    html += "</div>";

    // Boot Information Card
    html += "<div class=\"card\"><h2>Boot Information</h2>";
    html += "<div id=\"bootInfo\">";
    html += "<p><strong>Boot Cycle:</strong> <span id=\"bootCycle\">-</span></p>";
    html += "<p><strong>Firmware:</strong> <span id=\"firmware\">-</span></p>";
    html += "<p><strong>Free Heap:</strong> <span id=\"freeHeap\">-</span> bytes</p>";
    html += "<p><strong>Filesystem Usage:</strong> <span id=\"fsUsage\">-</span>%</p>";
    html += "<p><strong>Total Logs Size:</strong> <span id=\"logsSize\">-</span> bytes</p>";
    html += "<p><strong>Current Log Level:</strong> <span id=\"currentLogLevel\" style=\"font-weight:600;\">-</span></p>";
    html += "</div></div>";

    // Debug Logs (LittleFS) Card
    html += "<div class=\"card\"><h2>Debug Logs (LittleFS)</h2>";
    html += "<div style=\"margin-bottom:10px;\">";
    html += "<label for=\"logSelect\" style=\"display:block;font-weight:600;margin-bottom:4px;\">Select Log File:</label>";
    html += "<select id=\"logSelect\" style=\"width:100%;padding:8px;margin:5px 0;border:1px solid #cbd5e1;border-radius:4px;\">";
    html += "<option value=\"\">-- Select a log file --</option>";
    html += "</select>";
    html += "</div>";
    html += "<div id=\"logActions\" style=\"display:none;margin-bottom:10px;\">";
    html += "<button class=\"btn btn-primary btn-small\" onclick=\"downloadLog()\">Download</button>";
    html += "<button class=\"btn btn-danger btn-small\" style=\"margin-left:8px;\" onclick=\"eraseLog()\">Erase</button>";
    html += "<button class=\"btn btn-danger btn-small\" style=\"margin-left:16px;\" onclick=\"eraseAllLogs()\">Erase All Logs</button>";
    html += "</div>";
    html += "<div id=\"logInfo\" style=\"display:none;padding:12px;background:#f5f5f5;border-radius:6px;\">";
    html += "<p><strong>File:</strong> <span id=\"selectedLogName\">-</span></p>";
    html += "<p><strong>Size:</strong> <span id=\"selectedLogSize\">-</span> bytes (<span id=\"selectedLogSizeKB\">-</span> KB)</p>";
    html += "<p><strong>Path:</strong> <span id=\"selectedLogPath\">-</span></p>";
    html += "</div></div></div>";

    // FIRMWARE TAB
    html += "<div id=\"firmware-tab\" class=\"tab-content\">";
    html += "<div class=\"card\"><h2>Firmware Update (OTA)</h2>";

    html += "<div class=\"form-group\">";
    html += "<label class=\"form-label\">Current Version:</label>";
    html += "<div id=\"current-version\" style=\"font-weight:600;font-size:1.1em;color:#333;margin-bottom:4px;\">Loading...</div>";
    html += "<div id=\"current-partition\" style=\"font-size:0.9em;color:#6b7280;\">Partition: --</div>";
    html += "<div id=\"max-firmware-size\" style=\"font-size:0.9em;color:#6b7280;margin-top:4px;\">Max size: --</div>";
    html += "</div>";

    html += "<div class=\"form-group\" style=\"margin-top:24px;\">";
    html += "<label class=\"form-label\">Upload New Firmware (.bin):</label>";
    html += "<input type=\"file\" id=\"firmware-file\" accept=\".bin\" onchange=\"validateFirmware()\" class=\"form-input\" style=\"padding:8px;\">";
    html += "<div class=\"form-help\">Select ESP32-C3 firmware binary file</div>";
    html += "</div>";

    html += "<div id=\"upload-progress\" style=\"display:none;margin-top:16px;\">";
    html += "<progress id=\"upload-bar\" max=\"100\" value=\"0\" style=\"width:100%;height:30px;\"></progress>";
    html += "<div id=\"upload-status\" style=\"text-align:center;margin-top:8px;font-weight:600;color:#667eea;\">Uploading... 0%</div>";
    html += "</div>";

    html += "<button id=\"upload-btn\" class=\"btn btn-warning\" onclick=\"uploadFirmware()\" disabled style=\"margin-top:16px;width:100%;max-width:300px;\">";
    html += "Upload Firmware";
    html += "</button>";

    html += "<div style=\"margin-top:24px;padding:16px;background:#fef3c7;border-left:4px solid #f59e0b;border-radius:6px;\">";
    html += "<div style=\"font-weight:600;color:#92400e;margin-bottom:8px;\">&#9888; Warning</div>";
    html += "<ul style=\"margin:0;padding-left:20px;color:#92400e;font-size:0.9em;\">";
    html += "<li>Device will reboot automatically after upload</li>";
    html += "<li>Ensure stable power supply during update</li>";
    html += "<li>Do not interrupt the upload process</li>";
    html += "<li>Web interface will be unavailable for ~30 seconds</li>";
    html += "</ul>";
    html += "</div>";

    html += "</div></div>"; // End firmware tab

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
    html += "if(tab==='logs'){loadAvailableLogs();}";
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

    // Reboot device
    html += "async function rebootDevice(){";
    html += "if(!confirm('Are you sure you want to reboot the device?\\n\\nThe device will restart and the web interface will be unavailable for ~10 seconds.'))return;";
    html += "alert('Rebooting device now...\\n\\nThe page will reload automatically in 15 seconds.');";
    html += "setTimeout(()=>location.reload(),15000);";
    html += "try{";
    html += "await fetch('/api/reboot',{method:'POST'});";
    html += "}catch(e){}}"; // Connection lost during reboot is expected

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
    html += "document.getElementById('cfg-logLevel').value=(cfg.logging?.level!==undefined)?cfg.logging.level:2;";
    html += "document.getElementById('cfg-powerSaving').value=cfg.power?.savingEnabled?1:0;";
    html += "document.getElementById('cfg-dirSimultaneousThreshold').value=cfg.directionDetector?.simultaneousThresholdMs||150;";
    html += "document.getElementById('cfg-dirConfirmationWindow').value=cfg.directionDetector?.confirmationWindowMs||5000;";
    html += "document.getElementById('cfg-dirPatternTimeout').value=cfg.directionDetector?.patternTimeoutMs||10000;";
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
    html += "cfg.directionDetector=cfg.directionDetector||{};";
    html += "cfg.directionDetector.simultaneousThresholdMs=parseInt(document.getElementById('cfg-dirSimultaneousThreshold').value);";
    html += "cfg.directionDetector.confirmationWindowMs=parseInt(document.getElementById('cfg-dirConfirmationWindow').value);";
    html += "cfg.directionDetector.patternTimeoutMs=parseInt(document.getElementById('cfg-dirPatternTimeout').value);";
    html += "let jsonStr;";
    html += "try{jsonStr=JSON.stringify(cfg);console.log('Saving config:',JSON.stringify(cfg,null,2));}";
    html += "catch(e){console.error('JSON.stringify failed:',e);alert('Failed to serialize config: '+e.message);return;}";
    html += "try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:jsonStr});if(res.ok){";
    html += "document.getElementById('save-indicator').classList.add('show');";
    html += "setTimeout(()=>document.getElementById('save-indicator').classList.remove('show'),3000);";
    html += "loadConfig();}else{";
    html += "const errorText=await res.text();";
    html += "console.error('Config save failed:',res.status,errorText);";
    html += "alert('Failed to save configuration: '+errorText);}}catch(e){";
    html += "console.error('Config save error:',e);alert('Error: '+e.message);}}";

    // LittleFS Log Management
    html += "let availableLogs=[];";
    html += "let selectedLog=null;";

    // Load available logs on page load
    html += "async function loadAvailableLogs(){";
    html += "try{";
    html += "const res=await fetch('/api/debug/logs');";
    html += "const data=await res.json();";

    // Update boot info
    html += "document.getElementById('bootCycle').textContent=data.bootCycle||'-';";
    html += "document.getElementById('firmware').textContent=data.firmware||'-';";
    html += "document.getElementById('freeHeap').textContent=data.freeHeap?data.freeHeap.toLocaleString():'-';";
    html += "document.getElementById('fsUsage').textContent=data.filesystemUsage||'-';";
    html += "document.getElementById('logsSize').textContent=data.totalLogsSize?data.totalLogsSize.toLocaleString():'-';";

    // Fetch and display current log level
    html += "try{const cfgRes=await fetch('/api/debug/config');";
    html += "if(cfgRes.ok){const cfgData=await cfgRes.json();";
    html += "const levelEl=document.getElementById('currentLogLevel');";
    html += "levelEl.textContent=cfgData.level||'-';";
    html += "levelEl.style.color=(cfgData.level==='ERROR'||cfgData.level==='NONE')?'#dc2626':";
    html += "(cfgData.level==='WARN')?'#ea580c':(cfgData.level==='INFO')?'#0891b2':";
    html += "(cfgData.level==='DEBUG')?'#7c3aed':'#6366f1';}}";
    html += "catch(e){console.error('Failed to fetch log level:',e);}";

    // Populate log dropdown
    html += "availableLogs=data.logs||[];";
    html += "const select=document.getElementById('logSelect');";
    html += "select.innerHTML='<option value=\"\">-- Select a log file --</option>';";
    html += "availableLogs.forEach((log,index)=>{";
    html += "const option=document.createElement('option');";
    html += "option.value=index;";
    html += "const sizeKB=(log.size/1024).toFixed(1);";
    html += "option.textContent=log.name+' ('+sizeKB+' KB)';";
    html += "select.appendChild(option);";
    html += "});";
    html += "}catch(err){console.error('Failed to load logs:',err);}}";

    // Handle log selection
    html += "function onLogSelect(){";
    html += "const select=document.getElementById('logSelect');";
    html += "const index=parseInt(select.value);";
    html += "if(isNaN(index)){";
    html += "document.getElementById('logActions').style.display='none';";
    html += "document.getElementById('logInfo').style.display='none';";
    html += "selectedLog=null;";
    html += "return;}";
    html += "selectedLog=availableLogs[index];";
    html += "document.getElementById('selectedLogName').textContent=selectedLog.name;";
    html += "document.getElementById('selectedLogSize').textContent=selectedLog.size.toLocaleString();";
    html += "document.getElementById('selectedLogSizeKB').textContent=(selectedLog.size/1024).toFixed(1);";
    html += "document.getElementById('selectedLogPath').textContent=selectedLog.path;";
    html += "document.getElementById('logActions').style.display='block';";
    html += "document.getElementById('logInfo').style.display='block';}";

    // Download selected log
    html += "function downloadLog(){";
    html += "if(!selectedLog)return;";
    html += "const url='/api/debug/logs/'+selectedLog.name;";
    html += "const a=document.createElement('a');";
    html += "a.href=url;";
    html += "a.download='stepaware_'+selectedLog.name+'.log';";
    html += "document.body.appendChild(a);";
    html += "a.click();";
    html += "document.body.removeChild(a);}";

    // Erase selected log
    html += "async function eraseLog(){";
    html += "if(!selectedLog)return;";
    html += "if(!confirm('Are you sure you want to erase '+selectedLog.name+'?'))return;";
    html += "try{";
    html += "const res=await fetch('/api/debug/logs/'+selectedLog.name,{method:'DELETE'});";
    html += "if(res.ok){";
    html += "alert('Log erased successfully');";
    html += "loadAvailableLogs();";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Failed to erase log: '+err);}}";
    html += "catch(err){console.error('Error erasing log:',err);alert('Error erasing log');}}";

    // Erase all logs
    html += "async function eraseAllLogs(){";
    html += "if(!confirm('Are you sure you want to erase ALL logs? This cannot be undone!'))return;";
    html += "try{";
    html += "const res=await fetch('/api/debug/logs/clear',{method:'POST'});";
    html += "if(res.ok){";
    html += "alert('All logs erased successfully');";
    html += "loadAvailableLogs();";
    html += "}else{";
    html += "const err=await res.text();";
    html += "alert('Failed to erase logs: '+err);}}";
    html += "catch(err){console.error('Error erasing logs:',err);alert('Error erasing logs');}}";

    // Add event listener to select
    html += "document.addEventListener('DOMContentLoaded',()=>{";
    html += "const select=document.getElementById('logSelect');";
    html += "if(select)select.addEventListener('change',onLogSelect);";
    html += "});";
    // Hardware Tab - Sensor Management
    html += "let sensorSlots=[null,null,null,null];";
    html += "const SENSOR_TYPES={PIR:{name:'PIR Motion',pins:1,config:['warmup','debounce']},";
    html += "IR:{name:'IR Beam-Break',pins:1,config:['debounce']},";
    html += "ULTRASONIC:{name:'Ultrasonic (HC-SR04)',pins:2,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']},";
    html += "ULTRASONIC_GROVE:{name:'Ultrasonic (Grove)',pins:1,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}};";

    // Load sensors from configuration
    html += "async function loadSensors(){";
    html += "try{";
    html += "const cfgRes=await fetch('/api/config');if(!cfgRes.ok)return;";
    html += "const cfg=await cfgRes.json();";
    html += "sensorSlots=[null,null,null,null];";
    html += "if(cfg.sensors&&Array.isArray(cfg.sensors)){";
    html += "cfg.sensors.forEach(s=>{if(s.slot>=0&&s.slot<4){sensorSlots[s.slot]=s;}});}";
    html += "const statusRes=await fetch('/api/sensors');";
    html += "if(statusRes.ok){";
    html += "const status=await statusRes.json();";
    html += "if(status.sensors&&Array.isArray(status.sensors)){";
    html += "status.sensors.forEach(s=>{";
    html += "if(s.slot>=0&&s.slot<4&&sensorSlots[s.slot]){";
    html += "sensorSlots[s.slot].errorRate=s.errorRate;";
    html += "sensorSlots[s.slot].errorRateAvailable=s.errorRateAvailable;}});}}";
    html += "renderSensors();}catch(e){console.error('Failed to load sensors:',e);}}";

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
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Debounce:</span> <span>'+sensor.debounceMs+'ms</span></div>';";
    html += "const zoneStr=(sensor.distanceZone===1?'Near (0.5-4m)':sensor.distanceZone===2?'Far (3-12m)':'Auto');";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Distance Zone:</span> <span>'+zoneStr+'</span></div>';}";
    html += "else if(sensor.type===2||sensor.type===4){";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Type:</span> <span>'+(sensor.type===2?'HC-SR04 (4-pin)':'Grove (3-pin)')+'</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Max Range:</span> <span>'+(sensor.maxDetectionDistance||3000)+'mm</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warn At:</span> <span>'+sensor.detectionThreshold+'mm</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Direction:</span> <span>'+(sensor.enableDirectionDetection?'Enabled':'Disabled')+'</span></div>';";
    html += "if(sensor.enableDirectionDetection){";
    html += "const dirMode=(sensor.directionTriggerMode===0?'Approaching':sensor.directionTriggerMode===1?'Receding':'Both');";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Trigger:</span> <span>'+dirMode+'</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Samples:</span> <span>'+sensor.sampleWindowSize+' @ '+sensor.sampleRateMs+'ms</span></div>';";
    html += "const dirSensStr=(sensor.directionSensitivity===0||sensor.directionSensitivity===undefined?'Auto':''+sensor.directionSensitivity+'mm');";
    html += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Dir. Sensitivity:</span> <span>'+dirSensStr+'</span></div>';}";
    html += "}";  // Close else if block for ultrasonic sensors
    html += "html+='</div></div>';";

    html += "html+='</div>';";  // Close grid

    // Hardware Info section (for sensors that support error rate monitoring)
    html += "if(sensor.type===2||sensor.type===4){";  // Ultrasonic sensors only
    html += "html+='<div style=\"margin-top:12px;padding:12px;background:#f8fafc;border-radius:6px;border:1px solid #e2e8f0;\">';";
    html += "html+='<div style=\"font-weight:600;margin-bottom:8px;font-size:0.9em;color:#1e293b;\">Hardware Info</div>';";
    html += "html+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html += "html+='<span style=\"font-size:0.85em;color:#64748b;\">Error Rate:</span>';";
    html += "const errorRate=sensor.errorRate!==undefined?sensor.errorRate:-1;";
    html += "const errorRateAvailable=sensor.errorRateAvailable!==undefined?sensor.errorRateAvailable:false;";
    html += "if(errorRate<0||!errorRateAvailable){";
    html += "html+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet</span>';}";
    html += "else{";
    html += "const colorClass=errorRate<5.0?'#10b981':errorRate<15.0?'#f59e0b':'#ef4444';";
    html += "const statusText=errorRate<5.0?'Excellent':errorRate<15.0?'Fair':'Poor';";
    html += "html+='<span style=\"font-size:0.85em;font-weight:600;color:'+colorClass+';\">'+errorRate.toFixed(1)+'%</span>';";
    html += "html+='<span style=\"font-size:0.8em;color:#94a3b8;\">('+statusText+')</span>';}";
    html += "html+='</div>';";
    html += "html+='<div style=\"font-size:0.75em;color:#64748b;margin-top:4px;\">Based on 100 sample test. Lower is better. Error rate will be high if distances measured are greater than the sensor\\'s capabilities, but this may not be a problem for functionality.</div>';";
    html += "html+='</div>';}";

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
    // Set default pin based on sensor type
    html += "let defaultPin='5';";
    html += "if(typeNum===2)defaultPin='8';";  // HC-SR04 trigger pin
    html += "if(typeNum===4)defaultPin='8';";  // Grove signal pin
    html += "const pin=parseInt(prompt('Enter primary pin (GPIO number):',defaultPin));";
    html += "if(isNaN(pin)||pin<0||pin>48){alert('Invalid pin number');return;}";
    // Create sensor with type-specific defaults
    html += "const sensor={type:typeNum,name:name,primaryPin:pin,enabled:true,isPrimary:freeSlot===0,";
    html += "warmupMs:60000,debounceMs:50,detectionThreshold:1100,maxDetectionDistance:3000,enableDirectionDetection:true,";
    html += "directionTriggerMode:0,directionSensitivity:0,sampleWindowSize:3,sampleRateMs:75};";
    html += "if(typeNum===2){";
    html += "const echoPin=parseInt(prompt('Enter echo pin for HC-SR04 (GPIO number):','9'));";
    html += "if(isNaN(echoPin)||echoPin<0||echoPin>48){alert('Invalid echo pin');return;}";
    html += "sensor.secondaryPin=echoPin;}";
    html += "if(typeNum===4){sensor.secondaryPin=0;}";
    html += "if(typeNum===0){sensor.warmupMs=60000;sensor.debounceMs=50;sensor.enableDirectionDetection=false;}";  // PIR-specific overrides
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
    html += "if(!isNaN(debounce)&&debounce>=10&&debounce<=1000)sensor.debounceMs=debounce;";
    html += "const zoneStr=prompt('Distance Zone:\\n0=Auto (default)\\n1=Near (0.5-4m, position lower)\\n2=Far (3-12m, position higher)',sensor.distanceZone||0);";
    html += "if(zoneStr!==null){const zone=parseInt(zoneStr);if(!isNaN(zone)&&zone>=0&&zone<=2)sensor.distanceZone=zone;}}";
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
    html += "const samples=parseInt(prompt('Rapid sample count (2-20):',sensor.sampleWindowSize||5));";
    html += "if(!isNaN(samples)&&samples>=2&&samples<=20)sensor.sampleWindowSize=samples;";
    html += "const interval=parseInt(prompt('Sample interval ms (50-1000):',sensor.sampleRateMs||200));";
    html += "if(!isNaN(interval)&&interval>=50&&interval<=1000)sensor.sampleRateMs=interval;";
    html += "const dirSens=parseInt(prompt('Direction sensitivity (mm):\\n0=Auto (adaptive threshold)\\nOr enter value (will be min: sample interval):',sensor.directionSensitivity||0));";
    html += "if(!isNaN(dirSens)&&dirSens>=0)sensor.directionSensitivity=dirSens;}}";
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

    // Hardware Info section for displays (LED Matrix I2C error rate)
    html += "if(display.type===1){";  // 8x8 Matrix only
    html += "content+='<div style=\"margin-top:12px;padding:12px;background:#f8fafc;border-radius:6px;border:1px solid #e2e8f0;\">';";
    html += "content+='<div style=\"font-weight:600;margin-bottom:8px;font-size:0.9em;color:#1e293b;\">Hardware Info</div>';";
    html += "content+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html += "content+='<span style=\"font-size:0.85em;color:#64748b;\">I2C Error Rate:</span>';";
    html += "const errorRate=display.errorRate!==undefined?display.errorRate:-1;";
    html += "const errorRateAvailable=display.errorRateAvailable!==undefined?display.errorRateAvailable:false;";
    html += "const txCount=display.transactionCount!==undefined?display.transactionCount:0;";
    html += "if(errorRate<0||!errorRateAvailable){";
    html += "const remaining=Math.max(0,10-txCount);";
    html += "if(remaining>0){";
    html += "content+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet ('+remaining+' operations remaining)</span>';}";
    html += "else{";
    html += "content+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet</span>';}}";
    html += "else{";
    html += "const colorClass=errorRate<1.0?'#10b981':errorRate<5.0?'#f59e0b':'#ef4444';";
    html += "const statusText=errorRate<1.0?'Excellent':errorRate<5.0?'Fair':'Poor';";
    html += "content+='<span style=\"font-size:0.85em;font-weight:600;color:'+colorClass+';\">'+errorRate.toFixed(1)+'%</span>';";
    html += "content+='<span style=\"font-size:0.8em;color:#94a3b8;\">('+statusText+')</span>';}";
    html += "content+='</div>';";
    html += "content+='<div style=\"font-size:0.75em;color:#64748b;margin-top:4px;\">Based on I2C transaction history. Lower is better.</div>';";
    html += "content+='</div>';}";

    html += "card.innerHTML=content;";
    html += "return card;}";

    // Add display
    html += "function addDisplay(){";
    html += "let slot=-1;";
    html += "for(let i=0;i<2;i++){if(!displaySlots[i]){slot=i;break;}}";
    html += "if(slot===-1){alert('Maximum 2 displays reached');return;}";
    html += "const name=prompt('Display name:','8x8 Matrix');";
    html += "if(!name)return;";
    html += "const newDisplay={slot:slot,name:name,type:1,i2cAddress:0x70,sdaPin:7,sclPin:10,enabled:true,brightness:15,rotation:0,useForStatus:true};";
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
    html += "if(brightness){display.brightness=parseInt(brightness)||15;}";
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

    // Firmware OTA Functions
    html += "function validateFirmware(){";
    html += "const file=document.getElementById('firmware-file').files[0];";
    html += "const btn=document.getElementById('upload-btn');";
    html += "if(!file){btn.disabled=true;return;}";
    html += "if(file.size<100000){alert('File too small - not a valid firmware');btn.disabled=true;return;}";
    html += "if(file.size>2000000){alert('File too large - exceeds 2MB limit');btn.disabled=true;return;}";
    html += "btn.disabled=false;}";

    html += "function uploadFirmware(){";
    html += "const file=document.getElementById('firmware-file').files[0];";
    html += "if(!file){alert('Please select a firmware file');return;}";
    html += "if(!confirm('Upload firmware and reboot device?\\n\\nThis will restart the device.'))return;";
    html += "const formData=new FormData();";
    html += "formData.append('firmware',file);";
    html += "const xhr=new XMLHttpRequest();";
    html += "const progressDiv=document.getElementById('upload-progress');";
    html += "const progressBar=document.getElementById('upload-bar');";
    html += "const statusDiv=document.getElementById('upload-status');";
    html += "const uploadBtn=document.getElementById('upload-btn');";
    html += "progressDiv.style.display='block';";
    html += "uploadBtn.disabled=true;";
    html += "xhr.upload.addEventListener('progress',(e)=>{";
    html += "if(e.lengthComputable){";
    html += "const percent=Math.round((e.loaded/e.total)*100);";
    html += "progressBar.value=percent;";
    html += "statusDiv.textContent='Uploading... '+percent+'%';}});";
    html += "xhr.addEventListener('load',()=>{";
    html += "if(xhr.status===200){";
    html += "statusDiv.textContent='Success! Device rebooting...';";
    html += "statusDiv.style.color='#10b981';";
    html += "setTimeout(()=>{";
    html += "alert('Firmware updated. Device is rebooting.\\nReconnect in 30 seconds.');";
    html += "window.location.reload();},3000);}";
    html += "else{";
    html += "statusDiv.textContent='Failed: '+xhr.responseText;";
    html += "statusDiv.style.color='#ef4444';";
    html += "uploadBtn.disabled=false;}});";
    html += "xhr.addEventListener('error',()=>{";
    html += "statusDiv.textContent='Upload error - check connection';";
    html += "statusDiv.style.color='#ef4444';";
    html += "uploadBtn.disabled=false;});";
    html += "xhr.open('POST','/api/ota/upload');";
    html += "xhr.send(formData);}";

    html += "fetch('/api/ota/status')";
    html += ".then(r=>r.json())";
    html += ".then(data=>{";
    html += "const versionElem=document.getElementById('current-version');";
    html += "const partitionElem=document.getElementById('current-partition');";
    html += "const maxSizeElem=document.getElementById('max-firmware-size');";
    html += "if(versionElem)versionElem.textContent=data.currentVersion||'Unknown';";
    html += "if(partitionElem)partitionElem.textContent='Partition: '+data.currentPartition;";
    html += "if(maxSizeElem)maxSizeElem.textContent='Max size: '+(data.maxFirmwareSize/1024).toFixed(0)+' KB';})";
    html += ".catch(e=>console.error('Failed to load OTA status:',e));";

    // Log streaming variables
    html += "let logSocket=null;";
    html += "let autoScroll=true;";
    html += "let activeFilters=new Set([0,1,2,3,4]);";
    html += "let lastSequence=0;";

    // Connect to log streaming WebSocket
    html += "function connectLogStream(){";
    html += "const protocol=window.location.protocol==='https:'?'wss:':'ws:';";
    html += "const wsUrl=protocol+'//'+window.location.host+'/ws/logs';";
    html += "logSocket=new WebSocket(wsUrl);";
    html += "logSocket.onopen=function(){";
    html += "document.getElementById('ws-status').textContent='\\u25CF Connected';";
    html += "document.getElementById('ws-status').className='badge badge-success';};";
    html += "logSocket.onmessage=function(event){";
    html += "const entry=JSON.parse(event.data);appendLogEntry(entry);};";
    html += "logSocket.onerror=function(){";
    html += "document.getElementById('ws-status').textContent='\\u25CF Error';";
    html += "document.getElementById('ws-status').className='badge badge-error';};";
    html += "logSocket.onclose=function(){";
    html += "document.getElementById('ws-status').textContent='\\u25CF Disconnected';";
    html += "document.getElementById('ws-status').className='badge badge-warning';";
    html += "setTimeout(connectLogStream,3000);};};";

    // Append log entry to viewer
    html += "function appendLogEntry(entry){";
    html += "if(lastSequence>0&&entry.seq>lastSequence+1){";
    html += "const missed=entry.seq-lastSequence-1;";
    html += "const warningDiv=document.createElement('div');";
    html += "warningDiv.className='log-entry log-warn';";
    html += "warningDiv.textContent='\\u26A0\\uFE0F Missed '+missed+' log entries (connection lag)';";
    html += "document.getElementById('log-viewer').appendChild(warningDiv);}";
    html += "lastSequence=entry.seq;";
    html += "const viewer=document.getElementById('log-viewer');";
    html += "const div=document.createElement('div');";
    html += "div.className='log-entry log-'+entry.levelName.toLowerCase();";
    html += "div.dataset.level=entry.level;";
    html += "div.dataset.seq=entry.seq;";
    html += "div.dataset.source=entry.source||'logger';";
    html += "const ts=formatTimestamp(entry.ts);";
    html += "div.textContent='['+ts+'] ['+entry.levelName+'] '+entry.msg;";
    html += "if(!activeFilters.has(entry.level))div.classList.add('hidden');";
    html += "viewer.appendChild(div);";
    html += "while(viewer.children.length>1000)viewer.removeChild(viewer.firstChild);";
    html += "if(autoScroll)viewer.scrollTop=viewer.scrollHeight;};";

    // Format timestamp
    html += "function formatTimestamp(ms){";
    html += "const sec=Math.floor(ms/1000);";
    html += "const min=Math.floor(sec/60);";
    html += "const hr=Math.floor(min/60);";
    html += "const msec=ms%1000;";
    html += "return pad(hr%24)+':'+pad(min%60)+':'+pad(sec%60)+'.'+pad(msec,3);};";

    // Pad numbers with leading zeros
    html += "function pad(n,len){";
    html += "len=len||2;";
    html += "return String(n).padStart(len,'0');};";

    // Toggle level filter
    html += "function toggleLevelFilter(btn){";
    html += "const level=parseInt(btn.dataset.level);";
    html += "if(activeFilters.has(level)){";
    html += "activeFilters.delete(level);";
    html += "btn.classList.remove('active');}else{";
    html += "activeFilters.add(level);";
    html += "btn.classList.add('active');}";
    html += "document.querySelectorAll('.log-entry').forEach(function(entry){";
    html += "const entryLevel=parseInt(entry.dataset.level);";
    html += "if(activeFilters.has(entryLevel)){";
    html += "entry.classList.remove('hidden');}else{";
    html += "entry.classList.add('hidden');}});};";

    // Filter logs by search text
    html += "function filterLogs(){";
    html += "const search=document.getElementById('log-search').value.toLowerCase();";
    html += "document.querySelectorAll('.log-entry').forEach(function(entry){";
    html += "const matches=entry.textContent.toLowerCase().includes(search);";
    html += "const levelOk=activeFilters.has(parseInt(entry.dataset.level));";
    html += "if(search===''){";
    html += "if(levelOk)entry.style.display='block';";
    html += "else entry.style.display='none';}else{";
    html += "if(matches&&levelOk)entry.style.display='block';";
    html += "else entry.style.display='none';}});};";

    // Clear log viewer
    html += "function clearLogViewer(){";
    html += "document.getElementById('log-viewer').innerHTML='';";
    html += "lastSequence=0;};";

    // Auto-scroll checkbox handler
    html += "document.getElementById('auto-scroll').addEventListener('change',function(e){";
    html += "autoScroll=e.target.checked;});";

    // Auto-refresh status and logs
    html += "fetchStatus();";
    html += "setInterval(fetchStatus,2000);";

    // Load animation assignments on page load
    html += "updateActiveAnimations();";

    // Initialize WebSocket connection on page load
    html += "window.addEventListener('load',function(){connectLogStream();});";

    html += "</script></body></html>";

    LOG_DEBUG("buildDashboardHTML() complete: %u bytes, free heap: %u", html.length(), ESP.getFreeHeap());

    // Verify HTML is complete
    if (html.endsWith("</html>")) {
        LOG_INFO("HTML is complete (ends with </html>)");
    } else {
        LOG_ERROR("HTML TRUNCATED! Last 20 chars: %s", html.substring(html.length()-20).c_str());
    }

    return html;
}

// ============================================================================
// Debug Logging Endpoints
// ============================================================================

void WebAPI::handleGetDebugLogs(AsyncWebServerRequest* request) {
#if !MOCK_HARDWARE
    // Helper function to get file size
    auto getFileSize = [](const char* path) -> size_t {
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f) {
                size_t size = f.size();
                f.close();
                return size;
            }
        }
        return 0;
    };

    // Build JSON response with system info
    char json[768];
    snprintf(json, sizeof(json),
        "{"
        "\"bootCycle\":%u,"
        "\"firmware\":\"%s\","
        "\"freeHeap\":%u,"
        "\"filesystemUsage\":%u,"
        "\"totalLogsSize\":%u,"
        "\"logs\":["
        "{\"name\":\"current\",\"size\":%u,\"path\":\"/logs/boot_current.log\"},"
        "{\"name\":\"boot_1\",\"size\":%u,\"path\":\"/logs/boot_1.log\"},"
        "{\"name\":\"boot_2\",\"size\":%u,\"path\":\"/logs/boot_2.log\"}"
        "]"
        "}",
        g_debugLogger.getBootCycle(),
        FIRMWARE_VERSION,
        ESP.getFreeHeap(),
        g_debugLogger.getFilesystemUsage(),
        g_debugLogger.getTotalLogsSize(),
        getFileSize("/logs/boot_current.log"),
        getFileSize("/logs/boot_1.log"),
        getFileSize("/logs/boot_2.log")
    );
    sendJSON(request, 200, json);
#else
    sendError(request, 501, "Not available in mock mode");
#endif
}

void WebAPI::handleClearDebugLogs(AsyncWebServerRequest* request) {
#if !MOCK_HARDWARE
    g_debugLogger.clearAllLogs();
    DEBUG_LOG_API("All debug logs cleared via API");
    sendJSON(request, 200, "{\"success\":true,\"message\":\"All logs cleared\"}");
#else
    sendError(request, 501, "Not available in mock mode");
#endif
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
    // Allocate buffer on first chunk
    if (index == 0) {
        char* requestBuffer = allocateRequestBuffer(request, total);
        if (!requestBuffer) {
            sendError(request, 500, "Out of memory");
            return;
        }

        // Set up disconnect handler to clean up buffer if request is aborted
        request->onDisconnect([request]() {
            freeRequestBuffer(request);
        });
    }

    // Get buffer and copy this chunk
    char* requestBuffer = getRequestBuffer(request);
    if (requestBuffer) {
        memcpy(requestBuffer + index, data, len);
    }

    // Only process when all data received
    if (index + len != total) {
        return;
    }

    if (!requestBuffer) {
        sendError(request, 500, "Internal error");
        freeRequestBuffer(request);
        return;
    }

    requestBuffer[total] = '\0';

    // Parse JSON body
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, requestBuffer);

    if (error) {
        freeRequestBuffer(request);
        sendError(request, 400, "Invalid JSON");
        return;
    }

    // Update debug logger config
    bool changed = false;

    if (doc.containsKey("level")) {
        const char* levelStr = doc["level"];
        DebugLogger::LogLevel newLevel = DebugLogger::LEVEL_DEBUG;

        if (strcmp(levelStr, "VERBOSE") == 0) newLevel = DebugLogger::LEVEL_VERBOSE;
        else if (strcmp(levelStr, "DEBUG") == 0) newLevel = DebugLogger::LEVEL_DEBUG;
        else if (strcmp(levelStr, "INFO") == 0) newLevel = DebugLogger::LEVEL_INFO;
        else if (strcmp(levelStr, "WARN") == 0) newLevel = DebugLogger::LEVEL_WARN;
        else if (strcmp(levelStr, "ERROR") == 0) newLevel = DebugLogger::LEVEL_ERROR;
        else if (strcmp(levelStr, "NONE") == 0) newLevel = DebugLogger::LEVEL_NONE;

        g_debugLogger.setLevel(newLevel);
        DEBUG_LOG_API("Debug log level changed to %s", levelStr);
        changed = true;
    }

    if (doc.containsKey("categoryMask")) {
        uint8_t mask = doc["categoryMask"];
        g_debugLogger.setCategoryMask(mask);
        DEBUG_LOG_API("Debug category mask changed to 0x%02X", mask);
        changed = true;
    }

    freeRequestBuffer(request);

    if (changed) {
        sendJSON(request, 200, "{\"success\":true,\"message\":\"Debug config updated\"}");
    } else {
        sendError(request, 400, "No valid fields to update");
    }
}

// ============================================================================
// WebSocket Support for Real-Time Log Streaming
// ============================================================================

void WebAPI::handleLogWebSocketEvent(AsyncWebSocket* server,
                                      AsyncWebSocketClient* client,
                                      AwsEventType type, void* arg,
                                      uint8_t* data, size_t len) {
    switch(type) {
        case WS_EVT_CONNECT:
            // Enforce connection limit
            if (server->count() > m_maxWebSocketClients) {
                client->close();
                DEBUG_LOG_API("WebSocket client rejected (max %u)", m_maxWebSocketClients);
                return;
            }

            DEBUG_LOG_API("WebSocket client #%u connected", client->id());

            // Send last 50 log entries as catchup
            {
                uint32_t count = g_logger.getEntryCount();
                uint32_t start = count > 50 ? count - 50 : 0;
                for (uint32_t i = start; i < count; i++) {
                    Logger::LogEntry entry;
                    if (g_logger.getEntry(i, entry)) {
                        client->text(formatLogEntryJSON(entry, "logger"));
                    }
                }
            }
            break;

        case WS_EVT_DISCONNECT:
            DEBUG_LOG_API("WebSocket client #%u disconnected", client->id());
            break;

        case WS_EVT_DATA:
            // Future: handle filter/control messages from client
            break;

        case WS_EVT_ERROR:
            DEBUG_LOG_API("WebSocket client #%u error", client->id());
            break;
    }
}

String WebAPI::formatLogEntryJSON(const Logger::LogEntry& entry, const char* source) {
    StaticJsonDocument<256> doc;
    doc["seq"] = entry.sequenceNumber;
    doc["ts"] = entry.timestamp;
    doc["level"] = entry.level;
    doc["levelName"] = Logger::getLevelName((Logger::LogLevel)entry.level);
    doc["msg"] = entry.message;
    doc["source"] = source;

    String json;
    serializeJson(doc, json);
    return json;
}

void WebAPI::broadcastLogEntry(const Logger::LogEntry& entry, const char* source) {
    if (!m_logWebSocket) {
        Serial.println("[WS] broadcastLogEntry: m_logWebSocket is NULL");
        return;
    }

    int clientCount = m_logWebSocket->count();
    if (clientCount == 0) {
        return;  // No clients connected (normal, don't spam)
    }

    String json = formatLogEntryJSON(entry, source);
    m_logWebSocket->textAll(json);

    // Debug: confirm broadcast
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 5000) {  // Only log every 5 seconds
        Serial.printf("[WS] Broadcast to %d clients: seq=%u, msg=%s\n",
                     clientCount, entry.sequenceNumber, entry.message);
        lastDebugTime = millis();
    }
}
