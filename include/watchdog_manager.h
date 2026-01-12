#ifndef STEPAWARE_WATCHDOG_MANAGER_H
#define STEPAWARE_WATCHDOG_MANAGER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Watchdog Manager for System Health Monitoring
 *
 * Monitors all critical modules during runtime to ensure system health
 * and automatically recover from failures. Integrates with ESP32 hardware
 * watchdog timer as last line of defense against system lockup.
 *
 * Features:
 * - Hardware watchdog timer management (feeds when system healthy)
 * - Per-module health monitoring
 * - Automatic recovery actions (soft recovery, module restart, system reboot)
 * - Failure tracking and logging
 * - Configurable check intervals and thresholds
 *
 * Usage:
 * ```cpp
 * WatchdogManager watchdog;
 * watchdog.begin();
 * watchdog.registerModule(MODULE_STATE_MACHINE, checkStateMachineHealth);
 *
 * void loop() {
 *     watchdog.update();  // Checks health, feeds HW WDT if OK
 *     // ... other code
 * }
 * ```
 */
class WatchdogManager {
public:
    /**
     * @brief Module identifiers
     */
    enum ModuleID {
        MODULE_STATE_MACHINE,
        MODULE_CONFIG_MANAGER,
        MODULE_LOGGER,
        MODULE_HAL_BUTTON,
        MODULE_HAL_LED,
        MODULE_HAL_PIR,
        MODULE_WEB_SERVER,
        MODULE_MEMORY,
        MODULE_COUNT  // Must be last
    };

    /**
     * @brief Module health status levels
     */
    enum HealthStatus {
        HEALTH_OK,        ///< Module operating normally
        HEALTH_WARNING,   ///< Minor issues, may need attention
        HEALTH_CRITICAL,  ///< Severe issues, recovery needed soon
        HEALTH_FAILED     ///< Module failed, recovery needed now
    };

    /**
     * @brief Recovery action types
     */
    enum RecoveryAction {
        RECOVERY_NONE,           ///< No action taken
        RECOVERY_SOFT,           ///< Reset module state, clear buffers
        RECOVERY_MODULE_RESTART, ///< Deinitialize and reinitialize module
        RECOVERY_SYSTEM_REBOOT,  ///< Controlled system reboot
        RECOVERY_HW_WATCHDOG     ///< Stop feeding WDT, trigger HW reset
    };

    /**
     * @brief Module health information
     */
    struct ModuleHealth {
        HealthStatus status;      ///< Current health status
        uint32_t lastCheckTime;   ///< Last health check timestamp (ms)
        uint32_t failureCount;    ///< Consecutive failure count
        uint32_t totalFailures;   ///< Total failures since boot
        const char* message;      ///< Optional status message
    };

    /**
     * @brief Health check function signature
     *
     * Module-specific health check function that returns current status.
     * Should be fast (< 10ms) and non-blocking.
     *
     * @param message Output parameter for optional status message
     * @return Current health status
     */
    typedef HealthStatus (*HealthCheckFunc)(const char** message);

    /**
     * @brief Recovery function signature
     *
     * Module-specific recovery function called when module fails.
     *
     * @param action Type of recovery action to perform
     * @return True if recovery succeeded
     */
    typedef bool (*RecoveryFunc)(RecoveryAction action);

    /**
     * @brief Watchdog configuration
     */
    struct Config {
        uint32_t hardwareTimeoutMs;           ///< Hardware WDT timeout (default: 8000ms)
        uint32_t memoryCheckIntervalMs;       ///< Memory check interval (default: 1000ms)
        uint32_t stateMachineCheckIntervalMs; ///< State machine check interval (default: 5000ms)
        uint32_t halCheckIntervalMs;          ///< HAL check interval (default: 10000ms)
        uint32_t configCheckIntervalMs;       ///< Config check interval (default: 60000ms)
        uint32_t loggerCheckIntervalMs;       ///< Logger check interval (default: 10000ms)
        uint32_t webServerCheckIntervalMs;    ///< Web server check interval (default: 30000ms)

        uint8_t softRecoveryThreshold;        ///< Failures before soft recovery (default: 2)
        uint8_t moduleRestartThreshold;       ///< Failures before module restart (default: 5)
        uint8_t systemRecoveryThreshold;      ///< Failures before system reboot (default: 10)

        uint32_t memoryWarningBytes;          ///< Free heap warning threshold (default: 50000)
        uint32_t memoryCriticalBytes;         ///< Free heap critical threshold (default: 10000)

        bool enableMemoryCheck;               ///< Enable memory health check (default: true)
        bool enableStateMachineCheck;         ///< Enable state machine check (default: true)
        bool enableHALCheck;                  ///< Enable HAL check (default: true)
        bool enableConfigCheck;               ///< Enable config check (default: true)
        bool enableLoggerCheck;               ///< Enable logger check (default: true)
        bool enableWebServerCheck;            ///< Enable web server check (default: true)
    };

