#ifndef STEPAWARE_CONFIG_MANAGER_H
#define STEPAWARE_CONFIG_MANAGER_H

#include <Arduino.h>
#include "config.h"

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
