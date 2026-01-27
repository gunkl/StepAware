#ifndef STEPAWARE_SERIAL_CONFIG_H
#define STEPAWARE_SERIAL_CONFIG_H

#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "sensor_manager.h"

/**
 * @file serial_config.h
 * @brief Serial port configuration interface
 *
 * Provides an interactive command-line interface for device configuration
 * over USB serial. Supports line-based commands with parameters.
 *
 * Command format: <command> [param] [value]
 *
 * Examples:
 *   help                    - Show available commands
 *   config                  - Show current configuration
 *   set wifi.ssid MyNetwork - Set WiFi SSID
 *   set led.brightness 200  - Set LED brightness
 *   save                    - Save configuration to flash
 *   reset                   - Reset to factory defaults
 */
class SerialConfigUI {
public:
    /**
     * @brief Construct SerialConfigUI with ConfigManager and SensorManager references
     *
     * @param configManager Reference to the configuration manager
     * @param sensorManager Reference to the sensor manager
     */
    SerialConfigUI(ConfigManager& configManager, SensorManager& sensorManager);

    /**
     * @brief Initialize the serial configuration interface
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Process incoming serial data
     *
     * Call this in the main loop to handle serial input.
     * Accumulates characters until newline, then processes command.
     */
    void update();

    /**
     * @brief Check if currently in config mode
     *
     * @return true if in interactive config mode
     */
    bool isInConfigMode() const { return m_configMode; }

    /**
     * @brief Enter interactive configuration mode
     */
    void enterConfigMode();

    /**
     * @brief Exit interactive configuration mode
     */
    void exitConfigMode();

    /**
     * @brief Print help information
     */
    void printHelp();

    /**
     * @brief Print current configuration
     */
    void printConfig();

private:
    ConfigManager& m_configManager;
    SensorManager& m_sensorManager;
    bool m_initialized;
    bool m_configMode;

    // Input buffer
    static const size_t BUFFER_SIZE = 256;
    char m_inputBuffer[BUFFER_SIZE];
    size_t m_inputPos;

    // Command parsing
    static const size_t MAX_ARGS = 4;
    char* m_args[MAX_ARGS];
    size_t m_argCount;

    /**
     * @brief Process a complete command line
     *
     * @param line Command line to process
     */
    void processLine(const char* line);

    /**
     * @brief Parse command line into arguments
     *
     * @param line Command line to parse
     * @return Number of arguments parsed
     */
    size_t parseArgs(char* line);

    /**
     * @brief Execute a command
     *
     * @param cmd Command name
     * @param argc Argument count
     * @param argv Argument values
     */
    void executeCommand(const char* cmd, size_t argc, char** argv);

    // Command handlers
    void cmdHelp();
    void cmdConfig();
    void cmdSet(size_t argc, char** argv);
    void cmdGet(size_t argc, char** argv);
    void cmdSave();
    void cmdLoad();
    void cmdReset();
    void cmdReboot();
    void cmdStatus();
    void cmdVersion();
    void cmdWifi(size_t argc, char** argv);
    void cmdSensor(size_t argc, char** argv);

    /**
     * @brief Print a configuration value
     *
     * @param key Configuration key
     */
    void printValue(const char* key);

    /**
     * @brief Set a configuration value
     *
     * @param key Configuration key
     * @param value Value to set
     * @return true if value was set successfully
     */
    bool setValue(const char* key, const char* value);

    /**
     * @brief Print command prompt
     */
    void printPrompt();
};

#endif // STEPAWARE_SERIAL_CONFIG_H
