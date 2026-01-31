#include "logger.h"
#include "web_api.h"  // For WebAPI::broadcastLogEntry()
#include <SPIFFS.h>
#include <stdarg.h>

// Global WebAPI pointer (defined in main.cpp)
extern WebAPI* g_webAPI;

// Global logger instance
Logger g_logger;

Logger::Logger()
    : m_level(LEVEL_INFO)
    , m_serialEnabled(true)
    , m_fileEnabled(false)
    , m_initialized(false)
    , m_bufferHead(0)
    , m_bufferTail(0)
    , m_totalEntries(0)
    , m_sequenceCounter(0)
    , m_lastFlushTime(0)
    , m_pendingWrites(0)
{
    memset(m_buffer, 0, sizeof(m_buffer));
}

Logger::~Logger() {
    if (m_fileEnabled && m_pendingWrites > 0) {
        flush();
    }
}

bool Logger::begin(LogLevel level, bool serialEnabled, bool fileEnabled) {
    if (m_initialized) {
        return true;
    }

    m_level = level;
    m_serialEnabled = serialEnabled;
    m_fileEnabled = fileEnabled;

    if (m_fileEnabled) {
        if (!SPIFFS.begin(true)) {
            m_fileEnabled = false;
            if (m_serialEnabled) {
                Serial.println("[Logger] WARNING: Failed to mount SPIFFS, file logging disabled");
            }
        }
    }

    m_initialized = true;

    if (m_serialEnabled) {
        Serial.println("[Logger] ✓ Logger initialized");
        Serial.printf("[Logger] Level: %s, Serial: %s, File: %s\n",
                      getLevelName(m_level),
                      m_serialEnabled ? "ON" : "OFF",
                      m_fileEnabled ? "ON" : "OFF");
    }

    return true;
}

void Logger::setLevel(LogLevel level) {
    m_level = level;
}

Logger::LogLevel Logger::getLevel() const {
    return m_level;
}

void Logger::setSerialEnabled(bool enabled) {
    m_serialEnabled = enabled;
}

void Logger::setFileEnabled(bool enabled) {
    if (enabled && !SPIFFS.begin(true)) {
        return;  // Can't enable file logging without SPIFFS
    }
    m_fileEnabled = enabled;
}

void Logger::verbose(const char* format, ...) {
    if (m_level > LEVEL_VERBOSE) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(LEVEL_VERBOSE, buffer);
}

void Logger::debug(const char* format, ...) {
    if (m_level > LEVEL_DEBUG) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(LEVEL_DEBUG, buffer);
}

void Logger::info(const char* format, ...) {
    if (m_level > LEVEL_INFO) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(LEVEL_INFO, buffer);
}

void Logger::warn(const char* format, ...) {
    if (m_level > LEVEL_WARN) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(LEVEL_WARN, buffer);
}

void Logger::error(const char* format, ...) {
    if (m_level > LEVEL_ERROR) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(LEVEL_ERROR, buffer);
}

void Logger::log(LogLevel level, const char* format, ...) {
    if (m_level > level) return;

    va_list args;
    va_start(args, format);
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addEntry(level, buffer);
}

uint32_t Logger::getEntryCount() const {
    if (m_totalEntries < LOG_BUFFER_SIZE) {
        return m_totalEntries;
    }
    return LOG_BUFFER_SIZE;
}

bool Logger::getEntry(uint32_t index, LogEntry& entry) const {
    uint32_t count = getEntryCount();
    if (index >= count) {
        return false;
    }

    uint32_t bufferIndex = (m_bufferTail + index) % LOG_BUFFER_SIZE;
    entry = m_buffer[bufferIndex];
    return true;
}

void Logger::clear() {
    m_bufferHead = 0;
    m_bufferTail = 0;
    m_totalEntries = 0;
    m_pendingWrites = 0;
    memset(m_buffer, 0, sizeof(m_buffer));
}

