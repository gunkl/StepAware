# Issue #4 Phase 3: Core Hardware Abstraction - Implementation Plan

**Date**: 2026-01-21
**Status**: Planning Phase
**Dependencies**: Phase 1 ✅ Complete, Phase 2 ✅ Complete

---

## Executive Summary

Phase 3 creates a platform abstraction layer (PAL) to support multiple ESP32 variants and future hardware platforms. This enables:
- ESP32-C6 and ESP32-H2 support for Thread/Matter/Zigbee
- Lower power consumption through Thread networking
- Easy addition of new hardware platforms
- Clean separation between application logic and hardware drivers

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│              Application Layer                          │
│  (state_machine, config_manager, web_api, etc.)        │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│         Hardware Abstraction Layer (HAL)                │
│  (sensor_manager, hal_led, hal_button, etc.)           │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│       Platform Abstraction Layer (PAL) - NEW            │
│  ┌──────────────┬──────────────┬──────────────┐        │
│  │ GPIO Driver  │ I2C Driver   │ PWM Driver   │        │
│  │ Timer Driver │ ADC Driver   │ UART Driver  │        │
│  └──────────────┴──────────────┴──────────────┘        │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│           Device Drivers (devices_core/)                │
│  ┌────────────────┬──────────────┬──────────────┐      │
│  │ ESP32-C3       │ ESP32-C6     │ ESP32-H2     │      │
│  │ (Baseline)     │ (Thread/WiFi)│ (Thread/BLE) │      │
│  └────────────────┴──────────────┴──────────────┘      │
└─────────────────────────────────────────────────────────┘
```

---

## Goals

### Primary Goals
1. ✅ **Platform Independence** - Application code runs on any supported platform
2. ✅ **Thread/Matter Support** - Enable ESP32-C6/H2 for low-power mesh networking
3. ✅ **Easy Platform Addition** - New platforms require only device driver implementation
4. ✅ **No Breaking Changes** - Existing code continues to work on ESP32-C3

### Secondary Goals
1. ✅ **Reduced Power Consumption** - Thread uses ~1/10th the power of WiFi
2. ✅ **Future-Proof** - Support for upcoming hardware (ESP32-P4, etc.)
3. ✅ **Build System Flexibility** - Multi-platform builds via PlatformIO environments
4. ✅ **Documentation** - Clear guide for adding new platforms

---

## Phase 3 Implementation Steps

### Step 1: Create Platform Abstraction Layer (PAL)

#### 1.1 Directory Structure

Create new directory structure:
```
devices_core/
├── README.md                    # Platform integration guide
├── pal/                         # Platform Abstraction Layer
│   ├── pal_gpio.h              # GPIO interface
│   ├── pal_i2c.h               # I2C interface
│   ├── pal_pwm.h               # PWM interface
│   ├── pal_timer.h             # Timer interface
│   ├── pal_adc.h               # ADC interface
│   ├── pal_uart.h              # UART interface
│   └── pal_types.h             # Common PAL types
├── esp32_c3/                    # ESP32-C3 driver
│   ├── device_esp32_c3.h       # Device header
│   ├── device_esp32_c3.cpp     # Device implementation
│   ├── gpio_esp32_c3.cpp       # GPIO driver
│   ├── i2c_esp32_c3.cpp        # I2C driver
│   ├── pwm_esp32_c3.cpp        # PWM driver
│   ├── timer_esp32_c3.cpp      # Timer driver
│   ├── adc_esp32_c3.cpp        # ADC driver
│   └── uart_esp32_c3.cpp       # UART driver
├── esp32_c6/                    # ESP32-C6 driver (NEW)
│   ├── device_esp32_c6.h
│   ├── device_esp32_c6.cpp
│   ├── gpio_esp32_c6.cpp
│   ├── i2c_esp32_c6.cpp
│   ├── pwm_esp32_c6.cpp
│   ├── timer_esp32_c6.cpp
│   ├── adc_esp32_c6.cpp
│   ├── uart_esp32_c6.cpp
│   └── thread_esp32_c6.cpp     # Thread/Matter support
└── esp32_h2/                    # ESP32-H2 driver (NEW)
    ├── device_esp32_h2.h
    ├── device_esp32_h2.cpp
    ├── gpio_esp32_h2.cpp
    ├── i2c_esp32_h2.cpp
    ├── pwm_esp32_h2.cpp
    ├── timer_esp32_h2.cpp
    ├── adc_esp32_h2.cpp
    ├── uart_esp32_h2.cpp
    └── thread_esp32_h2.cpp     # Thread/Zigbee support
