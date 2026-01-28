#include "watchdog_manager.h"
#include "logger.h"
#include "debug_logger.h"
#include <esp_task_wdt.h>

// Global watchdog instance
WatchdogManager g_watchdog;

WatchdogManager::WatchdogManager()
    : m_lastHWFeedTime(0)
    , m_initialized(false)
    , m_systemHealthy(true)
{
    // Initialize default configuration
    m_config.hardwareTimeoutMs = 8000;
    m_config.memoryCheckIntervalMs = 1000;
    m_config.stateMachineCheckIntervalMs = 5000;
    m_config.halCheckIntervalMs = 10000;
    m_config.configCheckIntervalMs = 60000;
    m_config.loggerCheckIntervalMs = 10000;
    m_config.webServerCheckIntervalMs = 30000;
    m_config.wifiCheckIntervalMs = 15000;

    m_config.softRecoveryThreshold = 2;
    m_config.moduleRestartThreshold = 5;
    m_config.systemRecoveryThreshold = 10;

    m_config.memoryWarningBytes = 50000;
    m_config.memoryCriticalBytes = 10000;

    m_config.enableMemoryCheck = true;
    m_config.enableStateMachineCheck = true;
    m_config.enableHALCheck = true;
    m_config.enableConfigCheck = true;
    m_config.enableLoggerCheck = true;
    m_config.enableWebServerCheck = true;
    m_config.enableWiFiCheck = true;

    // Initialize module array
    for (int i = 0; i < MODULE_COUNT; i++) {
        m_modules[i].id = static_cast<ModuleID>(i);
        m_modules[i].checkFunc = nullptr;
        m_modules[i].recoveryFunc = nullptr;
        m_modules[i].checkInterval = 0;
        m_modules[i].nextCheckTime = 0;
        m_modules[i].enabled = false;
        m_modules[i].health.status = HEALTH_OK;
        m_modules[i].health.lastCheckTime = 0;
        m_modules[i].health.failureCount = 0;
        m_modules[i].health.totalFailures = 0;
        m_modules[i].health.message = nullptr;
    }
}

WatchdogManager::~WatchdogManager() {
}

bool WatchdogManager::begin(const Config* config) {
    if (m_initialized) {
        DEBUG_LOG_SYSTEM("Watchdog: Already initialized");
        return true;
    }

    // Apply custom configuration if provided
    if (config) {
        m_config = *config;
    }

    DEBUG_LOG_SYSTEM("Watchdog: Initializing hardware watchdog timer (%ums timeout)", m_config.hardwareTimeoutMs);

#ifndef MOCK_MODE
    // Configure ESP32 hardware watchdog
    esp_task_wdt_init(m_config.hardwareTimeoutMs / 1000, true);  // timeout in seconds, panic on timeout
    esp_task_wdt_add(NULL);  // Add current task to WDT
#endif

    m_lastHWFeedTime = millis();
    m_initialized = true;

    DEBUG_LOG_SYSTEM("Watchdog: Initialized successfully");

    return true;
}

void WatchdogManager::update() {
    if (!m_initialized) {
        return;
    }

    // Check module health
    checkModulesHealth();

    // Update overall system health
    updateSystemHealth();

    // Feed hardware watchdog if system is healthy
    if (m_systemHealthy) {
        feedHardwareWatchdog();
    } else {
        DEBUG_LOG_SYSTEM("Watchdog: System unhealthy, not feeding hardware watchdog");
    }
}

void WatchdogManager::registerModule(ModuleID id, HealthCheckFunc checkFunc, RecoveryFunc recoveryFunc) {
    if (id >= MODULE_COUNT) {
        DEBUG_LOG_SYSTEM("Watchdog: Invalid module ID %d", id);
        return;
    }

    ModuleInfo& info = m_modules[id];
    info.checkFunc = checkFunc;
    info.recoveryFunc = recoveryFunc;
    info.checkInterval = getCheckInterval(id);
    info.nextCheckTime = millis() + info.checkInterval;
    info.enabled = isModuleEnabled(id);

    DEBUG_LOG_SYSTEM("Watchdog: Registered module %s (interval: %ums)", getModuleName(id), info.checkInterval);
}

void WatchdogManager::reportHealth(ModuleID id, HealthStatus status, const char* message) {
    if (id >= MODULE_COUNT) {
        return;
    }

    ModuleInfo& info = m_modules[id];
    info.health.status = status;
    info.health.lastCheckTime = millis();
    info.health.message = message;

    // Handle failures
    if (status == HEALTH_CRITICAL || status == HEALTH_FAILED) {
        info.health.failureCount++;
        info.health.totalFailures++;
        handleModuleFailure(info);
    } else if (status == HEALTH_OK) {
        // Reset failure count on recovery
        if (info.health.failureCount > 0) {
            DEBUG_LOG_SYSTEM("Watchdog: Module %s recovered", getModuleName(id));
            info.health.failureCount = 0;
        }
    }
}