bool Logger::flush() {
    if (!m_fileEnabled || m_pendingWrites == 0) {
        return true;
    }

    File file = SPIFFS.open("/logs.txt", "a");
    if (!file) {
        return false;
    }

    // Write all pending entries
    uint32_t count = getEntryCount();
    for (uint32_t i = count > m_pendingWrites ? count - m_pendingWrites : 0; i < count; i++) {
        LogEntry entry;
        if (getEntry(i, entry)) {
            char timestamp[16];
            formatTimestamp(entry.timestamp, timestamp, sizeof(timestamp));
            file.printf("[%s] [%s] %s\n",
                       timestamp,
                       getLevelName((LogLevel)entry.level),
                       entry.message);
        }
    }

    file.close();
    m_pendingWrites = 0;
    m_lastFlushTime = millis();

    return true;
}

void Logger::printAll() {
    if (!m_serialEnabled) {
        return;
    }

    Serial.println("\n========================================");
    Serial.println("Log Buffer");
    Serial.println("========================================");

    uint32_t count = getEntryCount();
    if (count == 0) {
        Serial.println("(empty)");
    } else {
        for (uint32_t i = 0; i < count; i++) {
            LogEntry entry;
            if (getEntry(i, entry)) {
                char timestamp[16];
                formatTimestamp(entry.timestamp, timestamp, sizeof(timestamp));
                Serial.printf("[%s] [%s] %s\n",
                             timestamp,
                             getLevelName((LogLevel)entry.level),
                             entry.message);
            }
        }
    }

    Serial.println("========================================\n");
}

const char* Logger::getLevelName(LogLevel level) {
    switch (level) {
        case LEVEL_VERBOSE: return "VERBOSE";
        case LEVEL_DEBUG: return "DEBUG";
        case LEVEL_INFO:  return "INFO ";
        case LEVEL_WARN:  return "WARN ";
        case LEVEL_ERROR: return "ERROR";
        case LEVEL_NONE:  return "NONE ";
        default:          return "?????";
    }
}

void Logger::addEntry(LogLevel level, const char* message) {
    // Create entry
    LogEntry entry;
    entry.sequenceNumber = m_sequenceCounter++;
    entry.timestamp = millis();
    entry.level = level;
    strlcpy(entry.message, message, sizeof(entry.message));

    // Add to circular buffer
    m_buffer[m_bufferHead] = entry;
    m_bufferHead = (m_bufferHead + 1) % LOG_BUFFER_SIZE;

    // If buffer is full, advance tail
    if (m_totalEntries >= LOG_BUFFER_SIZE) {
        m_bufferTail = (m_bufferTail + 1) % LOG_BUFFER_SIZE;
    }

    m_totalEntries++;
    m_pendingWrites++;

    // Write to serial
    if (m_serialEnabled) {
        writeToSerial(entry);
    }

    // Auto-flush if enough pending writes
    if (m_fileEnabled && m_pendingWrites >= 10) {
        flush();
    }

    // Auto-flush if enough time has passed
    if (m_fileEnabled && millis() - m_lastFlushTime >= LOG_FLUSH_INTERVAL) {
        flush();
    }

    // Broadcast to WebSocket if available
    if (g_webAPI) {
        g_webAPI->broadcastLogEntry(entry);
    }
}

void Logger::writeToSerial(const LogEntry& entry) {
    char timestamp[16];
    formatTimestamp(entry.timestamp, timestamp, sizeof(timestamp));

    Serial.printf("[%s] [%s] %s\n",
                  timestamp,
                  getLevelName((LogLevel)entry.level),
                  entry.message);
}

// Removed unused private function (2026-01-30):
// bool Logger::writeToFile(const LogEntry& entry) - Never called, flush() writes directly to file

void Logger::formatTimestamp(uint32_t timestamp, char* buffer, size_t bufferSize) {
    uint32_t seconds = timestamp / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    snprintf(buffer, bufferSize, "%02u:%02u:%02u.%03u",
             hours, minutes, seconds, timestamp % 1000);
}
