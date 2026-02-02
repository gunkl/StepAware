#ifndef STEPAWARE_CRASH_HANDLER_H
#define STEPAWARE_CRASH_HANDLER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Crash Handler for ESP32 Exception Capture
 *
 * Registers panic/abort hooks to capture crash context before reboot.
 * Writes human-readable crash summary to LittleFS for post-reboot analysis.
 *
 * Features:
 * - Captures PC (program counter), LR (link register), SP (stack pointer)
 * - Parses stack trace (up to 10 frames)
 * - Writes crash summary to /logs/last_crash.log
 * - Logs reset reason on boot
 * - Archives crash logs for historical analysis
 *
 * Usage:
 * 1. Call CrashHandler::begin() early in setup() after LittleFS is mounted
 * 2. Call CrashHandler::logResetReason() after DebugLogger::begin()
 *
 * ESP32-C3 RISC-V compatible.
 */
class CrashHandler {
public:
    /**
     * @brief Initialize crash handler system
     *
     * Registers ESP32 panic and abort hooks.
     * Call early in setup() after LittleFS is mounted.
     */
    static void begin();

    /**
     * @brief Check and log reset reason on boot
     *
     * Uses esp_reset_reason() to determine why device rebooted:
     * - POWERON, SW_RESET, PANIC, INT_WDT, TASK_WDT, etc.
     *
     * If a crash log from previous boot exists (/logs/last_crash.log):
     * - Logs crash summary to serial and DebugLogger
     * - Archives to /logs/crash_<bootcycle>.log
     *
     * Call after DebugLogger::begin() so logs are captured.
     */
    static void logResetReason();

private:
    /**
     * @brief Panic handler callback (unhandled exceptions)
     *
     * Called by ESP-IDF when unhandled exception occurs.
     * Captures PC, LR, SP, exception type, and stack trace.
     *
     * @param frame Exception frame (ESP32-C3 RISC-V format)
     * @param pseudo_excause True if synthetic exception
     */
    static void panicHandler(void* frame, bool pseudo_excause);

    /**
     * @brief Abort hook callback (software abort)
     *
     * Called by ESP-IDF when abort() is called or assertion fails.
     *
     * @param message Abort message (if available)
     */
    static void abortHook(const char* message);

    /**
     * @brief Write crash summary to persistent storage
     *
     * Writes crash details to /logs/last_crash.log for post-reboot analysis.
     *
     * @param type "PANIC", "ABORT", "WATCHDOG"
     * @param pc Program counter at crash
     * @param lr Link register (return address)
     * @param sp Stack pointer
     * @param exceptionCode ESP32 exception code (RISC-V cause register)
     */
    static void writeCrashLog(const char* type, uint32_t pc, uint32_t lr,
                               uint32_t sp, uint32_t exceptionCode);

    static bool s_initialized;                  ///< Initialization flag
    static const char* CRASH_LOG_PATH;          ///< Path to crash log: "/logs/last_crash.log"
};

#endif // STEPAWARE_CRASH_HANDLER_H