    /**
     * @brief Construct watchdog manager
     */
    WatchdogManager();

    /**
     * @brief Destructor
     */
    ~WatchdogManager();

    /**
     * @brief Initialize watchdog system
     *
     * Initializes hardware watchdog timer and sets up monitoring.
     *
     * @param config Optional configuration (uses defaults if not provided)
     * @return True if initialization successful
     */
    bool begin(const Config* config = nullptr);

    /**
     * @brief Update watchdog (call every loop iteration)
     *
     * Checks module health and feeds hardware watchdog if system is healthy.
     * This must be called frequently (< 1 second) to prevent HW WDT timeout.
     */
    void update();

    /**
     * @brief Register module for monitoring
     *
     * @param id Module identifier
     * @param checkFunc Health check function
     * @param recoveryFunc Optional recovery function (nullptr for none)
     */
    void registerModule(ModuleID id, HealthCheckFunc checkFunc, RecoveryFunc recoveryFunc = nullptr);

    /**
     * @brief Manually report module health
     *
     * Use when module detects its own health issues.
     *
     * @param id Module identifier
     * @param status Health status
     * @param message Optional status message
     */
    void reportHealth(ModuleID id, HealthStatus status, const char* message = nullptr);

    /**
     * @brief Get overall system health
     *
     * Returns worst health status across all monitored modules.
     *
     * @return System health status
     */
    HealthStatus getSystemHealth() const;

    /**
     * @brief Get specific module health
     *
     * @param id Module identifier
     * @return Module health status
     */
    HealthStatus getModuleHealth(ModuleID id) const;

    /**
     * @brief Get module health information
     *
     * @param id Module identifier
     * @return Module health info (nullptr if module not registered)
     */
    const ModuleHealth* getModuleInfo(ModuleID id) const;

    /**
     * @brief Check if system is healthy
     *
     * System is healthy if all modules are OK or WARNING.
     *
     * @return True if system healthy
     */
    bool isHealthy() const;

    /**
     * @brief Manually trigger recovery for module
     *
     * @param id Module identifier
     * @param action Recovery action to perform
     * @return True if recovery succeeded
     */
    bool triggerRecovery(ModuleID id, RecoveryAction action);

    /**
     * @brief Reset module failure count
     *
     * @param id Module identifier
     */
    void resetFailureCount(ModuleID id);

    /**
     * @brief Get module name string
     *
     * @param id Module identifier
     * @return Module name
     */
    static const char* getModuleName(ModuleID id);

    /**
     * @brief Get health status string
     *
     * @param status Health status
     * @return Status name
     */
    static const char* getHealthStatusName(HealthStatus status);

    /**
     * @brief Get recovery action string
     *
     * @param action Recovery action
     * @return Action name
     */
    static const char* getRecoveryActionName(RecoveryAction action);

private:
    struct ModuleInfo {
        ModuleID id;
        HealthCheckFunc checkFunc;
        RecoveryFunc recoveryFunc;
        uint32_t checkInterval;
        uint32_t nextCheckTime;
        bool enabled;
        ModuleHealth health;
    };

    Config m_config;                      ///< Watchdog configuration
    ModuleInfo m_modules[MODULE_COUNT];   ///< Module information
    uint32_t m_lastHWFeedTime;            ///< Last HW WDT feed time
    bool m_initialized;                   ///< Initialization status
    bool m_systemHealthy;                 ///< Overall system health flag

    /**
     * @brief Feed hardware watchdog timer
     */
    void feedHardwareWatchdog();

    /**
     * @brief Check all module health
     */
    void checkModulesHealth();

    /**
     * @brief Check specific module health
     *
     * @param info Module information
     */
    void checkModuleHealth(ModuleInfo& info);

    /**
     * @brief Handle module failure
     *
     * @param info Module information
     */
    void handleModuleFailure(ModuleInfo& info);

    /**
     * @brief Determine recovery action based on failure count
     *
     * @param failureCount Number of consecutive failures
     * @return Recommended recovery action
     */
    RecoveryAction determineRecoveryAction(uint32_t failureCount) const;

    /**
     * @brief Execute recovery action
     *
     * @param info Module information
     * @param action Recovery action to perform
     * @return True if recovery succeeded
     */
    bool executeRecovery(ModuleInfo& info, RecoveryAction action);

    /**
     * @brief Update system health flag
     */
    void updateSystemHealth();

    /**
     * @brief Get check interval for module
     *
     * @param id Module identifier
     * @return Check interval in milliseconds
     */
    uint32_t getCheckInterval(ModuleID id) const;

    /**
     * @brief Check if module monitoring is enabled
     *
     * @param id Module identifier
     * @return True if enabled
     */
    bool isModuleEnabled(ModuleID id) const;
};

// Global watchdog instance (defined in watchdog_manager.cpp)
extern WatchdogManager g_watchdog;

#endif // STEPAWARE_WATCHDOG_MANAGER_H
