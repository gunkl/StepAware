/**
 * @file Arduino.h
 * @brief Minimal Arduino shim for native unit tests.
 *
 * Provides the bare-minimum type definitions and constants so that project
 * headers (hal_pir.h, recal_scheduler.h) can be included on a native
 * (non-ESP32) build without pulling in the full Arduino framework.
 *
 * Function stubs (millis, pinMode, etc.) are defined in the test .cpp file
 * before this header is ever reached via transitive includes.
 */
#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>

// GPIO constants
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW  0
#endif
#ifndef INPUT
#define INPUT  0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif

// Arduino function prototypes (implemented as mocks in the test .cpp)
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void pinMode(uint8_t pin, uint8_t mode);
int  digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t value);

#endif // ARDUINO_H
