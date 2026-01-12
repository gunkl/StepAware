#ifndef STEPAWARE_WEB_API_H
#define STEPAWARE_WEB_API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
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

private:
    AsyncWebServer* m_server;              ///< Web server instance
    StateMachine* m_stateMachine;          ///< State machine reference
    ConfigManager* m_config;               ///< Config manager reference
    bool m_corsEnabled;                    ///< CORS enabled flag

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
     * @brief POST /api/reset - Factory reset
     */
    void handlePostReset(AsyncWebServerRequest* request);

    /**
     * @brief GET /api/version - Version info
     */
    void handleGetVersion(AsyncWebServerRequest* request);

    /**
     * @brief OPTIONS handler for CORS preflight
     */
    void handleOptions(AsyncWebServerRequest* request);

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
};

#endif // STEPAWARE_WEB_API_H
