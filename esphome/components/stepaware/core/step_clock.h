/**
 * @file step_clock.h
 * @brief Injectable millisecond clock shared by the StepAware core logic.
 *
 * The core classes never call Arduino/ESP-IDF/ESPHome time functions directly.
 * They are handed a plain function pointer instead, so they can be driven by
 * esphome::millis() on the device and by a mock clock in native unit tests.
 */
#ifndef STEPAWARE_CORE_STEP_CLOCK_H
#define STEPAWARE_CORE_STEP_CLOCK_H

#include <stdint.h>

namespace step_logic {

/// Returns monotonic milliseconds (wraps at 2^32 like Arduino millis()).
typedef uint32_t (*StepClockFn)();

}  // namespace step_logic

#endif  // STEPAWARE_CORE_STEP_CLOCK_H
