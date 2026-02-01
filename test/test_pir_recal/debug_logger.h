/**
 * @file debug_logger.h
 * @brief Stub debug_logger.h for native unit tests — shadows include/debug_logger.h.
 *
 * All DEBUG_LOG_* macros are defined as no-ops so that hal_pir.cpp and
 * recal_scheduler.cpp compile without the real DebugLogger or LittleFS.
 */
#ifndef STEPAWARE_DEBUG_LOGGER_H
#define STEPAWARE_DEBUG_LOGGER_H

#ifndef DEBUG_LOG_SENSOR
#define DEBUG_LOG_SENSOR(fmt, ...)
#endif
#ifndef DEBUG_LOG_SYSTEM
#define DEBUG_LOG_SYSTEM(fmt, ...)
#endif
#ifndef DEBUG_LOG_BOOT
#define DEBUG_LOG_BOOT(fmt, ...)
#endif
#ifndef DEBUG_LOG_CONFIG
#define DEBUG_LOG_CONFIG(fmt, ...)
#endif
#ifndef DEBUG_LOG_API
#define DEBUG_LOG_API(fmt, ...)
#endif
#ifndef DEBUG_LOG_LED
#define DEBUG_LOG_LED(fmt, ...)
#endif
#ifndef DEBUG_LOG_WIFI
#define DEBUG_LOG_WIFI(fmt, ...)
#endif
#ifndef DEBUG_LOG_STATE
#define DEBUG_LOG_STATE(fmt, ...)
#endif

#endif // STEPAWARE_DEBUG_LOGGER_H
