#include "web_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "watchdog_manager.h"
#include "hal_ledmatrix_8x8.h"
#include "hal_pir.h"
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

// Static buffers for HTML dashboard (split into two parts to stay under ESP32's
// ~64KB max contiguous heap allocation limit; each part is reserved independently)
static String g_htmlPart1;  // HTML head + CSS + body + tab content (no <script>)
static String g_htmlPart2;  // <script>...</script></body></html>
static unsigned long g_lastHTMLBuildTime = 0;
static bool g_htmlResponseInProgress = false;

// Reentrancy guard: set while inside handleLogWebSocketEvent to prevent
// broadcastLogEntry from calling textAll() on the same WebSocket that is
// currently dispatching an event.  Without this, any log statement inside
// the event handler (e.g. DEBUG_LOG_API) triggers DebugLogger::log() →
// broadcastLogEntry() → textAll(), which re-enters the WebSocket internals
// and crashes the device.
static bool s_inWebSocketEvent = false;

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

    // POST /api/sensors/recalibrate - trigger manual PIR recalibration
    m_server->on("/api/sensors/recalibrate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        this->handlePostSensorRecalibrate(req);
    });
    // GET /api/sensors/recalibrate - poll PIR warmup status without triggering
    m_server->on("/api/sensors/recalibrate", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetSensorRecalibrate(req);
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

    // Dashboard polls this every 2 seconds — keep the idle timer alive so the
    // device does not enter light/deep sleep while a client is active.
    if (m_power) {
        m_power->recordActivity();
    }

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
        powerObj["usbPower"] = battery.usbPower;
        powerObj["low"] = battery.low;
        powerObj["critical"] = battery.critical;
        powerObj["monitoringEnabled"] = m_power->isBatteryMonitoringEnabled();

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

    // Apply power manager config changes at runtime
    if (m_power) {
        m_power->setPowerSavingMode(cfg.powerSavingMode);
        DEBUG_LOG_API("Power saving mode set to %u via config", cfg.powerSavingMode);
    }

    // Apply direction detector config changes at runtime
    if (m_directionDetector) {
        const ConfigManager::DirectionDetectorConfig& dirCfg = cfg.directionDetector;
        m_directionDetector->setSimultaneousThresholdMs(dirCfg.simultaneousThresholdMs);
        m_directionDetector->setConfirmationWindowMs(dirCfg.confirmationWindowMs);
        m_directionDetector->setPatternTimeoutMs(dirCfg.patternTimeoutMs);
        DEBUG_LOG_API("Direction detector config applied at runtime");
    }

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
            logObj["wallTimestamp"] = entry.wallTimestamp;
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

void WebAPI::handlePostSensorRecalibrate(AsyncWebServerRequest* request) {
    if (!m_sensorManager) {
        sendError(request, 503, "Sensor manager not available");
        return;
    }

    bool found = false;
    bool initiated = false;
    bool alreadyInProgress = false;

    // Find first PIR sensor and trigger recalibrate.
    // Both sensors share one power wire, so one call handles both.
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
        if (sensor && sensor->getSensorType() == SENSOR_TYPE_PIR) {
            HAL_PIR* pir = static_cast<HAL_PIR*>(sensor);
            found = true;
            if (pir->isRecalibrating()) {
                alreadyInProgress = true;
                break;
            }
            if (pir->recalibrate()) {
                initiated = true;
                break;  // Only trigger once (shared power wire)
            }
        }
    }

    StaticJsonDocument<512> doc;
    if (!found) {
        doc["success"] = false;
        doc["message"] = "No PIR sensor found";
        String json;
        serializeJson(doc, json);
        sendJSON(request, 404, json.c_str());
        return;
    }

    doc["success"] = true;
    if (alreadyInProgress) {
        doc["message"] = "Recalibration already in progress";
        doc["status"] = "recalibrating";
    } else if (initiated) {
        doc["message"] = "Recalibration initiated";
        doc["status"] = "recalibrating";
    } else {
        doc["message"] = "Recalibration could not be started (no power pin configured)";
        doc["status"] = "error";
    }

    // Report warmup status on all PIR sensors
    JsonArray sensorsArr = doc.createNestedArray("sensors");
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
        if (sensor && sensor->getSensorType() == SENSOR_TYPE_PIR) {
            JsonObject sObj = sensorsArr.createNestedObject();
            sObj["slot"] = i;
            sObj["ready"] = sensor->isReady();
            sObj["warmupRemaining"] = sensor->getWarmupTimeRemaining();
            HAL_PIR* pir = static_cast<HAL_PIR*>(sensor);
            sObj["recalibrating"] = pir->isRecalibrating();
        }
    }

    String json;
    serializeJson(doc, json);
    sendJSON(request, 200, json.c_str());

    DEBUG_LOG_API("PIR recalibrate: found=%d initiated=%d alreadyInProgress=%d",
                  found, initiated, alreadyInProgress);
}

void WebAPI::handleGetSensorRecalibrate(AsyncWebServerRequest* request) {
    if (!m_sensorManager) {
        sendError(request, 503, "Sensor manager not available");
        return;
    }

    StaticJsonDocument<512> doc;
    doc["success"] = true;

    // Report warmup status on all PIR sensors (read-only, no trigger)
    JsonArray sensorsArr = doc.createNestedArray("sensors");
    bool anyPIR = false;
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
        if (sensor && sensor->getSensorType() == SENSOR_TYPE_PIR) {
            anyPIR = true;
            JsonObject sObj = sensorsArr.createNestedObject();
            sObj["slot"] = i;
            sObj["ready"] = sensor->isReady();
            sObj["warmupRemaining"] = sensor->getWarmupTimeRemaining();
            HAL_PIR* pir = static_cast<HAL_PIR*>(sensor);
            sObj["recalibrating"] = pir->isRecalibrating();
        }
    }

    if (!anyPIR) {
        doc["success"] = false;
        doc["message"] = "No PIR sensor found";
    } else if (sensorsArr.size() > 0) {
        // Determine overall status from sensor states
        bool anyRecalibrating = false;
        bool allReady = true;
        for (size_t i = 0; i < sensorsArr.size(); i++) {
            if (sensorsArr[i]["recalibrating"].as<bool>()) anyRecalibrating = true;
            if (!sensorsArr[i]["ready"].as<bool>()) allReady = false;
        }
        if (anyRecalibrating) {
            doc["status"] = "recalibrating";
            doc["message"] = "Recalibration in progress";
        } else if (!allReady) {
            doc["status"] = "warming_up";
            doc["message"] = "Warming up";
        } else {
            doc["status"] = "ready";
            doc["message"] = "Sensors ready";
        }
    }

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

    // Keep the power manager's idle timer alive so the device does not
    // enter light/deep sleep while the upload is in progress.
    if (m_power) {
        m_power->recordActivity();
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

    // Dashboard page load is user activity — keep the idle timer alive.
    if (m_power) {
        m_power->recordActivity();
    }

    // Build HTML only if not already cached
    // Note: We use inline HTML instead of filesystem-based UI because:
    // 1. All features are implemented (multi-sensor, LED matrix, animations)
    // 2. Simpler deployment (no filesystem upload step)
    // 3. No LittleFS mount/filesystem issues
    // 4. For single-developer embedded projects, reflashing is acceptable
    if (g_htmlPart1.length() == 0 || !g_htmlPart2.endsWith("</html>")) {
        LOG_INFO("Building dashboard HTML...");
        buildDashboardHTML();  // populates g_htmlPart1 and g_htmlPart2

        // Verify build succeeded (part2 must end with </html>)
        if (g_htmlPart1.length() == 0 || !g_htmlPart2.endsWith("</html>")) {
            LOG_ERROR("Dashboard HTML build failed - truncated or empty (part1=%u, part2=%u)",
                      g_htmlPart1.length(), g_htmlPart2.length());
            g_htmlResponseInProgress = false;
            request->send(500, "text/plain", "Dashboard build failed - low memory");
            return;
        }

        LOG_INFO("Dashboard HTML built: %u bytes (part1=%u, part2=%u), free heap: %u",
                 g_htmlPart1.length() + g_htmlPart2.length(),
                 g_htmlPart1.length(), g_htmlPart2.length(), ESP.getFreeHeap());
    } else {
        LOG_DEBUG("Using cached dashboard HTML (%u bytes, part1=%u part2=%u)",
                  g_htmlPart1.length() + g_htmlPart2.length(),
                  g_htmlPart1.length(), g_htmlPart2.length());
    }

    size_t totalLen = g_htmlPart1.length() + g_htmlPart2.length();
    const char* p1 = g_htmlPart1.c_str();
    size_t p1Len = g_htmlPart1.length();
    const char* p2 = g_htmlPart2.c_str();
    size_t p2Len = g_htmlPart2.length();

    // Send using chunked response callback — streams across both cached parts
    AsyncWebServerResponse *response = request->beginResponse(
        "text/html", totalLen,
        [p1, p1Len, p2, p2Len, totalLen](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= totalLen) {
                g_htmlResponseInProgress = false;
                return 0;
            }

            size_t toSend = 0;
            if (index < p1Len) {
                size_t available = p1Len - index;
                size_t chunk = (available < maxLen) ? available : maxLen;
                memcpy(buffer, p1 + index, chunk);
                toSend = chunk;
            } else {
                size_t p2Index = index - p1Len;
                size_t available = p2Len - p2Index;
                size_t chunk = (available < maxLen) ? available : maxLen;
                memcpy(buffer, p2 + p2Index, chunk);
                toSend = chunk;
            }

            if (index + toSend >= totalLen) {
                g_htmlResponseInProgress = false;
            }
            return toSend;
        }
    );

    response->addHeader("Cache-Control", "no-cache");
    request->send(response);

    LOG_INFO("Dashboard HTML response started (chunked, %u bytes)", totalLen);
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
    html += "var ts;";
    html += "if(data.wallTs&&data.wallTs>0){";
    html += "var d=new Date(data.wallTs*1000);";
    html += "ts=String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0')+' '+";
    html += "String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');";
    html += "}else{";
    html += "var ms=data.ts;var s=Math.floor(ms/1000);var m=Math.floor(s/60);var h=Math.floor(m/60);";
    html += "ts=String(h).padStart(2,'0')+':'+String(m%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0')+'.'+String(ms%1000).padStart(3,'0');";
    html += "}";
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

