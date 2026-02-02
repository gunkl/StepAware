/**
 * @file logger.h
 * @brief Stub logger.h for native unit tests — shadows include/logger.h.
 *
 * Provides the minimum declarations needed so that hal_pir.cpp compiles
 * without pulling in the real Logger class or its Arduino dependencies.
 */
#ifndef STEPAWARE_LOGGER_H
#define STEPAWARE_LOGGER_H

#include <stdint.h>

class Logger {
public:
    enum LogLevel { LEVEL_VERBOSE=0, LEVEL_DEBUG=1, LEVEL_INFO=2, LEVEL_WARN=3, LEVEL_ERROR=4, LEVEL_NONE=5 };
    static const char* getLevelName(LogLevel) { return "TEST"; }
    void setLevel(LogLevel) {}
    void verbose(const char*, ...) {}
    void debug(const char*, ...) {}
    void info(const char*, ...) {}
    void warn(const char*, ...) {}
    void error(const char*, ...) {}
};

extern Logger g_logger;

#define LOG_VERBOSE(...)
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)

#endif // STEPAWARE_LOGGER_H
