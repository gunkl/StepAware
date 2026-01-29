#ifndef STEPAWARE_CONFIG_H
#define STEPAWARE_CONFIG_H

// ============================================================================
// StepAware Configuration File
// ============================================================================
// This file contains all system-wide configuration constants, pin definitions,
// and compile-time settings for the StepAware hazard detection system.
//
// Project: StepAware - ESP32-C3 Motion-Activated Hazard Warning System
// Board: Olimex ESP32-C3-DevKit-Lipo
// Sensor: AM312 PIR Motion Sensor
// ============================================================================

// ============================================================================
// Hardware Pin Assignments (ESP32-C3)
// ============================================================================

// Input Pins
#define PIN_BUTTON          0    // Mode button (GPIO0, boot button, pull-up)
#define PIN_PIR_SENSOR      1    // AM312 PIR motion sensor output (GPIO1)
#define PIN_LIGHT_SENSOR    4    // Photoresistor for ambient light sensing (GPIO4, ADC1_CH0)
#define PIN_BATTERY_ADC     5    // Battery voltage monitor (GPIO5, ADC1_CH1)
#define PIN_VBUS_DETECT     6    // USB VBUS detection (GPIO6)

// Direction Detection Pins (Dual-PIR)
#define PIN_PIR_NEAR        1    // Near zone PIR (existing sensor, GPIO1)
#define PIN_PIR_FAR         11   // Far zone PIR (new sensor, GPIO11)

// Output Pins
#define PIN_STATUS_LED      2    // Built-in status LED (GPIO2)
#define PIN_HAZARD_LED      3    // Main hazard warning LED with PWM (GPIO3)

// Ultrasonic Sensor Pins (optional)
// HC-SR04 (4-pin): Separate trigger and echo pins
#define PIN_ULTRASONIC_TRIGGER  8    // Ultrasonic trigger pin (GPIO8)
#define PIN_ULTRASONIC_ECHO     9    // Ultrasonic echo pin (GPIO9)

// Grove Ultrasonic v2.0 (3-pin): Single signal pin for trigger/echo
#define PIN_ULTRASONIC_GROVE_SIG 8   // Grove ultrasonic signal pin (GPIO8, shared trigger/echo)

// I2C Pins (for LED Matrix and other I2C devices)
// Note: Using GPIO 7 and 10 to avoid conflict with ultrasonic sensor (GPIO 8/9)
#define I2C_SDA_PIN             7    // GPIO 7 (SDA)
#define I2C_SCL_PIN             10   // GPIO 10 (SCL)
#define I2C_FREQUENCY           100000  // 100kHz standard mode

// ============================================================================
// Sensor Selection
// ============================================================================
// Select which motion sensor to use at compile time.
// Options:
//   SENSOR_TYPE_PIR              - AM312 PIR sensor (default)
//   SENSOR_TYPE_ULTRASONIC       - HC-SR04 ultrasonic (4-pin: VCC/GND/Trig/Echo)
//   SENSOR_TYPE_ULTRASONIC_GROVE - Grove Ultrasonic v2.0 (3-pin: VCC/GND/SIG)
//
// Default: SENSOR_TYPE_PIR (AM312 PIR sensor)

#ifndef ACTIVE_SENSOR_TYPE
#define ACTIVE_SENSOR_TYPE      SENSOR_TYPE_PIR
#endif

// Ultrasonic sensor configuration (when using SENSOR_TYPE_ULTRASONIC)
#define ULTRASONIC_THRESHOLD_MM     500     // Detection threshold: 50cm
#define ULTRASONIC_INTERVAL_MS      60      // Measurement interval: 60ms minimum (hardware limit)
#define ULTRASONIC_SAMPLE_INTERVAL_MS 75    // Default sample interval: 75ms (adaptive threshold)

// Distance-based detection defaults
#define SENSOR_MIN_DISTANCE_CM      30      // Minimum detection distance: 30cm
#define SENSOR_MAX_DISTANCE_CM      200     // Maximum detection distance: 200cm (2m)
#define SENSOR_DIRECTION_ENABLED    true    // Enable direction detection by default
#define SENSOR_RAPID_SAMPLE_COUNT   5       // Take 5 samples for direction detection
#define SENSOR_RAPID_SAMPLE_MS      100     // 100ms between rapid samples

// ============================================================================
// System Constants
// ============================================================================

// Version Information
#define FIRMWARE_VERSION    "0.1.1"
#define FIRMWARE_NAME       "StepAware"
#define BUILD_DATE          __DATE__
#define BUILD_TIME          __TIME__

// Serial Communication
#define SERIAL_BAUD_RATE    115200

// ============================================================================
// Timing Constants (milliseconds)
// ============================================================================

