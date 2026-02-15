#include "debug_logger.h"
#include "config_manager.h"
#include "web_api.h"
#include "logger.h"
#include <stdarg.h>
#include <time.h>
#if !MOCK_HARDWARE
#include "esp_system.h"    // For esp_reset_reason()
#include "esp_task_wdt.h"  // For esp_task_wdt_reset()
#endif

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
    , m_rotCurrentExisted(false)
    , m_rotCurrentToPrevOk(false)
{
    m_crashBackupStatusBuf[0] = '\0';

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

bool DebugLogger::begin(LogLevel level, uint16_t categoryMask) {
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

    // Preserve crash log BEFORE rotating (must run while current log still exists)
    preserveCrashLog();

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

    // Append rotation status so it survives to the file log
    if (m_rotCurrentExisted) {
        char rotLine[64];
        snprintf(rotLine, sizeof(rotLine),
                 "Rotation: current->prev=%s\n",
                 m_rotCurrentToPrevOk ? "OK" : "FAILED");
        m_currentFile.print(rotLine);
    }

    // Append crash backup status
    {
        const char* backupStatus = (m_crashBackupStatusBuf[0] != '\0')
                                   ? m_crashBackupStatusBuf
                                   : "none";
        char crashLine[64];
        snprintf(crashLine, sizeof(crashLine), "Crash backup: %s\n", backupStatus);
        m_currentFile.print(crashLine);
    }

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
    time_t now = time(NULL);
    // NOTE: If NTP corrects the system clock significantly after initial sync,
    // calendar timestamps may appear to go backwards in the log. This is expected
    // behavior — the initial timestamps used a stale RTC value, corrected by NTP.
    if (now > 946684800) {  // 946684800 = Jan 1 2000; valid only after NTP sync
        struct tm* tm = localtime(&now);
        if (tm) {
            char timeStr[24];
            strftime(timeStr, sizeof(timeStr), "%m-%d %H:%M:%S", tm);
            snprintf(logLine, sizeof(logLine), "[%s] [%s] [%s] %s\n",
                     timeStr,
                     getLevelName(level),
                     getCategoryName(category),
                     message);
        } else {
            // Timezone not configured yet — fall back to UTC rather than millis
            tm = gmtime(&now);
            if (tm) {
                char timeStr[24];
                strftime(timeStr, sizeof(timeStr), "%m-%d %H:%M:%S", tm);
                snprintf(logLine, sizeof(logLine), "[%s] [%s] [%s] %s\n",
                         timeStr,
                         getLevelName(level),
                         getCategoryName(category),
                         message);
            } else {
                snprintf(logLine, sizeof(logLine), "[%010lu] [%s] [%s] %s\n",
                         millis(),
                         getLevelName(level),
                         getCategoryName(category),
                         message);
            }
        }
    } else {
        snprintf(logLine, sizeof(logLine), "[%010lu] [%s] [%s] %s\n",
                 millis(),
                 getLevelName(level),
                 getCategoryName(category),
                 message);
    }

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

void DebugLogger::logCrash(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LEVEL_ERROR, CAT_CRASH, "%s", buffer);
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
        // Pre-compute all expressions to avoid stack corruption in variadic functions
        const char* triggeredStr = motion ? "TRIGGERED" : "idle";
        const char* motionStr = motion ? "YES" : "NO";
        log(LEVEL_VERBOSE, CAT_SENSOR,
            "Slot %u: PIR=%s, motion=%s, dir=%s",
            slot, triggeredStr, motionStr, dirStr);
    } else {
        // Pre-compute all expressions to avoid stack corruption in variadic functions
        const char* motionStr = motion ? "YES" : "NO";
        log(LEVEL_VERBOSE, CAT_SENSOR,
            "Slot %u: dist=%u mm, motion=%s, dir=%s",
            slot, distance, motionStr, dirStr);
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
            // Pre-compute all expressions to avoid stack corruption in variadic functions
            const char* triggeredStr = motion ? "TRIGGERED" : "idle";
            const char* motionStr = motion ? "YES" : "NO";
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: PIR=%s, motion=%s, dir=%s [INITIAL]",
                slot, triggeredStr, motionStr, dirStr);
        } else {
            // Pre-compute all expressions to avoid stack corruption in variadic functions
            const char* motionStr = motion ? "YES" : "NO";
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: dist=%u mm, motion=%s, dir=%s [INITIAL]",
                slot, distance, motionStr, dirStr);
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
            // Pre-compute all expressions to avoid stack corruption in variadic functions
            const char* triggeredStr = motion ? "TRIGGERED" : "idle";
            const char* motionStr = motion ? "YES" : "NO";
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: PIR=%s, motion=%s, dir=%s [CHANGED: %s]",
                slot, triggeredStr, motionStr, dirStr, changeDesc);
        } else {
            // Pre-compute all expressions to avoid stack corruption in variadic functions
            const char* motionStr = motion ? "YES" : "NO";
            log(LEVEL_VERBOSE, CAT_SENSOR,
                "Slot %u: dist=%u mm, motion=%s, dir=%s [CHANGED: %s]",
                slot, distance, motionStr, dirStr, changeDesc);
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
                // Pre-compute all expressions to avoid stack corruption in variadic functions
                const char* triggeredStr = motion ? "TRIGGERED" : "idle";
                const char* motionStr = motion ? "YES" : "NO";
                log(LEVEL_VERBOSE, CAT_SENSOR,
                    "Slot %u: No change (%u readings over %u ms) - PIR=%s, motion=%s",
                    slot, state.unchangedCount, timeSinceLastLog,
                    triggeredStr, motionStr);
            } else {
                // Pre-compute all expressions to avoid stack corruption in variadic functions
                const char* motionStr = motion ? "YES" : "NO";
                log(LEVEL_VERBOSE, CAT_SENSOR,
                    "Slot %u: No change (%u readings over %u ms) - dist=%u mm, motion=%s",
                    slot, state.unchangedCount, timeSinceLastLog,
                    distance, motionStr);
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

    // Previous boot log
    if (LittleFS.exists(PREV_LOG)) {
        File f = LittleFS.open(PREV_LOG, "r");
        if (f) {
            total += f.size();
            f.close();
        }
    }

    // Overflow log (runtime rotation artifact)
    if (LittleFS.exists(OVERFLOW_LOG)) {
        File f = LittleFS.open(OVERFLOW_LOG, "r");
        if (f) {
            total += f.size();
            f.close();
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

void DebugLogger::logRotationAttempt() {
#if !MOCK_HARDWARE
    // Write rotation diagnostics to persistent file
    // This survives reboot even if rotation fails
    File debugFile = LittleFS.open("/logs/rotation_debug.txt", "w");
    if (!debugFile) {
        Serial.println("[DebugLogger] ERROR: Cannot create rotation_debug.txt");
        return;
    }

    // Log timestamp using Arduino time functions
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[Boot #%lu]", m_bootCycle);
    debugFile.printf("%s ROTATION ATTEMPT\n", timestamp);

    // Log filesystem state
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    size_t available = total - used;
    float percentUsed = (used * 100.0f) / total;
    debugFile.printf("  FS: %u/%u bytes (%.1f%% used, %u free)\n",
                    used, total, percentUsed, available);
    debugFile.printf("  Free bytes: %u (runtime rotation triggers at < %u)\n",
                    available, (size_t)(CRASH_RESERVE_BYTES + 8192));

    // Log file existence (new layout: prev + overflow + current)
    debugFile.printf("  Files: prev=%d overflow=%d current=%d crash_backup=%d\n",
                    LittleFS.exists(PREV_LOG),
                    LittleFS.exists(OVERFLOW_LOG),
                    LittleFS.exists(CURRENT_LOG),
                    LittleFS.exists(CRASH_BACKUP_LOG));

    debugFile.close();
    Serial.println("[DebugLogger] Rotation debug logged to /logs/rotation_debug.txt");
#endif
}

void DebugLogger::rotateLogs() {
#if !MOCK_HARDWARE
    Serial.println("[DebugLogger] === LOG ROTATION START ===");

    // Log rotation attempt to persistent file (survives reboot if rotation fails)
    logRotationAttempt();

    // Filesystem health check before rotation
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    Serial.printf("[DebugLogger] Pre-rotation FS: total=%u used=%u free=%u\n",
                  totalBytes, usedBytes, freeBytes);

    // Close current log if open
    if (m_currentFile) {
        m_currentFile.close();
    }

    // Reset rotation status flags
    m_rotCurrentExisted  = false;
    m_rotCurrentToPrevOk = false;

    // Pre-rotation file state
    bool prevExists     = LittleFS.exists(PREV_LOG);
    bool overflowExists = LittleFS.exists(OVERFLOW_LOG);
    bool currentExists  = LittleFS.exists(CURRENT_LOG);

    Serial.printf("[DebugLogger] Pre-rotation state: prev=%s overflow=%s current=%s\n",
                  prevExists     ? "EXISTS" : "MISSING",
                  overflowExists ? "EXISTS" : "MISSING",
                  currentExists  ? "EXISTS" : "MISSING");

    if (currentExists) {
        File currentFile = LittleFS.open(CURRENT_LOG, "r");
        if (currentFile) {
            Serial.printf("[DebugLogger] current.log size: %u bytes\n", currentFile.size());
            currentFile.close();
        }
    }

    // Delete old prev log (it belongs to the boot before last)
    if (prevExists) {
        bool removeOk = LittleFS.remove(PREV_LOG);
        Serial.printf("[DebugLogger] Delete boot_prev.log: %s\n", removeOk ? "OK" : "FAILED");
    }

    // Delete old overflow log (it belongs to the previous boot's runtime)
    if (overflowExists) {
        bool removeOk = LittleFS.remove(OVERFLOW_LOG);
        Serial.printf("[DebugLogger] Delete boot_overflow.log: %s\n", removeOk ? "OK" : "FAILED");
    }

    // Rename current -> boot_prev.log
    m_rotCurrentExisted = currentExists;
    if (m_rotCurrentExisted) {
        m_rotCurrentToPrevOk = LittleFS.rename(CURRENT_LOG, PREV_LOG);
        Serial.printf("[DebugLogger] rotateLogs: current -> prev: %s\n",
                      m_rotCurrentToPrevOk ? "OK" : "FAILED");
        if (!m_rotCurrentToPrevOk) {
            bool currentExistsAfter = LittleFS.exists(CURRENT_LOG);
            bool prevExistsAfter    = LittleFS.exists(PREV_LOG);
            Serial.printf("[DebugLogger] FAILURE DIAG: current=%s prev=%s after rename attempt\n",
                          currentExistsAfter ? "STILL EXISTS" : "MISSING",
                          prevExistsAfter    ? "EXISTS"       : "MISSING");
        }
    } else {
        Serial.println("[DebugLogger] rotateLogs: current log does not exist — nothing to rotate");
    }

    // Post-rotation filesystem state
    totalBytes = LittleFS.totalBytes();
    usedBytes  = LittleFS.usedBytes();
    freeBytes  = totalBytes - usedBytes;
    Serial.printf("[DebugLogger] Post-rotation FS: total=%u used=%u free=%u\n",
                  totalBytes, usedBytes, freeBytes);

    Serial.println("[DebugLogger] === LOG ROTATION COMPLETE ===");
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
    LittleFS.remove(PREV_LOG);
    LittleFS.remove(OVERFLOW_LOG);
    LittleFS.remove(CRASH_BACKUP_LOG);
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
        case CAT_CRASH:  return "CRASH ";
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
        esp_task_wdt_reset();  // Feed watchdog before potentially slow flush
        flush();
        checkAndRotateRunningLogs();  // Check space at flush intervals
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
    // Delete in order: prev -> overflow -> truncate current
    if (LittleFS.exists(PREV_LOG)) {
        LittleFS.remove(PREV_LOG);
    } else if (LittleFS.exists(OVERFLOW_LOG)) {
        LittleFS.remove(OVERFLOW_LOG);
    } else if (m_currentFile) {
        // Truncate current log as last resort
        m_currentFile.close();
        m_currentFile = LittleFS.open(CURRENT_LOG, "w");
        if (m_currentFile) {
            m_currentFile.println("[LOG TRUNCATED - emergency space reclaim]");
        }
    }
#endif
}

void DebugLogger::preserveCrashLog() {
#if !MOCK_HARDWARE
    // Check if this boot was caused by a crash
    esp_reset_reason_t reason = esp_reset_reason();
    bool isCrash = (reason == ESP_RST_PANIC ||
                    reason == ESP_RST_TASK_WDT ||
                    reason == ESP_RST_INT_WDT ||
                    reason == ESP_RST_WDT ||
                    reason == ESP_RST_BROWNOUT);

    m_crashBackupStatusBuf[0] = '\0';  // empty = "none"

    int reasonInt = (int)reason;

    if (!isCrash) {
        Serial.printf("[DebugLogger] Reset reason %d - not a crash, skipping backup\n", reasonInt);
        return;
    }

    Serial.printf("[DebugLogger] CRASH DETECTED (reset reason %d) - preserving crash log\n", reasonInt);

    if (!LittleFS.exists(CURRENT_LOG)) {
        Serial.println("[DebugLogger] No current log to preserve");
        snprintf(m_crashBackupStatusBuf, sizeof(m_crashBackupStatusBuf), "no log found");
        return;
    }

    File src = LittleFS.open(CURRENT_LOG, "r");
    if (!src) {
        Serial.println("[DebugLogger] ERROR: Cannot open current log for crash backup");
        snprintf(m_crashBackupStatusBuf, sizeof(m_crashBackupStatusBuf), "open failed");
        return;
    }

    size_t fileSize    = src.size();
    size_t maxBackup   = CRASH_RESERVE_BYTES - 256;  // Leave room for header
    size_t startOffset = (fileSize > maxBackup) ? (fileSize - maxBackup) : 0;
    size_t bytesToCopy = fileSize - startOffset;

    File dst = LittleFS.open(CRASH_BACKUP_LOG, "w");
    if (!dst) {
        src.close();
        Serial.println("[DebugLogger] ERROR: Cannot create crash backup file");
        snprintf(m_crashBackupStatusBuf, sizeof(m_crashBackupStatusBuf), "create failed");
        return;
    }

    // Determine reason string
    const char* reasonStr = "unknown";
    switch (reason) {
        case ESP_RST_PANIC:    reasonStr = "PANIC";    break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT"; break;
        case ESP_RST_INT_WDT:  reasonStr = "INT_WDT";  break;
        case ESP_RST_WDT:      reasonStr = "WDT";      break;
        case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT"; break;
        default: break;
    }

    // Write header
    dst.printf("=== CRASH BACKUP (Boot #%lu) ===\n", m_bootCycle);
    dst.printf("Reset reason: %s (%d)\n", reasonStr, reasonInt);
    dst.printf("Original log size: %u bytes\n", fileSize);
    dst.printf("Backup offset: %u (last %u bytes)\n", startOffset, bytesToCopy);
    dst.println("================================\n");

    // Copy log data in chunks
    src.seek(startOffset);
    uint8_t buf[512];
    size_t totalCopied = 0;
    while (totalCopied < bytesToCopy) {
        size_t toRead = bytesToCopy - totalCopied;
        if (toRead > sizeof(buf)) toRead = sizeof(buf);
        size_t bytesRead = src.read(buf, toRead);
        if (bytesRead == 0) break;
        dst.write(buf, bytesRead);
        totalCopied += bytesRead;
    }

    dst.close();
    src.close();

    unsigned int copiedK = (unsigned int)(totalCopied / 1024);
    snprintf(m_crashBackupStatusBuf, sizeof(m_crashBackupStatusBuf),
             "saved %uK (%s)", copiedK, reasonStr);

    Serial.printf("[DebugLogger] Crash backup saved: %u bytes from offset %u (%s)\n",
                  totalCopied, startOffset, reasonStr);
#endif
}

void DebugLogger::checkAndRotateRunningLogs() {
#if !MOCK_HARDWARE
    if (!m_currentFile) return;

    // Use actual FS free space — accounts for ALL files (prev log, crash_backup,
    // configs, etc.), not just current + overflow.  The old check only counted
    // currentSize + overflowSize against a "budget", which ignored the ~498KB prev
    // log and caused the filesystem to fill to 99.6% before rotation fired.
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes  = LittleFS.usedBytes();
    size_t freeBytes  = totalBytes - usedBytes;

    // Get current log size (still needed for tail-copy calculation below)
    size_t currentSize = m_currentFile.size();

    // Rotate when free space drops below crash reserve + 8KB headroom.
    // CRASH_RESERVE_BYTES (~32KB) keeps room for crash_backup writes post-crash.
    // The extra 8KB headroom prevents writes from failing before rotation fires.
    if (freeBytes > (CRASH_RESERVE_BYTES + 8192)) return;

    // Pre-compute for safe printf use (no expressions in variadic args)
    unsigned int freePct = (unsigned int)(((totalBytes - freeBytes) * 100) / totalBytes);
    Serial.printf("[DebugLogger] Runtime rotation: %u bytes free (%u%% used) - rotating\n",
                  freeBytes, freePct);

    // Feed watchdog before potentially slow filesystem operations
    esp_task_wdt_reset();

    // Save last 25% of current log to overflow
    size_t tailSize   = currentSize / 4;
    size_t tailOffset = currentSize - tailSize;

    // Close current file before reading it
    m_currentFile.flush();
    m_currentFile.close();

    File src = LittleFS.open(CURRENT_LOG, "r");
    if (!src) {
        // Reopen for append and bail out
        m_currentFile = LittleFS.open(CURRENT_LOG, "a");
        return;
    }

    File dst = LittleFS.open(OVERFLOW_LOG, "w");
    if (dst) {
        src.seek(tailOffset);
        uint8_t buf[512];
        size_t copied = 0;
        while (copied < tailSize) {
            size_t toRead = tailSize - copied;
            if (toRead > sizeof(buf)) toRead = sizeof(buf);
            size_t bytesRead = src.read(buf, toRead);
            if (bytesRead == 0) break;
            dst.write(buf, bytesRead);
            copied += bytesRead;
        }
        dst.close();
        Serial.printf("[DebugLogger] Saved %u bytes to overflow\n", copied);
    }
    src.close();

    // Truncate current log (reopen as write mode)
    m_currentFile = LittleFS.open(CURRENT_LOG, "w");
    if (m_currentFile) {
        m_currentFile.println("[LOG ROTATED - runtime space management]");
        unsigned int freed = (unsigned int)(currentSize - tailSize);
        Serial.printf("[DebugLogger] Current log truncated, freed ~%u bytes\n", freed);
    }

    esp_task_wdt_reset();
#endif
}
