#ifndef STEPAWARE_CONFIG_MANAGER_H
#define STEPAWARE_CONFIG_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "sensor_types.h"
#include "display_types.h"

/**
 * @brief Configuration Manager for StepAware
 *
 * Manages runtime configuration stored in SPIFFS as JSON.
 * Provides validation, defaults, and save/load functionality.
 *
 * Features:
 * - JSON serialization/deserialization
 * - Default values for all settings
 * - Validation of all parameters
 * - Automatic migration of old configs
 * - Factory reset capability
 */
class ConfigManager {
public:
    /**
     * @brief Sensor slot configuration
     */
    struct SensorSlotConfig {
        bool active;                      // Slot in use
        char name[32];                    // Sensor name
        SensorType type;                  // Sensor type
        uint8_t primaryPin;               // Primary GPIO pin
        uint8_t secondaryPin;             // Secondary GPIO pin (ultrasonic echo)
        bool enabled;                     // Sensor enabled
        bool isPrimary;                   // Primary sensor flag
        uint32_t detectionThreshold;      // mm - warning trigger distance
        uint32_t maxDetectionDistance;    // mm - max detection range (0=use threshold)
        uint32_t debounceMs;              // ms (not used by PIR sensors)
        uint32_t warmupMs;                // ms
        bool enableDirectionDetection;    // Direction detection enabled
        uint8_t directionTriggerMode;     // 0=approaching, 1=receding, 2=both
        uint16_t directionSensitivity;    // Direction change threshold (mm), 0 = auto (adaptive threshold)
        uint8_t sampleWindowSize;         // Rolling window size (3-20, 0=default 10)
        uint16_t sampleRateMs;            // Sample rate in ms (60+ for ultrasonic, 0=default 60)
        uint8_t distanceZone;             // PIR distance zone: 0=None, 1=Near (0.5-4m), 2=Far (3-12m)
    };

    /**
     * @brief Display slot configuration
     */
    struct DisplaySlotConfig {
        bool active;                    // Slot in use
        char name[32];                  // Display name
        DisplayType type;               // DISPLAY_TYPE_SINGLE_LED, DISPLAY_TYPE_MATRIX_8X8
        uint8_t i2cAddress;             // I2C address (0x70-0x77 for HT16K33)
        uint8_t sdaPin;                 // I2C SDA pin
        uint8_t sclPin;                 // I2C SCL pin
        bool enabled;                   // Display enabled
        uint8_t brightness;             // Brightness level (0-15 for matrix, 0-255 for LED)
        uint8_t rotation;               // Rotation (0-3 for 90° increments)
        bool useForStatus;              // Use for status displays
    };

    /**
     * @brief Direction detector configuration (Dual-PIR)
     */
    struct DirectionDetectorConfig {
        bool enabled;                      // Enable direction detection (default: false)
        uint8_t farSensorSlot;            // Which sensor slot is "far" (0-3, default: 1)
        uint8_t nearSensorSlot;           // Which sensor slot is "near" (0-3, default: 0)
        uint32_t confirmationWindowMs;    // Confirmation window (default: 5000ms)
        uint32_t simultaneousThresholdMs; // Simultaneous threshold (default: 150ms)
        uint32_t patternTimeoutMs;        // Pattern timeout (default: 10000ms)
        bool triggerOnApproaching;        // Only trigger on approaching (default: true)
    };

    /**
     * @brief Runtime configuration structure
     */
    struct Config {
        // Motion Detection
        uint32_t motionWarningDuration;    // ms
        uint32_t pirWarmupTime;            // ms

        // Button
        uint32_t buttonDebounceMs;         // ms
        uint32_t buttonLongPressMs;        // ms

        // LED Settings
        uint8_t ledBrightnessFull;         // 0-255
        uint8_t ledBrightnessMedium;       // 0-255
        uint8_t ledBrightnessDim;          // 0-255
        uint16_t ledBlinkFastMs;           // ms
        uint16_t ledBlinkSlowMs;           // ms
        uint16_t ledBlinkWarningMs;        // ms

        // Battery Management
        uint16_t batteryVoltageFull;       // mV
        uint16_t batteryVoltageLow;        // mV
        uint16_t batteryVoltageCritical;   // mV

        // Light Sensor
        uint16_t lightThresholdDark;       // ADC value
        uint16_t lightThresholdBright;     // ADC value

        // Distance Sensor (Ultrasonic/IR)
        uint16_t sensorMinDistance;        // cm - minimum detection distance
        uint16_t sensorMaxDistance;        // cm - maximum detection distance
        bool sensorDirectionEnabled;       // Enable direction detection
        uint8_t sensorRapidSampleCount;    // Number of samples for direction
        uint16_t sensorRapidSampleMs;      // Interval between rapid samples (ms)

