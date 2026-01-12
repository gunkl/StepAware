#ifndef STEPAWARE_LOGGER_H
#define STEPAWARE_LOGGER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Logger for StepAware
 *
 * Provides structured logging with multiple log levels, circular buffer storage,
 * and optional file persistence to SPIFFS.
 *
 * Features:
 * - Multiple log levels (DEBUG, INFO, WARN, ERROR)
 * - Circular buffer for recent logs
 * - Timestamp support
 * - Serial output
 * - Optional file logging to SPIFFS
 * - Thread-safe (basic)
 * - Memory efficient
 */
class Logger {
public:
    /**
     * @brief Log entry structure
     */
    struct LogEntry {
        uint32_t timestamp;        // millis() when logged
        uint8_t level;             // Log level
        char message[128];         // Log message
    };

    /**
     * @brief Log levels
     */
    enum LogLevel {
        LEVEL_DEBUG = LOG_LEVEL_DEBUG,
        LEVEL_INFO  = LOG_LEVEL_INFO,
        LEVEL_WARN  = LOG_LEVEL_WARN,
        LEVEL_ERROR = LOG_LEVEL_ERROR,
        LEVEL_NONE  = LOG_LEVEL_NONE
    };

    /**
     * @brief Construct a new Logger
     */
    Logger();

    /**
     * @brief Destructor
     */
    ~Logger();

    /**
     * @brief Initialize the logger
     *
     * @param level Minimum log level to record
     * @param serialEnabled Enable serial output
     * @param fileEnabled Enable file logging
     * @return true if initialization successful
     */
    bool begin(LogLevel level = LEVEL_INFO, bool serialEnabled = true, bool fileEnabled = false);

    /**
     * @brief Set log level
     *
     * @param level Minimum level to log
     */
    void setLevel(LogLevel level);

    /**
     * @brief Get current log level
     *
     * @return LogLevel Current level
     */
    LogLevel getLevel() const;

    /**
     * @brief Enable/disable serial logging
     *
     * @param enabled True to enable
     */
    void setSerialEnabled(bool enabled);

    /**
     * @brief Enable/disable file logging
     *
     * @param enabled True to enable
     */
    void setFileEnabled(bool enabled);

    /**
     * @brief Log a debug message
     *
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void debug(const char* format, ...);

    /**
     * @brief Log an info message
     *
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void info(const char* format, ...);

    /**
     * @brief Log a warning message
     *
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void warn(const char* format, ...);

    /**
     * @brief Log an error message
     *
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void error(const char* format, ...);

    /**
     * @brief Log a message at specified level
     *
     * @param level Log level
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void log(LogLevel level, const char* format, ...);

    /**
     * @brief Get number of log entries in buffer
     *
     * @return uint32_t Number of entries
     */
    uint32_t getEntryCount() const;

    /**
     * @brief Get log entry by index
     *
     * @param index Index (0 = oldest, count-1 = newest)
     * @param entry Output entry
     * @return true if entry exists
     */
    bool getEntry(uint32_t index, LogEntry& entry) const;

    /**
     * @brief Clear all log entries from buffer
     */
    void clear();

    /**
     * @brief Flush logs to file (if file logging enabled)
     *
     * @return true if flush successful
     */
    bool flush();

    /**
     * @brief Print all log entries to Serial
     */
    void printAll();

    /**
     * @brief Get log level name
     *
     * @param level Log level
     * @return const char* Level name
     */
    static const char* getLevelName(LogLevel level);

private:
    LogLevel m_level;                       ///< Current log level
    bool m_serialEnabled;                   ///< Serial output enabled
    bool m_fileEnabled;                     ///< File logging enabled
    bool m_initialized;                     ///< Initialization complete

    // Circular buffer
    LogEntry m_buffer[LOG_BUFFER_SIZE];     ///< Log entry buffer
    uint32_t m_bufferHead;                  ///< Write index
    uint32_t m_bufferTail;                  ///< Read index (oldest)
    uint32_t m_totalEntries;                ///< Total entries ever logged

    // File logging
    uint32_t m_lastFlushTime;               ///< Last flush time (millis)
    uint32_t m_pendingWrites;               ///< Entries pending flush

    /**
     * @brief Add entry to circular buffer
     *
     * @param level Log level
     * @param message Log message
     */
    void addEntry(LogLevel level, const char* message);

    /**
     * @brief Write entry to serial
     *
     * @param entry Log entry
     */
    void writeToSerial(const LogEntry& entry);

    /**
     * @brief Write entry to file
     *
     * @param entry Log entry
     * @return true if write successful
     */
    bool writeToFile(const LogEntry& entry);

    /**
     * @brief Format timestamp as string
     *
     * @param timestamp Timestamp in milliseconds
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     */
    void formatTimestamp(uint32_t timestamp, char* buffer, size_t bufferSize);
};

// Global logger instance
extern Logger g_logger;

// Convenience macros
#define LOG_DEBUG(...) g_logger.debug(__VA_ARGS__)
#define LOG_INFO(...)  g_logger.info(__VA_ARGS__)
#define LOG_WARN(...)  g_logger.warn(__VA_ARGS__)
#define LOG_ERROR(...) g_logger.error(__VA_ARGS__)

#endif // STEPAWARE_LOGGER_H
