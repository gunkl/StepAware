#ifndef STEPAWARE_DEBUG_LOGGER_H
#define STEPAWARE_DEBUG_LOGGER_H

#include <Arduino.h>
#include "config.h"

#if !MOCK_HARDWARE
#include <LittleFS.h>
#endif

/**
 * @brief Persistent Debug Logger for StepAware
 *
 * Features:
 * - Persistent storage to LittleFS (survives reboots)
 * - Rolling logs across boot cycles (keeps last 3 boots)
 * - Automatic space management (max 30% of filesystem)
 * - Detailed logging of config, sensors, state machine, LEDs
 * - Download API for remote diagnosis
 * - Boot cycle tracking
 *
 * Log Structure:
 * - /logs/boot_current.log - Current session
 * - /logs/boot_1.log - Previous session
 * - /logs/boot_2.log - 2 sessions ago
 * - /logs/boot_info.txt - Boot cycle metadata
 */
class DebugLogger {
public:
    /**
     * @brief Log levels
     */
    enum LogLevel {
        LEVEL_VERBOSE = 0,  // Very detailed (sensor readings every cycle)
        LEVEL_DEBUG   = 1,  // Debug info (config changes, state transitions)
        LEVEL_INFO    = 2,  // Important events (boot, mode changes)
        LEVEL_WARN    = 3,  // Warnings
        LEVEL_ERROR   = 4,  // Errors only
        LEVEL_NONE    = 5   // Disabled
    };

    /**
     * @brief Log categories for filtering
     */
    enum LogCategory {
        CAT_BOOT       = 0x01,  // Boot/initialization
        CAT_CONFIG     = 0x02,  // Configuration changes
        CAT_SENSOR     = 0x04,  // Sensor readings/events
        CAT_STATE      = 0x08,  // State machine transitions
        CAT_LED        = 0x10,  // LED operations
        CAT_WIFI       = 0x20,  // WiFi/network
        CAT_API        = 0x40,  // Web API calls
        CAT_SYSTEM     = 0x80,  // System events
        CAT_ALL        = 0xFF   // All categories
    };

    DebugLogger();
    ~DebugLogger();

    /**
     * @brief Initialize debug logger
     *
     * @param level Minimum log level
     * @param categoryMask Enabled categories (bitmask)
     * @return true if successful
     */
    bool begin(LogLevel level = LEVEL_ERROR, uint8_t categoryMask = CAT_ALL);

    /**
     * @brief Set log level
     */
    void setLevel(LogLevel level) { m_level = level; }

    /**
     * @brief Get current log level
     */
    LogLevel getLevel() const { return m_level; }

    /**
     * @brief Enable/disable category
     */
    void setCategoryMask(uint8_t mask) { m_categoryMask = mask; }

    /**
     * @brief Get current category mask
     */
    uint8_t getCategoryMask() const { return m_categoryMask; }

    /**
     * @brief Log message
     *
     * @param level Log level
     * @param category Category
     * @param format Printf-style format
     * @param ... Arguments
     */
    void log(LogLevel level, LogCategory category, const char* format, ...);

    /**
     * @brief Log with automatic category (based on context)
     */
    void logBoot(const char* format, ...);
    void logConfig(const char* format, ...);
    void logSensor(const char* format, ...);
    void logState(const char* format, ...);
    void logLED(const char* format, ...);
    void logWiFi(const char* format, ...);
    void logAPI(const char* format, ...);
    void logSystem(const char* format, ...);

    /**
     * @brief Special logging for detailed diagnostics
     */
    void logConfigDump();  // Dump entire config at boot
    void logSensorReading(uint8_t slot, uint32_t distance, bool motion, int8_t direction);
    void logSensorReadingIfChanged(uint8_t slot, uint32_t distance, bool motion, int8_t direction);
    void logStateTransition(const char* from, const char* to, const char* reason);
    void logLEDChange(const char* state, uint8_t brightness);

    /**
     * @brief Flush pending writes to disk
     */
    void flush();

