#include "serial_config.h"
#include "sensor_types.h"
#include <cstring>

SerialConfigUI::SerialConfigUI(ConfigManager& configManager)
    : m_configManager(configManager)
    , m_initialized(false)
    , m_configMode(false)
    , m_inputPos(0)
    , m_argCount(0)
{
    memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
    memset(m_args, 0, sizeof(m_args));
}

bool SerialConfigUI::begin() {
    if (m_initialized) {
        return true;
    }

    m_initialized = true;
    DEBUG_PRINTLN("[SerialConfigUI] Initialized");
    return true;
}

void SerialConfigUI::update() {
    if (!m_initialized) {
        return;
    }

    while (Serial.available()) {
        char c = Serial.read();

        // Handle backspace
        if (c == '\b' || c == 127) {
            if (m_inputPos > 0) {
                m_inputPos--;
                if (m_configMode) {
                    Serial.print("\b \b");  // Erase character on terminal
                }
            }
            continue;
        }

        // Handle enter/newline
        if (c == '\n' || c == '\r') {
            if (m_inputPos > 0) {
                m_inputBuffer[m_inputPos] = '\0';
                if (m_configMode) {
                    Serial.println();  // New line after input
                }
                processLine(m_inputBuffer);
                m_inputPos = 0;
                memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
            }
            if (m_configMode) {
                printPrompt();
            }
            continue;
        }

        // Add character to buffer
        if (m_inputPos < BUFFER_SIZE - 1) {
            m_inputBuffer[m_inputPos++] = c;
            if (m_configMode) {
                Serial.print(c);  // Echo character
            }
        }
    }
}

void SerialConfigUI::enterConfigMode() {
    m_configMode = true;
    Serial.println("\n========================================");
    Serial.println("StepAware Configuration Mode");
    Serial.println("========================================");
    Serial.println("Type 'help' for available commands");
    Serial.println("Type 'exit' to leave config mode");
    Serial.println();
    printPrompt();
}

void SerialConfigUI::exitConfigMode() {
    m_configMode = false;
    Serial.println("\n[Config] Exiting configuration mode");
    Serial.println("========================================\n");
}

void SerialConfigUI::processLine(const char* line) {
    // Skip empty lines
    if (strlen(line) == 0) {
        return;
    }

    // Copy line to working buffer for parsing
    char workBuffer[BUFFER_SIZE];
    strlcpy(workBuffer, line, sizeof(workBuffer));

    // Parse arguments
    m_argCount = parseArgs(workBuffer);

    if (m_argCount == 0) {
        return;
    }

    // Execute command
    executeCommand(m_args[0], m_argCount - 1, &m_args[1]);
}

size_t SerialConfigUI::parseArgs(char* line) {
    size_t count = 0;
    char* token = strtok(line, " \t");

    while (token != nullptr && count < MAX_ARGS) {
        m_args[count++] = token;
        token = strtok(nullptr, " \t");
    }

    return count;
}

void SerialConfigUI::executeCommand(const char* cmd, size_t argc, char** argv) {
    // Convert command to lowercase for comparison
    char cmdLower[32];
    size_t len = strlen(cmd);
    if (len >= sizeof(cmdLower)) len = sizeof(cmdLower) - 1;
    for (size_t i = 0; i < len; i++) {
        cmdLower[i] = tolower(cmd[i]);
    }
    cmdLower[len] = '\0';

    // Command dispatch
    if (strcmp(cmdLower, "help") == 0 || strcmp(cmdLower, "?") == 0) {
        cmdHelp();
    }
    else if (strcmp(cmdLower, "config") == 0) {
        cmdConfig();
    }
    else if (strcmp(cmdLower, "set") == 0) {
        cmdSet(argc, argv);
    }
    else if (strcmp(cmdLower, "get") == 0) {
        cmdGet(argc, argv);
    }
    else if (strcmp(cmdLower, "save") == 0) {
        cmdSave();
    }
    else if (strcmp(cmdLower, "load") == 0) {
        cmdLoad();
    }
    else if (strcmp(cmdLower, "reset") == 0) {
        cmdReset();
    }
    else if (strcmp(cmdLower, "reboot") == 0) {
        cmdReboot();
    }
    else if (strcmp(cmdLower, "status") == 0) {
        cmdStatus();
    }
    else if (strcmp(cmdLower, "version") == 0) {
        cmdVersion();
    }
    else if (strcmp(cmdLower, "wifi") == 0) {
        cmdWifi(argc, argv);
    }
    else if (strcmp(cmdLower, "sensor") == 0) {
        cmdSensor(argc, argv);
    }
    else if (strcmp(cmdLower, "exit") == 0 || strcmp(cmdLower, "quit") == 0) {
        exitConfigMode();
    }
    else {
        Serial.printf("Unknown command: %s\n", cmd);
        Serial.println("Type 'help' for available commands");
    }
}

