#include "watchdog_manager.h"
#include "state_machine.h"
#include "config_manager.h"
#include "logger.h"
#include "debug_logger.h"
#include "web_api.h"
#include "wifi_manager.h"
#include <Arduino.h>

#ifndef MOCK_MODE
#include <SPIFFS.h>
#endif

// External references to global instances
extern StateMachine g_stateMachine;
extern ConfigManager g_config;
extern Logger g_logger;
extern WiFiManager g_wifi;

// Module-specific state tracking
namespace {
    uint32_t g_lastStateMachineUpdate = 0;
    StateMachine::OperatingMode g_lastMode = StateMachine::OFF;
    uint32_t g_modeEnterTime = 0;
}

/**
 * @brief Check memory health
 */
WatchdogManager::HealthStatus checkMemoryHealth(const char** message) {
    uint32_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < 10000) {  // < 10KB
        if (message) *message = "Critical: < 10KB free";
        return WatchdogManager::HEALTH_CRITICAL;
    } else if (freeHeap < 50000) {  // < 50KB
        if (message) {
            static char msg[32];
            snprintf(msg, sizeof(msg), "Warning: %uKB free", freeHeap / 1024);
            *message = msg;
        }
        return WatchdogManager::HEALTH_WARNING;
    }

    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check state machine health
 */