WatchdogManager::HealthStatus WatchdogManager::getSystemHealth() const {
    HealthStatus worst = HEALTH_OK;

    for (int i = 0; i < MODULE_COUNT; i++) {
        if (m_modules[i].enabled && m_modules[i].checkFunc) {
            if (m_modules[i].health.status > worst) {
                worst = m_modules[i].health.status;
            }
        }
    }

    return worst;
}

WatchdogManager::HealthStatus WatchdogManager::getModuleHealth(ModuleID id) const {
    if (id >= MODULE_COUNT) {
        return HEALTH_FAILED;
    }

    return m_modules[id].health.status;
}

const WatchdogManager::ModuleHealth* WatchdogManager::getModuleInfo(ModuleID id) const {
    if (id >= MODULE_COUNT || !m_modules[id].checkFunc) {
        return nullptr;
    }

    return &m_modules[id].health;
}

bool WatchdogManager::isHealthy() const {
    HealthStatus status = getSystemHealth();
    return status == HEALTH_OK || status == HEALTH_WARNING;
}

bool WatchdogManager::triggerRecovery(ModuleID id, RecoveryAction action) {
    if (id >= MODULE_COUNT) {
        return false;
    }

    ModuleInfo& info = m_modules[id];

    DEBUG_LOG_SYSTEM("Watchdog: Manual recovery triggered for %s (action: %s)",
             getModuleName(id), getRecoveryActionName(action));

    return executeRecovery(info, action);
}

void WatchdogManager::resetFailureCount(ModuleID id) {
    if (id >= MODULE_COUNT) {
        return;
    }

    m_modules[id].health.failureCount = 0;
    DEBUG_LOG_SYSTEM("Watchdog: Reset failure count for %s", getModuleName(id));
}

const char* WatchdogManager::getModuleName(ModuleID id) {
    switch (id) {
        case MODULE_STATE_MACHINE: return "STATE_MACHINE";
        case MODULE_CONFIG_MANAGER: return "CONFIG_MANAGER";
        case MODULE_LOGGER: return "LOGGER";
        case MODULE_HAL_BUTTON: return "HAL_BUTTON";
        case MODULE_HAL_LED: return "HAL_LED";
        case MODULE_HAL_PIR: return "HAL_PIR";
        case MODULE_WEB_SERVER: return "WEB_SERVER";
        case MODULE_WIFI_MANAGER: return "WIFI_MANAGER";
        case MODULE_MEMORY: return "MEMORY";
        default: return "UNKNOWN";
    }
}

