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

    // Root endpoint - full dashboard UI
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
        currentConfig.sensors[slot].rapidSampleCount = sensorObj["rapidSampleCount"] | 5;
        currentConfig.sensors[slot].rapidSampleMs = sensorObj["rapidSampleMs"] | 100;
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

    // Return updated sensor configuration
    char buffer[2048];
    if (!m_config->toJSON(buffer, sizeof(buffer))) {
        sendError(request, 500, "Failed to serialize configuration");
        return;
    }

    sendJSON(request, 200, buffer);
    LOG_INFO("Sensor configuration saved successfully");
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
    html += "</div></div>"; // End hardware tab

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
    html += "if(tab==='hardware')loadSensors();";
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
    html += "async function loadConfig(){";
    html += "try{const res=await fetch('/api/config');const cfg=await res.json();";
    html += "document.getElementById('cfg-deviceName').value=cfg.device?.name||'';";
    html += "document.getElementById('cfg-defaultMode').value=cfg.device?.defaultMode||0;";
    html += "document.getElementById('cfg-wifiSSID').value=cfg.wifi?.ssid||'';";
    html += "if(cfg.wifi?.password&&cfg.wifi.password.length>0){";
    html += "document.getElementById('cfg-wifiPassword').value='';";
    html += "document.getElementById('cfg-wifiPassword').placeholder='••••••••';}";
    html += "else{document.getElementById('cfg-wifiPassword').value='';document.getElementById('cfg-wifiPassword').placeholder='';}";
    html += "document.getElementById('cfg-motionWarningDuration').value=Math.round((cfg.motion?.warningDuration||30000)/1000);";
    html += "document.getElementById('cfg-sensorMinDistance').value=cfg.sensor?.minDistance||30;";
    html += "document.getElementById('cfg-sensorMaxDistance').value=cfg.sensor?.maxDistance||200;";
    html += "document.getElementById('cfg-sensorDirection').value=cfg.sensor?.directionEnabled?1:0;";
    html += "document.getElementById('cfg-sensorSampleCount').value=cfg.sensor?.rapidSampleCount||5;";
    html += "document.getElementById('cfg-sensorSampleInterval').value=cfg.sensor?.rapidSampleMs||100;";
    html += "document.getElementById('cfg-ledBrightnessFull').value=(cfg.led?.brightnessFull!==undefined)?cfg.led.brightnessFull:255;";
    html += "document.getElementById('cfg-ledBrightnessDim').value=(cfg.led?.brightnessDim!==undefined)?cfg.led.brightnessDim:50;";
    html += "document.getElementById('cfg-logLevel').value=cfg.logging?.level||2;";
    html += "document.getElementById('cfg-powerSaving').value=cfg.power?.savingEnabled?1:0;";
    html += "}catch(e){console.error('Config load error:',e);}}";

    // Config saving
    html += "async function saveConfig(e){";
    html += "e.preventDefault();";
    html += "const pwdField=document.getElementById('cfg-wifiPassword');";
    html += "const cfg={device:{name:document.getElementById('cfg-deviceName').value,";
    html += "defaultMode:parseInt(document.getElementById('cfg-defaultMode').value)},";
    html += "wifi:{ssid:document.getElementById('cfg-wifiSSID').value,enabled:true},";
    html += "motion:{warningDuration:parseInt(document.getElementById('cfg-motionWarningDuration').value)*1000},";
    html += "sensor:{minDistance:parseInt(document.getElementById('cfg-sensorMinDistance').value),";
    html += "maxDistance:parseInt(document.getElementById('cfg-sensorMaxDistance').value),";
    html += "directionEnabled:parseInt(document.getElementById('cfg-sensorDirection').value)===1,";
    html += "rapidSampleCount:parseInt(document.getElementById('cfg-sensorSampleCount').value),";
    html += "rapidSampleMs:parseInt(document.getElementById('cfg-sensorSampleInterval').value)},";
    html += "led:{brightnessFull:parseInt(document.getElementById('cfg-ledBrightnessFull').value),";
    html += "brightnessDim:parseInt(document.getElementById('cfg-ledBrightnessDim').value)},";
    html += "logging:{level:parseInt(document.getElementById('cfg-logLevel').value)},";
    html += "power:{savingEnabled:parseInt(document.getElementById('cfg-powerSaving').value)===1}};";
    html += "if(pwdField.value.length>0){cfg.wifi.password=pwdField.value;}";
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
    html += "ULTRASONIC:{name:'Ultrasonic Distance',pins:2,config:['minDistance','maxDistance','directionEnabled','rapidSampleCount','rapidSampleMs']}};";

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
    html += "(sensor.type===0?'PIR':sensor.type===1?'IR':'ULTRASONIC')+'</span>';";
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
    html += "html+='<div style=\"font-size:0.75em;line-height:1.6;\">';";

    // PIR/IR wiring
    html += "if(sensor.type===0||sensor.type===1){";
    html += "html+='<div style=\"color:#64748b;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>';";
    html += "html+='<div style=\"color:#64748b;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "html+='<div style=\"color:#64748b;\">Sensor OUT → <span style=\"color:#2563eb;font-weight:600;font-family:monospace;\">GPIO '+sensor.primaryPin+'</span></div>';}";

    // Ultrasonic wiring
    html += "else if(sensor.type===2){";
    html += "html+='<div style=\"color:#64748b;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">5V</span></div>';";
    html += "html+='<div style=\"color:#64748b;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>';";
    html += "html+='<div style=\"color:#64748b;\">Sensor TRIG → <span style=\"color:#2563eb;font-weight:600;font-family:monospace;\">GPIO '+sensor.primaryPin+'</span></div>';";
    html += "html+='<div style=\"color:#64748b;\">Sensor ECHO → <span style=\"color:#2563eb;font-weight:600;font-family:monospace;\">GPIO '+sensor.secondaryPin+'</span></div>';}";

    html += "html+='</div></div>';";

    // Configuration column
    html += "html+='<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>';";
    html += "if(sensor.type===0){";
    html += "html+='<div style=\"font-size:0.85em;color:#64748b;\">Warmup: <span style=\"color:#1e293b;\">'+(sensor.warmupMs/1000)+'s</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;color:#64748b;\">Debounce: <span style=\"color:#1e293b;\">'+sensor.debounceMs+'ms</span></div>';}";
    html += "else if(sensor.type===2){";
    html += "html+='<div style=\"font-size:0.85em;color:#64748b;\">Range: <span style=\"color:#1e293b;\">'+sensor.detectionThreshold+'mm</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;color:#64748b;\">Direction: <span style=\"color:#1e293b;\">'+(sensor.enableDirectionDetection?'Enabled':'Disabled')+'</span></div>';";
    html += "html+='<div style=\"font-size:0.85em;color:#64748b;\">Samples: <span style=\"color:#1e293b;\">'+sensor.rapidSampleCount+' @ '+sensor.rapidSampleMs+'ms</span></div>';}";
    html += "html+='</div>';";

    html += "html+='</div>';";  // Close grid

    html += "card.innerHTML=html;";
    html += "return card;}";

    // Add new sensor
    html += "function addSensor(){";
    html += "const freeSlot=sensorSlots.findIndex(s=>s===null);";
    html += "if(freeSlot===-1){alert('Maximum 4 sensors allowed. Remove a sensor first.');return;}";
    html += "const type=prompt('Select sensor type:\\n0 = PIR Motion\\n1 = IR Beam-Break\\n2 = Ultrasonic Distance','0');";
    html += "if(type===null)return;";
    html += "const typeNum=parseInt(type);";
    html += "if(typeNum<0||typeNum>2){alert('Invalid sensor type');return;}";
    html += "const name=prompt('Enter sensor name:','Sensor '+(freeSlot+1));";
    html += "if(!name)return;";
    html += "const pin=parseInt(prompt('Enter primary pin (GPIO number):','5'));";
    html += "if(isNaN(pin)||pin<0||pin>48){alert('Invalid pin number');return;}";
    html += "const sensor={type:typeNum,name:name,primaryPin:pin,enabled:true,isPrimary:freeSlot===0,";
    html += "warmupMs:60000,debounceMs:100,detectionThreshold:300,enableDirectionDetection:true,";
    html += "rapidSampleCount:5,rapidSampleMs:200};";
    html += "if(typeNum===2){";
    html += "const echoPin=parseInt(prompt('Enter echo pin (GPIO number):','14'));";
    html += "if(isNaN(echoPin)||echoPin<0||echoPin>48){alert('Invalid echo pin');return;}";
    html += "sensor.secondaryPin=echoPin;}";
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
    html += "else if(sensor.type===2){";
    html += "const minDist=parseInt(prompt('Min detection distance (mm):',sensor.detectionThreshold));";
    html += "if(!isNaN(minDist)&&minDist>=10)sensor.detectionThreshold=minDist;";
    html += "const dirEn=confirm('Enable direction detection?');sensor.enableDirectionDetection=dirEn;";
    html += "const samples=parseInt(prompt('Rapid sample count:',sensor.rapidSampleCount));";
    html += "if(!isNaN(samples)&&samples>=2&&samples<=20)sensor.rapidSampleCount=samples;";
    html += "const interval=parseInt(prompt('Sample interval (ms):',sensor.rapidSampleMs));";
    html += "if(!isNaN(interval)&&interval>=50&&interval<=1000)sensor.rapidSampleMs=interval;}";
    html += "renderSensors();saveSensors();}";

    // Save sensors to backend
    html += "async function saveSensors(){";
    html += "try{";
    html += "const activeSensors=sensorSlots.filter(s=>s!==null);";
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

    // Auto-refresh status and logs
    html += "fetchStatus();";
    html += "setInterval(fetchStatus,2000);";
    html += "setInterval(()=>{";
    html += "if(document.getElementById('logs-tab').classList.contains('active')){fetchLogs();}";
    html += "},5000);";

    html += "</script></body></html>";

    return html;
}