        // WiFi (Phase 2)
        char wifiSSID[64];                 // WiFi SSID
        char wifiPassword[64];             // WiFi password
        char deviceName[32];               // Device name
        bool wifiEnabled;                  // WiFi enabled flag

        // Operating Mode
        uint8_t defaultMode;               // Default operating mode
        bool rememberLastMode;             // Remember mode on reboot

        // Power Management
        bool powerSavingEnabled;           // Enable power saving
        uint32_t deepSleepAfterMs;         // ms of inactivity before deep sleep

        // Logging
        uint8_t logLevel;                  // Log level (DEBUG, INFO, WARN, ERROR)
        bool serialLoggingEnabled;         // Enable serial logging
        bool fileLoggingEnabled;           // Enable file logging

        // Multi-Sensor Configuration (Phase 2)
        SensorSlotConfig sensors[4];       // Up to 4 sensor slots
        uint8_t fusionMode;                // Sensor fusion mode

        // Multi-Display Configuration (Issue #12)
        DisplaySlotConfig displays[2];     // Up to 2 display devices
        uint8_t primaryDisplaySlot;        // Primary display slot index

        // Direction Detection (Dual-PIR)
        DirectionDetectorConfig directionDetector;  // Direction detector configuration

        // Configuration metadata
        char version[16];                  // Config version
        uint32_t lastModified;             // Unix timestamp
    };

    /**
     * @brief Construct a new Config Manager
     */
    ConfigManager();

    /**
     * @brief Destructor
     */
    ~ConfigManager();

    /**
     * @brief Initialize the configuration manager
     *
     * Mounts SPIFFS and attempts to load config from file.
     * If config doesn't exist or is invalid, uses defaults.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Load configuration from SPIFFS
     *
     * @return true if config loaded successfully
     */
    bool load();

    /**
     * @brief Save current configuration to SPIFFS
     *
     * @return true if config saved successfully
     */
    bool save();

    /**
     * @brief Reset configuration to factory defaults
     *
     * @param save If true, saves defaults to SPIFFS
     * @return true if reset successful
     */
    bool reset(bool save = true);

    /**
     * @brief Validate current configuration
     *
     * Checks all values are within acceptable ranges.
     *
     * @return true if configuration is valid
     */
    bool validate();

    /**
     * @brief Validate and correct configuration at boot
     *
     * Checks all configuration values for corruption or out-of-range values.
     * Automatically corrects any invalid values with defaults and logs errors.
     * Should be called after loading configuration from file.
     *
     * @return true if no corrections were needed, false if corrections were made
     */
    bool validateAndCorrect();

    /**
     * @brief Auto-configure direction detector based on sensor distance zones
     *
     * Scans all PIR sensors for distance zone settings (Near/Far).
     * If one sensor is configured as Near and another as Far, automatically
     * enables dual-PIR direction detection and configures the sensor slots.
     * Otherwise, disables direction detection.
     */
    void autoConfigureDirectionDetector();

    /**
     * @brief Get current configuration
     *
     * @return const Config& Reference to current config
     */
    const Config& getConfig() const;

    /**
     * @brief Set configuration
     *
     * @param config New configuration (will be validated)
     * @return true if config is valid and was set
     */
    bool setConfig(const Config& config);

    /**
     * @brief Get configuration as JSON string
     *
     * @param buffer Output buffer
     * @param bufferSize Size of buffer
     * @return true if JSON generated successfully
     */
    bool toJSON(char* buffer, size_t bufferSize);

    /**
     * @brief Load configuration from JSON string
     *
     * @param json JSON string
     * @return true if JSON parsed successfully
     */
    bool fromJSON(const char* json);

    /**
     * @brief Print current configuration to Serial
     */
    void print();

    /**
     * @brief Get last error message
     *
     * @return const char* Error message
     */
    const char* getLastError() const;

    // NOTE: setWiFiCredentials() removed as unused (Wave 3b cleanup)
    // WiFi credentials are set via setConfig() in serial_config.cpp

private:
    Config m_config;                       ///< Current configuration
    bool m_initialized;                    ///< Initialization complete
    char m_lastError[128];                 ///< Last error message
    static const char* CONFIG_FILE_PATH;   ///< Config file path in SPIFFS

    /**
     * @brief Load default configuration
     */
    void loadDefaults();

    /**
     * @brief Validate individual parameters
     *
     * @return true if all parameters valid
     */
    bool validateParameters();

    /**
     * @brief Set error message
     *
     * @param error Error message
     */
    void setError(const char* error);
};

#endif // STEPAWARE_CONFIG_MANAGER_H
