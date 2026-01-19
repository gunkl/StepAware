#include "web_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "watchdog_manager.h"
#include <ArduinoJson.h>

WebAPI::WebAPI(AsyncWebServer* server, StateMachine* stateMachine, ConfigManager* config)
    : m_server(server)
    , m_stateMachine(stateMachine)
    , m_config(config)
    , m_wifi(nullptr)
    , m_power(nullptr)
    , m_watchdog(nullptr)
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

    // Root endpoint - simple landing page
    m_server->on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        String html = "<!DOCTYPE html><html><head><title>StepAware</title>";
        html += "<style>body{font-family:sans-serif;max-width:800px;margin:40px auto;padding:20px;}";
        html += "h1{color:#333;}a{color:#0066cc;}pre{background:#f4f4f4;padding:15px;overflow-x:auto;}</style></head>";
        html += "<body><h1>StepAware</h1>";
        html += "<p>Motion-activated hazard warning system</p>";
        html += "<h2>API Endpoints</h2><ul>";
        html += "<li><a href='/api/status'>GET /api/status</a> - System status</li>";
        html += "<li><a href='/api/config'>GET /api/config</a> - Configuration</li>";
        html += "<li><a href='/api/mode'>GET /api/mode</a> - Operating mode</li>";
        html += "<li><a href='/api/logs'>GET /api/logs</a> - Recent logs</li>";
        html += "<li><a href='/api/version'>GET /api/version</a> - Firmware version</li>";
        html += "<li>POST /api/config - Update configuration</li>";
        html += "<li>POST /api/mode - Set operating mode</li>";
        html += "<li>POST /api/reset - Factory reset</li>";
        html += "</ul>";
        html += "<h2>Quick Status</h2><pre id='status'>Loading...</pre>";
        html += "<script>fetch('/api/status').then(r=>r.json()).then(d=>document.getElementById('status').textContent=JSON.stringify(d,null,2));</script>";
        html += "</body></html>";
        req->send(200, "text/html", html);
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
    uint32_t maxEntries = 50;  // Limit to last 50 entries

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
