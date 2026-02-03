#include "serial_config.h"
#include "sensor_types.h"
#include "wifi_manager.h"
#include <cstring>
#if !MOCK_HARDWARE
#include <LittleFS.h>
#endif

// External WiFi manager reference (defined in main.cpp)
extern WiFiManager wifiManager;

SerialConfigUI::SerialConfigUI(ConfigManager& configManager, SensorManager& sensorManager)
    : m_configManager(configManager)
    , m_sensorManager(sensorManager)
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
    else if (strcmp(cmdLower, "format") == 0) {
        cmdFormat();
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
    Serial.println("Configuration (use 'set' then 'save' to persist):");
    Serial.println("  get <key>         Get a config value");
    Serial.println("  set <key> <value> Set a config value (in memory)");
    Serial.println("  save              Save all settings to flash");
    Serial.println("  load              Reload settings from flash");
    Serial.println("  reset             Reset to factory defaults");
    Serial.println("  format            Format LittleFS (erases all data)");
    Serial.println("  reboot            Reboot the device");
    Serial.println();
    Serial.println("WiFi (auto-saves to flash):");
    Serial.println("  wifi status       Show WiFi connection status");
    Serial.println("  wifi scan         Scan for available networks");
    Serial.println("  wifi ssid <name> [pass]  Set network and connect");
    Serial.println("  wifi pass <pass>  Change password and reconnect");
    Serial.println("  wifi connect      Reconnect to configured network");
    Serial.println("  wifi disconnect   Disconnect from WiFi");
    Serial.println("  wifi enable       Enable WiFi");
    Serial.println("  wifi disable      Disable WiFi");
    Serial.println();
    Serial.println("Sensor:");
    Serial.println("  sensor info       Show sensor information");
    Serial.println("  sensor list            List all configured sensors");
    Serial.println("  sensor <slot> status   Show detailed sensor status");
    Serial.println("  sensor <slot> ...      Configure sensor (see 'sensor' for details)");
    Serial.println();
    Serial.println("Configuration Keys (for 'get'/'set' commands):");
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
    Serial.println("Note: WiFi commands auto-save. Other settings need 'save'.");
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

void SerialConfigUI::cmdFormat() {
#if !MOCK_HARDWARE
    Serial.println("\n========================================");
    Serial.println("FORMAT LITTLEFS FILESYSTEM");
    Serial.println("========================================");
    Serial.println("WARNING: This will erase ALL stored data:");
    Serial.println("  - Configuration files");
    Serial.println("  - Log files");
    Serial.println("  - User uploaded content");
    Serial.println("\nType 'yes' to confirm, or anything else to cancel:");

    // Wait for user input
    unsigned long timeout = millis() + 30000;  // 30 second timeout
    String response = "";

    while (millis() < timeout) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (response.length() > 0) break;
            } else {
                response += c;
            }
        }
        delay(10);
    }

    response.trim();
    response.toLowerCase();

    if (response != "yes") {
        Serial.println("Format cancelled");
        return;
    }

    Serial.println("\nFormatting LittleFS (this may take 30-60 seconds)...");

    // Unmount first
    LittleFS.end();

    // Format
    if (LittleFS.format()) {
        Serial.println("✓ Format complete");

        // Remount
        Serial.println("Mounting filesystem...");
        if (LittleFS.begin(false)) {
            Serial.println("✓ Filesystem mounted successfully");
            Serial.println("\nYou should now:");
            Serial.println("  1. Use 'reset' to restore factory defaults");
            Serial.println("  2. Use 'save' to write configuration");
            Serial.println("  3. Use 'reboot' to restart device");
        } else {
            Serial.println("✗ Failed to mount after format");
            Serial.println("Device may need a reboot");
        }
    } else {
        Serial.println("✗ Format failed!");
        Serial.println("Try rebooting the device and format again");
    }
#else
    Serial.println("Format command not available in mock mode");
#endif
}

void SerialConfigUI::cmdStatus() {
    Serial.println("\n========================================");
    Serial.println("System Status");
    Serial.println("========================================");
    Serial.printf("Firmware: %s v%s (build %s)\n", FIRMWARE_NAME, FIRMWARE_VERSION, BUILD_NUMBER);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("Chip Revision: %u\n", ESP.getChipRevision());
    Serial.printf("Flash Size: %u bytes\n", ESP.getFlashChipSize());
    Serial.println("========================================\n");
}

