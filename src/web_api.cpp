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

    // Status grid
    html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;margin-bottom:20px;}";
    html += ".status-item{background:#f7f9fc;padding:16px;border-radius:8px;border-left:4px solid #667eea;}";
    html += ".status-label{font-size:0.85em;color:#666;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:4px;}";
    html += ".status-value{font-size:1.5em;font-weight:600;color:#333;}";
    html += ".status-value.warning{color:#f59e0b;}.status-value.active{color:#10b981;}.status-value.inactive{color:#6b7280;}";

    // Buttons
    html += ".mode-buttons{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-top:16px;}";
    html += ".btn{padding:16px;border:none;border-radius:8px;font-size:1em;font-weight:600;cursor:pointer;transition:all 0.3s;color:white;}";
    html += ".btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.15);}";
    html += ".btn-off{background:#6b7280;}.btn-off:hover{background:#4b5563;}";
    html += ".btn-always{background:#10b981;}.btn-always:hover{background:#059669;}";
    html += ".btn-motion{background:#3b82f6;}.btn-motion:hover{background:#2563eb;}";
    html += ".btn-primary{background:#667eea;}.btn-primary:hover{background:#5568d3;}";
    html += ".btn-success{background:#10b981;}.btn-success:hover{background:#059669;}";
    html += ".btn.active{box-shadow:0 0 0 3px rgba(102,126,234,0.5);}";
    html += ".btn-small{padding:8px 16px;font-size:0.9em;}";

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
    html += "font-size:0.85em;max-height:400px;overflow-y:auto;line-height:1.6;}";
    html += ".log-entry{margin-bottom:4px;}";
    html += ".log-info{color:#60a5fa;}.log-warn{color:#fbbf24;}.log-error{color:#f87171;}";
    html += "#log-status{margin-bottom:12px;padding:8px;background:#f3f4f6;border-radius:6px;text-align:center;}";

    // Misc
    html += "#wifi-status{display:flex;align-items:center;gap:8px;flex-wrap:wrap;}";
    html += ".config-grid{display:grid;gap:16px;}";
    html += ".save-indicator{display:none;padding:12px;background:#d1fae5;color:#065f46;border-radius:6px;margin-top:16px;text-align:center;font-weight:600;}";
    html += ".save-indicator.show{display:block;}";

    html += "</style></head><body><div class=\"container\">";
    html += "<h1>&#128680; StepAware Dashboard</h1>";

    // Tab Navigation
    html += "<div class=\"tabs\">";
    html += "<button class=\"tab active\" onclick=\"showTab('status')\">Status</button>";
    html += "<button class=\"tab\" onclick=\"showTab('config')\">Configuration</button>";
    html += "<button class=\"tab\" onclick=\"showTab('logs')\">Logs</button>";
    html += "</div>";

    // STATUS TAB
    html += "<div id=\"status-tab\" class=\"tab-content active\">";

    html += "<div class=\"card\"><h2>System Status</h2><div class=\"status-grid\">";
    html += "<div class=\"status-item\"><div class=\"status-label\">Operating Mode</div>";
    html += "<div class=\"status-value\" id=\"mode-display\">Loading...</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Warning Status</div>";
    html += "<div class=\"status-value\" id=\"warning-status\">--</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Motion Events</div>";
    html += "<div class=\"status-value\" id=\"motion-events\">0</div></div>";
    html += "<div class=\"status-item\"><div class=\"status-label\">Uptime</div>";
    html += "<div class=\"status-value\" id=\"uptime\">--</div></div>";
    html += "</div></div>";

    html += "<div class=\"card\"><h2>Control Panel</h2><div class=\"mode-buttons\">";
    html += "<button class=\"btn btn-off\" onclick=\"setMode(0)\" id=\"btn-0\">OFF</button>";
    html += "<button class=\"btn btn-always\" onclick=\"setMode(1)\" id=\"btn-1\">ALWAYS ON</button>";
    html += "<button class=\"btn btn-motion\" onclick=\"setMode(2)\" id=\"btn-2\">MOTION DETECT</button>";
    html += "</div></div>";

    html += "<div class=\"card\"><h2>Network Status</h2><div id=\"wifi-status\">";
    html += "<span class=\"status-label\">WiFi:</span>";
    html += "<span id=\"wifi-state\" class=\"badge badge-info\">--</span>";
    html += "<span id=\"wifi-details\"></span>";
    html += "</div></div>";

    html += "</div>"; // End status tab

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
    html += "<div class=\"form-group\"><label class=\"form-label\">Warning Duration (ms)</label>";
    html += "<input type=\"number\" id=\"cfg-motionWarningDuration\" class=\"form-input\" min=\"1000\" max=\"600000\"></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Sensor Threshold (mm)</label>";
    html += "<input type=\"number\" id=\"cfg-sensorThreshold\" class=\"form-input\" min=\"50\" max=\"4000\"></div>";
    html += "</div>";

    html += "<h3>LED Settings</h3>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-group\"><label class=\"form-label\">Full Brightness (0-255)</label>";
    html += "<input type=\"number\" id=\"cfg-ledBrightnessFull\" class=\"form-input\" min=\"0\" max=\"255\"></div>";
    html += "<div class=\"form-group\"><label class=\"form-label\">Dim Brightness (0-255)</label>";
    html += "<input type=\"number\" id=\"cfg-ledBrightnessDim\" class=\"form-input\" min=\"0\" max=\"255\"></div>";
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
    html += "if(tab==='logs')fetchLogs();";
    html += "}";

    // Status fetching (existing)
    html += "let currentMode=-1;";
    html += "async function fetchStatus(){";
    html += "if(document.getElementById('status-tab').classList.contains('active')){";
    html += "try{const res=await fetch('/api/status');const data=await res.json();";
    html += "currentMode=data.stateMachine.mode;";
    html += "document.getElementById('mode-display').textContent=data.stateMachine.modeName;";
    html += "const warningEl=document.getElementById('warning-status');";
    html += "if(data.stateMachine.warningActive){warningEl.textContent='ACTIVE';warningEl.className='status-value warning';}";
    html += "else{warningEl.textContent='Inactive';warningEl.className='status-value inactive';}";
    html += "document.getElementById('motion-events').textContent=data.stateMachine.motionEvents;";
    html += "const uptime=Math.floor(data.uptime/1000);";
    html += "const hours=Math.floor(uptime/3600);const mins=Math.floor((uptime%3600)/60);const secs=uptime%60;";
    html += "document.getElementById('uptime').textContent=hours+'h '+mins+'m '+secs+'s';";
    html += "if(data.wifi){const badge=document.getElementById('wifi-state');const details=document.getElementById('wifi-details');";
    html += "if(data.wifi.state===3){badge.textContent='Connected';badge.className='badge badge-success';";
    html += "details.textContent=data.wifi.ssid+' ('+data.wifi.ipAddress+') '+data.wifi.rssi+' dBm';}";
    html += "else if(data.wifi.state===2){badge.textContent='Connecting...';badge.className='badge badge-warning';details.textContent='';}";
    html += "else{badge.textContent=data.wifi.stateName;badge.className='badge badge-error';details.textContent='';}}";
    html += "for(let i=0;i<=2;i++){const btn=document.getElementById('btn-'+i);";
    html += "if(i===currentMode)btn.classList.add('active');else btn.classList.remove('active');}";
    html += "}catch(e){console.error('Status fetch error:',e);}}}";

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
    html += "document.getElementById('cfg-motionWarningDuration').value=cfg.motion?.warningDuration||30000;";
    html += "document.getElementById('cfg-sensorThreshold').value=cfg.sensor?.minDistance||600;";
    html += "document.getElementById('cfg-ledBrightnessFull').value=cfg.led?.brightnessFull||255;";
    html += "document.getElementById('cfg-ledBrightnessDim').value=cfg.led?.brightnessDim||50;";
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
    html += "motion:{warningDuration:parseInt(document.getElementById('cfg-motionWarningDuration').value)},";
    html += "sensor:{minDistance:parseInt(document.getElementById('cfg-sensorThreshold').value)},";
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

    // Log fetching
    html += "async function fetchLogs(){";
    html += "try{document.getElementById('log-status').textContent='Fetching logs...';";
    html += "const res=await fetch('/api/logs?limit=100');const data=await res.json();";
    html += "const viewer=document.getElementById('log-viewer');viewer.innerHTML='';";
    html += "if(data.logs && data.logs.length>0){";
    html += "data.logs.forEach(log=>{const div=document.createElement('div');div.className='log-entry';";
    html += "let cls='log-info';";
    html += "if(log.levelName==='WARN')cls='log-warn';";
    html += "if(log.levelName==='ERROR')cls='log-error';";
    html += "const time=new Date(log.timestamp).toLocaleTimeString();";
    html += "const msg='['+time+'] ['+log.levelName+'] '+log.message;";
    html += "div.innerHTML='<span class=\"'+cls+'\">'+msg+'</span>';viewer.appendChild(div);});";
    html += "document.getElementById('log-status').textContent='Showing '+data.logs.length+' of '+data.count+' logs - Last updated: '+new Date().toLocaleTimeString();";
    html += "}else{viewer.innerHTML='<div style=\"color:#94a3b8;\">No logs available. Check log level in Configuration tab.</div>';";
    html += "document.getElementById('log-status').textContent='No logs found';}}";
    html += "catch(e){document.getElementById('log-status').textContent='Error fetching logs: '+e.message;console.error(e);}}";

    html += "function clearLogView(){document.getElementById('log-viewer').innerHTML='';}";

    // Auto-refresh status
    html += "fetchStatus();setInterval(fetchStatus,2000);";

    html += "</script></body></html>";

    return html;
}
