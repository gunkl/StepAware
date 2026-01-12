#include "web_api.h"
#include <ArduinoJson.h>

WebAPI::WebAPI(AsyncWebServer* server, StateMachine* stateMachine, ConfigManager* config)
    : m_server(server)
    , m_stateMachine(stateMachine)
    , m_config(config)
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

void WebAPI::handleGetStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> doc;

    // System info
    doc["uptime"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();

    // Operating mode
    doc["mode"] = m_stateMachine->getMode();
    doc["modeName"] = StateMachine::getModeName(m_stateMachine->getMode());

    // Warning status
    doc["warningActive"] = m_stateMachine->isWarningActive();

    // Statistics
    doc["motionEvents"] = m_stateMachine->getMotionEventCount();
    doc["modeChanges"] = m_stateMachine->getModeChangeCount();

    // PIR sensor (if accessible)
    // doc["pirReady"] = pirSensor.isReady();

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
