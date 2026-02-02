#include "crash_handler.h"
#include "debug_logger.h"
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_debug_helpers.h>
#include <rom/rtc.h>

// Static member initialization
bool CrashHandler::s_initialized = false;
const char* CrashHandler::CRASH_LOG_PATH = "/logs/last_crash.log";

void CrashHandler::begin() {
    if (s_initialized) {
        return;  // Already initialized
    }

    // Note: ESP32 panic handler registration is complex and requires
    // ESP-IDF framework integration beyond Arduino framework scope.
    // For now, we'll focus on reset reason detection and manual crash logging.
    // Full panic handler integration would require custom esp_panic_handler.c

    s_initialized = true;
    Serial.println("[CrashHandler] Initialized");
}

void CrashHandler::logResetReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr = "";

    switch (reason) {
        case ESP_RST_POWERON:
            reasonStr = "Power-On Reset";
            break;
        case ESP_RST_SW:
            reasonStr = "Software Reset (ESP.restart())";
            break;
        case ESP_RST_PANIC:
            reasonStr = "Exception/Panic";
            break;
        case ESP_RST_INT_WDT:
            reasonStr = "Interrupt Watchdog Timeout";
            break;
        case ESP_RST_TASK_WDT:
            reasonStr = "Task Watchdog Timeout";
            break;
        case ESP_RST_WDT:
            reasonStr = "Other Watchdog Timeout";
            break;
        case ESP_RST_DEEPSLEEP:
            reasonStr = "Deep Sleep Wake";
            break;
        case ESP_RST_BROWNOUT:
            reasonStr = "Brownout Reset (low voltage)";
            break;
        case ESP_RST_SDIO:
            reasonStr = "SDIO Reset";
            break;
        default:
            reasonStr = "Unknown Reset Reason";
            break;
    }

    // Log to serial immediately
    Serial.printf("[CrashHandler] Reset Reason: %s (code: %d)\n", reasonStr, reason);

    // Log to debug logger (using SYSTEM category)
    DEBUG_LOG_SYSTEM("Reset Reason: %s (code: %d)", reasonStr, reason);

    // Check for crash log from previous boot
    if (!LittleFS.exists(CRASH_LOG_PATH)) {
        return;  // No crash log found
    }

    Serial.println("[CrashHandler] ===================================");
    Serial.println("[CrashHandler] CRASH DETECTED FROM PREVIOUS BOOT");
    Serial.println("[CrashHandler] ===================================");

    // Read and display crash log
    File crashFile = LittleFS.open(CRASH_LOG_PATH, "r");
    if (!crashFile) {
        Serial.println("[CrashHandler] ERROR: Cannot open crash log");
        DEBUG_LOG_SYSTEM("ERROR: Cannot open crash log at %s", CRASH_LOG_PATH);
        return;
    }

    // Log to serial and debug logger
    while (crashFile.available()) {
        String line = crashFile.readStringUntil('\n');
        Serial.printf("[CrashHandler] %s\n", line.c_str());
        DEBUG_LOG_SYSTEM("CRASH: %s", line.c_str());
    }
    crashFile.close();

    Serial.println("[CrashHandler] ===================================");

    // Archive crash log with boot cycle number
    uint32_t bootCycle = g_debugLogger.getBootCycle();
    if (bootCycle > 0) {
        bootCycle--;  // Previous boot
    }

    char archiveName[64];
    snprintf(archiveName, sizeof(archiveName), "/logs/crash_%u.log", bootCycle);

    if (LittleFS.rename(CRASH_LOG_PATH, archiveName)) {
        Serial.printf("[CrashHandler] Crash log archived to %s\n", archiveName);
        DEBUG_LOG_SYSTEM("Crash log archived to %s", archiveName);
    } else {
        Serial.println("[CrashHandler] WARNING: Failed to archive crash log");
        DEBUG_LOG_SYSTEM("WARNING: Failed to archive crash log");
        // Delete it anyway to prevent re-logging on next boot
        LittleFS.remove(CRASH_LOG_PATH);
    }
}

void CrashHandler::panicHandler(void* frame, bool pseudo_excause) {
    // Note: This function signature is for demonstration purposes.
    // Actual panic handler registration requires deeper ESP-IDF integration
    // that's not straightforward with Arduino framework.

    // In practice, ESP-IDF already writes core dumps when configured.
    // This handler would extract additional human-readable context.

    writeCrashLog("PANIC", 0, 0, 0, 0);
}

void CrashHandler::abortHook(const char* message) {
    // Note: Similar to panicHandler, this requires ESP-IDF integration

    Serial.printf("[CrashHandler] ABORT: %s\n", message ? message : "unknown");

    writeCrashLog("ABORT", 0, 0, 0, 0);
}

void CrashHandler::writeCrashLog(const char* type, uint32_t pc, uint32_t lr,
                                  uint32_t sp, uint32_t exceptionCode) {
    // Ensure LittleFS is available
    if (!LittleFS.begin()) {
        Serial.println("[CrashHandler] ERROR: LittleFS not available for crash log");
        return;
    }

    // Open crash log file for writing
    File crashFile = LittleFS.open(CRASH_LOG_PATH, "w");
    if (!crashFile) {
        Serial.println("[CrashHandler] ERROR: Cannot create crash log file");
        return;
    }

    // Write crash summary
    crashFile.println("=== CRASH DETECTED ===");

    // Timestamp (millis since boot)
    crashFile.printf("Timestamp: %lu ms\n", millis());

    // Crash type
    crashFile.printf("Type: %s\n", type);

    // Exception details (if available)
    if (exceptionCode != 0) {
        crashFile.printf("Exception Code: %u\n", exceptionCode);
    }

    // Register values (if available)
    if (pc != 0) {
        crashFile.printf("PC: 0x%08x\n", pc);
    }
    if (lr != 0) {
        crashFile.printf("LR: 0x%08x\n", lr);
    }
    if (sp != 0) {
        crashFile.printf("SP: 0x%08x\n", sp);
    }

    // Free heap at crash time
    crashFile.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());

    // Stack trace would go here if we had frame pointer
    // For now, ESP-IDF core dump will contain full stack trace
    crashFile.println("Stack Trace: See core dump for full backtrace");
    crashFile.println("Use /coredump skill or GET /api/ota/coredump to retrieve");

    crashFile.println("=== END CRASH LOG ===");

    // Flush and close
    crashFile.flush();
    crashFile.close();

    Serial.printf("[CrashHandler] Crash log written to %s\n", CRASH_LOG_PATH);
}