void SerialConfigUI::cmdVersion() {
    Serial.printf("%s v%s (build %s)\n", FIRMWARE_NAME, FIRMWARE_VERSION, BUILD_NUMBER);
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
        Serial.println("\nWiFi Configuration:");
        Serial.printf("  Enabled: %s\n", cfg.wifiEnabled ? "YES" : "NO");
        Serial.printf("  Configured SSID: %s\n", cfg.wifiSSID[0] ? cfg.wifiSSID : "(not set)");
        Serial.printf("  Password: %s\n", cfg.wifiPassword[0] ? "********" : "(not set)");

        // Show actual connection status from WiFi manager
        const WiFiManager::Status& status = wifiManager.getStatus();
        Serial.println("\nConnection Status:");
        Serial.printf("  State: %s\n", WiFiManager::getStateName(status.state));

        if (status.state == WiFiManager::STATE_CONNECTED) {
            Serial.printf("  Connected to: %s\n", status.ssid);
            Serial.printf("  IP Address: %s\n", status.ip.toString().c_str());
            Serial.printf("  Signal Strength: %d dBm\n", status.rssi);
            // Pre-compute arithmetic to avoid va_list stack corruption
            uint32_t connectionSeconds = status.connectionUptime / 1000;
            Serial.printf("  Connection Uptime: %u seconds\n", connectionSeconds);
            Serial.printf("  Reconnect Count: %u\n", status.reconnectCount);
        } else if (status.state == WiFiManager::STATE_AP_MODE) {
            Serial.printf("  AP SSID: %s\n", status.apSSID);
            Serial.printf("  AP IP: %s\n", status.ip.toString().c_str());
            Serial.println("  Connect to this AP to configure WiFi");
        } else if (status.state == WiFiManager::STATE_CONNECTING) {
            Serial.println("  Currently connecting...");
        } else if (status.state == WiFiManager::STATE_DISCONNECTED) {
            Serial.printf("  Failure Count: %u\n", status.failureCount);
        } else if (status.state == WiFiManager::STATE_FAILED) {
            Serial.printf("  Connection failed after %u attempts\n", status.failureCount);
        }
    }
    else if (strcmp(subcmd, "ssid") == 0) {
        if (argc < 2) {
            Serial.printf("Current SSID: %s\n", cfg.wifiSSID[0] ? cfg.wifiSSID : "(not set)");
            Serial.println("Usage: wifi ssid <network_name> [password]");
        } else {
            ConfigManager::Config newCfg = cfg;
            strlcpy(newCfg.wifiSSID, argv[1], sizeof(newCfg.wifiSSID));

            // Check if password was also provided
            if (argc >= 3) {
                strlcpy(newCfg.wifiPassword, argv[2], sizeof(newCfg.wifiPassword));
                Serial.printf("WiFi SSID set to: %s\n", argv[1]);
                Serial.println("WiFi password updated");
            } else {
                // Prompt for password
                Serial.printf("WiFi SSID set to: %s\n", argv[1]);
                Serial.print("Enter password (or press Enter for open network): ");

                // Read password from serial (blocking)
                char password[64] = {0};
                size_t pos = 0;
                unsigned long startTime = millis();
                const unsigned long timeout = 30000;  // 30 second timeout

                while (pos < sizeof(password) - 1 && (millis() - startTime) < timeout) {
                    if (Serial.available()) {
                        char c = Serial.read();
                        if (c == '\n' || c == '\r') {
                            Serial.println();  // New line
                            break;
                        } else if (c == '\b' || c == 127) {
                            if (pos > 0) {
                                pos--;
                                Serial.print("\b \b");
                            }
                        } else {
                            password[pos++] = c;
                            Serial.print('*');  // Mask password
                        }
                    }
                    delay(1);
                }
                password[pos] = '\0';
                strlcpy(newCfg.wifiPassword, password, sizeof(newCfg.wifiPassword));

                if (pos > 0) {
                    Serial.println("WiFi password set");
                } else {
                    Serial.println("No password (open network)");
                }
            }

            // Enable WiFi, save, and connect
            newCfg.wifiEnabled = true;
            m_configManager.setConfig(newCfg);
            m_configManager.save();
            Serial.println("Configuration saved");

            // Update WiFiManager and connect
            wifiManager.setCredentials(newCfg.wifiSSID, newCfg.wifiPassword);
            wifiManager.setEnabled(true);
            Serial.println("Connecting to WiFi...");
        }
    }
    else if (strcmp(subcmd, "pass") == 0 || strcmp(subcmd, "password") == 0) {
        if (argc < 2) {
            Serial.printf("Password: %s\n", cfg.wifiPassword[0] ? "********" : "(not set)");
        } else {
            ConfigManager::Config newCfg = cfg;
            strlcpy(newCfg.wifiPassword, argv[1], sizeof(newCfg.wifiPassword));
            m_configManager.setConfig(newCfg);
            m_configManager.save();  // Auto-save WiFi settings
            Serial.println("WiFi password updated (saved)");

            // If WiFi is enabled, reconnect with new password
            if (newCfg.wifiEnabled && strlen(newCfg.wifiSSID) > 0) {
                wifiManager.setCredentials(newCfg.wifiSSID, newCfg.wifiPassword);
                Serial.println("Reconnecting with new password...");
                wifiManager.reconnect();
            }
        }
    }
    else if (strcmp(subcmd, "enable") == 0) {
        ConfigManager::Config newCfg = cfg;
        newCfg.wifiEnabled = true;
        m_configManager.setConfig(newCfg);
        m_configManager.save();  // Auto-save WiFi settings
        Serial.println("WiFi enabled (saved)");

        // Update WiFiManager with new credentials and enable
        wifiManager.setCredentials(newCfg.wifiSSID, newCfg.wifiPassword);
        wifiManager.setEnabled(true);
    }
    else if (strcmp(subcmd, "disable") == 0) {
        ConfigManager::Config newCfg = cfg;
        newCfg.wifiEnabled = false;
        m_configManager.setConfig(newCfg);
        m_configManager.save();  // Auto-save WiFi settings
        Serial.println("WiFi disabled (saved)");

        // Disable WiFiManager immediately
        wifiManager.setEnabled(false);
    }
    else if (strcmp(subcmd, "connect") == 0) {
        // Update credentials from config before connecting
        wifiManager.setCredentials(cfg.wifiSSID, cfg.wifiPassword);
        wifiManager.setEnabled(true);  // Ensure enabled
        Serial.println("Triggering WiFi connection...");
        wifiManager.reconnect();
    }
    else if (strcmp(subcmd, "disconnect") == 0) {
        Serial.println("Disconnecting WiFi...");
        wifiManager.disconnect();
    }
    else if (strcmp(subcmd, "scan") == 0) {
        Serial.println("Scanning for WiFi networks...");
        String networks[10];
        int count = wifiManager.scanNetworks(networks, 10);
        if (count > 0) {
            Serial.printf("Found %d networks:\n", count);
            for (int i = 0; i < count; i++) {
                Serial.printf("  %d. %s\n", i + 1, networks[i].c_str());
            }
        } else {
            Serial.println("No networks found");
        }
    }
    else {
        Serial.printf("Unknown wifi command: %s\n", subcmd);
        Serial.println("Available: status, ssid, pass, enable, disable, connect, disconnect, scan");
    }
}