void SerialConfigUI::printPrompt() {
    Serial.print("config> ");
}

void SerialConfigUI::printHelp() {
    cmdHelp();
}

void SerialConfigUI::printConfig() {
    cmdConfig();
}

// ============================================================================
// Command Handlers
// ============================================================================

void SerialConfigUI::cmdHelp() {
    Serial.println("\n========================================");
    Serial.println("Available Commands");
    Serial.println("========================================");
    Serial.println();
    Serial.println("General:");
    Serial.println("  help              Show this help");
    Serial.println("  status            Show system status");
    Serial.println("  version           Show firmware version");
    Serial.println("  config            Show all configuration");
    Serial.println("  exit              Exit config mode");
    Serial.println();
    Serial.println("Configuration:");
    Serial.println("  get <key>         Get a config value");
    Serial.println("  set <key> <value> Set a config value");
    Serial.println("  save              Save config to flash");
    Serial.println("  load              Reload config from flash");
    Serial.println("  reset             Reset to factory defaults");
    Serial.println("  reboot            Reboot the device");
    Serial.println();
    Serial.println("WiFi:");
    Serial.println("  wifi status       Show WiFi status");
    Serial.println("  wifi ssid <name>  Set WiFi SSID");
    Serial.println("  wifi pass <pass>  Set WiFi password");
    Serial.println("  wifi enable       Enable WiFi");
    Serial.println("  wifi disable      Disable WiFi");
    Serial.println();
    Serial.println("Sensor:");
    Serial.println("  sensor info       Show sensor information");
    Serial.println("  sensor threshold <mm>  Set detection threshold");
    Serial.println();
    Serial.println("Configuration Keys:");
    Serial.println("  motion.duration   Warning duration (ms)");
    Serial.println("  motion.warmup     PIR warmup time (ms)");
    Serial.println("  led.brightness    Full brightness (0-255)");
    Serial.println("  led.dim           Dim brightness (0-255)");
    Serial.println("  led.blink.fast    Fast blink interval (ms)");
    Serial.println("  led.blink.slow    Slow blink interval (ms)");
    Serial.println("  button.debounce   Debounce time (ms)");
    Serial.println("  button.longpress  Long press time (ms)");
    Serial.println("  sensor.min        Min distance (cm)");
    Serial.println("  sensor.max        Max distance (cm)");
    Serial.println("  sensor.direction  Direction detect (0/1)");
    Serial.println("  sensor.samples    Rapid sample count (2-20)");
    Serial.println("  sensor.interval   Rapid sample interval (ms)");
    Serial.println("  device.name       Device name");
    Serial.println("  device.mode       Default mode (0-2)");
    Serial.println("  power.saving      Power saving (0/1)");
    Serial.println("  log.level         Log level (0-4)");
    Serial.println();
}

void SerialConfigUI::cmdConfig() {
    m_configManager.print();
}

