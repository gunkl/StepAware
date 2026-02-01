#include "debug_logger.h"
#include "config_manager.h"
#include "web_api.h"
#include "logger.h"
#include <stdarg.h>
#include <time.h>

// Global WebAPI pointer (defined in main.cpp)
extern WebAPI* g_webAPI;

// Global debug logger instance
DebugLogger g_debugLogger;

DebugLogger::DebugLogger()
    : m_level(LEVEL_DEBUG)
    , m_categoryMask(CAT_ALL)
    , m_initialized(false)
    , m_bootCycle(0)
    , m_lastFlushTime(0)
    , m_writesSinceFlush(0)
{
    // Initialize sensor state tracking
    for (uint8_t i = 0; i < 8; i++) {
        m_sensorStates[i].lastDistance = 0;
        m_sensorStates[i].lastMotion = false;
        m_sensorStates[i].lastDirection = -1;
        m_sensorStates[i].unchangedCount = 0;
        m_sensorStates[i].lastLogTime = 0;
        m_sensorStates[i].initialized = false;
    }
}

DebugLogger::~DebugLogger() {
    flush();
    if (m_currentFile) {
        m_currentFile.close();
    }
}

bool DebugLogger::begin(LogLevel level, uint8_t categoryMask) {
    if (m_initialized) {
        return true;
    }

#if !MOCK_HARDWARE
    // LittleFS should already be mounted by main.cpp
    if (!LittleFS.begin()) {
        Serial.println("[DebugLogger] ERROR: LittleFS not mounted!");
        return false;
    }

    // Create logs directory if it doesn't exist
    if (!LittleFS.exists(LOG_DIR)) {
        LittleFS.mkdir(LOG_DIR);
    }

    m_level = level;
    m_categoryMask = categoryMask;

    // Load boot info (boot cycle count)
    loadBootInfo();

    // Rotate logs from previous boot
    rotateLogs();

    // Increment boot cycle
    m_bootCycle++;
    saveBootInfo();

    // Open current log file for writing
    m_currentFile = LittleFS.open(CURRENT_LOG, "w");
    if (!m_currentFile) {
        Serial.println("[DebugLogger] ERROR: Failed to open log file!");
        return false;
    }

    m_initialized = true;

    // Log boot header
    char header[256];
    snprintf(header, sizeof(header),
             "=== BOOT CYCLE #%u ===\n"
             "Timestamp: %lu ms\n"
             "Log Level: %s\n"
             "Categories: 0x%02X\n"
             "Free Heap: %u bytes\n"
             "Filesystem: %u%% used\n"
             "==============================\n",
             m_bootCycle,
             millis(),
             getLevelName(m_level),
             m_categoryMask,
             ESP.getFreeHeap(),
             getFilesystemUsage());

    m_currentFile.print(header);
    m_currentFile.flush();

    Serial.println("[DebugLogger] ✓ Debug logger initialized");
    Serial.printf("[DebugLogger] Boot cycle: #%u\n", m_bootCycle);
    Serial.printf("[DebugLogger] Log file: %s\n", CURRENT_LOG);
    Serial.printf("[DebugLogger] Filesystem usage: %u%%\n", getFilesystemUsage());
#else
    m_initialized = true;
    Serial.println("[DebugLogger] ✓ Debug logger initialized (MOCK MODE - no file logging)");
#endif

    return true;
}

void DebugLogger::log(LogLevel level, LogCategory category, const char* format, ...) {
    // Check if level and category enabled
    if (level < m_level) return;
    if (!(m_categoryMask & category)) return;

    // Format message
    va_list args;
    va_start(args, format);
    char message[256];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Build log line with timestamp, level, category
    char logLine[384];
    snprintf(logLine, sizeof(logLine), "[%010lu] [%s] [%s] %s\n",
             millis(),
             getLevelName(level),
             getCategoryName(category),
             message);

    // Write to serial
    Serial.print(logLine);

    // Broadcast to WebSocket if available
    if (g_webAPI) {
        // Create a Logger::LogEntry for broadcasting
        // Include category in the message for clarity
        char fullMessage[384];
        snprintf(fullMessage, sizeof(fullMessage), "[%s] %s",
                 getCategoryName(category), message);

        Logger::LogEntry entry;
        static uint32_t debugLogSeq = 0;  // Separate sequence counter for debug logs
        entry.sequenceNumber = debugLogSeq++;
        entry.timestamp = millis();
        time_t now = time(NULL);
        entry.wallTimestamp = (now > 946684800) ? (uint32_t)now : 0;
        entry.level = level;  // DebugLogger levels match Logger levels
        strlcpy(entry.message, fullMessage, sizeof(entry.message));

        g_webAPI->broadcastLogEntry(entry, "debuglog");
    }

#if !MOCK_HARDWARE
    // Write to file
    if (m_initialized && m_currentFile) {
        writeToFile(logLine);
    }
#endif
}