const char* WatchdogManager::getHealthStatusName(HealthStatus status) {
    switch (status) {
        case HEALTH_OK: return "OK";
        case HEALTH_WARNING: return "WARNING";
        case HEALTH_CRITICAL: return "CRITICAL";
        case HEALTH_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char* WatchdogManager::getRecoveryActionName(RecoveryAction action) {
    switch (action) {
        case RECOVERY_NONE: return "NONE";
        case RECOVERY_SOFT: return "SOFT_RECOVERY";
        case RECOVERY_MODULE_RESTART: return "MODULE_RESTART";
        case RECOVERY_SYSTEM_REBOOT: return "SYSTEM_REBOOT";
        case RECOVERY_HW_WATCHDOG: return "HW_WATCHDOG_RESET";
        default: return "UNKNOWN";
    }
}

void WatchdogManager::feedHardwareWatchdog() {
#ifndef MOCK_MODE
    esp_task_wdt_reset();
#endif
    m_lastHWFeedTime = millis();
}

void WatchdogManager::checkModulesHealth() {
    uint32_t now = millis();

    for (int i = 0; i < MODULE_COUNT; i++) {
        ModuleInfo& info = m_modules[i];

        if (!info.enabled || !info.checkFunc) {
            continue;
        }

        // Check if it's time to check this module
        if (now >= info.nextCheckTime) {
            checkModuleHealth(info);
            info.nextCheckTime = now + info.checkInterval;
        }
    }
}

void WatchdogManager::checkModuleHealth(ModuleInfo& info) {
    const char* message = nullptr;
    HealthStatus status = info.checkFunc(&message);

    // Update health info
    HealthStatus oldStatus = info.health.status;
    info.health.status = status;
    info.health.lastCheckTime = millis();
    info.health.message = message;

    // Log status changes
    if (status != oldStatus) {
        if (status == HEALTH_OK) {
            DEBUG_LOG_SYSTEM("Watchdog: %s is now %s", getModuleName(info.id), getHealthStatusName(status));
        } else if (status == HEALTH_WARNING) {
            DEBUG_LOG_SYSTEM("Watchdog: %s is now %s%s%s",
                     getModuleName(info.id), getHealthStatusName(status),
                     message ? ": " : "", message ? message : "");
        } else {
            DEBUG_LOG_SYSTEM("Watchdog: %s is now %s%s%s",
                      getModuleName(info.id), getHealthStatusName(status),
                      message ? ": " : "", message ? message : "");
        }
    }

    // Handle failures
    if (status == HEALTH_CRITICAL || status == HEALTH_FAILED) {
        info.health.failureCount++;
        info.health.totalFailures++;
        handleModuleFailure(info);
    } else if (status == HEALTH_OK && info.health.failureCount > 0) {
        // Module recovered
        DEBUG_LOG_SYSTEM("Watchdog: %s recovered after %u failures", getModuleName(info.id), info.health.failureCount);
        info.health.failureCount = 0;
    }
}

void WatchdogManager::handleModuleFailure(ModuleInfo& info) {
    RecoveryAction action = determineRecoveryAction(info.health.failureCount);

    DEBUG_LOG_SYSTEM("Watchdog: Module %s failed (failure count: %u, total: %u), attempting %s",
              getModuleName(info.id), info.health.failureCount, info.health.totalFailures,
              getRecoveryActionName(action));

    if (action != RECOVERY_NONE) {
        bool recovered = executeRecovery(info, action);
        if (recovered) {
            DEBUG_LOG_SYSTEM("Watchdog: Recovery succeeded for %s", getModuleName(info.id));
        } else {
            DEBUG_LOG_SYSTEM("Watchdog: Recovery failed for %s", getModuleName(info.id));
        }
    }
}

WatchdogManager::RecoveryAction WatchdogManager::determineRecoveryAction(uint32_t failureCount) const {
    if (failureCount >= m_config.systemRecoveryThreshold) {
        return RECOVERY_HW_WATCHDOG;  // Stop feeding, let HW WDT reset system
    } else if (failureCount >= m_config.moduleRestartThreshold) {
        return RECOVERY_SYSTEM_REBOOT;  // Controlled reboot
    } else if (failureCount >= m_config.softRecoveryThreshold) {
        return RECOVERY_MODULE_RESTART;  // Restart specific module
    } else {
        return RECOVERY_SOFT;  // Try soft recovery first
    }
}

bool WatchdogManager::executeRecovery(ModuleInfo& info, RecoveryAction action) {
    switch (action) {
        case RECOVERY_NONE:
            return true;

        case RECOVERY_SOFT:
            // Soft recovery: let module handle it
            if (info.recoveryFunc) {
                return info.recoveryFunc(RECOVERY_SOFT);
            }
            return false;

        case RECOVERY_MODULE_RESTART:
            // Module restart
            if (info.recoveryFunc) {
                return info.recoveryFunc(RECOVERY_MODULE_RESTART);
            }
            return false;

        case RECOVERY_SYSTEM_REBOOT:
            // Controlled system reboot
            DEBUG_LOG_SYSTEM("Watchdog: Initiating controlled system reboot");
            delay(1000);  // Allow logs to flush
#ifndef MOCK_MODE
            ESP.restart();
#endif
            return true;

        case RECOVERY_HW_WATCHDOG:
            // Stop feeding HW WDT, let it reset the system
            DEBUG_LOG_SYSTEM("Watchdog: Stopping hardware watchdog feed, system will reset in %ums",
                      m_config.hardwareTimeoutMs);
            m_systemHealthy = false;  // Prevent feeding
            return true;

        default:
            return false;
    }
}

void WatchdogManager::updateSystemHealth() {
    HealthStatus worst = getSystemHealth();

    // System is healthy if all modules are OK or WARNING
    bool healthy = (worst == HEALTH_OK || worst == HEALTH_WARNING);

    if (healthy != m_systemHealthy) {
        m_systemHealthy = healthy;

        if (healthy) {
            DEBUG_LOG_SYSTEM("Watchdog: System is now healthy");
        } else {
            DEBUG_LOG_SYSTEM("Watchdog: System is now unhealthy (worst status: %s)",
                      getHealthStatusName(worst));
        }
    }
}

uint32_t WatchdogManager::getCheckInterval(ModuleID id) const {
    switch (id) {
        case MODULE_MEMORY: return m_config.memoryCheckIntervalMs;
        case MODULE_STATE_MACHINE: return m_config.stateMachineCheckIntervalMs;
        case MODULE_HAL_BUTTON:
        case MODULE_HAL_LED:
        case MODULE_HAL_PIR: return m_config.halCheckIntervalMs;
        case MODULE_CONFIG_MANAGER: return m_config.configCheckIntervalMs;
        case MODULE_LOGGER: return m_config.loggerCheckIntervalMs;
        case MODULE_WEB_SERVER: return m_config.webServerCheckIntervalMs;
        case MODULE_WIFI_MANAGER: return m_config.wifiCheckIntervalMs;
        default: return 10000;  // Default: 10 seconds
    }
}

bool WatchdogManager::isModuleEnabled(ModuleID id) const {
    switch (id) {
        case MODULE_MEMORY: return m_config.enableMemoryCheck;
        case MODULE_STATE_MACHINE: return m_config.enableStateMachineCheck;
        case MODULE_HAL_BUTTON:
        case MODULE_HAL_LED:
        case MODULE_HAL_PIR: return m_config.enableHALCheck;
        case MODULE_CONFIG_MANAGER: return m_config.enableConfigCheck;
        case MODULE_LOGGER: return m_config.enableLoggerCheck;
        case MODULE_WEB_SERVER: return m_config.enableWebServerCheck;
        case MODULE_WIFI_MANAGER: return m_config.enableWiFiCheck;
        default: return true;
    }
}