void SerialConfigUI::cmdSet(size_t argc, char** argv) {
    if (argc < 2) {
        Serial.println("Usage: set <key> <value>");
        Serial.println("Example: set led.brightness 200");
        return;
    }

    const char* key = argv[0];
    const char* value = argv[1];

    if (setValue(key, value)) {
        Serial.printf("Set %s = %s\n", key, value);
        Serial.println("Note: Use 'save' to persist changes");
    } else {
        Serial.printf("Failed to set %s\n", key);
    }
}

void SerialConfigUI::cmdGet(size_t argc, char** argv) {
    if (argc < 1) {
        Serial.println("Usage: get <key>");
        Serial.println("Example: get led.brightness");
        return;
    }

    printValue(argv[0]);
}

void SerialConfigUI::cmdSave() {
    Serial.println("Saving configuration...");
    if (m_configManager.save()) {
        Serial.println("Configuration saved successfully");
    } else {
        Serial.printf("Failed to save: %s\n", m_configManager.getLastError());
    }
}

void SerialConfigUI::cmdLoad() {
    Serial.println("Loading configuration...");
    if (m_configManager.load()) {
        Serial.println("Configuration loaded successfully");
    } else {
        Serial.printf("Failed to load: %s\n", m_configManager.getLastError());
    }
}

void SerialConfigUI::cmdReset() {
    Serial.println("Resetting to factory defaults...");
    if (m_configManager.reset(true)) {
        Serial.println("Configuration reset to defaults");
    } else {
        Serial.printf("Failed to reset: %s\n", m_configManager.getLastError());
    }
}

void SerialConfigUI::cmdReboot() {
    Serial.println("Rebooting device in 2 seconds...");
    delay(2000);
    ESP.restart();
}