void SerialConfigUI::cmdSensor(size_t argc, char** argv) {
    if (argc < 1) {
        Serial.println("Usage: sensor <slot|command> [subcommand] [value]");
        Serial.println("Commands:");
        Serial.println("  sensor list                      - List all sensors");
        Serial.println("  sensor <slot> status             - Show sensor status");
        Serial.println("  sensor <slot> threshold <mm>     - Set warning trigger distance");
        Serial.println("  sensor <slot> maxrange <mm>      - Set max detection distance");
        Serial.println("  sensor <slot> direction <0|1>    - Enable/disable direction");
        Serial.println("  sensor <slot> dirmode <0|1|2>    - 0=approach, 1=recede, 2=both");
        Serial.println("  sensor <slot> samples <count>    - Set rapid sample count (2-20)");
        Serial.println("  sensor <slot> interval <ms>      - Set sample interval (50-1000ms)");
        return;
    }

    const char* subcmd = argv[0];

    // List all sensors
    if (strcmp(subcmd, "list") == 0) {
        Serial.println("\n=== Configured Sensors ===");
        m_sensorManager.printStatus();
        Serial.printf("Fusion Mode: %s\n",
                     m_sensorManager.getFusionMode() == FUSION_MODE_ANY ? "ANY" :
                     m_sensorManager.getFusionMode() == FUSION_MODE_ALL ? "ALL" : "TRIGGER_MEASURE");
        Serial.printf("Active Count: %u\n", m_sensorManager.getActiveSensorCount());
        return;
    }

    // Slot-specific commands
    uint8_t slot = atoi(subcmd);
    if (slot > 3) {
        Serial.printf("Invalid slot: %s (must be 0-3)\n", subcmd);
        return;
    }

    HAL_MotionSensor* sensor = m_sensorManager.getSensor(slot);
    if (!sensor) {
        Serial.printf("No sensor in slot %u\n", slot);
        return;
    }

    // Get configuration for this sensor
    ConfigManager::Config cfg = m_configManager.getConfig();
    ConfigManager::SensorSlotConfig& sensorCfg = cfg.sensors[slot];

    if (argc < 2) {
        Serial.printf("Missing subcommand for slot %u\n", slot);
        return;
    }

    const char* action = argv[1];

    // Status command
    if (strcmp(action, "status") == 0) {
        const SensorCapabilities& caps = sensor->getCapabilities();
        Serial.printf("\n=== Sensor Slot %u ===\n", slot);
        Serial.printf("Name: %s\n", sensorCfg.name);
        Serial.printf("Type: %s\n", caps.sensorTypeName);
        Serial.printf("Enabled: %s\n", sensorCfg.enabled ? "YES" : "NO");
        Serial.printf("Primary: %s\n", sensorCfg.isPrimary ? "YES" : "NO");
        Serial.printf("Ready: %s\n", sensor->isReady() ? "YES" : "NO");

        if (caps.supportsDistanceMeasurement) {
            Serial.printf("Current Distance: %u mm\n", sensor->getDistance());
            Serial.printf("Max Detection Range: %u mm\n", sensorCfg.maxDetectionDistance);
            Serial.printf("Warning Trigger At: %u mm\n", sensorCfg.detectionThreshold);
            Serial.printf("Direction Detection: %s\n", sensorCfg.enableDirectionDetection ? "Enabled" : "Disabled");
            if (sensorCfg.enableDirectionDetection) {
                const char* triggerMode = (sensorCfg.directionTriggerMode == 0) ? "Approaching" :
                                         (sensorCfg.directionTriggerMode == 1) ? "Receding" : "Both";
                Serial.printf("Trigger Mode: %s\n", triggerMode);
                Serial.printf("Rapid Samples: %u @ %u ms\n",
                             sensorCfg.sampleWindowSize, sensorCfg.sampleRateMs);
                const char* dirName = "Unknown";
                switch (sensor->getDirection()) {
                    case DIRECTION_STATIONARY: dirName = "Stationary"; break;
                    case DIRECTION_APPROACHING: dirName = "Approaching"; break;
                    case DIRECTION_RECEDING: dirName = "Receding"; break;
                }
                Serial.printf("Current Direction: %s\n", dirName);
            }
        }
        Serial.println();
        return;
    }

    // Configuration commands (require value parameter)
    if (argc < 3) {
        Serial.println("Missing value parameter");
        return;
    }

    const char* valueStr = argv[2];
    uint32_t value = atoi(valueStr);

    if (strcmp(action, "threshold") == 0) {
        if (value < 10) {
            Serial.println("Error: Threshold must be >= 10mm");
            return;
        }
        sensorCfg.detectionThreshold = value;
        sensor->setDetectionThreshold(value);
        Serial.printf("Slot %u warning trigger distance set to %u mm\n", slot, value);
    }
    else if (strcmp(action, "maxrange") == 0) {
        if (value < 100) {
            Serial.println("Error: Max range must be >= 100mm");
            return;
        }
        sensorCfg.maxDetectionDistance = value;
        Serial.printf("Slot %u max detection range set to %u mm\n", slot, value);
    }
    else if (strcmp(action, "direction") == 0) {
        bool enable = (value != 0);
        sensorCfg.enableDirectionDetection = enable;
        sensor->setDirectionDetection(enable);
        Serial.printf("Slot %u direction detection %s\n", slot, enable ? "enabled" : "disabled");
    }
    else if (strcmp(action, "dirmode") == 0) {
        if (value > 2) {
            Serial.println("Error: Direction mode must be 0 (approach), 1 (recede), or 2 (both)");
            return;
        }
        sensorCfg.directionTriggerMode = value;
        const char* modeName = (value == 0) ? "approaching" : (value == 1) ? "receding" : "both";
        Serial.printf("Slot %u direction trigger mode set to %s\n", slot, modeName);
    }
    else if (strcmp(action, "samples") == 0) {
        if (value < 2 || value > 20) {
            Serial.println("Error: Sample count must be 2-20");
            return;
        }
        sensorCfg.sampleWindowSize = value;
        sensor->setSampleWindowSize(value);
        Serial.printf("Slot %u rapid sample count set to %u\n", slot, value);
    }
    else if (strcmp(action, "interval") == 0) {
        if (value < 50 || value > 1000) {
            Serial.println("Error: Interval must be 50-1000ms");
            return;
        }
        sensorCfg.sampleRateMs = value;
        // Note: Sample rate is set via setMeasurementInterval(), not implemented here yet
        Serial.printf("Slot %u sample interval set to %u ms\n", slot, value);
    }
    else {
        Serial.printf("Unknown action: %s\n", action);
        return;
    }

    // Save updated configuration
    if (!m_configManager.setConfig(cfg)) {
        Serial.println("Error: Failed to update configuration");
        return;
    }

    Serial.println("Configuration updated (use 'save' to persist)");
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
        Serial.printf("%s = %u\n", key, cfg.powerSavingMode);
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
        uint8_t mode = atoi(value);
        cfg.powerSavingMode = (mode <= 2) ? mode : 0;
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