// Motion Detection
#define MOTION_WARNING_DURATION_MS    15000   // 15 seconds LED warning
#define PIR_WARMUP_TIME_MS            60000   // 1 minute PIR sensor warm-up
#define PIR_OUTPUT_DELAY_MS           2300    // AM312 output timing delay

// Direction Detection (Dual-PIR)
#define DIR_CONFIRMATION_WINDOW_MS    5000    // 5s window for pattern confirmation
#define DIR_SIMULTANEOUS_THRESHOLD_MS 500     // <500ms = simultaneous (ambiguous)
#define DIR_PATTERN_TIMEOUT_MS        10000   // 10s timeout to reset state
#define DIR_MIN_SEQUENCE_TIME_MS      300     // Minimum 300ms between triggers

// Button Debouncing
#define BUTTON_DEBOUNCE_MS            50      // 50ms debounce time
#define BUTTON_LONG_PRESS_MS          3000    // 3 seconds for long press
#define BUTTON_WIFI_RESET_MS          15000   // 15 seconds for WiFi credential reset
#define BUTTON_FACTORY_RESET_MS       30000   // 30 seconds for full factory reset

// LED Patterns
#define LED_BLINK_FAST_MS             250     // Fast blink (250ms on/off)
#define LED_BLINK_SLOW_MS             1000    // Slow blink (1s on/off)
#define LED_BLINK_WARNING_MS          500     // Warning blink (500ms on/off)

// ============================================================================
// LED PWM Configuration
// ============================================================================

#define LED_PWM_FREQUENCY    5000      // 5 kHz PWM frequency
#define LED_PWM_RESOLUTION   8         // 8-bit resolution (0-255)
#define LED_PWM_CHANNEL      0         // PWM channel 0

// LED Brightness Levels (0-255)
#define LED_BRIGHTNESS_OFF         0
#define LED_BRIGHTNESS_DIM         20    // Night light mode
#define LED_BRIGHTNESS_MEDIUM      128   // Status indication
#define LED_BRIGHTNESS_FULL        255   // Hazard warning (full brightness)

// ============================================================================
// LED Matrix Configuration (8x8 Adafruit Mini w/I2C Backpack)
// ============================================================================

#define MATRIX_I2C_ADDRESS          0x70    // Default HT16K33 address (0x70-0x77)
#define MATRIX_BRIGHTNESS_DEFAULT   15      // 0-15 scale (max brightness)
#define MATRIX_ROTATION             0       // 0, 1, 2, or 3 (90° increments)

// Animation timings
#define MATRIX_SCROLL_SPEED_MS      100     // Scroll delay between frames
#define MATRIX_FLASH_DURATION_MS    200     // Flash on/off duration
#define MATRIX_BOOT_DISPLAY_MS      3000    // Boot status display time

// ============================================================================
// Battery Management
// ============================================================================

// Battery Voltage Thresholds (in millivolts)
#define BATTERY_VOLTAGE_FULL       4200    // 4.2V - Fully charged LiPo
#define BATTERY_VOLTAGE_NOMINAL    3700    // 3.7V - Nominal voltage
#define BATTERY_VOLTAGE_LOW        3300    // 3.3V - Low battery (25%)
#define BATTERY_VOLTAGE_CRITICAL   3000    // 3.0V - Critical (shutdown)

// ADC Configuration
#define ADC_RESOLUTION         12         // 12-bit ADC
#define ADC_MAX_VALUE          4095       // Maximum ADC reading (2^12 - 1)
#define ADC_REFERENCE_VOLTAGE  3300       // 3.3V reference (in millivolts)
#define ADC_SAMPLES_AVERAGE    10         // Number of samples to average

// Battery voltage divider ratio (adjust based on actual circuit)
#define BATTERY_DIVIDER_RATIO  2.0        // Voltage divider: R1=R2 -> ratio=2

// ADC Channel Mappings (ESP32-C3)
#define BATTERY_ADC_CHANNEL    ADC1_CHANNEL_4  // GPIO5 = ADC1_CH4
#define LIGHT_ADC_CHANNEL      ADC1_CHANNEL_3  // GPIO4 = ADC1_CH3

// Pin Aliases for Power Manager
#define VBUS_DETECT_PIN        PIN_VBUS_DETECT
#define PIR_SENSOR_PIN         PIN_PIR_SENSOR
#define BUTTON_PIN             PIN_BUTTON

// ============================================================================
// Light Sensor Configuration
// ============================================================================

// Light Level Thresholds (ADC values, 0-4095)
#define LIGHT_THRESHOLD_DARK      500     // Below this = dark
#define LIGHT_THRESHOLD_BRIGHT    2000    // Above this = bright
#define LIGHT_HYSTERESIS          100     // Hysteresis to prevent flicker

// ============================================================================
// Power Management
// ============================================================================