void SerialConfigUI::cmdStatus() {
    Serial.println("\n========================================");
    Serial.println("System Status");
    Serial.println("========================================");
    Serial.printf("Firmware: %s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("Chip Revision: %u\n", ESP.getChipRevision());
    Serial.printf("Flash Size: %u bytes\n", ESP.getFlashChipSize());
    Serial.println("========================================\n");
}

void SerialConfigUI::cmdVersion() {
    Serial.printf("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
}

void SerialConfigUI::cmdWifi(size_t argc, char** argv) {
    if (argc < 1) {
        Serial.println("Usage: wifi <command> [value]");
        Serial.println("Commands: status, ssid, pass, enable, disable");
        return;
    }

    const char* subcmd = argv[0];
    const ConfigManager::Config& cfg = m_configManager.getConfig();

    if (strcmp(subcmd, "status") == 0) {
        Serial.println("\nWiFi Status:");
        Serial.printf("  Enabled: %s\n", cfg.wifiEnabled ? "YES" : "NO");
        Serial.printf("  SSID: %s\n", cfg.wifiSSID[0] ? cfg.wifiSSID : "(not set)");
        Serial.printf("  Password: %s\n", cfg.wifiPassword[0] ? "********" : "(not set)");
    }
    else if (strcmp(subcmd, "ssid") == 0) {
        if (argc < 2) {
            Serial.printf("Current SSID: %s\n", cfg.wifiSSID[0] ? cfg.wifiSSID : "(not set)");
        } else {
            ConfigManager::Config newCfg = cfg;
            strlcpy(newCfg.wifiSSID, argv[1], sizeof(newCfg.wifiSSID));
            m_configManager.setConfig(newCfg);
            Serial.printf("WiFi SSID set to: %s\n", argv[1]);
        }
    }
    else if (strcmp(subcmd, "pass") == 0 || strcmp(subcmd, "password") == 0) {
        if (argc < 2) {
            Serial.printf("Password: %s\n", cfg.wifiPassword[0] ? "********" : "(not set)");
        } else {
            ConfigManager::Config newCfg = cfg;
            strlcpy(newCfg.wifiPassword, argv[1], sizeof(newCfg.wifiPassword));
            m_configManager.setConfig(newCfg);
            Serial.println("WiFi password updated");
        }
    }
    else if (strcmp(subcmd, "enable") == 0) {
        ConfigManager::Config newCfg = cfg;
        newCfg.wifiEnabled = true;
        m_configManager.setConfig(newCfg);
        Serial.println("WiFi enabled");
    }
    else if (strcmp(subcmd, "disable") == 0) {
        ConfigManager::Config newCfg = cfg;
        newCfg.wifiEnabled = false;
        m_configManager.setConfig(newCfg);
        Serial.println("WiFi disabled");
    }
    else {
        Serial.printf("Unknown wifi command: %s\n", subcmd);
    }
}

void SerialConfigUI::cmdSensor(size_t argc, char** argv) {
    if (argc < 1) {
        Serial.println("Usage: sensor <command> [value]");
        Serial.println("Commands: info, threshold");
        return;
    }

    const char* subcmd = argv[0];

    if (strcmp(subcmd, "info") == 0) {
        Serial.println("\nSensor Information:");
        Serial.printf("  Active Type: %s\n", getSensorTypeName(ACTIVE_SENSOR_TYPE));
        Serial.printf("  Supported: PIR, Ultrasonic\n");
#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ULTRASONIC
        Serial.printf("  Threshold: %u mm\n", ULTRASONIC_THRESHOLD_MM);
        Serial.printf("  Interval: %u ms\n", ULTRASONIC_INTERVAL_MS);
#endif
    }
    else if (strcmp(subcmd, "threshold") == 0) {
        if (argc < 2) {
#if ACTIVE_SENSOR_TYPE == SENSOR_TYPE_ULTRASONIC
            Serial.printf("Current threshold: %u mm\n", ULTRASONIC_THRESHOLD_MM);
#else
            Serial.println("Threshold not applicable for PIR sensor");
#endif
        } else {
            Serial.println("Note: Sensor threshold requires recompilation.");
            Serial.println("Edit config.h ULTRASONIC_THRESHOLD_MM and rebuild.");
        }
    }
    else {
        Serial.printf("Unknown sensor command: %s\n", subcmd);
    }
}

// ============================================================================
// Value Get/Set Helpers
// ============================================================================

void SerialConfigUI::printValue(const char* key) {
    const ConfigManager::Config& cfg = m_configManager.getConfig();

    if (strcmp(key, "motion.duration") == 0) {
        Serial.printf("%s = %u\n", key, cfg.motionWarningDuration);
    }
    else if (strcmp(key, "motion.warmup") == 0) {
        Serial.printf("%s = %u\n", key, cfg.pirWarmupTime);
    }
    else if (strcmp(key, "led.brightness") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBrightnessFull);
    }
    else if (strcmp(key, "led.dim") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBrightnessDim);
    }
    else if (strcmp(key, "led.medium") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBrightnessMedium);
    }
    else if (strcmp(key, "led.blink.fast") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBlinkFastMs);
    }
    else if (strcmp(key, "led.blink.slow") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBlinkSlowMs);
    }
    else if (strcmp(key, "led.blink.warning") == 0) {
        Serial.printf("%s = %u\n", key, cfg.ledBlinkWarningMs);
    }
    else if (strcmp(key, "button.debounce") == 0) {
        Serial.printf("%s = %u\n", key, cfg.buttonDebounceMs);
    }
    else if (strcmp(key, "button.longpress") == 0) {
        Serial.printf("%s = %u\n", key, cfg.buttonLongPressMs);
    }
    else if (strcmp(key, "device.name") == 0) {
        Serial.printf("%s = %s\n", key, cfg.deviceName);
    }
    else if (strcmp(key, "device.mode") == 0) {
        Serial.printf("%s = %u\n", key, cfg.defaultMode);
    }
    else if (strcmp(key, "power.saving") == 0) {
        Serial.printf("%s = %s\n", key, cfg.powerSavingEnabled ? "1" : "0");
    }
    else if (strcmp(key, "power.sleep") == 0) {
        Serial.printf("%s = %u\n", key, cfg.deepSleepAfterMs);
    }
    else if (strcmp(key, "log.level") == 0) {
        Serial.printf("%s = %u\n", key, cfg.logLevel);
    }
    else if (strcmp(key, "wifi.ssid") == 0) {
        Serial.printf("%s = %s\n", key, cfg.wifiSSID[0] ? cfg.wifiSSID : "(not set)");
    }
    else if (strcmp(key, "wifi.enabled") == 0) {
        Serial.printf("%s = %s\n", key, cfg.wifiEnabled ? "1" : "0");
    }
    else if (strcmp(key, "sensor.min") == 0) {
        Serial.printf("%s = %u\n", key, cfg.sensorMinDistance);
    }
    else if (strcmp(key, "sensor.max") == 0) {
        Serial.printf("%s = %u\n", key, cfg.sensorMaxDistance);
    }
    else if (strcmp(key, "sensor.direction") == 0) {
        Serial.printf("%s = %s\n", key, cfg.sensorDirectionEnabled ? "1" : "0");
    }
    else if (strcmp(key, "sensor.samples") == 0) {
        Serial.printf("%s = %u\n", key, cfg.sensorRapidSampleCount);
    }
    else if (strcmp(key, "sensor.interval") == 0) {
        Serial.printf("%s = %u\n", key, cfg.sensorRapidSampleMs);
    }
    else {
        Serial.printf("Unknown key: %s\n", key);
    }
}