```

#### 1.2 Platform Abstraction Interfaces

**File**: `devices_core/pal/pal_types.h`

```cpp
#ifndef STEPAWARE_PAL_TYPES_H
#define STEPAWARE_PAL_TYPES_H

#include <stdint.h>

/**
 * @brief Platform Abstraction Layer common types
 */

// GPIO types
typedef uint8_t pal_gpio_pin_t;
typedef enum {
    PAL_GPIO_MODE_INPUT,
    PAL_GPIO_MODE_OUTPUT,
    PAL_GPIO_MODE_INPUT_PULLUP,
    PAL_GPIO_MODE_INPUT_PULLDOWN,
    PAL_GPIO_MODE_OUTPUT_OD
} pal_gpio_mode_t;

typedef enum {
    PAL_GPIO_LOW = 0,
    PAL_GPIO_HIGH = 1
} pal_gpio_level_t;

typedef enum {
    PAL_GPIO_INTR_DISABLE,
    PAL_GPIO_INTR_POSEDGE,
    PAL_GPIO_INTR_NEGEDGE,
    PAL_GPIO_INTR_ANYEDGE,
    PAL_GPIO_INTR_LOW_LEVEL,
    PAL_GPIO_INTR_HIGH_LEVEL
} pal_gpio_intr_type_t;

// PWM types
typedef uint8_t pal_pwm_channel_t;
typedef uint32_t pal_pwm_freq_t;
typedef uint16_t pal_pwm_duty_t;  // 0-1023

// I2C types
typedef uint8_t pal_i2c_port_t;
typedef uint8_t pal_i2c_addr_t;
typedef enum {
    PAL_I2C_MODE_MASTER,
    PAL_I2C_MODE_SLAVE
} pal_i2c_mode_t;

// ADC types
typedef uint8_t pal_adc_channel_t;
typedef enum {
    PAL_ADC_ATTEN_0DB,    // 0dB, ~800mV range
    PAL_ADC_ATTEN_2_5DB,  // 2.5dB, ~1100mV range
    PAL_ADC_ATTEN_6DB,    // 6dB, ~1350mV range
    PAL_ADC_ATTEN_11DB    // 11dB, ~2600mV range
} pal_adc_atten_t;

// Timer types
typedef uint8_t pal_timer_id_t;
typedef void (*pal_timer_callback_t)(void* arg);

// Error codes
typedef enum {
    PAL_OK = 0,
    PAL_ERR_INVALID_ARG,
    PAL_ERR_INVALID_STATE,
    PAL_ERR_TIMEOUT,
    PAL_ERR_NO_MEM,
    PAL_ERR_NOT_SUPPORTED,
    PAL_ERR_NOT_FOUND
} pal_err_t;

#endif // STEPAWARE_PAL_TYPES_H
```

**File**: `devices_core/pal/pal_gpio.h`

```cpp
#ifndef STEPAWARE_PAL_GPIO_H
#define STEPAWARE_PAL_GPIO_H

#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GPIO subsystem
 */
pal_err_t pal_gpio_init(void);

/**
 * @brief Set GPIO pin mode
 */
pal_err_t pal_gpio_set_mode(pal_gpio_pin_t pin, pal_gpio_mode_t mode);

/**
 * @brief Set GPIO pin level
 */
pal_err_t pal_gpio_set_level(pal_gpio_pin_t pin, pal_gpio_level_t level);

/**
 * @brief Get GPIO pin level
 */
pal_gpio_level_t pal_gpio_get_level(pal_gpio_pin_t pin);

/**
 * @brief Enable GPIO interrupt
 */
pal_err_t pal_gpio_set_intr_type(pal_gpio_pin_t pin, pal_gpio_intr_type_t type);

/**
 * @brief Register GPIO interrupt handler
 */
pal_err_t pal_gpio_isr_handler_add(pal_gpio_pin_t pin,
                                     void (*handler)(void*),
                                     void* arg);

/**
 * @brief Remove GPIO interrupt handler
 */