// Power Modes
#define POWER_MODE_ACTIVE      0    // Full power, all features active
#define POWER_MODE_IDLE        1    // Reduced power, waiting for events
#define POWER_MODE_LIGHT_SLEEP 2    // Light sleep, quick wake-up
#define POWER_MODE_DEEP_SLEEP  3    // Deep sleep, button wake only

// Power Consumption Targets (milliamps)
#define POWER_TARGET_ACTIVE    220
#define POWER_TARGET_IDLE      37
#define POWER_TARGET_SLEEP     3
#define POWER_TARGET_OFF       0.02

// ============================================================================
// Logging Configuration
// ============================================================================

// Log Levels (must match DebugLogger::LogLevel enum)
#define LOG_LEVEL_VERBOSE  0
#define LOG_LEVEL_DEBUG    1
#define LOG_LEVEL_INFO     2
#define LOG_LEVEL_WARN     3
#define LOG_LEVEL_ERROR    4
#define LOG_LEVEL_NONE     5

// Default Log Level
// IMPORTANT: LOG_LEVEL_DEBUG can cause device bricking due to serial flooding!
// Only use DEBUG level for specific troubleshooting, not for normal operation.
// Override in platformio.ini with -D LOG_LEVEL=LOG_LEVEL_DEBUG if needed
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// Circular Buffer Size (number of log entries)
#define LOG_BUFFER_SIZE    256

// Log File Configuration
#define LOG_MAX_FILE_SIZE   20480      // 20KB per log file
#define LOG_MAX_FILES       5          // Max 5 log files (100KB total)
#define LOG_FLUSH_INTERVAL  60000      // Flush to flash every 60 seconds

// ============================================================================
// WiFi Configuration (Phase 2)
// ============================================================================

// Access Point Settings
#define WIFI_AP_SSID_PREFIX     "StepAware-"
#define WIFI_AP_PASSWORD        ""              // Open AP for first setup
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_MAX_CONNECTIONS 4
#define WIFI_AP_TIMEOUT_MS      600000          // 10 minutes idle timeout

// WiFi Connection
#define WIFI_CONNECT_TIMEOUT_MS 10000           // 10 seconds connection timeout
#define WIFI_RECONNECT_DELAY_MS 5000            // 5 seconds between retries
#define WIFI_MAX_RECONNECT_ATTEMPTS 5

// ============================================================================
// Web Server Configuration (Phase 2)
// ============================================================================

#define WEB_SERVER_PORT         80              // HTTP port
#define WEB_SERVER_PORT_HTTPS   443             // HTTPS port

// API Endpoints
#define API_ENDPOINT_STATUS         "/api/status"
#define API_ENDPOINT_CONFIG         "/api/config"
#define API_ENDPOINT_HISTORY        "/api/history"
#define API_ENDPOINT_VERSION        "/api/version"
#define API_ENDPOINT_AUTH_LOGIN     "/api/auth/login"
#define API_ENDPOINT_AUTH_PASSWORD  "/api/auth/change-password"
#define API_ENDPOINT_EVENTS         "/events"

// ============================================================================
// Testing Configuration (Phase 3)
// ============================================================================

// Test Database
#define TEST_DB_PATH            "/test_results.db"
#define TEST_DB_MAX_RUNS        100             // Keep last 100 test runs

// ============================================================================
// Feature Flags
// ============================================================================

// Enable/disable features at compile time
#define FEATURE_WIFI_ENABLED         1    // WiFi and web interface
#define FEATURE_LIGHT_SENSOR_ENABLED 1    // Ambient light sensing
#define FEATURE_BATTERY_MONITOR      1    // Battery voltage monitoring
#define FEATURE_POWER_MANAGEMENT     1    // Power saving features
#define FEATURE_TESTING_FRAMEWORK    1    // Testing infrastructure

// Mock Hardware (set to 1 for development without physical hardware)
#ifndef MOCK_HARDWARE
#define MOCK_HARDWARE                0    // 0 = real hardware, 1 = mock
#endif

// ============================================================================
// Debug Helpers
// ============================================================================

// Debug macros (only active when LOG_LEVEL <= LOG_LEVEL_DEBUG)
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
    #define DEBUG_PRINT(x)      Serial.print(x)
    #define DEBUG_PRINTLN(x)    Serial.println(x)
    #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

// ============================================================================
// Compile-Time Checks
// ============================================================================

// Ensure sensible configuration
#if MOTION_WARNING_DURATION_MS < 1000
    #error "MOTION_WARNING_DURATION_MS must be at least 1000ms (1 second)"
#endif

#if LED_PWM_RESOLUTION < 8 || LED_PWM_RESOLUTION > 16
    #error "LED_PWM_RESOLUTION must be between 8 and 16 bits"
#endif

#if LOG_BUFFER_SIZE < 64
    #error "LOG_BUFFER_SIZE must be at least 64 entries"
#endif

// ============================================================================
// End of Configuration
// ============================================================================

#endif // STEPAWARE_CONFIG_H