// Category-specific logging helpers
void DebugLogger::logBoot(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_INFO, CAT_BOOT, "%s", buffer);
}

void DebugLogger::logConfig(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_DEBUG, CAT_CONFIG, "%s", buffer);
}

void DebugLogger::logSensor(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_VERBOSE, CAT_SENSOR, "%s", buffer);
}

void DebugLogger::logState(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_DEBUG, CAT_STATE, "%s", buffer);
}

void DebugLogger::logLED(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_DEBUG, CAT_LED, "%s", buffer);
}

void DebugLogger::logWiFi(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_DEBUG, CAT_WIFI, "%s", buffer);
}

void DebugLogger::logAPI(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_DEBUG, CAT_API, "%s", buffer);
}

void DebugLogger::logSystem(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_INFO, CAT_SYSTEM, "%s", buffer);
}

void DebugLogger::logConfigDump() {
    log(LEVEL_INFO, CAT_CONFIG, "=== CONFIGURATION DUMP ===");

    // Note: Config dump is handled by ConfigManager after load
    // This is a placeholder for manual dumps
    log(LEVEL_INFO, CAT_CONFIG, "Config dump requested");
    log(LEVEL_INFO, CAT_CONFIG, "=========================");
}

void DebugLogger::logSensorReading(uint8_t slot, uint8_t sensorType, uint32_t distance, bool motion, int8_t direction) {
    // This method now respects the current log level
    // Only logs if VERBOSE level is enabled
    if (m_level > LEVEL_VERBOSE) {
        return;  // Don't log if level is higher than VERBOSE
    }

    const char* dirStr = "UNKNOWN";
    if (direction == 0) dirStr = "APPROACHING";
    else if (direction == 1) dirStr = "RECEDING";
    else if (direction == 2) dirStr = "STATIONARY";

    // Format output based on sensor type
    if (sensorType == 1) {  // SENSOR_TYPE_PIR
        log(LEVEL_VERBOSE, CAT_SENSOR,
            "Slot %u: PIR=%s, motion=%s, dir=%s",
            slot, motion ? "TRIGGERED" : "idle", motion ? "YES" : "NO", dirStr);
    } else {
        log(LEVEL_VERBOSE, CAT_SENSOR,
            "Slot %u: dist=%u mm, motion=%s, dir=%s",
            slot, distance, motion ? "YES" : "NO", dirStr);
    }
}