pal_err_t pal_gpio_isr_handler_remove(pal_gpio_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif // STEPAWARE_PAL_GPIO_H
```

**File**: `devices_core/pal/pal_pwm.h`

```cpp
#ifndef STEPAWARE_PAL_PWM_H
#define STEPAWARE_PAL_PWM_H

#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize PWM subsystem
 */
pal_err_t pal_pwm_init(void);

/**
 * @brief Configure PWM channel
 */
pal_err_t pal_pwm_config(pal_pwm_channel_t channel,
                          pal_gpio_pin_t pin,
                          pal_pwm_freq_t freq);

/**
 * @brief Set PWM duty cycle
 */
pal_err_t pal_pwm_set_duty(pal_pwm_channel_t channel, pal_pwm_duty_t duty);

/**
 * @brief Start PWM output
 */
pal_err_t pal_pwm_start(pal_pwm_channel_t channel);

/**
 * @brief Stop PWM output
 */
pal_err_t pal_pwm_stop(pal_pwm_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif // STEPAWARE_PAL_PWM_H
```

**File**: `devices_core/pal/pal_i2c.h`

```cpp
#ifndef STEPAWARE_PAL_I2C_H
#define STEPAWARE_PAL_I2C_H

#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C configuration structure
 */
typedef struct {
    pal_i2c_mode_t mode;
    pal_gpio_pin_t sda_pin;
    pal_gpio_pin_t scl_pin;
    uint32_t freq_hz;
    bool sda_pullup_en;
    bool scl_pullup_en;
} pal_i2c_config_t;

/**
 * @brief Initialize I2C port
 */
pal_err_t pal_i2c_init(pal_i2c_port_t port, const pal_i2c_config_t* config);

/**
 * @brief Deinitialize I2C port
 */
pal_err_t pal_i2c_deinit(pal_i2c_port_t port);

/**
 * @brief Write to I2C device
 */
pal_err_t pal_i2c_write(pal_i2c_port_t port,
                         pal_i2c_addr_t addr,
                         const uint8_t* data,
                         size_t len,
                         uint32_t timeout_ms);

/**
 * @brief Read from I2C device
 */
pal_err_t pal_i2c_read(pal_i2c_port_t port,
                        pal_i2c_addr_t addr,
                        uint8_t* data,
                        size_t len,
                        uint32_t timeout_ms);

/**
 * @brief Write then read from I2C device
 */
pal_err_t pal_i2c_write_read(pal_i2c_port_t port,
                              pal_i2c_addr_t addr,
                              const uint8_t* write_data,
                              size_t write_len,
                              uint8_t* read_data,
                              size_t read_len,
                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // STEPAWARE_PAL_I2C_H
```

**File**: `devices_core/pal/pal_adc.h`

```cpp
#ifndef STEPAWARE_PAL_ADC_H
#define STEPAWARE_PAL_ADC_H

#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ADC subsystem
 */
pal_err_t pal_adc_init(void);

/**
 * @brief Configure ADC channel
 */
pal_err_t pal_adc_config(pal_adc_channel_t channel,
                          pal_gpio_pin_t pin,
                          pal_adc_atten_t atten);

/**
 * @brief Read ADC raw value
 */
int pal_adc_read_raw(pal_adc_channel_t channel);

/**
 * @brief Read ADC voltage (mV)
 */
int pal_adc_read_voltage(pal_adc_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif // STEPAWARE_PAL_ADC_H
```

**File**: `devices_core/pal/pal_timer.h`

```cpp
#ifndef STEPAWARE_PAL_TIMER_H
#define STEPAWARE_PAL_TIMER_H

#include "pal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Timer configuration structure
 */
typedef struct {
    uint32_t period_us;         // Timer period in microseconds
    bool auto_reload;           // Auto-reload timer
    pal_timer_callback_t callback;
    void* callback_arg;
} pal_timer_config_t;

/**
 * @brief Initialize timer subsystem
 */
pal_err_t pal_timer_init(void);

/**
 * @brief Create timer
 */
pal_err_t pal_timer_create(pal_timer_id_t* timer_id, const pal_timer_config_t* config);

/**
 * @brief Start timer
 */
pal_err_t pal_timer_start(pal_timer_id_t timer_id);

/**
 * @brief Stop timer
 */
pal_err_t pal_timer_stop(pal_timer_id_t timer_id);

/**
 * @brief Delete timer
 */
pal_err_t pal_timer_delete(pal_timer_id_t timer_id);

/**
 * @brief Get microsecond timestamp
 */
uint64_t pal_timer_get_us(void);

/**
 * @brief Delay microseconds
 */
void pal_timer_delay_us(uint32_t us);

/**
 * @brief Delay milliseconds
 */
void pal_timer_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // STEPAWARE_PAL_TIMER_H
```

### Step 2: ESP32-C3 Device Driver (Baseline)

Extract existing ESP32-C3 specific code into device driver.

**File**: `devices_core/esp32_c3/device_esp32_c3.h`

```cpp
#ifndef STEPAWARE_DEVICE_ESP32_C3_H
#define STEPAWARE_DEVICE_ESP32_C3_H

#include "../pal/pal_types.h"

/**
 * @brief ESP32-C3 specific device information
 */

#define DEVICE_NAME "ESP32-C3"
#define DEVICE_CHIP_ID 0x1B31506F
#define DEVICE_FLASH_SIZE (4 * 1024 * 1024)  // 4MB
#define DEVICE_RAM_SIZE (400 * 1024)          // 400KB
#define DEVICE_GPIO_COUNT 22

// Capabilities
#define DEVICE_HAS_WIFI 1
#define DEVICE_HAS_BLE 1
#define DEVICE_HAS_THREAD 0
#define DEVICE_HAS_ZIGBEE 0

// Power consumption (µA)
#define DEVICE_DEEP_SLEEP_CURRENT 5
#define DEVICE_LIGHT_SLEEP_CURRENT 130
#define DEVICE_MODEM_SLEEP_WIFI_CURRENT 15000
#define DEVICE_ACTIVE_CURRENT_WIFI 80000

/**
 * @brief Initialize ESP32-C3 device
 */
pal_err_t device_esp32_c3_init(void);

/**
 * @brief Get device information
 */
const char* device_esp32_c3_get_name(void);
uint32_t device_esp32_c3_get_chip_id(void);
uint32_t device_esp32_c3_get_flash_size(void);
uint32_t device_esp32_c3_get_ram_size(void);

#endif // STEPAWARE_DEVICE_ESP32_C3_H
```

**File**: `devices_core/esp32_c3/gpio_esp32_c3.cpp`

```cpp
#include "../pal/pal_gpio.h"
#include <driver/gpio.h>

pal_err_t pal_gpio_init(void) {
    // ESP32-C3 GPIO initialization
    return PAL_OK;
}

pal_err_t pal_gpio_set_mode(pal_gpio_pin_t pin, pal_gpio_mode_t mode) {
    gpio_mode_t esp_mode;

    switch (mode) {
        case PAL_GPIO_MODE_INPUT:
            esp_mode = GPIO_MODE_INPUT;
            break;
        case PAL_GPIO_MODE_OUTPUT:
            esp_mode = GPIO_MODE_OUTPUT;
            break;
        case PAL_GPIO_MODE_INPUT_PULLUP:
            esp_mode = GPIO_MODE_INPUT;
            gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
            break;
        case PAL_GPIO_MODE_INPUT_PULLDOWN:
            esp_mode = GPIO_MODE_INPUT;
            gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLDOWN_ONLY);
            break;
        case PAL_GPIO_MODE_OUTPUT_OD:
            esp_mode = GPIO_MODE_OUTPUT_OD;
            break;
        default:
            return PAL_ERR_INVALID_ARG;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = esp_mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        return PAL_ERR_INVALID_STATE;
    }

    return PAL_OK;
}

pal_err_t pal_gpio_set_level(pal_gpio_pin_t pin, pal_gpio_level_t level) {
    if (gpio_set_level((gpio_num_t)pin, level) != ESP_OK) {
        return PAL_ERR_INVALID_ARG;
    }
    return PAL_OK;
}

pal_gpio_level_t pal_gpio_get_level(pal_gpio_pin_t pin) {
    return (pal_gpio_level_t)gpio_get_level((gpio_num_t)pin);
}

// ... additional GPIO functions
```

Similar implementations for:
- `i2c_esp32_c3.cpp`
- `pwm_esp32_c3.cpp`
- `timer_esp32_c3.cpp`
- `adc_esp32_c3.cpp`

### Step 3: ESP32-C6 Device Driver (Thread/WiFi)

**File**: `devices_core/esp32_c6/device_esp32_c6.h`

```cpp
#ifndef STEPAWARE_DEVICE_ESP32_C6_H
#define STEPAWARE_DEVICE_ESP32_C6_H

#include "../pal/pal_types.h"

/**
 * @brief ESP32-C6 specific device information
 */

#define DEVICE_NAME "ESP32-C6"
#define DEVICE_CHIP_ID 0x0D // Placeholder
#define DEVICE_FLASH_SIZE (8 * 1024 * 1024)  // 8MB typical
#define DEVICE_RAM_SIZE (512 * 1024)          // 512KB
#define DEVICE_GPIO_COUNT 30

// Capabilities
#define DEVICE_HAS_WIFI 1
#define DEVICE_HAS_BLE 1
#define DEVICE_HAS_THREAD 1    // 802.15.4 radio
#define DEVICE_HAS_ZIGBEE 1    // Same radio as Thread

// Power consumption (µA) - IMPROVED over C3
#define DEVICE_DEEP_SLEEP_CURRENT 7
#define DEVICE_LIGHT_SLEEP_CURRENT 110
#define DEVICE_MODEM_SLEEP_THREAD_CURRENT 1200  // ~1.2mA vs 15mA WiFi!
#define DEVICE_ACTIVE_CURRENT_THREAD 8000       // ~8mA vs 80mA WiFi!

/**
 * @brief Initialize ESP32-C6 device
 */
pal_err_t device_esp32_c6_init(void);

/**
 * @brief Initialize Thread network
 */
pal_err_t device_esp32_c6_thread_init(void);

/**
 * @brief Get device information
 */
const char* device_esp32_c6_get_name(void);
uint32_t device_esp32_c6_get_chip_id(void);

#endif // STEPAWARE_DEVICE_ESP32_C6_H
```

**File**: `devices_core/esp32_c6/thread_esp32_c6.cpp`

```cpp
#include "device_esp32_c6.h"
#include <esp_openthread.h>
#include <openthread/thread.h>

/**
 * @brief Initialize Thread network on ESP32-C6
 */
pal_err_t device_esp32_c6_thread_init(void) {
    // Initialize OpenThread
    esp_openthread_platform_config_t config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_CLI_UART,
        },
        .port_config = {
            .storage_partition_name = "nvs",
        }
    };

    if (esp_openthread_init(&config) != ESP_OK) {
        return PAL_ERR_INVALID_STATE;
    }

    // Configure Thread network
    otInstance* instance = esp_openthread_get_instance();

    // Set network key, pan ID, etc.
    // (Configuration would come from ConfigManager)

    otThreadSetEnabled(instance, true);

    return PAL_OK;
}

/**
 * @brief Send data over Thread network
 */
pal_err_t thread_send_message(const uint8_t* data, size_t len) {
    otInstance* instance = esp_openthread_get_instance();

    otMessage* message = otUdpNewMessage(instance, NULL);
    if (!message) {
        return PAL_ERR_NO_MEM;
    }

    // Send UDP message over Thread
    // Implementation details...

    return PAL_OK;
}
```

### Step 4: ESP32-H2 Device Driver (Thread/Zigbee/BLE)

**File**: `devices_core/esp32_h2/device_esp32_h2.h`

```cpp
#ifndef STEPAWARE_DEVICE_ESP32_H2_H
#define STEPAWARE_DEVICE_ESP32_H2_H

#include "../pal/pal_types.h"

/**
 * @brief ESP32-H2 specific device information
 */

#define DEVICE_NAME "ESP32-H2"
#define DEVICE_CHIP_ID 0x10 // Placeholder
#define DEVICE_FLASH_SIZE (4 * 1024 * 1024)  // 4MB
#define DEVICE_RAM_SIZE (320 * 1024)          // 320KB
#define DEVICE_GPIO_COUNT 25

// Capabilities
#define DEVICE_HAS_WIFI 0      // NO WiFi - Thread/Zigbee only
#define DEVICE_HAS_BLE 1       // BLE 5.2
#define DEVICE_HAS_THREAD 1    // 802.15.4 radio
#define DEVICE_HAS_ZIGBEE 1    // Zigbee 3.0

// Power consumption (µA) - LOWEST of all
#define DEVICE_DEEP_SLEEP_CURRENT 5
#define DEVICE_LIGHT_SLEEP_CURRENT 100
#define DEVICE_MODEM_SLEEP_THREAD_CURRENT 1000  // ~1mA
#define DEVICE_ACTIVE_CURRENT_THREAD 7000       // ~7mA

/**
 * @brief Initialize ESP32-H2 device
 */
pal_err_t device_esp32_h2_init(void);

/**
 * @brief Initialize Thread/Zigbee network
 */
pal_err_t device_esp32_h2_thread_init(void);
pal_err_t device_esp32_h2_zigbee_init(void);

#endif // STEPAWARE_DEVICE_ESP32_H2_H
```

### Step 5: Update HAL to Use PAL

Update existing HAL classes to use PAL instead of direct ESP32 calls.

**Example**: Update `HAL_LED` to use PAL

**File**: `src/hal_led.cpp`

```cpp
// OLD (ESP32-specific):
#include <driver/ledc.h>
#include <driver/gpio.h>

// NEW (Platform-agnostic):
#include "devices_core/pal/pal_gpio.h"
#include "devices_core/pal/pal_pwm.h"

bool HAL_LED::begin() {
    if (m_mockMode) {
        m_initialized = true;
        return true;
    }

    // OLD:
    // gpio_reset_pin((gpio_num_t)m_pin);
    // ledc_timer_config_t timer_conf = { ... };

    // NEW (platform-agnostic):
    pal_pwm_config(m_pwmChannel, m_pin, 5000);
    m_initialized = true;

    return true;
}

void HAL_LED::setBrightness(uint8_t brightness) {
    if (!m_initialized || m_mockMode) {
        m_brightness = brightness;
        return;
    }

    // OLD:
    // uint32_t duty = (brightness * 1023) / 255;
    // ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)m_pwmChannel, duty);

    // NEW (platform-agnostic):
    uint16_t duty = (brightness * 1023) / 255;
    pal_pwm_set_duty(m_pwmChannel, duty);

    m_brightness = brightness;
}
```

Similar updates for:
- `HAL_Button` → use `pal_gpio.h`
- `HAL_PIR` → use `pal_gpio.h`
- `HAL_Ultrasonic` → use `pal_gpio.h` + `pal_timer.h`
- `HAL_LEDMatrix_8x8` → use `pal_i2c.h`
- `PowerManager` → use `pal_adc.h`

### Step 6: Build System Updates

**File**: `platformio.ini`

Add environments for each platform:

```ini
# Baseline ESP32-C3
[env:esp32-c3-devkitlipo]
platform = espressif32
board = esp32-c3-devkitlipo
framework = arduino
build_flags =
    -DDEVICE_PLATFORM=ESP32_C3
    -DCONFIG_FREERTOS_HZ=100
lib_deps = ${common.lib_deps}
build_src_filter =
    +<*>
    +<../devices_core/pal/*.cpp>
    +<../devices_core/esp32_c3/*.cpp>

# NEW: ESP32-C6 with Thread support
[env:esp32-c6-thread]
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
build_flags =
    -DDEVICE_PLATFORM=ESP32_C6
    -DDEVICE_HAS_THREAD=1
    -DCONFIG_OPENTHREAD_ENABLED=1
lib_deps =
    ${common.lib_deps}
    espressif/esp-openthread @ ^1.0.0
build_src_filter =
    +<*>
    +<../devices_core/pal/*.cpp>
    +<../devices_core/esp32_c6/*.cpp>

# NEW: ESP32-H2 with Thread/Zigbee support
[env:esp32-h2-thread]
platform = espressif32
board = esp32-h2-devkitm-1
framework = arduino
build_flags =
    -DDEVICE_PLATFORM=ESP32_H2
    -DDEVICE_HAS_THREAD=1
    -DDEVICE_HAS_ZIGBEE=1
    -DCONFIG_OPENTHREAD_ENABLED=1
lib_deps =
    ${common.lib_deps}
    espressif/esp-openthread @ ^1.0.0
    espressif/esp-zigbee-lib @ ^1.0.0
build_src_filter =
    +<*>
    +<../devices_core/pal/*.cpp>
    +<../devices_core/esp32_h2/*.cpp>
```

### Step 7: Configuration for Multiple Platforms

**File**: `include/config.h`

```cpp
// Platform detection
#if defined(DEVICE_PLATFORM_ESP32_C3)
    #include "devices_core/esp32_c3/device_esp32_c3.h"
#elif defined(DEVICE_PLATFORM_ESP32_C6)
    #include "devices_core/esp32_c6/device_esp32_c6.h"
#elif defined(DEVICE_PLATFORM_ESP32_H2)
    #include "devices_core/esp32_h2/device_esp32_h2.h"
#else
    #error "Unknown device platform"
#endif

// Conditional networking
#if DEVICE_HAS_WIFI
    #define NETWORK_MODE_WIFI 1
#endif

#if DEVICE_HAS_THREAD
    #define NETWORK_MODE_THREAD 1
#endif

// Power consumption optimization
#if DEVICE_HAS_THREAD && !defined(NETWORK_MODE_FORCE_WIFI)
    #define NETWORK_MODE_DEFAULT NETWORK_MODE_THREAD
#else
    #define NETWORK_MODE_DEFAULT NETWORK_MODE_WIFI
#endif
```

### Step 8: Documentation

**File**: `devices_core/README.md`

```markdown
# Platform Abstraction Layer (PAL) - Adding New Hardware

## Overview

The Platform Abstraction Layer (PAL) allows StepAware to run on multiple hardware platforms without changing application code.

## Supported Platforms

| Platform | WiFi | BLE | Thread | Zigbee | Status |
|----------|------|-----|--------|--------|--------|
| ESP32-C3 | ✅   | ✅  | ❌     | ❌     | ✅ Baseline |
| ESP32-C6 | ✅   | ✅  | ✅     | ✅     | 🚧 In Progress |
| ESP32-H2 | ❌   | ✅  | ✅     | ✅     | 🚧 Planned |

## Adding a New Platform

### 1. Create Device Directory

```bash
mkdir devices_core/your_platform
```

### 2. Implement Device Driver

Create `device_your_platform.h`:
```cpp
#ifndef STEPAWARE_DEVICE_YOUR_PLATFORM_H
#define STEPAWARE_DEVICE_YOUR_PLATFORM_H

#include "../pal/pal_types.h"

#define DEVICE_NAME "Your Platform"
#define DEVICE_HAS_WIFI 1
// ... define capabilities

pal_err_t device_your_platform_init(void);

#endif
```

### 3. Implement PAL Functions

For each PAL interface (GPIO, PWM, I2C, etc.), create implementation:
- `gpio_your_platform.cpp`
- `pwm_your_platform.cpp`
- `i2c_your_platform.cpp`
- etc.

### 4. Add Build Environment

In `platformio.ini`:
```ini
[env:your-platform]
platform = ...
board = ...
build_flags = -DDEVICE_PLATFORM=YOUR_PLATFORM
build_src_filter =
    +<*>
    +<../devices_core/pal/*.cpp>
    +<../devices_core/your_platform/*.cpp>
```

### 5. Test

```bash
pio run -e your-platform
pio test -e native  # Mock tests work on all platforms
```

## Power Consumption Comparison

| Platform | Deep Sleep | Light Sleep | WiFi Active | Thread Active |
|----------|------------|-------------|-------------|---------------|
| ESP32-C3 | 5µA        | 130µA       | 80mA        | N/A           |
| ESP32-C6 | 7µA        | 110µA       | 80mA        | 8mA           |
| ESP32-H2 | 5µA        | 100µA       | N/A         | 7mA           |

**Thread = 10x lower power than WiFi!**

## Thread vs WiFi

### Why Thread?

1. **Power Efficiency**: ~8mA vs ~80mA (10x improvement)
2. **Mesh Networking**: Self-healing, multi-hop
3. **Matter Compatible**: Smart home interoperability
4. **Lower Latency**: < 10ms vs 100ms+

### Battery Life Comparison

**Motion Detection Mode** (1000mAh battery):

| Platform | Network | Battery Life |
|----------|---------|--------------|
| ESP32-C3 | WiFi    | 11 days      |
| ESP32-C6 | WiFi    | 11 days      |
| ESP32-C6 | Thread  | **90 days**  |
| ESP32-H2 | Thread  | **100 days** |

### When to Use WiFi vs Thread

**Use WiFi:**
- Direct internet access needed
- Existing WiFi infrastructure
- Web dashboard access

**Use Thread:**
- Maximum battery life
- Mesh network coverage
- Matter/smart home integration
- Multiple StepAware devices

**Use Both (ESP32-C6):**
- Thread for device communication
- WiFi for cloud/web access
- Best of both worlds
```

---

## Testing Strategy

### Phase 1: ESP32-C3 Baseline
1. Extract ESP32-C3 code to device driver
2. Update HAL to use PAL
3. Verify all existing functionality works
4. Run full test suite

### Phase 2: ESP32-C6 Thread
1. Implement ESP32-C6 device driver
2. Add Thread network support
3. Test basic functionality
4. Measure power consumption
5. Compare WiFi vs Thread battery life

### Phase 3: ESP32-H2 Zigbee
1. Implement ESP32-H2 device driver
2. Add Zigbee support
3. Test Thread and Zigbee modes
4. Validate power consumption claims

---

## Implementation Timeline

| Task | Estimated Effort | Priority |
|------|-----------------|----------|
| Create PAL interfaces | 4 hours | P0 |
| ESP32-C3 device driver | 8 hours | P0 |
| Update HAL to use PAL | 12 hours | P0 |
| Testing and validation | 6 hours | P0 |
| ESP32-C6 device driver | 10 hours | P1 |
| Thread network integration | 8 hours | P1 |
| ESP32-H2 device driver | 8 hours | P2 |
| Zigbee integration | 6 hours | P2 |
| Documentation | 4 hours | P1 |
| **Total** | **66 hours** | |

---

## Files to Create

### PAL Interfaces (7 files)
- `devices_core/pal/pal_types.h`
- `devices_core/pal/pal_gpio.h`
- `devices_core/pal/pal_pwm.h`
- `devices_core/pal/pal_i2c.h`
- `devices_core/pal/pal_adc.h`
- `devices_core/pal/pal_timer.h`
- `devices_core/pal/pal_uart.h`

### ESP32-C3 Driver (8 files)
- `devices_core/esp32_c3/device_esp32_c3.h`
- `devices_core/esp32_c3/device_esp32_c3.cpp`
- `devices_core/esp32_c3/gpio_esp32_c3.cpp`
- `devices_core/esp32_c3/pwm_esp32_c3.cpp`
- `devices_core/esp32_c3/i2c_esp32_c3.cpp`
- `devices_core/esp32_c3/adc_esp32_c3.cpp`
- `devices_core/esp32_c3/timer_esp32_c3.cpp`
- `devices_core/esp32_c3/uart_esp32_c3.cpp`

### ESP32-C6 Driver (9 files)
- Same structure as C3 + `thread_esp32_c6.cpp`

### ESP32-H2 Driver (10 files)
- Same structure as C6 + `zigbee_esp32_h2.cpp`

### Documentation (2 files)
- `devices_core/README.md`
- `docs/PLATFORM_MIGRATION.md`

### Files to Modify
- `platformio.ini` - Add new environments
- `include/config.h` - Platform detection
- All HAL files - Use PAL instead of ESP32 APIs
- `src/main.cpp` - Platform-specific initialization

**Total New Files**: ~36
**Total Modified Files**: ~15

---

## Success Criteria

### Phase 3 Complete When:
- ✅ ESP32-C3 code extracted to device driver
- ✅ PAL interfaces defined and documented
- ✅ All HAL classes use PAL (no direct ESP32 calls)
- ✅ Build system supports multiple platforms
- ✅ ESP32-C6 driver implemented
- ✅ Thread networking functional
- ✅ Power consumption validated (8mA Thread vs 80mA WiFi)
- ✅ ESP32-H2 driver implemented
- ✅ Documentation complete
- ✅ Migration guide written
- ✅ All tests passing on all platforms

---

## Benefits Summary

### Developer Benefits
- Write once, run on any supported platform
- Easy to add new platforms (just implement PAL)
- Cleaner separation of concerns
- Better testability

### User Benefits
- **10x battery life improvement** with Thread networking
- Matter/smart home compatibility
- Future-proof design
- Hardware choice flexibility

### Project Benefits
- ESP32-C6/H2 support opens new markets
- Thread/Matter ecosystem integration
- Professional architecture
- Competitive advantage

---

## Next Steps

1. **Review this plan** with stakeholders
2. **Create GitHub Issue #4 Phase 3** with this plan
3. **Start with PAL interfaces** (lowest risk)
4. **Extract ESP32-C3 driver** (validate approach)
5. **Update HAL classes** (incremental, testable)
6. **Add ESP32-C6 support** (prove the concept)
7. **Measure power consumption** (validate claims)
8. **Add ESP32-H2 support** (complete the vision)

---

**Ready to implement Phase 3!** 🚀
