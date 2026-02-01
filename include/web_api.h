#ifndef STEPAWARE_WEB_API_H
#define STEPAWARE_WEB_API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include "config.h"
#include "config_manager.h"
#include "logger.h"
#include "state_machine.h"

/**
 * @brief Web API Handler for StepAware
 *
 * Provides REST API endpoints for monitoring and controlling the system.
 *
 * Endpoints:
 * - GET  /api/status       - System status
 * - GET  /api/config       - Get configuration
 * - POST /api/config       - Update configuration
 * - GET  /api/mode         - Get current operating mode
 * - POST /api/mode         - Set operating mode
 * - GET  /api/logs         - Get recent log entries
 * - POST /api/sensors      - Update sensor configuration
 * - GET /api/displays       - Get display configuration
 * - POST /api/displays      - Update display configuration
 * - POST /api/reset        - Factory reset
 * - GET  /api/version      - Firmware version info
 *
 * Features:
 * - JSON request/response
 * - CORS support
 * - Error handling
 * - Authentication (future)
 */
class WebAPI {
public:
    /**
     * @brief Construct a new Web API handler
     *
     * @param server AsyncWebServer instance
     * @param stateMachine State machine instance
     * @param config Configuration manager instance
     */
    WebAPI(AsyncWebServer* server, StateMachine* stateMachine, ConfigManager* config);

    /**
     * @brief Set WiFi Manager reference (optional)
     *
     * @param wifi WiFi Manager instance
     */
    void setWiFiManager(class WiFiManager* wifi);

    /**
     * @brief Set Power Manager reference (optional)
     *
     * @param power Power Manager instance
     */
    void setPowerManager(class PowerManager* power);

    /**
     * @brief Set Watchdog Manager reference (optional)
     *
     * @param watchdog Watchdog Manager instance
     */
    void setWatchdogManager(class WatchdogManager* watchdog);

    /**
     * @brief Set LED Matrix reference (optional)
     *
     * @param ledMatrix LED Matrix instance for animation control
     */
    void setLEDMatrix(class HAL_LEDMatrix_8x8* ledMatrix);

    /**
     * @brief Set Sensor Manager reference (optional)
     *
     * @param sensorManager Sensor Manager instance for live config updates
     */
    void setSensorManager(class SensorManager* sensorManager);

    /**
     * @brief Set Direction Detector reference (optional)
     *
     * @param directionDetector Direction Detector instance for dual-PIR status
     */
    void setDirectionDetector(class DirectionDetector* directionDetector);

    /**
     * @brief Destructor
     */
    ~WebAPI();

    /**
     * @brief Initialize API routes
     *
     * Registers all endpoint handlers with the web server.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Enable/disable CORS
     *
     * @param enabled True to enable CORS headers
     */
    void setCORSEnabled(bool enabled);

    /**
     * @brief Broadcast log entry to WebSocket clients
     *
     * @param entry Log entry to broadcast
     * @param source Optional source identifier (defaults to "logger")
     */
    void broadcastLogEntry(const Logger::LogEntry& entry, const char* source = "logger");

private:
    AsyncWebServer* m_server;                  ///< Web server instance
    StateMachine* m_stateMachine;              ///< State machine reference
    ConfigManager* m_config;                   ///< Config manager reference
    class WiFiManager* m_wifi;                 ///< WiFi Manager reference (optional)
    class PowerManager* m_power;               ///< Power Manager reference (optional)
    class WatchdogManager* m_watchdog;         ///< Watchdog Manager reference (optional)
    class HAL_LEDMatrix_8x8* m_ledMatrix;      ///< LED Matrix reference (optional, Issue #12)
    class SensorManager* m_sensorManager;      ///< Sensor Manager reference (optional)
    class DirectionDetector* m_directionDetector; ///< Direction Detector reference (optional, dual-PIR)
    class OTAManager* m_otaManager;            ///< OTA Manager reference (firmware updates)
    bool m_corsEnabled;                        ///< CORS enabled flag
    AsyncWebSocket* m_logWebSocket;            ///< WebSocket for log streaming
    uint32_t m_maxWebSocketClients;            ///< Maximum WebSocket clients (default: 3)

    // Endpoint handlers