void DebugLogger::logSensorReadingIfChanged(uint8_t slot, uint8_t sensorType, uint32_t distance, bool motion, int8_t direction) {
    // Only log at VERBOSE level
    if (m_level > LEVEL_VERBOSE) {
        return;  // Don't log if level is higher than VERBOSE
    }

    // Check if category enabled
    if (!(m_categoryMask & CAT_SENSOR)) {
        return;
    }

    // Validate slot index
    if (slot >= 8) {
        return;
    }

    SensorState& state = m_sensorStates[slot];
    uint32_t now = millis();

    // Check if this is first reading for this slot
    if (!state.initialized) {
        state.lastDistance = distance;
        state.lastMotion = motion;
        state.lastDirection = direction;
        state.unchangedCount = 0;
        state.lastLogTime = now;
        state.initialized = true;

        // Log first reading
        const char* dirStr = "UNKNOWN";
        if (direction == 1) dirStr = "STATIONARY";
        else if (direction == 2) dirStr = "APPROACHING";
        else if (direction == 3) dirStr = "RECEDING";

        if (sensorType == 1) {  // SENSOR_TYPE_PIR
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: PIR=%s, motion=%s, dir=%s [INITIAL]",
                slot, motion ? "TRIGGERED" : "idle", motion ? "YES" : "NO", dirStr);
        } else {
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: dist=%u mm, motion=%s, dir=%s [INITIAL]",
                slot, distance, motion ? "YES" : "NO", dirStr);
        }
        return;
    }

    // Check for significant changes
    bool distanceChanged = false;
    if (distance > state.lastDistance) {
        distanceChanged = (distance - state.lastDistance) > DISTANCE_CHANGE_THRESHOLD_MM;
    } else {
        distanceChanged = (state.lastDistance - distance) > DISTANCE_CHANGE_THRESHOLD_MM;
    }

    bool motionChanged = (motion != state.lastMotion);
    bool directionChanged = (direction != state.lastDirection);

    if (distanceChanged || motionChanged || directionChanged) {
        // Log the change
        const char* dirStr = "UNKNOWN";
        if (direction == 1) dirStr = "STATIONARY";
        else if (direction == 2) dirStr = "APPROACHING";
        else if (direction == 3) dirStr = "RECEDING";

        // Build change description with safe string operations
        char changeDesc[128] = "";  // Increased from 64 to 128 to prevent overflow
        size_t pos = 0;

        if (distanceChanged) {
            pos += snprintf(changeDesc + pos, sizeof(changeDesc) - pos,
                          "dist changed from %u mm", state.lastDistance);
        }
        if (motionChanged) {
            if (pos > 0 && pos < sizeof(changeDesc) - 2) {
                changeDesc[pos++] = ',';
                changeDesc[pos++] = ' ';
            }
            pos += snprintf(changeDesc + pos, sizeof(changeDesc) - pos,
                          "%s", motion ? "motion DETECTED" : "motion CLEARED");
        }
        if (directionChanged) {
            if (pos > 0 && pos < sizeof(changeDesc) - 2) {
                changeDesc[pos++] = ',';
                changeDesc[pos++] = ' ';
            }
            const char* oldDir = "UNKNOWN";
            if (state.lastDirection == 0) oldDir = "APPROACHING";
            else if (state.lastDirection == 1) oldDir = "RECEDING";
            else if (state.lastDirection == 2) oldDir = "STATIONARY";

            pos += snprintf(changeDesc + pos, sizeof(changeDesc) - pos,
                          "dir %s->%s", oldDir, dirStr);
        }

        if (sensorType == 1) {  // SENSOR_TYPE_PIR
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: PIR=%s, motion=%s, dir=%s [CHANGED: %s]",
                slot, motion ? "TRIGGERED" : "idle", motion ? "YES" : "NO", dirStr, changeDesc);
        } else {
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: dist=%u mm, motion=%s, dir=%s [CHANGED: %s]",
                slot, distance, motion ? "YES" : "NO", dirStr, changeDesc);
        }

        // Update state
        state.lastDistance = distance;
        state.lastMotion = motion;
        state.lastDirection = direction;
        state.unchangedCount = 0;
        state.lastLogTime = now;
    } else {
        // No change detected
        state.unchangedCount++;

        // Log periodic summary to show system is still working
        bool shouldLogSummary = false;

        // Log every N unchanged readings (if enabled)
        if (UNCHANGED_SUMMARY_INTERVAL > 0 && state.unchangedCount >= UNCHANGED_SUMMARY_INTERVAL) {
            shouldLogSummary = true;
        }

        // Or log every N seconds (primary trigger)
        if ((now - state.lastLogTime) >= UNCHANGED_TIME_SUMMARY_MS) {
            shouldLogSummary = true;
        }

        if (shouldLogSummary) {
            uint32_t timeSinceLastLog = now - state.lastLogTime;
            if (sensorType == 1) {  // SENSOR_TYPE_PIR
                log(LEVEL_VERBOSE, CAT_SENSOR,
                    "Slot %u: No change (%u readings over %u ms) - PIR=%s, motion=%s",
                    slot, state.unchangedCount, timeSinceLastLog,
                    motion ? "TRIGGERED" : "idle", motion ? "YES" : "NO");
            } else {
                log(LEVEL_VERBOSE, CAT_SENSOR,
                    "Slot %u: No change (%u readings over %u ms) - dist=%u mm, motion=%s",
                    slot, state.unchangedCount, timeSinceLastLog,
                    distance, motion ? "YES" : "NO");
            }

            state.unchangedCount = 0;
            state.lastLogTime = now;
        }
    }
}

void DebugLogger::logStateTransition(const char* from, const char* to, const char* reason) {
    log(LEVEL_INFO, CAT_STATE,
        "State: %s -> %s (reason: %s)",
        from, to, reason);
}

void DebugLogger::logLEDChange(const char* state, uint8_t brightness) {
    log(LEVEL_DEBUG, CAT_LED,
        "LED: %s (brightness: %u)",
        state, brightness);
}

void DebugLogger::flush() {
#if !MOCK_HARDWARE
    if (m_currentFile) {
        m_currentFile.flush();
        m_writesSinceFlush = 0;
        m_lastFlushTime = millis();
    }
#endif
}

size_t DebugLogger::getLogSize() {
#if !MOCK_HARDWARE
    if (LittleFS.exists(CURRENT_LOG)) {
        File f = LittleFS.open(CURRENT_LOG, "r");
        if (f) {
            size_t size = f.size();
            f.close();
            return size;
        }
    }
#endif
    return 0;
}

size_t DebugLogger::getTotalLogsSize() {
#if !MOCK_HARDWARE
    size_t total = 0;

    // Current log
    total += getLogSize();

    // Previous boot logs
    for (uint8_t i = 1; i <= MAX_BOOT_LOGS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/logs/boot_%u.log", i);
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f) {
                total += f.size();
                f.close();
            }
        }
    }

    return total;
#else
    return 0;
#endif
}