bool SerialConfigUI::setValue(const char* key, const char* value) {
    ConfigManager::Config cfg = m_configManager.getConfig();
    bool changed = false;

    if (strcmp(key, "motion.duration") == 0) {
        cfg.motionWarningDuration = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "motion.warmup") == 0) {
        cfg.pirWarmupTime = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.brightness") == 0) {
        cfg.ledBrightnessFull = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.dim") == 0) {
        cfg.ledBrightnessDim = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.medium") == 0) {
        cfg.ledBrightnessMedium = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.blink.fast") == 0) {
        cfg.ledBlinkFastMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.blink.slow") == 0) {
        cfg.ledBlinkSlowMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "led.blink.warning") == 0) {
        cfg.ledBlinkWarningMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "button.debounce") == 0) {
        cfg.buttonDebounceMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "button.longpress") == 0) {
        cfg.buttonLongPressMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "device.name") == 0) {
        strlcpy(cfg.deviceName, value, sizeof(cfg.deviceName));
        changed = true;
    }
    else if (strcmp(key, "device.mode") == 0) {
        cfg.defaultMode = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "power.saving") == 0) {
        cfg.powerSavingEnabled = (atoi(value) != 0);
        changed = true;
    }
    else if (strcmp(key, "power.sleep") == 0) {
        cfg.deepSleepAfterMs = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "log.level") == 0) {
        cfg.logLevel = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "wifi.ssid") == 0) {
        strlcpy(cfg.wifiSSID, value, sizeof(cfg.wifiSSID));
        changed = true;
    }
    else if (strcmp(key, "wifi.password") == 0 || strcmp(key, "wifi.pass") == 0) {
        strlcpy(cfg.wifiPassword, value, sizeof(cfg.wifiPassword));
        changed = true;
    }
    else if (strcmp(key, "wifi.enabled") == 0) {
        cfg.wifiEnabled = (atoi(value) != 0);
        changed = true;
    }
    else if (strcmp(key, "sensor.min") == 0) {
        cfg.sensorMinDistance = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "sensor.max") == 0) {
        cfg.sensorMaxDistance = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "sensor.direction") == 0) {
        cfg.sensorDirectionEnabled = (atoi(value) != 0);
        changed = true;
    }
    else if (strcmp(key, "sensor.samples") == 0) {
        cfg.sensorRapidSampleCount = atoi(value);
        changed = true;
    }
    else if (strcmp(key, "sensor.interval") == 0) {
        cfg.sensorRapidSampleMs = atoi(value);
        changed = true;
    }
    else {
        Serial.printf("Unknown key: %s\n", key);
        return false;
    }

    if (changed) {
        if (!m_configManager.setConfig(cfg)) {
            Serial.printf("Validation failed: %s\n", m_configManager.getLastError());
            return false;
        }
    }

    return changed;
}