    /**
     * @brief GET /api/status - System status
     */
    void handleGetStatus(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/config - Get configuration
     */
    void handleGetConfig(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/config - Update configuration
     */
    void handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief GET /api/mode - Get operating mode
     */
    void handleGetMode(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/mode - Set operating mode
     */
    void handlePostMode(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief GET /api/logs - Get log entries
     */
    void handleGetLogs(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/sensors - Get sensor runtime status including error rates
     */
    void handleGetSensors(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/sensors - Update sensor configuration
     */
    void handlePostSensors(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief POST /api/sensors/:slot/errorrate - Check sensor error rate
     */
    void handleCheckSensorErrorRate(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/displays - Get display configuration
     */
    void handleGetDisplays(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/displays - Update display configuration
     */
    void handlePostDisplays(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief GET /api/animations - Get loaded animations (Issue #12 Phase 2)
     */
    void handleGetAnimations(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/animations/upload - Upload animation file (Issue #12 Phase 2)
     */
    void handleUploadAnimation(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final);

    /**
     * @brief POST /api/animations/play - Play animation (Issue #12 Phase 2)
     */
    void handlePlayAnimation(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief POST /api/animations/builtin - Play built-in animation (Issue #12)
     */
    void handlePlayBuiltInAnimation(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief POST /api/animations/stop - Stop current animation (Issue #12 Phase 2)
     */
    void handleStopAnimation(AsyncWebServerRequest* request);

    /**
     * @brief DELETE /api/animations/:name - Remove animation from memory (Issue #12 Phase 2)
     */
    void handleDeleteAnimation(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/animations/template/:type - Download animation template (Issue #12)
     */
    void handleGetAnimationTemplate(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/animations/assign - Assign animation to function (Issue #12)
     */
    void handleAssignAnimation(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief GET /api/animations/assignments - Get animation assignments (Issue #12)
     */
    void handleGetAssignments(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/reset - Factory reset
     */
    void handlePostReset(AsyncWebServerRequest* request);

    /**
     * @brief POST /api/reboot - Reboot device
     */
    void handlePostReboot(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/version - Version info
     */
    void handleGetVersion(AsyncWebServerRequest* request);

    /**
     * @brief OPTIONS handler for CORS preflight
     */
    void handleOptions(AsyncWebServerRequest* request);

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

    /**
     * @brief POST /api/ota/upload - Upload firmware binary for OTA update
     */
    void handleOTAUpload(AsyncWebServerRequest* request, const String& filename,
                         size_t index, uint8_t* data, size_t len, bool final);

    /**
     * @brief GET /api/ota/status - Get OTA upload status
     */
    void handleGetOTAStatus(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/ota/coredump - Download core dump file
     */
    void handleGetCoredump(AsyncWebServerRequest* request);

    /**
     * @brief DELETE /api/ota/coredump - Clear core dump partition
     */
    void handleClearCoredump(AsyncWebServerRequest* request);

    /**
     * @brief Handle WebSocket events for log streaming
     */
    void handleLogWebSocketEvent(AsyncWebSocket* server,
                                  AsyncWebSocketClient* client,
                                  AwsEventType type, void* arg,
                                  uint8_t* data, size_t len);

    /**
     * @brief Format log entry as JSON
     *
     * @param entry Log entry to format
     * @param source Source identifier (defaults to "logger")
     */
    String formatLogEntryJSON(const Logger::LogEntry& entry, const char* source = "logger");

    // Helper methods

    /**
     * @brief Send JSON response
     *
     * @param request HTTP request
     * @param code HTTP status code
     * @param json JSON string
     */
    void sendJSON(AsyncWebServerRequest* request, int code, const char* json);

    /**
     * @brief Send error response
     *
     * @param request HTTP request
     * @param code HTTP status code
     * @param message Error message
     */
    void sendError(AsyncWebServerRequest* request, int code, const char* message);

    /**
     * @brief Handle root dashboard page
     */
    void handleRoot(AsyncWebServerRequest* request);

    /**
     * @brief Handle live logs popup window
     */
    void handleLiveLogs(AsyncWebServerRequest* request);

    /**
     * @brief Build dashboard HTML
     */
    void buildDashboardHTML();
};

#endif // STEPAWARE_WEB_API_H