uint8_t DebugLogger::getFilesystemUsage() {
#if !MOCK_HARDWARE
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    if (total == 0) return 0;
    return (uint8_t)((used * 100) / total);
#else
    return 0;
#endif
}

bool DebugLogger::needsRotation() {
    uint8_t usage = getFilesystemUsage();
    return usage >= MAX_FILESYSTEM_PERCENT;
}

void DebugLogger::rotateLogs() {
#if !MOCK_HARDWARE
    // Close current log if open
    if (m_currentFile) {
        m_currentFile.close();
    }

    // Rotate logs: boot_2.log -> delete, boot_1.log -> boot_2.log, current -> boot_1.log

    // Delete oldest log (boot_2.log)
    if (LittleFS.exists("/logs/boot_2.log")) {
        LittleFS.remove("/logs/boot_2.log");
    }

    // Rename boot_1.log -> boot_2.log
    if (LittleFS.exists("/logs/boot_1.log")) {
        LittleFS.rename("/logs/boot_1.log", "/logs/boot_2.log");
    }

    // Rename current -> boot_1.log
    if (LittleFS.exists(CURRENT_LOG)) {
        LittleFS.rename(CURRENT_LOG, "/logs/boot_1.log");
    }

    // Check if we need to delete more to stay under limit
    while (needsRotation()) {
        deleteOldestLog();
    }
#endif
}

void DebugLogger::clearAllLogs() {
#if !MOCK_HARDWARE
    // Close current log
    if (m_currentFile) {
        m_currentFile.close();
    }

    // Delete all log files
    LittleFS.remove(CURRENT_LOG);
    LittleFS.remove("/logs/boot_1.log");
    LittleFS.remove("/logs/boot_2.log");
    LittleFS.remove(BOOT_INFO);

    // Reset boot cycle
    m_bootCycle = 0;
    saveBootInfo();

    // Reopen current log
    m_currentFile = LittleFS.open(CURRENT_LOG, "w");

    Serial.println("[DebugLogger] All logs cleared");
#endif
}

const char* DebugLogger::getCategoryName(LogCategory cat) {
    switch (cat) {
        case CAT_BOOT:   return "BOOT  ";
        case CAT_CONFIG: return "CONFIG";
        case CAT_SENSOR: return "SENSOR";
        case CAT_STATE:  return "STATE ";
        case CAT_LED:    return "LED   ";
        case CAT_WIFI:   return "WIFI  ";
        case CAT_API:    return "API   ";
        case CAT_SYSTEM: return "SYSTEM";
        default:         return "UNKNWN";
    }
}

const char* DebugLogger::getLevelName(LogLevel level) {
    switch (level) {
        case LEVEL_VERBOSE: return "VERBOSE";
        case LEVEL_DEBUG:   return "DEBUG  ";
        case LEVEL_INFO:    return "INFO   ";
        case LEVEL_WARN:    return "WARN   ";
        case LEVEL_ERROR:   return "ERROR  ";
        case LEVEL_NONE:    return "NONE   ";
        default:            return "UNKNOWN";
    }
}

// Private methods

void DebugLogger::writeToFile(const char* message) {
#if !MOCK_HARDWARE
    if (!m_currentFile) return;

    m_currentFile.print(message);
    m_writesSinceFlush++;

    // Auto-flush based on time or write count
    uint32_t now = millis();
    if (m_writesSinceFlush >= WRITES_PER_FLUSH ||
        (now - m_lastFlushTime) >= FLUSH_INTERVAL_MS) {
        flush();
    }
#endif
}

void DebugLogger::loadBootInfo() {
#if !MOCK_HARDWARE
    if (LittleFS.exists(BOOT_INFO)) {
        File f = LittleFS.open(BOOT_INFO, "r");
        if (f) {
            String line = f.readStringUntil('\n');
            m_bootCycle = line.toInt();
            f.close();
        }
    } else {
        m_bootCycle = 0;
    }
#endif
}

void DebugLogger::saveBootInfo() {
#if !MOCK_HARDWARE
    File f = LittleFS.open(BOOT_INFO, "w");
    if (f) {
        f.println(m_bootCycle);
        f.close();
    }
#endif
}

bool DebugLogger::checkSpace() {
    return !needsRotation();
}

void DebugLogger::deleteOldestLog() {
#if !MOCK_HARDWARE
    // Delete in order: boot_2 -> boot_1 -> current
    if (LittleFS.exists("/logs/boot_2.log")) {
        LittleFS.remove("/logs/boot_2.log");
    } else if (LittleFS.exists("/logs/boot_1.log")) {
        LittleFS.remove("/logs/boot_1.log");
    } else if (LittleFS.exists(CURRENT_LOG)) {
        LittleFS.remove(CURRENT_LOG);
        // Reopen for writing
        m_currentFile = LittleFS.open(CURRENT_LOG, "w");
    }
#endif
}