WatchdogManager::HealthStatus checkStateMachineHealth(const char** message) {
    uint32_t now = millis();
    StateMachine::OperatingMode currentMode = g_stateMachine.getMode();

    // Check if state machine is being updated
    uint32_t timeSinceUpdate = now - g_lastStateMachineUpdate;
    if (timeSinceUpdate > 10000) {  // Not updated in 10 seconds
        if (message) *message = "State machine not updating";
        return WatchdogManager::HEALTH_FAILED;
    }

    // Track mode changes
    if (currentMode != g_lastMode) {
        g_lastMode = currentMode;
        g_modeEnterTime = now;
    }

    // Update last check time
    g_lastStateMachineUpdate = now;

    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check config manager health
 */
WatchdogManager::HealthStatus checkConfigManagerHealth(const char** message) {
    // Check if config is valid
    if (!g_config.validate()) {
        if (message) *message = "Config validation failed";
        return WatchdogManager::HEALTH_FAILED;
    }

#ifndef MOCK_MODE
    // Check SPIFFS availability (on real hardware)
    if (!SPIFFS.begin()) {
        if (message) *message = "SPIFFS mount failed";
        return WatchdogManager::HEALTH_CRITICAL;
    }
#endif

    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check logger health
 */
WatchdogManager::HealthStatus checkLoggerHealth(const char** message) {
    uint32_t entryCount = g_logger.getEntryCount();
    uint32_t maxEntries = 256;  // From logger implementation

    // Check if buffer is getting full
    float usage = (float)entryCount / maxEntries;

    if (usage > 0.95f) {  // > 95% full
        if (message) *message = "Log buffer near full";
        return WatchdogManager::HEALTH_CRITICAL;
    } else if (usage > 0.80f) {  // > 80% full
        if (message) {
            static char msg[32];
            snprintf(msg, sizeof(msg), "Log buffer %d%% full", (int)(usage * 100));
            *message = msg;
        }
        return WatchdogManager::HEALTH_WARNING;
    }

    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check HAL button health
 */
WatchdogManager::HealthStatus checkButtonHealth(const char** message) {
    // HAL button is passive (no active health check needed)
    // Could add: check if button hasn't responded in X time
    // For now, assume healthy if registered
    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check HAL LED health
 */
WatchdogManager::HealthStatus checkLEDHealth(const char** message) {
    // HAL LED is passive (no active health check needed)
    // Could add: verify GPIO can be written
    // For now, assume healthy if registered
    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check HAL PIR health
 */
WatchdogManager::HealthStatus checkPIRHealth(const char** message) {
    // HAL PIR is passive (no active health check needed)
    // Could add: verify GPIO can be read, sensor warmup status
    // For now, assume healthy if registered
    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check web server health
 */
WatchdogManager::HealthStatus checkWebServerHealth(const char** message) {
    // Web server health check
    // Could add: check if server is responding, connection count, etc.
    // For now, assume healthy if registered
    return WatchdogManager::HEALTH_OK;
}

/**
 * @brief Check WiFi manager health
 */
WatchdogManager::HealthStatus checkWiFiHealth(const char** message) {
    WiFiManager::State state = g_wifi.getState();

    switch (state) {
        case WiFiManager::STATE_CONNECTED:
            // Check signal strength
            if (g_wifi.getRSSI() < -85) {
                if (message) *message = "Weak signal";
                return WatchdogManager::HEALTH_WARNING;
            }
            return WatchdogManager::HEALTH_OK;

        case WiFiManager::STATE_CONNECTING:
            return WatchdogManager::HEALTH_OK;  // Normal transitional state

        case WiFiManager::STATE_DISCONNECTED:
            if (message) *message = "Disconnected, will retry";
            return WatchdogManager::HEALTH_WARNING;

        case WiFiManager::STATE_FAILED:
            if (message) *message = "Connection failed";
            return WatchdogManager::HEALTH_CRITICAL;

        case WiFiManager::STATE_AP_MODE:
            return WatchdogManager::HEALTH_OK;  // Normal for setup

        case WiFiManager::STATE_DISABLED:
            return WatchdogManager::HEALTH_OK;  // Intentional

        default:
            return WatchdogManager::HEALTH_FAILED;
    }
}

/**
 * @brief WiFi manager recovery function
 */
bool recoverWiFi(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            // Try reconnecting
            DEBUG_LOG_SYSTEM("Watchdog: Attempting WiFi soft recovery");
            return g_wifi.reconnect();

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            // Restart WiFi subsystem
            DEBUG_LOG_SYSTEM("Watchdog: Attempting WiFi restart");
            g_wifi.disconnect();
            delay(1000);
            return g_wifi.connect();

        default:
            return false;
    }
}

/**
 * @brief Memory recovery function
 */
bool recoverMemory(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            // Try to free memory
            DEBUG_LOG_SYSTEM("Watchdog: Attempting memory recovery");

            // Reduce logger buffer if possible
            // (Logger implementation would need a clearOldEntries() method)

            return true;

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            // Cannot restart memory module
            return false;

        default:
            return false;
    }
}

/**
 * @brief State machine recovery function
 */
bool recoverStateMachine(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            DEBUG_LOG_SYSTEM("Watchdog: Attempting state machine soft recovery");
            // Reset to known good state
            g_stateMachine.setMode(StateMachine::OFF);
            g_modeEnterTime = millis();
            g_lastMode = StateMachine::OFF;
            return true;

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            DEBUG_LOG_SYSTEM("Watchdog: Attempting state machine restart");
            // Reinitialize state machine
            g_stateMachine.begin();
            g_modeEnterTime = millis();
            g_lastMode = g_stateMachine.getMode();
            return true;

        default:
            return false;
    }
}

/**
 * @brief Config manager recovery function
 */
bool recoverConfigManager(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            DEBUG_LOG_SYSTEM("Watchdog: Attempting config manager soft recovery");
            // Reload configuration
            return g_config.load();

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            DEBUG_LOG_SYSTEM("Watchdog: Performing config factory reset");
            // Factory reset
            return g_config.reset(true);

        default:
            return false;
    }
}

/**
 * @brief Logger recovery function
 */
bool recoverLogger(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            DEBUG_LOG_SYSTEM("Watchdog: Attempting logger soft recovery");
            // Could clear old logs if logger supports it
            return true;

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            DEBUG_LOG_SYSTEM("Watchdog: Attempting logger restart");
            // Reinitialize logger
            return g_logger.begin();

        default:
            return false;
    }
}

/**
 * @brief Register all watchdog health checks
 *
 * Call this during system initialization after all modules are initialized.
 */
void registerWatchdogHealthChecks() {
    DEBUG_LOG_SYSTEM("Watchdog: Registering health checks");

    g_watchdog.registerModule(
        WatchdogManager::MODULE_MEMORY,
        checkMemoryHealth,
        recoverMemory
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_STATE_MACHINE,
        checkStateMachineHealth,
        recoverStateMachine
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_CONFIG_MANAGER,
        checkConfigManagerHealth,
        recoverConfigManager
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_LOGGER,
        checkLoggerHealth,
        recoverLogger
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_HAL_BUTTON,
        checkButtonHealth,
        nullptr
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_HAL_LED,
        checkLEDHealth,
        nullptr
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_HAL_PIR,
        checkPIRHealth,
        nullptr
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_WEB_SERVER,
        checkWebServerHealth,
        nullptr
    );

    g_watchdog.registerModule(
        WatchdogManager::MODULE_WIFI_MANAGER,
        checkWiFiHealth,
        recoverWiFi
    );

    DEBUG_LOG_SYSTEM("Watchdog: All health checks registered");
}