    /**
     * @brief Get current log file path
     */
    const char* getCurrentLogPath() const { return "/logs/boot_current.log"; }

    /**
     * @brief Get log file size
     */
    size_t getLogSize();

    /**
     * @brief Get total logs size
     */
    size_t getTotalLogsSize();

    /**
     * @brief Get filesystem usage percentage
     */
    uint8_t getFilesystemUsage();

    /**
     * @brief Check if logs need rotation
     */
    bool needsRotation();

    /**
     * @brief Rotate logs (call at boot)
     */
    void rotateLogs();

    /**
     * @brief Get boot cycle count
     */
    uint32_t getBootCycle() const { return m_bootCycle; }

    /**
     * @brief Clear all logs
     */
    void clearAllLogs();

    /**
     * @brief Get category name
     */
    static const char* getCategoryName(LogCategory cat);

    /**
     * @brief Get level name
     */
    static const char* getLevelName(LogLevel level);

private:
    // Sensor state tracking for change detection
    struct SensorState {
        uint32_t lastDistance;
        bool lastMotion;
        int8_t lastDirection;
        uint32_t unchangedCount;
        uint32_t lastLogTime;
        bool initialized;
    };

    LogLevel m_level;
    uint8_t m_categoryMask;
    bool m_initialized;
    uint32_t m_bootCycle;
    File m_currentFile;
    uint32_t m_lastFlushTime;
    size_t m_writesSinceFlush;
    SensorState m_sensorStates[8];  // Track state for up to 8 sensors

    // Constants
    static constexpr const char* LOG_DIR = "/logs";
    static constexpr const char* CURRENT_LOG = "/logs/boot_current.log";
    static constexpr const char* BOOT_INFO = "/logs/boot_info.txt";
    static constexpr size_t FLUSH_INTERVAL_MS = 5000;  // Flush every 5 seconds
    static constexpr size_t WRITES_PER_FLUSH = 20;     // Or every 20 writes
    static constexpr uint8_t MAX_FILESYSTEM_PERCENT = 30;  // Max 30% of filesystem
    static constexpr uint8_t MAX_BOOT_LOGS = 3;        // Keep last 3 boots

    // Sensor logging thresholds for change detection
    static constexpr uint32_t DISTANCE_CHANGE_THRESHOLD_MM = 50;      // 50mm change
    static constexpr uint32_t UNCHANGED_SUMMARY_INTERVAL = 0;         // Disabled (use time-based only)
    static constexpr uint32_t UNCHANGED_TIME_SUMMARY_MS = 10000;      // Log summary every 10 seconds

    /**
     * @brief Write to log file
     */
    void writeToFile(const char* message);

    /**
     * @brief Load boot info
     */
    void loadBootInfo();

    /**
     * @brief Save boot info
     */
    void saveBootInfo();

    /**
     * @brief Check filesystem space
     */
    bool checkSpace();

    /**
     * @brief Delete old log if needed
     */
    void deleteOldestLog();
};

// Global debug logger instance
extern DebugLogger g_debugLogger;

// Convenience macros
#define DEBUG_LOG_BOOT(...)   g_debugLogger.logBoot(__VA_ARGS__)
#define DEBUG_LOG_CONFIG(...) g_debugLogger.logConfig(__VA_ARGS__)
#define DEBUG_LOG_SENSOR(...) g_debugLogger.logSensor(__VA_ARGS__)
#define DEBUG_LOG_STATE(...)  g_debugLogger.logState(__VA_ARGS__)
#define DEBUG_LOG_LED(...)    g_debugLogger.logLED(__VA_ARGS__)
#define DEBUG_LOG_WIFI(...)   g_debugLogger.logWiFi(__VA_ARGS__)
#define DEBUG_LOG_API(...)    g_debugLogger.logAPI(__VA_ARGS__)
#define DEBUG_LOG_SYSTEM(...) g_debugLogger.logSystem(__VA_ARGS__)

#endif // STEPAWARE_DEBUG_LOGGER_H