void WebAPI::buildDashboardHTML() {
    LOG_DEBUG("buildDashboardHTML() starting, free heap: %u", ESP.getFreeHeap());

    // The full dashboard HTML is ~67KB.  ESP32 heap fragmentation limits the
    // largest single contiguous allocation to ~64KB.  Split into two Strings:
    //   Part 1 — HTML head + CSS + body + all tab content (no <script>)
    //   Part 2 — <script>...</script></body></html>
    // Each part is well under 64KB so reserve() succeeds independently.
    g_htmlPart1.clear();
    g_htmlPart1.reserve(45056);  // 44KB — comfortably fits the HTML/CSS portion
    String& html = g_htmlPart1;  // alias for the build loop below
    LOG_DEBUG("Reserved 44KB for HTML part 1");

    // HTML Head
    html += "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
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
    html += "<div class=\"status-item-compact\"><div class=\"status-icon\" id=\"battery-icon\"></div>";
    html += "<div><div class=\"status-label-compact\">Battery</div><div class=\"status-value-compact\" id=\"battery-status\">--</div></div></div>";
    html += "</div></div>";

    // Tab Navigation
    html += "<div class=\"tabs\">";
    html += "<button class=\"tab active\" onclick=\"showTab('status')\">Status</button>";
    html += "<button class=\"tab\" onclick=\"showTab('hardware')\">Hardware</button>";
    html += "<button class=\"tab\" onclick=\"showTab('config')\">Configuration</button>";
    html += "<button class=\"tab\" onclick=\"showTab('firmware')\">Firmware</button>";
    html += "</div>";

    // STATUS TAB
    html += "<div id=\"status-tab\" class=\"tab-content active\">";

    // Control Panel card
    html += "<div class=\"card\"><h2>Control Panel</h2><div class=\"mode-buttons\">";
    html += "<button class=\"btn btn-off\" onclick=\"setMode(0)\" id=\"btn-0\">OFF</button>";
    html += "<button class=\"btn btn-always\" onclick=\"setMode(1)\" id=\"btn-1\">ALWAYS ON</button>";
    html += "<button class=\"btn btn-motion\" onclick=\"setMode(2)\" id=\"btn-2\">MOTION DETECT</button>";
    html += "</div></div>";

    // System card — tabular: Network + Battery
    html += "<div class=\"card\"><h2>System</h2>";
    html += "<table style=\"width:100%;border-collapse:collapse;\">";
    html += "<tr style=\"background:#f8fafc;\"><td style=\"padding:6px 10px;font-weight:600;color:#64748b;width:38%;\">WiFi</td><td style=\"padding:6px 10px;\" id=\"sys-wifi\">--</td></tr>";
    html += "<tr><td style=\"padding:6px 10px;font-weight:600;color:#64748b;\">IP Address</td><td style=\"padding:6px 10px;\" id=\"sys-ip\">--</td></tr>";
    html += "<tr style=\"background:#f8fafc;\"><td style=\"padding:6px 10px;font-weight:600;color:#64748b;\">Signal</td><td style=\"padding:6px 10px;\" id=\"sys-rssi\">--</td></tr>";
    html += "<tr style=\"border-top:1px solid #e2e8f0;\"><td style=\"padding:6px 10px;font-weight:600;color:#64748b;\">Battery</td><td style=\"padding:6px 10px;\" id=\"sys-battery\">--</td></tr>";
    html += "<tr style=\"background:#f8fafc;\"><td style=\"padding:6px 10px;font-weight:600;color:#64748b;\">Voltage</td><td style=\"padding:6px 10px;\" id=\"sys-voltage\">--</td></tr>";
    html += "<tr><td style=\"padding:6px 10px;font-weight:600;color:#64748b;\">Power State</td><td style=\"padding:6px 10px;\" id=\"sys-power-state\">--</td></tr>";
    html += "</table></div>";

    // Logs card — file download management
    html += "<div class=\"card\"><div style=\"display:flex;justify-content:space-between;align-items:center;\"><h2 style=\"margin:0;\">Logs</h2>";
    html += "<button class=\"btn btn-primary btn-small\" onclick=\"window.open('/live-logs','logs','width=900,height=600')\">&#129717; Live Logs &#129717;</button></div>";
    html += "<div style=\"margin-bottom:10px;\">";
    html += "<label for=\"logSelect\" style=\"display:block;font-weight:600;margin-bottom:4px;\">Select Log File:</label>";
    html += "<select id=\"logSelect\" style=\"width:100%;padding:8px;border:1px solid #cbd5e1;border-radius:4px;\">";
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
    html += "</div></div>";

    html += "</div>"; // End status tab

    // HARDWARE TAB
    html += "<div id=\"hardware-tab\" class=\"tab-content\">";
    html += "<div class=\"card\"><h2>Sensor Configuration</h2>";
    html += "<p class=\"form-help\" style=\"margin-bottom:16px;\">Configure up to 4 motion sensors. Each sensor can be enabled/disabled independently.</p>";

    html += "<div id=\"sensors-list\"></div>";

    // Recalibrate button (power-cycles PIR sensors to re-trigger warm-up)
    html += "<div style=\"margin-top:16px;display:flex;align-items:center;gap:12px;\">";
    html += "<button class=\"btn btn-secondary\" id=\"recal-btn\" onclick=\"triggerRecalibrate()\">Recalibrate PIR Sensors</button>";
    html += "<span id=\"recal-status\" style=\"font-size:0.85em;color:#64748b;\"></span>";
    html += "</div>";

    html += "<button class=\"btn btn-primary\" style=\"margin-top:12px;\" onclick=\"addSensor()\">+ Add Sensor</button>";

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

    html += "<h3>NTP Sync</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">NTP Sync</label>";
    html += "<select id=\"cfg-ntpEnabled\" class=\"form-select\">";
    html += "<option value=\"0\">Disabled</option>";
    html += "<option value=\"1\">Enabled</option>";
    html += "</select></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">NTP Server</label>";
    html += "<input type=\"text\" id=\"cfg-ntpServer\" class=\"form-input\" maxlength=\"63\" placeholder=\"pool.ntp.org\"></div>";
    html += "</div>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Timezone (UTC offset, hours)</label>";
    html += "<input type=\"number\" id=\"cfg-ntpTimezone\" class=\"form-input\" min=\"-12\" max=\"14\" step=\"1\">";
    html += "<div class=\"form-help\">Enter offset from UTC/GMT (e.g., -8 for PST, -5 for EST, 0 for UTC). "
            "Time will be synced at boot and once per day.</div></div>";
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
    html += "</div>";

    html += "<h3>Power</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Battery Monitoring</label>";
    html += "<select id=\"cfg-batteryMonitoring\" class=\"form-select\">";
    html += "<option value=\"0\">Disabled</option>";
    html += "<option value=\"1\">Enabled</option>";
    html += "</select>";
    html += "<div class=\"form-help\">Requires external voltage divider on GPIO5 (see hardware docs). Disabling forces Power Saving off.</div></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Power Saving</label>";
    html += "<select id=\"cfg-powerSaving\" class=\"form-select\">";
    html += "<option value=\"0\">Disabled</option>";
    html += "<option value=\"1\">Light Sleep</option>";
    html += "<option value=\"2\">Deep Sleep + ULP</option>";
    html += "</select>";
    html += "<div class=\"form-help\">Light Sleep: wakes on PIR/button (~1ms latency). Deep Sleep+ULP: ULP coprocessor polls PIR for maximum battery life. Requires Battery Monitoring.</div></div>";
    html += "</div>";

    html += "<div style=\"margin-top:24px;\">";
    html += "<button type=\"submit\" class=\"btn btn-primary\">Save Configuration</button>";
    html += "<button type=\"button\" class=\"btn btn-off btn-small\" style=\"margin-left:12px;\" onclick=\"loadConfig()\">Reload</button>";
    html += "</div>";
    html += "<div id=\"save-indicator\" class=\"save-indicator\">Configuration saved successfully!</div>";
    html += "</form></div>";

    // Reboot card at bottom of config tab
    html += "<div class=\"card\"><h2>Device Management</h2>";
    html += "<div style=\"display:flex;justify-content:center;\">";
    html += "<button class=\"btn btn-danger\" onclick=\"rebootDevice()\" style=\"width:50%;max-width:200px;font-weight:600;\">Reboot Device</button>";
    html += "</div>";
    html += "<p class=\"form-help\" style=\"margin-top:8px;margin-bottom:0;text-align:center;\">Restart the ESP32 device. The web interface will be unavailable for ~10 seconds.</p>";
    html += "</div>";
    html += "</div>"; // End config tab

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

    LOG_DEBUG("Part 1 complete: %u bytes, free heap: %u", g_htmlPart1.length(), ESP.getFreeHeap());

    // --- Part 2: JavaScript ---
    g_htmlPart2.clear();
    g_htmlPart2.reserve(45056);  // 44KB — fits the JS portion (last measured: ~41KB)
    String& html2 = g_htmlPart2;
    LOG_DEBUG("Reserved 44KB for HTML part 2");

    html2 += "<script>";

    // Tab switching
    html2 += "function showTab(tab){";
    html2 += "document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));";
    html2 += "document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));";
    html2 += "event.target.classList.add('active');";
    html2 += "document.getElementById(tab+'-tab').classList.add('active');";
    html2 += "if(tab==='config')loadConfig();";
    html2 += "if(tab==='hardware'){loadSensors();loadDisplays();loadAnimations();}";
    html2 += "}";

    // Status fetching
    html2 += "let currentMode=-1;";
    html2 += "async function fetchStatus(){";
    html2 += "try{const res=await fetch('/api/status');const data=await res.json();";
    html2 += "currentMode=data.stateMachine.mode;";

    // Update compact status bar
    html2 += "document.getElementById('mode-display').textContent=data.stateMachine.modeName;";
    html2 += "const modeIcon=document.getElementById('mode-icon');";
    html2 += "modeIcon.className='status-icon '+(data.stateMachine.mode===0?'inactive':data.stateMachine.mode===1?'active':'');";

    html2 += "const warningEl=document.getElementById('warning-status');";
    html2 += "const warningIcon=document.getElementById('warning-icon');";
    html2 += "if(data.stateMachine.warningActive){warningEl.textContent='Active';warningIcon.className='status-icon warning';}";
    html2 += "else{warningEl.textContent='Idle';warningIcon.className='status-icon inactive';}";

    html2 += "document.getElementById('motion-events').textContent=data.stateMachine.motionEvents;";

    html2 += "const uptime=Math.floor(data.uptime/1000);";
    html2 += "const hours=Math.floor(uptime/3600);const mins=Math.floor((uptime%3600)/60);";
    html2 += "document.getElementById('uptime').textContent=hours+'h '+mins+'m';";

    // WiFi compact status
    html2 += "const wifiIcon=document.getElementById('wifi-icon');";
    html2 += "const wifiCompact=document.getElementById('wifi-status-compact');";
    html2 += "if(data.wifi){";
    html2 += "if(data.wifi.state===3){wifiCompact.textContent=data.wifi.ssid;wifiIcon.className='status-icon active';}";
    html2 += "else if(data.wifi.state===2){wifiCompact.textContent='Connecting';wifiIcon.className='status-icon warning';}";
    html2 += "else{wifiCompact.textContent='Disconnected';wifiIcon.className='status-icon inactive';}}";

    // Battery compact status
    html2 += "if(data.power){";
    html2 += "const battPct=data.power.batteryPercent;";
    html2 += "const battIcon=document.getElementById('battery-icon');";
    html2 += "const battStatus=document.getElementById('battery-status');";
    html2 += "if(!data.power.monitoringEnabled){";
    html2 += "if(data.power.usbPower){battIcon.className='status-icon active';battStatus.textContent='USB Power';}";
    html2 += "else{battIcon.className='status-icon inactive';battStatus.textContent='No battery';}}";
    html2 += "else if(data.power.usbPower){battIcon.className='status-icon active';";
    html2 += "battStatus.textContent='USB Power';}";
    html2 += "else if(data.power.critical){battIcon.className='status-icon warning';";
    html2 += "battStatus.textContent=battPct+'% CRITICAL';}";
    html2 += "else if(data.power.low){battIcon.className='status-icon warning';";
    html2 += "battStatus.textContent=battPct+'% LOW';}";
    html2 += "else{battIcon.className='status-icon '+(battPct>20?'active':'inactive');";
    html2 += "battStatus.textContent=battPct+'%';}}";

    // Update System table — WiFi rows
    html2 += "if(data.wifi){";
    html2 += "if(data.wifi.state===3){";
    html2 += "document.getElementById('sys-wifi').innerHTML=data.wifi.ssid+' <span class=\"badge badge-success\">Connected</span>';";
    html2 += "document.getElementById('sys-ip').textContent=data.wifi.ipAddress;";
    html2 += "document.getElementById('sys-rssi').textContent=data.wifi.rssi+' dBm';}";
    html2 += "else if(data.wifi.state===2){";
    html2 += "document.getElementById('sys-wifi').innerHTML='<span class=\"badge badge-warning\">Connecting...</span>';";
    html2 += "document.getElementById('sys-ip').textContent='--';";
    html2 += "document.getElementById('sys-rssi').textContent='--';}";
    html2 += "else{";
    html2 += "document.getElementById('sys-wifi').innerHTML='<span class=\"badge badge-error\">Disconnected</span>';";
    html2 += "document.getElementById('sys-ip').textContent='--';";
    html2 += "document.getElementById('sys-rssi').textContent='--';}}";

    // Update System table — Battery rows
    html2 += "if(data.power){";
    html2 += "if(!data.power.monitoringEnabled){";
    html2 += "document.getElementById('sys-battery').textContent=data.power.usbPower?'USB Power':'No battery';";
    html2 += "document.getElementById('sys-voltage').textContent='--';";
    html2 += "document.getElementById('sys-power-state').textContent=data.power.usbPower?'USB_POWER':'--';}";
    html2 += "else{";
    html2 += "let battText;";
    html2 += "if(data.power.usbPower){battText='USB Power';}";
    html2 += "else if(data.power.critical){battText=data.power.batteryPercent+'% CRITICAL';}";
    html2 += "else if(data.power.low){battText=data.power.batteryPercent+'% LOW';}";
    html2 += "else{battText=data.power.batteryPercent+'%';}";
    html2 += "document.getElementById('sys-battery').textContent=battText;";
    html2 += "document.getElementById('sys-voltage').textContent=data.power.batteryVoltage.toFixed(2)+' V';";
    html2 += "document.getElementById('sys-power-state').textContent=data.power.stateName;}}";

    // Update mode buttons
    html2 += "for(let i=0;i<=2;i++){const btn=document.getElementById('btn-'+i);";
    html2 += "if(i===currentMode)btn.classList.add('active');else btn.classList.remove('active');}";
    html2 += "}catch(e){console.error('Status fetch error:',e);}}";

    html2 += "async function setMode(mode){";
    html2 += "try{const res=await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},";
    html2 += "body:JSON.stringify({mode:mode})});if(res.ok)fetchStatus();}catch(e){}}";

    // Reboot device
    html2 += "async function rebootDevice(){";
    html2 += "if(!confirm('Are you sure you want to reboot the device?\\n\\nThe device will restart and the web interface will be unavailable for ~10 seconds.'))return;";
    html2 += "alert('Rebooting device now...\\n\\nThe page will reload automatically in 15 seconds.');";
    html2 += "setTimeout(()=>location.reload(),15000);";
    html2 += "try{";
    html2 += "await fetch('/api/reboot',{method:'POST'});";
    html2 += "}catch(e){}}"; // Connection lost during reboot is expected

    // Config loading
    html2 += "let currentConfig={};";
    html2 += "async function loadConfig(){";
    html2 += "try{const res=await fetch('/api/config');const cfg=await res.json();";
    html2 += "currentConfig=cfg;";
    html2 += "document.getElementById('cfg-deviceName').value=cfg.device?.name||'';";
    html2 += "document.getElementById('cfg-defaultMode').value=cfg.device?.defaultMode||0;";
    html2 += "document.getElementById('cfg-wifiSSID').value=cfg.wifi?.ssid||'';";
    html2 += "if(cfg.wifi?.password&&cfg.wifi.password.length>0){";
    html2 += "document.getElementById('cfg-wifiPassword').value='';";
    html2 += "document.getElementById('cfg-wifiPassword').placeholder='••••••••';}";
    html2 += "else{document.getElementById('cfg-wifiPassword').value='';document.getElementById('cfg-wifiPassword').placeholder='';}";
    html2 += "document.getElementById('cfg-motionWarningDuration').value=Math.round((cfg.motion?.warningDuration||30000)/1000);";
    html2 += "document.getElementById('cfg-ledBrightnessFull').value=(cfg.led?.brightnessFull!==undefined)?cfg.led.brightnessFull:255;";
    html2 += "document.getElementById('cfg-ledBrightnessDim').value=(cfg.led?.brightnessDim!==undefined)?cfg.led.brightnessDim:50;";
    html2 += "document.getElementById('cfg-logLevel').value=(cfg.logging?.level!==undefined)?cfg.logging.level:2;";
    html2 += "document.getElementById('cfg-batteryMonitoring').value=cfg.power?.batteryMonitoringEnabled?1:0;";
    html2 += "document.getElementById('cfg-powerSaving').value=cfg.power?.savingMode!==undefined?cfg.power.savingMode:0;";
    html2 += "var bmSel=document.getElementById('cfg-batteryMonitoring');";
    html2 += "var psSel=document.getElementById('cfg-powerSaving');";
    html2 += "if(bmSel.value==='0'){psSel.value='0';psSel.disabled=true;}";
    html2 += "bmSel.addEventListener('change',function(){if(this.value==='0'){psSel.value='0';psSel.disabled=true;}else{psSel.disabled=false;}});";
    html2 += "document.getElementById('cfg-dirSimultaneousThreshold').value=cfg.directionDetector?.simultaneousThresholdMs||150;";
    html2 += "document.getElementById('cfg-dirConfirmationWindow').value=cfg.directionDetector?.confirmationWindowMs||5000;";
    html2 += "document.getElementById('cfg-dirPatternTimeout').value=cfg.directionDetector?.patternTimeoutMs||10000;";
    // NTP config fields
    html2 += "document.getElementById('cfg-ntpEnabled').value=cfg.ntp?.enabled?1:0;";
    html2 += "document.getElementById('cfg-ntpServer').value=cfg.ntp?.server||'pool.ntp.org';";
    html2 += "document.getElementById('cfg-ntpTimezone').value=cfg.ntp?.timezoneOffset!==undefined?cfg.ntp.timezoneOffset:-8;";
    // NTP grey-out: disable server/timezone fields when NTP is off
    html2 += "var ntpSel=document.getElementById('cfg-ntpEnabled');";
    html2 += "var ntpServerEl=document.getElementById('cfg-ntpServer');";
    html2 += "var ntpTzEl=document.getElementById('cfg-ntpTimezone');";
    html2 += "function updateNTPFields(){var en=ntpSel.value!=='0';ntpServerEl.disabled=!en;ntpTzEl.disabled=!en;}";
    html2 += "ntpSel.addEventListener('change',updateNTPFields);";
    html2 += "updateNTPFields();";
    html2 += "}catch(e){console.error('Config load error:',e);}}";

    // Config saving
    html2 += "async function saveConfig(e){";
    html2 += "e.preventDefault();";
    html2 += "const pwdField=document.getElementById('cfg-wifiPassword');";
    html2 += "const cfg=JSON.parse(JSON.stringify(currentConfig));";
    html2 += "cfg.device=cfg.device||{};";
    html2 += "cfg.device.name=document.getElementById('cfg-deviceName').value;";
    html2 += "cfg.device.defaultMode=parseInt(document.getElementById('cfg-defaultMode').value);";
    html2 += "cfg.wifi=cfg.wifi||{};";
    html2 += "cfg.wifi.ssid=document.getElementById('cfg-wifiSSID').value;";
    html2 += "cfg.wifi.enabled=true;";
    html2 += "if(pwdField.value.length>0){cfg.wifi.password=pwdField.value;}";
    html2 += "cfg.motion=cfg.motion||{};";
    html2 += "cfg.motion.warningDuration=parseInt(document.getElementById('cfg-motionWarningDuration').value)*1000;";
    html2 += "cfg.led=cfg.led||{};";
    html2 += "cfg.led.brightnessFull=parseInt(document.getElementById('cfg-ledBrightnessFull').value);";
    html2 += "cfg.led.brightnessDim=parseInt(document.getElementById('cfg-ledBrightnessDim').value);";
    html2 += "cfg.logging=cfg.logging||{};";
    html2 += "cfg.logging.level=parseInt(document.getElementById('cfg-logLevel').value);";
    html2 += "cfg.power=cfg.power||{};";
    html2 += "cfg.power.batteryMonitoringEnabled=parseInt(document.getElementById('cfg-batteryMonitoring').value)===1;";
    html2 += "cfg.power.savingMode=parseInt(document.getElementById('cfg-powerSaving').value);";
    html2 += "cfg.directionDetector=cfg.directionDetector||{};";
    html2 += "cfg.directionDetector.simultaneousThresholdMs=parseInt(document.getElementById('cfg-dirSimultaneousThreshold').value);";
    html2 += "cfg.directionDetector.confirmationWindowMs=parseInt(document.getElementById('cfg-dirConfirmationWindow').value);";
    html2 += "cfg.directionDetector.patternTimeoutMs=parseInt(document.getElementById('cfg-dirPatternTimeout').value);";
    html2 += "cfg.ntp=cfg.ntp||{};";
    html2 += "cfg.ntp.enabled=parseInt(document.getElementById('cfg-ntpEnabled').value)===1;";
    html2 += "cfg.ntp.server=document.getElementById('cfg-ntpServer').value;";
    html2 += "cfg.ntp.timezoneOffset=parseInt(document.getElementById('cfg-ntpTimezone').value);";
    html2 += "let jsonStr;";
    html2 += "try{jsonStr=JSON.stringify(cfg);console.log('Saving config:',JSON.stringify(cfg,null,2));}";
    html2 += "catch(e){console.error('JSON.stringify failed:',e);alert('Failed to serialize config: '+e.message);return;}";
    html2 += "try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},";
    html2 += "body:jsonStr});if(res.ok){";
    html2 += "document.getElementById('save-indicator').classList.add('show');";
    html2 += "setTimeout(()=>document.getElementById('save-indicator').classList.remove('show'),3000);";
    html2 += "loadConfig();}else{";
    html2 += "const errorText=await res.text();";
    html2 += "console.error('Config save failed:',res.status,errorText);";
    html2 += "alert('Failed to save configuration: '+errorText);}}catch(e){";
    html2 += "console.error('Config save error:',e);alert('Error: '+e.message);}}";

    // LittleFS Log Management
    html2 += "let availableLogs=[];";
    html2 += "let selectedLog=null;";

    // Load available logs on page load
    html2 += "async function loadAvailableLogs(){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/debug/logs');";
    html2 += "const data=await res.json();";

    // Populate log dropdown

    html2 += "availableLogs=data.logs||[];";
    html2 += "const select=document.getElementById('logSelect');";
    html2 += "select.innerHTML='<option value=\"\">-- Select a log file --</option>';";
    html2 += "availableLogs.forEach((log,index)=>{";
    html2 += "const option=document.createElement('option');";
    html2 += "option.value=index;";
    html2 += "const sizeKB=(log.size/1024).toFixed(1);";
    html2 += "option.textContent=log.name+' ('+sizeKB+' KB)';";
    html2 += "select.appendChild(option);";
    html2 += "});";
    html2 += "}catch(err){console.error('Failed to load logs:',err);}}";

    // Handle log selection
    html2 += "function onLogSelect(){";
    html2 += "const select=document.getElementById('logSelect');";
    html2 += "const index=parseInt(select.value);";
    html2 += "if(isNaN(index)){";
    html2 += "document.getElementById('logActions').style.display='none';";
    html2 += "document.getElementById('logInfo').style.display='none';";
    html2 += "selectedLog=null;";
    html2 += "return;}";
    html2 += "selectedLog=availableLogs[index];";
    html2 += "document.getElementById('selectedLogName').textContent=selectedLog.name;";
    html2 += "document.getElementById('selectedLogSize').textContent=selectedLog.size.toLocaleString();";
    html2 += "document.getElementById('selectedLogSizeKB').textContent=(selectedLog.size/1024).toFixed(1);";
    html2 += "document.getElementById('selectedLogPath').textContent=selectedLog.path;";
    html2 += "document.getElementById('logActions').style.display='block';";
    html2 += "document.getElementById('logInfo').style.display='block';}";

    // Download selected log
    html2 += "function downloadLog(){";
    html2 += "if(!selectedLog)return;";
    html2 += "const url='/api/debug/logs/'+selectedLog.name;";
    html2 += "const a=document.createElement('a');";
    html2 += "a.href=url;";
    html2 += "a.download='stepaware_'+selectedLog.name+'.log';";
    html2 += "document.body.appendChild(a);";
    html2 += "a.click();";
    html2 += "document.body.removeChild(a);}";

    // Erase selected log
    html2 += "async function eraseLog(){";
    html2 += "if(!selectedLog)return;";
    html2 += "if(!confirm('Are you sure you want to erase '+selectedLog.name+'?'))return;";
    html2 += "try{";
    html2 += "const res=await fetch('/api/debug/logs/'+selectedLog.name,{method:'DELETE'});";
    html2 += "if(res.ok){";
    html2 += "alert('Log erased successfully');";
    html2 += "loadAvailableLogs();";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to erase log: '+err);}}";
    html2 += "catch(err){console.error('Error erasing log:',err);alert('Error erasing log');}}";

    // Erase all logs
    html2 += "async function eraseAllLogs(){";
    html2 += "if(!confirm('Are you sure you want to erase ALL logs? This cannot be undone!'))return;";
    html2 += "try{";
    html2 += "const res=await fetch('/api/debug/logs/clear',{method:'POST'});";
    html2 += "if(res.ok){";
    html2 += "alert('All logs erased successfully');";
    html2 += "loadAvailableLogs();";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to erase logs: '+err);}}";
    html2 += "catch(err){console.error('Error erasing logs:',err);alert('Error erasing logs');}}";

    // Add event listener to select
    html2 += "document.addEventListener('DOMContentLoaded',()=>{";
    html2 += "const select=document.getElementById('logSelect');";
    html2 += "if(select)select.addEventListener('change',onLogSelect);";
    html2 += "});";
    // Hardware Tab - Sensor Management
    html2 += "let sensorSlots=[null,null,null,null];";
    html2 += "const SENSOR_TYPES={PIR:{name:'PIR Motion',pins:1,config:['warmup','distanceZone']},";
    html2 += "IR:{name:'IR Beam-Break',pins:1,config:['debounce']},";
    html2 += "ULTRASONIC:{name:'Ultrasonic (HC-SR04)',pins:2,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']},";
    html2 += "ULTRASONIC_GROVE:{name:'Ultrasonic (Grove)',pins:1,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}};";

    // PIR recalibration trigger
    html2 += "function triggerRecalibrate(){";
    html2 += "var btn=document.getElementById('recal-btn');";
    html2 += "var status=document.getElementById('recal-status');";
    html2 += "btn.disabled=true;status.textContent='Sending...';";
    html2 += "fetch('/api/sensors/recalibrate',{method:'POST'})";
    html2 += ".then(function(r){return r.json();})";
    html2 += ".then(function(d){";
    html2 += "status.textContent=d.message||'Done';btn.disabled=false;";
    html2 += "if(d.status==='recalibrating'){";
    html2 += "status.style.color='#f59e0b';";
    html2 += "setTimeout(function pollRecal(){";
    html2 += "fetch('/api/sensors/recalibrate')";
    html2 += ".then(function(r){return r.json();})";
    html2 += ".then(function(d2){";
    html2 += "if(d2.sensors&&d2.sensors.some(function(s){return!s.ready;})){";
    html2 += "var rem=d2.sensors.reduce(function(m,s){return s.warmupRemaining>m?s.warmupRemaining:m;},0);";
    html2 += "status.textContent='Warming up... ('+Math.ceil(rem/1000)+'s remaining)';";
    html2 += "setTimeout(pollRecal,3000);}else{";
    html2 += "status.textContent='Recalibration complete';status.style.color='#10b981';}});},3000);";
    html2 += "}else{status.style.color='#10b981';}})";
    html2 += ".catch(function(){status.textContent='Request failed';status.style.color='#ef4444';btn.disabled=false;});}";

    // Load sensors from configuration
    html2 += "async function loadSensors(){";
    html2 += "try{";
    html2 += "const cfgRes=await fetch('/api/config');if(!cfgRes.ok)return;";
    html2 += "const cfg=await cfgRes.json();";
    html2 += "sensorSlots=[null,null,null,null];";
    html2 += "if(cfg.sensors&&Array.isArray(cfg.sensors)){";
    html2 += "cfg.sensors.forEach(s=>{if(s.slot>=0&&s.slot<4){sensorSlots[s.slot]=s;}});}";
    html2 += "const statusRes=await fetch('/api/sensors');";
    html2 += "if(statusRes.ok){";
    html2 += "const status=await statusRes.json();";
    html2 += "if(status.sensors&&Array.isArray(status.sensors)){";
    html2 += "status.sensors.forEach(s=>{";
    html2 += "if(s.slot>=0&&s.slot<4&&sensorSlots[s.slot]){";
    html2 += "sensorSlots[s.slot].errorRate=s.errorRate;";
    html2 += "sensorSlots[s.slot].errorRateAvailable=s.errorRateAvailable;}});}}";
    html2 += "renderSensors();}catch(e){console.error('Failed to load sensors:',e);}}";

    // Render all sensor cards
    html2 += "function renderSensors(){";
    html2 += "const container=document.getElementById('sensors-list');container.innerHTML='';";
    html2 += "sensorSlots.forEach((sensor,idx)=>{";
    html2 += "if(sensor!==null){container.appendChild(createSensorCard(sensor,idx));}});";
    html2 += "if(sensorSlots.filter(s=>s!==null).length===0){";
    html2 += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:20px;\">No sensors configured. Click \"Add Sensor\" to get started.</p>';}}";

    // Create sensor card element
    html2 += "function createSensorCard(sensor,slotIdx){";
    html2 += "const card=document.createElement('div');";
    html2 += "card.className='sensor-card'+(sensor.enabled?'':' disabled');";
    html2 += "let html='';";

    // Header with badge, title, and buttons on one line
    html2 += "html+='<div class=\"sensor-header\">';";
    html2 += "html+='<div style=\"display:flex;align-items:center;gap:10px;\">';";
    html2 += "html+='<span class=\"badge badge-'+(sensor.type===0?'success':sensor.type===1?'info':'primary')+'\">'+";
    html2 += "(sensor.type===0?'PIR':sensor.type===1?'IR':sensor.type===4?'GROVE':'HC-SR04')+'</span>';";
    html2 += "html+='<span class=\"sensor-title\">Slot '+slotIdx+': '+(sensor.name||'Unnamed Sensor')+'</span></div>';";
    html2 += "html+='<div class=\"sensor-actions\">';";
    html2 += "html+='<button class=\"btn btn-sm btn-'+(sensor.enabled?'warning':'success')+'\" onclick=\"toggleSensor('+slotIdx+')\">'+(sensor.enabled?'Disable':'Enable')+'</button>';";
    html2 += "html+='<button class=\"btn btn-sm btn-secondary\" onclick=\"editSensor('+slotIdx+')\">Edit</button>';";
    html2 += "html+='<button class=\"btn btn-sm btn-danger\" onclick=\"removeSensor('+slotIdx+')\">Remove</button>';";
    html2 += "html+='</div></div>';";

    // Compact horizontal layout for pin and config info
    html2 += "html+='<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px;\">';";

    // Wiring diagram column
    html2 += "html+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>';";
    html2 += "html+='<div style=\"line-height:1.6;\">';";

    // PIR/IR wiring
    html2 += "if(sensor.type===0||sensor.type===1){";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor OUT → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';}";

    // Ultrasonic HC-SR04 wiring (4-pin)
    html2 += "else if(sensor.type===2){";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">5V</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor TRIG → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Sensor ECHO → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.secondaryPin+'</span></div>';}";

    // Grove Ultrasonic wiring (3-pin)
    html2 += "else if(sensor.type===4){";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove VCC (Red) → <span style=\"color:#dc2626;font-weight:600;\">3.3V/5V</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove GND (Black) → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html2 += "html+='<div style=\"color:#64748b;font-size:0.85em;\">Grove SIG (Yellow) → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+sensor.primaryPin+'</span></div>';}";

    html2 += "html+='</div></div>';";

    // Configuration column
    html2 += "html+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>';";
    html2 += "html+='<div style=\"line-height:1.6;\">';";
    html2 += "if(sensor.type===0){";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warmup:</span> <span>'+(sensor.warmupMs/1000)+'s</span></div>';";
    html2 += "const zoneStr=(sensor.distanceZone===1?'Near (0.5-4m)':sensor.distanceZone===2?'Far (3-12m)':'None');";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Distance Zone:</span> <span>'+zoneStr+'</span></div>';";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Sensor Status:</span> <span>'+(sensor.sensorStatusDisplay?'On':'Off')+'</span></div>';}";
    html2 += "else if(sensor.type===2||sensor.type===4){";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Type:</span> <span>'+(sensor.type===2?'HC-SR04 (4-pin)':'Grove (3-pin)')+'</span></div>';";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Max Range:</span> <span>'+(sensor.maxDetectionDistance||3000)+'mm</span></div>';";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warn At:</span> <span>'+sensor.detectionThreshold+'mm</span></div>';";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Direction:</span> <span>'+(sensor.enableDirectionDetection?'Enabled':'Disabled')+'</span></div>';";
    html2 += "if(sensor.enableDirectionDetection){";
    html2 += "const dirMode=(sensor.directionTriggerMode===0?'Approaching':sensor.directionTriggerMode===1?'Receding':'Both');";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Trigger:</span> <span>'+dirMode+'</span></div>';";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Samples:</span> <span>'+sensor.sampleWindowSize+' @ '+sensor.sampleRateMs+'ms</span></div>';";
    html2 += "const dirSensStr=(sensor.directionSensitivity===0||sensor.directionSensitivity===undefined?'Auto':''+sensor.directionSensitivity+'mm');";
    html2 += "html+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Dir. Sensitivity:</span> <span>'+dirSensStr+'</span></div>';}";
    html2 += "}";  // Close else if block for ultrasonic sensors
    html2 += "html+='</div></div>';";

    html2 += "html+='</div>';";  // Close grid

    // Hardware Info section (for sensors that support error rate monitoring)
    html2 += "if(sensor.type===2||sensor.type===4){";  // Ultrasonic sensors only
    html2 += "html+='<div style=\"margin-top:12px;padding:12px;background:#f8fafc;border-radius:6px;border:1px solid #e2e8f0;\">';";
    html2 += "html+='<div style=\"font-weight:600;margin-bottom:8px;font-size:0.9em;color:#1e293b;\">Hardware Info</div>';";
    html2 += "html+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html2 += "html+='<span style=\"font-size:0.85em;color:#64748b;\">Error Rate:</span>';";
    html2 += "const errorRate=sensor.errorRate!==undefined?sensor.errorRate:-1;";
    html2 += "const errorRateAvailable=sensor.errorRateAvailable!==undefined?sensor.errorRateAvailable:false;";
    html2 += "if(errorRate<0||!errorRateAvailable){";
    html2 += "html+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet</span>';}";
    html2 += "else{";
    html2 += "const colorClass=errorRate<5.0?'#10b981':errorRate<15.0?'#f59e0b':'#ef4444';";
    html2 += "const statusText=errorRate<5.0?'Excellent':errorRate<15.0?'Fair':'Poor';";
    html2 += "html+='<span style=\"font-size:0.85em;font-weight:600;color:'+colorClass+';\">'+errorRate.toFixed(1)+'%</span>';";
    html2 += "html+='<span style=\"font-size:0.8em;color:#94a3b8;\">('+statusText+')</span>';}";
    html2 += "html+='</div>';";
    html2 += "html+='<div style=\"font-size:0.75em;color:#64748b;margin-top:4px;\">Based on 100 sample test. Lower is better. Error rate will be high if distances measured are greater than the sensor\\'s capabilities, but this may not be a problem for functionality.</div>';";
    html2 += "html+='</div>';}";

    html2 += "card.innerHTML=html;";
    html2 += "return card;}";

    // Add new sensor
    html2 += "function addSensor(){";
    html2 += "const freeSlot=sensorSlots.findIndex(s=>s===null);";
    html2 += "if(freeSlot===-1){alert('Maximum 4 sensors allowed. Remove a sensor first.');return;}";
    html2 += "const type=prompt('Select sensor type:\\n0 = PIR Motion\\n1 = IR Beam-Break\\n2 = Ultrasonic (HC-SR04 4-pin)\\n4 = Ultrasonic (Grove 3-pin)','0');";
    html2 += "if(type===null)return;";
    html2 += "const typeNum=parseInt(type);";
    html2 += "if(typeNum<0||typeNum>4||typeNum===3){alert('Invalid sensor type');return;}";
    html2 += "const name=prompt('Enter sensor name:','Sensor '+(freeSlot+1));";
    html2 += "if(!name)return;";
    // Set default pin based on sensor type
    html2 += "let defaultPin='5';";
    html2 += "if(typeNum===2)defaultPin='8';";  // HC-SR04 trigger pin
    html2 += "if(typeNum===4)defaultPin='8';";  // Grove signal pin
    html2 += "const pin=parseInt(prompt('Enter primary pin (GPIO number):',defaultPin));";
    html2 += "if(isNaN(pin)||pin<0||pin>48){alert('Invalid pin number');return;}";
    // Create sensor with type-specific defaults
    html2 += "const sensor={type:typeNum,name:name,primaryPin:pin,enabled:true,isPrimary:freeSlot===0,";
    html2 += "warmupMs:60000,debounceMs:50,detectionThreshold:1100,maxDetectionDistance:3000,enableDirectionDetection:true,";
    html2 += "directionTriggerMode:0,directionSensitivity:0,sampleWindowSize:3,sampleRateMs:75};";
    html2 += "if(typeNum===2){";
    html2 += "const echoPin=parseInt(prompt('Enter echo pin for HC-SR04 (GPIO number):','9'));";
    html2 += "if(isNaN(echoPin)||echoPin<0||echoPin>48){alert('Invalid echo pin');return;}";
    html2 += "sensor.secondaryPin=echoPin;}";
    html2 += "if(typeNum===4){sensor.secondaryPin=0;}";
    html2 += "if(typeNum===0){sensor.warmupMs=60000;sensor.debounceMs=0;sensor.enableDirectionDetection=false;sensor.distanceZone=0;}";  // PIR-specific overrides
    html2 += "sensorSlots[freeSlot]=sensor;renderSensors();saveSensors();}";

    // Remove sensor
    html2 += "function removeSensor(slotIdx){";
    html2 += "if(!confirm('Remove sensor from slot '+slotIdx+'?'))return;";
    html2 += "sensorSlots[slotIdx]=null;renderSensors();saveSensors();}";

    // Toggle sensor enabled/disabled
    html2 += "function toggleSensor(slotIdx){";
    html2 += "if(sensorSlots[slotIdx]){";
    html2 += "sensorSlots[slotIdx].enabled=!sensorSlots[slotIdx].enabled;";
    html2 += "renderSensors();saveSensors();}}";

    // Edit sensor
    html2 += "function editSensor(slotIdx){";
    html2 += "const sensor=sensorSlots[slotIdx];if(!sensor)return;";
    html2 += "const newName=prompt('Sensor name:',sensor.name);";
    html2 += "if(newName!==null&&newName.length>0){sensor.name=newName;}";
    html2 += "if(sensor.type===0){";
    html2 += "const warmup=parseInt(prompt('PIR warmup time (seconds):',sensor.warmupMs/1000));";
    html2 += "if(!isNaN(warmup)&&warmup>=1&&warmup<=120)sensor.warmupMs=warmup*1000;";
    html2 += "const zoneStr=prompt('Distance Zone:\\n0=None (default)\\n1=Near (0.5-4m, position lower)\\n2=Far (3-12m, position higher)',sensor.distanceZone||0);";
    html2 += "if(zoneStr!==null){const zone=parseInt(zoneStr);if(!isNaN(zone)&&zone>=0&&zone<=2)sensor.distanceZone=zone;}";
    html2 += "const statusStr=prompt('Sensor status (show on LED matrix):\\n0=Off\\n1=On',sensor.sensorStatusDisplay?1:0);";
    html2 += "if(statusStr!==null){const st=parseInt(statusStr);if(!isNaN(st)&&(st===0||st===1))sensor.sensorStatusDisplay=(st===1);}}";
    html2 += "else if(sensor.type===2||sensor.type===4){";
    html2 += "const maxDist=parseInt(prompt('Max detection distance (mm)\\nSensor starts detecting at this range:',sensor.maxDetectionDistance||3000));";
    html2 += "if(!isNaN(maxDist)&&maxDist>=100)sensor.maxDetectionDistance=maxDist;";
    html2 += "const warnDist=parseInt(prompt('Warning trigger distance (mm)\\nWarning activates when person is within:',sensor.detectionThreshold||1500));";
    html2 += "if(!isNaN(warnDist)&&warnDist>=10)sensor.detectionThreshold=warnDist;";
    html2 += "const dirStr=prompt('Enable direction detection? (yes/no):',(sensor.enableDirectionDetection?'yes':'no'));";
    html2 += "if(dirStr!==null){sensor.enableDirectionDetection=(dirStr.toLowerCase()==='yes'||dirStr==='1');}";
    html2 += "if(sensor.enableDirectionDetection){";
    html2 += "const dirMode=prompt('Trigger on:\\n0=Approaching (walking towards)\\n1=Receding (walking away)\\n2=Both directions',sensor.directionTriggerMode||0);";
    html2 += "if(dirMode!==null&&!isNaN(parseInt(dirMode))){sensor.directionTriggerMode=parseInt(dirMode);}";
    html2 += "const samples=parseInt(prompt('Rapid sample count (2-20):',sensor.sampleWindowSize||5));";
    html2 += "if(!isNaN(samples)&&samples>=2&&samples<=20)sensor.sampleWindowSize=samples;";
    html2 += "const interval=parseInt(prompt('Sample interval ms (50-1000):',sensor.sampleRateMs||200));";
    html2 += "if(!isNaN(interval)&&interval>=50&&interval<=1000)sensor.sampleRateMs=interval;";
    html2 += "const dirSens=parseInt(prompt('Direction sensitivity (mm):\\n0=Auto (adaptive threshold)\\nOr enter value (will be min: sample interval):',sensor.directionSensitivity||0));";
    html2 += "if(!isNaN(dirSens)&&dirSens>=0)sensor.directionSensitivity=dirSens;}}";
    html2 += "renderSensors();saveSensors();}";

    // Save sensors to backend
    html2 += "async function saveSensors(){";
    html2 += "try{";
    html2 += "const activeSensors=sensorSlots.map((s,idx)=>s?{...s,slot:idx}:null).filter(s=>s!==null);";
    html2 += "const res=await fetch('/api/sensors',{method:'POST',";
    html2 += "headers:{'Content-Type':'application/json'},body:JSON.stringify(activeSensors)});";
    html2 += "if(res.ok){";
    html2 += "const data=await res.json();";
    html2 += "console.log('Sensors saved:',data);";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "console.error('Save failed:',err);";
    html2 += "alert('Failed to save sensor configuration: '+err);}}";
    html2 += "catch(e){console.error('Save error:',e);alert('Error saving sensors: '+e.message);}}";

    // === DISPLAY MANAGEMENT ===
    html2 += "let displaySlots=[null,null];";

    // Load displays from backend
    html2 += "async function loadDisplays(){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/displays');";
    html2 += "if(res.ok){";
    html2 += "const data=await res.json();";
    html2 += "if(data.displays&&Array.isArray(data.displays)){";
    html2 += "data.displays.forEach(d=>{if(d.slot>=0&&d.slot<2){displaySlots[d.slot]=d;}});";
    html2 += "renderDisplays();}}";
    html2 += "}catch(e){console.error('Load displays error:',e);}}";

    // Render displays list
    html2 += "function renderDisplays(){";
    html2 += "const container=document.getElementById('displays-list');";
    html2 += "if(!container)return;";
    html2 += "container.innerHTML='';";
    html2 += "let anyDisplay=false;";
    html2 += "for(let i=0;i<displaySlots.length;i++){";
    html2 += "if(displaySlots[i]){anyDisplay=true;const card=createDisplayCard(displaySlots[i],i);container.appendChild(card);}}";
    html2 += "if(!anyDisplay){";
    html2 += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:20px;\">No displays configured. Click \"Add Display\" to get started.</p>';}}";

    // Create display card
    html2 += "function createDisplayCard(display,slotIdx){";
    html2 += "const card=document.createElement('div');";
    html2 += "card.className='sensor-card';";
    html2 += "card.style.cssText='border:1px solid #e2e8f0;border-radius:8px;padding:16px;margin-bottom:12px;background:#fff;';";
    html2 += "let content='';";
    html2 += "content+='<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;\">';";
    html2 += "content+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html2 += "const typeName=display.type===1?'8x8 Matrix':'LED';";
    html2 += "const typeColor=display.type===1?'#3b82f6':'#10b981';";
    html2 += "content+='<span style=\"background:'+typeColor+';color:white;padding:4px 8px;border-radius:4px;font-size:0.75em;font-weight:600;\">'+typeName+'</span>';";
    html2 += "content+='<span style=\"font-weight:600;\">Slot '+slotIdx+': '+display.name+'</span>';";
    html2 += "content+='</div>';";
    html2 += "content+='<div style=\"display:flex;gap:8px;\">';";
    html2 += "content+='<button class=\"btn btn-sm btn-'+(display.enabled?'warning':'success')+'\" onclick=\"toggleDisplay('+slotIdx+')\">'+(display.enabled?'Disable':'Enable')+'</button>';";
    html2 += "content+='<button class=\"btn btn-sm btn-secondary\" onclick=\"editDisplay('+slotIdx+')\">Edit</button>';";
    html2 += "content+='<button class=\"btn btn-sm btn-danger\" onclick=\"removeDisplay('+slotIdx+')\">Remove</button>';";
    html2 += "content+='</div></div>';";
    html2 += "content+='<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:16px;\">';";
    html2 += "content+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>';";
    html2 += "content+='<div style=\"line-height:1.6;\">';";
    html2 += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>';";
    html2 += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html2 += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix SDA → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+display.sdaPin+'</span></div>';";
    html2 += "content+='<div style=\"color:#64748b;font-size:0.85em;\">Matrix SCL → <span style=\"color:#2563eb;font-weight:600;\">GPIO '+display.sclPin+'</span></div>';";
    html2 += "content+='</div></div>';";
    html2 += "content+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>';";
    html2 += "content+='<div style=\"line-height:1.6;\">';";
    html2 += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">I2C Address:</span> <span>0x'+display.i2cAddress.toString(16).toUpperCase()+'</span></div>';";
    html2 += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Brightness:</span> <span>'+display.brightness+'/15</span></div>';";
    html2 += "content+='<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Rotation:</span> <span>'+(display.rotation*90)+'°</span></div>';";
    html2 += "content+='</div></div></div>';";

    // Hardware Info section for displays (LED Matrix I2C error rate)
    html2 += "if(display.type===1){";  // 8x8 Matrix only
    html2 += "content+='<div style=\"margin-top:12px;padding:12px;background:#f8fafc;border-radius:6px;border:1px solid #e2e8f0;\">';";
    html2 += "content+='<div style=\"font-weight:600;margin-bottom:8px;font-size:0.9em;color:#1e293b;\">Hardware Info</div>';";
    html2 += "content+='<div style=\"display:flex;align-items:center;gap:8px;\">';";
    html2 += "content+='<span style=\"font-size:0.85em;color:#64748b;\">I2C Error Rate:</span>';";
    html2 += "const errorRate=display.errorRate!==undefined?display.errorRate:-1;";
    html2 += "const errorRateAvailable=display.errorRateAvailable!==undefined?display.errorRateAvailable:false;";
    html2 += "const txCount=display.transactionCount!==undefined?display.transactionCount:0;";
    html2 += "if(errorRate<0||!errorRateAvailable){";
    html2 += "const remaining=Math.max(0,10-txCount);";
    html2 += "if(remaining>0){";
    html2 += "content+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet ('+remaining+' operations remaining)</span>';}";
    html2 += "else{";
    html2 += "content+='<span style=\"font-size:0.85em;color:#94a3b8;font-style:italic;\">Not available yet</span>';}}";
    html2 += "else{";
    html2 += "const colorClass=errorRate<1.0?'#10b981':errorRate<5.0?'#f59e0b':'#ef4444';";
    html2 += "const statusText=errorRate<1.0?'Excellent':errorRate<5.0?'Fair':'Poor';";
    html2 += "content+='<span style=\"font-size:0.85em;font-weight:600;color:'+colorClass+';\">'+errorRate.toFixed(1)+'%</span>';";
    html2 += "content+='<span style=\"font-size:0.8em;color:#94a3b8;\">('+statusText+')</span>';}";
    html2 += "content+='</div>';";
    html2 += "content+='<div style=\"font-size:0.75em;color:#64748b;margin-top:4px;\">Based on I2C transaction history. Lower is better.</div>';";
    html2 += "content+='</div>';}";

    html2 += "card.innerHTML=content;";
    html2 += "return card;}";

    // Add display
    html2 += "function addDisplay(){";
    html2 += "let slot=-1;";
    html2 += "for(let i=0;i<2;i++){if(!displaySlots[i]){slot=i;break;}}";
    html2 += "if(slot===-1){alert('Maximum 2 displays reached');return;}";
    html2 += "const name=prompt('Display name:','8x8 Matrix');";
    html2 += "if(!name)return;";
    html2 += "const newDisplay={slot:slot,name:name,type:1,i2cAddress:0x70,sdaPin:7,sclPin:10,enabled:true,brightness:15,rotation:0,useForStatus:true};";
    html2 += "displaySlots[slot]=newDisplay;";
    html2 += "renderDisplays();";
    html2 += "saveDisplays();}";

    // Remove display
    html2 += "function removeDisplay(slotIdx){";
    html2 += "if(!confirm('Remove this display?'))return;";
    html2 += "displaySlots[slotIdx]=null;";
    html2 += "renderDisplays();";
    html2 += "saveDisplays();}";

    // Toggle display
    html2 += "function toggleDisplay(slotIdx){";
    html2 += "if(displaySlots[slotIdx]){";
    html2 += "displaySlots[slotIdx].enabled=!displaySlots[slotIdx].enabled;";
    html2 += "renderDisplays();";
    html2 += "saveDisplays();}}";

    // Edit display
    html2 += "function editDisplay(slotIdx){";
    html2 += "const display=displaySlots[slotIdx];";
    html2 += "if(!display)return;";
    html2 += "const name=prompt('Display name:',display.name);";
    html2 += "if(name){display.name=name;}";
    html2 += "const brightness=prompt('Brightness (0-15):',display.brightness);";
    html2 += "if(brightness){display.brightness=parseInt(brightness)||15;}";
    html2 += "const rotation=prompt('Rotation (0,1,2,3):',display.rotation);";
    html2 += "if(rotation){display.rotation=parseInt(rotation)||0;}";
    html2 += "renderDisplays();";
    html2 += "saveDisplays();}";

    // Save displays to backend
    html2 += "async function saveDisplays(){";
    html2 += "try{";
    html2 += "const activeDisplays=displaySlots.filter(d=>d!==null);";
    html2 += "const res=await fetch('/api/displays',{method:'POST',";
    html2 += "headers:{'Content-Type':'application/json'},body:JSON.stringify(activeDisplays)});";
    html2 += "if(res.ok){";
    html2 += "const data=await res.json();";
    html2 += "console.log('Displays saved:',data);";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "console.error('Save failed:',err);";
    html2 += "alert('Failed to save display configuration: '+err);}}";
    html2 += "catch(e){console.error('Save error:',e);alert('Error saving displays: '+e.message);}}";

    // ========================================================================
    // Custom Animation Management (Issue #12 Phase 2)
    // ========================================================================

    // Load animations list
    html2 += "async function loadAnimations(){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations');";
    html2 += "if(res.ok){";
    html2 += "const data=await res.json();";
    html2 += "renderAnimations(data.animations||[]);";
    html2 += "updateAnimationSelect(data.animations||[]);";
    html2 += "}else{console.error('Failed to load animations');}}";
    html2 += "catch(e){console.error('Load animations error:',e);}}";

    // Render animations list
    html2 += "function renderAnimations(animations){";
    html2 += "const container=document.getElementById('animations-list');";
    html2 += "if(!container)return;";
    html2 += "if(animations.length===0){";
    html2 += "container.innerHTML='<p style=\"color:#94a3b8;text-align:center;padding:12px;background:#f8fafc;border-radius:4px;\">No custom animations loaded. Upload an animation file to get started.</p>';";
    html2 += "return;}";
    html2 += "let html='<div style=\"display:grid;gap:8px;\">';";
    html2 += "animations.forEach((anim,idx)=>{";
    html2 += "html+='<div style=\"display:flex;justify-content:space-between;align-items:center;padding:12px;background:#fff;border:1px solid #e2e8f0;border-radius:6px;\">';";
    html2 += "html+='<div style=\"flex:1;\">';";
    html2 += "html+='<div style=\"font-weight:600;color:#1e293b;\">'+anim.name+'</div>';";
    html2 += "html+='<div style=\"font-size:0.8em;color:#64748b;margin-top:2px;\">'+anim.frameCount+' frames';";
    html2 += "if(anim.loop)html+=' • Looping';";
    html2 += "html+='</div></div>';";
    html2 += "html+='<div style=\"display:flex;gap:6px;\">';";
    html2 += "html+='<button class=\"btn btn-sm btn-primary\" onclick=\"playCustomAnimation(\\''+anim.name+'\\')\" title=\"Play animation\" style=\"width:36px;padding:8px;\">▶</button>';";
    html2 += "html+='<button class=\"btn btn-sm btn-success\" onclick=\"assignCustomAnimation(\\''+anim.name+'\\')\" title=\"Assign to function\" style=\"width:36px;padding:8px;\">✓</button>';";
    html2 += "html+='<button class=\"btn btn-sm btn-danger\" onclick=\"deleteAnimation(\\''+anim.name+'\\')\" title=\"Remove from memory\" style=\"width:36px;padding:8px;\">×</button>';";
    html2 += "html+='</div></div>';});";
    html2 += "html+='</div>';";
    html2 += "container.innerHTML=html;}";

    // Update animation select dropdown
    html2 += "function updateAnimationSelect(animations){";
    html2 += "const select=document.getElementById('test-animation-select');";
    html2 += "if(!select)return;";
    html2 += "select.innerHTML='<option value=\"\">Select animation...</option>';";
    html2 += "animations.forEach(anim=>{";
    html2 += "const opt=document.createElement('option');";
    html2 += "opt.value=anim.name;";
    html2 += "opt.textContent=anim.name+' ('+anim.frameCount+' frames)';";
    html2 += "select.appendChild(opt);});}";

    // Upload animation file
    html2 += "async function uploadAnimation(){";
    html2 += "const fileInput=document.getElementById('animation-file-input');";
    html2 += "if(!fileInput||!fileInput.files||fileInput.files.length===0){";
    html2 += "alert('Please select a file first');return;}";
    html2 += "const file=fileInput.files[0];";
    html2 += "if(!file.name.endsWith('.txt')){";
    html2 += "alert('Please select a .txt animation file');return;}";
    html2 += "const formData=new FormData();";
    html2 += "formData.append('file',file);";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/upload',{method:'POST',body:formData});";
    html2 += "if(res.ok){";
    html2 += "const data=await res.json();";
    html2 += "alert('Animation uploaded successfully: '+data.name);";
    html2 += "fileInput.value='';";
    html2 += "loadAnimations();";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Upload failed: '+err);}}";
    html2 += "catch(e){alert('Upload error: '+e.message);}}";

    // Play built-in animation
    html2 += "async function playBuiltInAnimation(animType,duration){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/builtin',{";
    html2 += "method:'POST',";
    html2 += "headers:{'Content-Type':'application/json'},";
    html2 += "body:JSON.stringify({type:animType,duration:duration||0})});";
    html2 += "if(res.ok){";
    html2 += "console.log('Playing built-in animation: '+animType);";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to play animation: '+err);}}";
    html2 += "catch(e){alert('Error playing animation: '+e.message);}}";

    // Play animation from test controls
    html2 += "function playTestAnimation(){";
    html2 += "const select=document.getElementById('test-animation-select');";
    html2 += "const duration=parseInt(document.getElementById('test-duration').value)||0;";
    html2 += "if(!select||!select.value){alert('Select an animation first');return;}";
    html2 += "playAnimation(select.value,duration);}";

    // Play specific custom animation
    html2 += "async function playAnimation(name,duration){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/play',{";
    html2 += "method:'POST',";
    html2 += "headers:{'Content-Type':'application/json'},";
    html2 += "body:JSON.stringify({name:name,duration:duration||0})});";
    html2 += "if(res.ok){";
    html2 += "console.log('Playing animation: '+name);";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to play animation: '+err);}}";
    html2 += "catch(e){alert('Error playing animation: '+e.message);}}";

    // Stop current animation
    html2 += "async function stopAnimation(){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/stop',{method:'POST'});";
    html2 += "if(res.ok){console.log('Animation stopped');}";
    html2 += "}catch(e){console.error('Error stopping animation:',e);}}";

    // Delete animation from memory
    html2 += "async function deleteAnimation(name){";
    html2 += "if(!confirm('Remove \"'+name+'\" from memory?\\n\\nThis will free memory but you can re-upload the file anytime.'))return;";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/'+encodeURIComponent(name),{method:'DELETE'});";
    html2 += "if(res.ok){";
    html2 += "alert('Animation removed from memory');";
    html2 += "loadAnimations();";
    html2 += "}else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to delete: '+err);}}";
    html2 += "catch(e){alert('Delete error: '+e.message);}}";

    // Show animation format help
    html2 += "function showAnimationHelp(){";
    html2 += "alert('Animation File Format:\\n\\n'+";
    html2 += "'name=MyAnimation\\n'+";
    html2 += "'loop=true\\n'+";
    html2 += "'frame=11111111,10000001,...,100\\n\\n'+";
    html2 += "'• Each frame: 8 binary bytes + delay (ms)\\n'+";
    html2 += "'• Max 16 frames per animation\\n'+";
    html2 += "'• Max 8 animations loaded at once\\n\\n'+";
    html2 += "'See /data/animations/README.md for examples');}";

    // Download built-in animation as template
    html2 += "async function downloadTemplate(animType){";
    html2 += "try{";
    html2 += "console.log('Downloading template for:',animType);";
    html2 += "const res=await fetch('/api/animations/template?type='+animType);";
    html2 += "console.log('Fetch complete, status:',res.status);";
    html2 += "if(res.ok){";
    html2 += "const text=await res.text();";
    html2 += "const blob=new Blob([text],{type:'text/plain'});";
    html2 += "const url=URL.createObjectURL(blob);";
    html2 += "const a=document.createElement('a');";
    html2 += "a.href=url;";
    html2 += "a.download=animType.toLowerCase()+'_template.txt';";
    html2 += "document.body.appendChild(a);";
    html2 += "a.click();";
    html2 += "document.body.removeChild(a);";
    html2 += "URL.revokeObjectURL(url);";
    html2 += "console.log('Download triggered');}else{";
    html2 += "const err=await res.text();";
    html2 += "console.error('Download failed:',res.status,err);";
    html2 += "alert('Failed to download template: '+res.status);}}";
    html2 += "catch(e){";
    html2 += "console.error('Download error:',e);";
    html2 += "alert('Download error: '+e.message);}}";

    // Play selected built-in animation from dropdown
    html2 += "function playSelectedBuiltIn(){";
    html2 += "const select=document.getElementById('builtin-animation-select');";
    html2 += "if(!select||!select.value)return;";
    html2 += "const duration=parseInt(document.getElementById('test-duration').value)||5000;";
    html2 += "playBuiltInAnimation(select.value,duration);}";

    // Download selected animation as template
    html2 += "function downloadSelectedTemplate(){";
    html2 += "const select=document.getElementById('builtin-animation-select');";
    html2 += "if(!select||!select.value)return;";
    html2 += "downloadTemplate(select.value);}";

    // Assign selected built-in animation to a function
    html2 += "function assignSelectedBuiltIn(){";
    html2 += "const select=document.getElementById('builtin-animation-select');";
    html2 += "if(!select||!select.value)return;";
    html2 += "const functions=['motion-alert','battery-low','boot-status','wifi-connected'];";
    html2 += "const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];";
    html2 += "let message='Assign \"'+select.selectedOptions[0].text+'\" to which function?\\n\\n';";
    html2 += "for(let i=0;i<functions.length;i++){";
    html2 += "message+=(i+1)+'. '+functionNames[i]+'\\n';}";
    html2 += "const choice=prompt(message,'1');";
    html2 += "if(!choice)return;";
    html2 += "const idx=parseInt(choice)-1;";
    html2 += "if(idx>=0&&idx<functions.length){";
    html2 += "assignAnimation(functions[idx],'builtin',select.value);}}";

    // Assign animation to a function
    html2 += "async function assignAnimation(functionKey,type,animName){";
    html2 += "try{";
    html2 += "const res=await fetch('/api/animations/assign',{";
    html2 += "method:'POST',";
    html2 += "headers:{'Content-Type':'application/json'},";
    html2 += "body:JSON.stringify({function:functionKey,type:type,animation:animName})});";
    html2 += "if(res.ok){";
    html2 += "updateActiveAnimations();";
    html2 += "alert('Animation assigned successfully');}";
    html2 += "else{";
    html2 += "const err=await res.text();";
    html2 += "alert('Failed to assign animation: '+err);}}";
    html2 += "catch(e){alert('Assignment error: '+e.message);}}";

    // Update active animations display
    html2 += "function updateActiveAnimations(){";
    html2 += "fetch('/api/animations/assignments')";
    html2 += ".then(res=>res.json())";
    html2 += ".then(data=>{";
    html2 += "const panel=document.getElementById('active-animations-panel');";
    html2 += "if(panel){panel.style.display='block';}";
    html2 += "if(data['motion-alert']){";
    html2 += "const elem=document.getElementById('anim-motion-alert');";
    html2 += "if(elem)elem.textContent=data['motion-alert'].type==='builtin'?'Built-in: '+data['motion-alert'].name:data['motion-alert'].name;}";
    html2 += "if(data['battery-low']){";
    html2 += "const elem=document.getElementById('anim-battery-low');";
    html2 += "if(elem)elem.textContent=data['battery-low'].type==='builtin'?'Built-in: '+data['battery-low'].name:data['battery-low'].name;}";
    html2 += "if(data['boot-status']){";
    html2 += "const elem=document.getElementById('anim-boot-status');";
    html2 += "if(elem)elem.textContent=data['boot-status'].type==='builtin'?'Built-in: '+data['boot-status'].name:data['boot-status'].name;}";
    html2 += "if(data['wifi-connected']){";
    html2 += "const elem=document.getElementById('anim-wifi-connected');";
    html2 += "if(elem)elem.textContent=data['wifi-connected'].type==='builtin'?'Built-in: '+data['wifi-connected'].name:data['wifi-connected'].name;}";
    html2 += "})";
    html2 += ".catch(e=>console.error('Failed to load assignments:',e));}";

    // Play custom animation with duration from input
    html2 += "function playCustomAnimation(name){";
    html2 += "const duration=parseInt(document.getElementById('test-duration').value)||5000;";
    html2 += "playAnimation(name,duration);}";

    // Assign custom animation to a function
    html2 += "function assignCustomAnimation(name){";
    html2 += "const functions=['motion-alert','battery-low','boot-status','wifi-connected'];";
    html2 += "const functionNames=['Motion Alert','Battery Low','Boot Status','WiFi Connected'];";
    html2 += "let message='Assign \"'+name+'\" to which function?\\n\\n';";
    html2 += "for(let i=0;i<functions.length;i++){";
    html2 += "message+=(i+1)+'. '+functionNames[i]+'\\n';}";
    html2 += "const choice=prompt(message,'1');";
    html2 += "if(!choice)return;";
    html2 += "const idx=parseInt(choice)-1;";
    html2 += "if(idx>=0&&idx<functions.length){";
    html2 += "assignAnimation(functions[idx],'custom',name);}}";

    // Firmware OTA Functions
    html2 += "function validateFirmware(){";
    html2 += "const file=document.getElementById('firmware-file').files[0];";
    html2 += "const btn=document.getElementById('upload-btn');";
    html2 += "if(!file){btn.disabled=true;return;}";
    html2 += "if(file.size<100000){alert('File too small - not a valid firmware');btn.disabled=true;return;}";
    html2 += "if(file.size>2000000){alert('File too large - exceeds 2MB limit');btn.disabled=true;return;}";
    html2 += "btn.disabled=false;}";

    html2 += "function uploadFirmware(){";
    html2 += "const file=document.getElementById('firmware-file').files[0];";
    html2 += "if(!file){alert('Please select a firmware file');return;}";
    html2 += "if(!confirm('Upload firmware and reboot device?\\n\\nThis will restart the device.'))return;";
    html2 += "const formData=new FormData();";
    html2 += "formData.append('firmware',file);";
    html2 += "const xhr=new XMLHttpRequest();";
    html2 += "const progressDiv=document.getElementById('upload-progress');";
    html2 += "const progressBar=document.getElementById('upload-bar');";
    html2 += "const statusDiv=document.getElementById('upload-status');";
    html2 += "const uploadBtn=document.getElementById('upload-btn');";
    html2 += "progressDiv.style.display='block';";
    html2 += "uploadBtn.disabled=true;";
    html2 += "xhr.upload.addEventListener('progress',(e)=>{";
    html2 += "if(e.lengthComputable){";
    html2 += "const percent=Math.round((e.loaded/e.total)*100);";
    html2 += "progressBar.value=percent;";
    html2 += "statusDiv.textContent='Uploading... '+percent+'%';}});";
    html2 += "xhr.addEventListener('load',()=>{";
    html2 += "if(xhr.status===200){";
    html2 += "statusDiv.textContent='Success! Device rebooting...';";
    html2 += "statusDiv.style.color='#10b981';";
    html2 += "setTimeout(()=>{";
    html2 += "alert('Firmware updated. Device is rebooting.\\nReconnect in 30 seconds.');";
    html2 += "window.location.reload();},3000);}";
    html2 += "else{";
    html2 += "statusDiv.textContent='Failed: '+xhr.responseText;";
    html2 += "statusDiv.style.color='#ef4444';";
    html2 += "uploadBtn.disabled=false;}});";
    html2 += "xhr.addEventListener('error',()=>{";
    html2 += "statusDiv.textContent='Upload error - check connection';";
    html2 += "statusDiv.style.color='#ef4444';";
    html2 += "uploadBtn.disabled=false;});";
    html2 += "xhr.open('POST','/api/ota/upload');";
    html2 += "xhr.send(formData);}";

    html2 += "fetch('/api/ota/status')";
    html2 += ".then(r=>r.json())";
    html2 += ".then(data=>{";
    html2 += "const versionElem=document.getElementById('current-version');";
    html2 += "const partitionElem=document.getElementById('current-partition');";
    html2 += "const maxSizeElem=document.getElementById('max-firmware-size');";
    html2 += "if(versionElem)versionElem.textContent=data.currentVersion||'Unknown';";
    html2 += "if(partitionElem)partitionElem.textContent='Partition: '+data.currentPartition;";
    html2 += "if(maxSizeElem)maxSizeElem.textContent='Max size: '+(data.maxFirmwareSize/1024).toFixed(0)+' KB';})";
    html2 += ".catch(e=>console.error('Failed to load OTA status:',e));";

    // Auto-refresh status
    html2 += "fetchStatus();";
    html2 += "setInterval(fetchStatus,2000);";

    // Load animation assignments and log list on page load
    html2 += "updateActiveAnimations();";
    html2 += "loadAvailableLogs();";

    html2 += "</script></body></html>";

    LOG_DEBUG("buildDashboardHTML() complete: part1=%u part2=%u total=%u bytes, free heap: %u",
              g_htmlPart1.length(), g_htmlPart2.length(),
              g_htmlPart1.length() + g_htmlPart2.length(), ESP.getFreeHeap());

    // Verify part 2 is complete (it holds the closing </html>)
    if (g_htmlPart2.endsWith("</html>")) {
        LOG_INFO("HTML is complete (ends with </html>)");
    } else {
        LOG_ERROR("HTML TRUNCATED! Part2 last 20 chars: %s",
                  g_htmlPart2.substring(g_htmlPart2.length()-20).c_str());
    }
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
    s_inWebSocketEvent = true;
    switch(type) {
        case WS_EVT_CONNECT:
            // Enforce connection limit
            if (server->count() > m_maxWebSocketClients) {
                client->close();
                s_inWebSocketEvent = false;
                DEBUG_LOG_API("WebSocket client rejected (max %u)", m_maxWebSocketClients);
                return;
            }

            {
                uint32_t freeHeap = ESP.getFreeHeap();
                DEBUG_LOG_API("WebSocket client #%u connected (free heap: %u)", client->id(), freeHeap);

                // Scale catchup count by available heap.  After the dashboard
                // HTML is cached (~65 KB) free heap is typically ~42 KB.  Each
                // queued WebSocket frame stays on the heap until TCP drains it,
                // so flooding 50 frames into a fragmented 42 KB heap crashes.
                uint32_t maxCatchup;
                if (freeHeap < 35000) {
                    maxCatchup = 0;   // Too low — skip catchup entirely
                } else if (freeHeap < 45000) {
                    maxCatchup = 10;  // Reduced catchup
                } else {
                    maxCatchup = 50;  // Normal catchup
                }

                uint32_t count = g_logger.getEntryCount();
                uint32_t start = count > maxCatchup ? count - maxCatchup : 0;
                for (uint32_t i = start; i < count; i++) {
                    // Bail out if heap drops too low mid-catchup
                    if (ESP.getFreeHeap() < 25000) {
                        DEBUG_LOG_API("WebSocket catchup aborted at entry %u (heap: %u)", i, ESP.getFreeHeap());
                        break;
                    }
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
    s_inWebSocketEvent = false;
}

String WebAPI::formatLogEntryJSON(const Logger::LogEntry& entry, const char* source) {
    StaticJsonDocument<256> doc;
    doc["seq"] = entry.sequenceNumber;
    doc["ts"] = entry.timestamp;
    doc["wallTs"] = entry.wallTimestamp;
    doc["level"] = entry.level;
    doc["levelName"] = Logger::getLevelName((Logger::LogLevel)entry.level);
    doc["msg"] = entry.message;
    doc["source"] = source;

    String json;
    serializeJson(doc, json);
    return json;
}

void WebAPI::broadcastLogEntry(const Logger::LogEntry& entry, const char* source) {
    // Reentrancy guard: both Logger and DebugLogger call broadcastLogEntry
    // synchronously whenever a log entry is created.  If we are already inside
    // handleLogWebSocketEvent (e.g. a DEBUG_LOG_API call in the connect handler),
    // calling textAll() here would re-enter the WebSocket internals and crash.
    if (s_inWebSocketEvent) {
        return;
    }

    if (!m_logWebSocket) {
        Serial.println("[WS] broadcastLogEntry: m_logWebSocket is NULL");
        return;
    }

    int clientCount = m_logWebSocket->count();
    if (clientCount == 0) {
        return;  // No clients connected (normal, don't spam)
    }

    // Skip broadcast if heap is too low to safely queue frame buffers
    if (ESP.getFreeHeap() < 25000) {
        return;
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
