#include "config_manager.h"
#include "logger.h"
#include "debug_logger.h"
#include <ArduinoJson.h>

// Use LittleFS instead of FILESYSTEM for better ESP32-C3 support
// LittleFS is more reliable and has better wear leveling
#include <LittleFS.h>
#define FILESYSTEM LittleFS

const char* ConfigManager::CONFIG_FILE_PATH = "/config.json";

ConfigManager::ConfigManager()
    : m_initialized(false)
{
    memset(&m_config, 0, sizeof(Config));
    memset(m_lastError, 0, sizeof(m_lastError));
}

ConfigManager::~ConfigManager() {
    // FILESYSTEM cleanup handled by framework
}

bool ConfigManager::begin() {
    if (m_initialized) {
        return true;
    }

    DEBUG_LOG_CONFIG("ConfigManager: Initializing...");

    // Mount FILESYSTEM
    if (!FILESYSTEM.begin(true)) {  // true = format on fail
        setError("Failed to mount FILESYSTEM");
        DEBUG_LOG_CONFIG("ConfigManager: Failed to mount FILESYSTEM");
        return false;
    }

    DEBUG_LOG_CONFIG("ConfigManager: FILESYSTEM mounted");

    // Load defaults first
    loadDefaults();

    // Try to load config from file
    if (FILESYSTEM.exists(CONFIG_FILE_PATH)) {
        DEBUG_LOG_CONFIG("ConfigManager: Config file found, loading...");
        if (!load()) {
            DEBUG_LOG_CONFIG("ConfigManager: Failed to load config, using defaults");
            // Continue with defaults
        }
    } else {
        DEBUG_LOG_CONFIG("ConfigManager: No config file found, using defaults");
        // Save defaults
        save();
    }

    m_initialized = true;
    DEBUG_LOG_CONFIG("ConfigManager: Initialization complete");

    return true;
}

bool ConfigManager::load() {
    File file = FILESYSTEM.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        setError("Failed to open config file");
        return false;
    }

    // Read file into buffer
    size_t size = file.size();
    if (size == 0 || size > 4096) {
        setError("Invalid config file size");
        file.close();
        return false;
    }

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        setError("Out of memory");
        file.close();
        return false;
    }

    file.readBytes(buffer, size);
    buffer[size] = '\0';
    file.close();

    // Parse JSON
    bool result = fromJSON(buffer);
    free(buffer);

    if (result) {
        DEBUG_LOG_CONFIG("ConfigManager: Config loaded successfully");
    } else {
        DEBUG_LOG_CONFIG("ConfigManager: Failed to parse config");
    }

    return result;
}

bool ConfigManager::save() {
    DEBUG_LOG_CONFIG("Saving config to %s", CONFIG_FILE_PATH);
    DEBUG_LOG_CONFIG("=== Saving Configuration ===");

    // Log key config values being saved (VERBOSE level)
    DEBUG_LOG_CONFIG("Device: %s, Mode: %d, WiFi: %s", m_config.deviceName, m_config.defaultMode,
                m_config.wifiEnabled ? "enabled" : "disabled");
    DEBUG_LOG_CONFIG("Motion: duration=%ums, PIR warmup=%ums", m_config.motionWarningDuration, m_config.pirWarmupTime);
    DEBUG_LOG_CONFIG("Logging: level=%u, serial=%s, file=%s", m_config.logLevel,
                m_config.serialLoggingEnabled ? "enabled" : "disabled",
                m_config.fileLoggingEnabled ? "enabled" : "disabled");

    // Count and log active sensors being saved
    int sensorCount = 0;
    for (int i = 0; i < 4; i++) {
        if (m_config.sensors[i].active) {
            sensorCount++;
            DEBUG_LOG_CONFIG("Sensor[%d]: %s, type=%d, enabled=%s, directionMode=%u, threshold=%umm",
                        i, m_config.sensors[i].name, m_config.sensors[i].type,
                        m_config.sensors[i].enabled ? "yes" : "no",
                        m_config.sensors[i].directionTriggerMode,
                        m_config.sensors[i].detectionThreshold);
        }
    }
    DEBUG_LOG_CONFIG("Total sensors: %d, Fusion mode: %d", sensorCount, m_config.fusionMode);

    // Count and log active displays being saved
    int displayCount = 0;
    for (int i = 0; i < 2; i++) {
        if (m_config.displays[i].active) {
            displayCount++;
            DEBUG_LOG_CONFIG("Display[%d]: %s, type=%d, enabled=%s, brightness=%u",
                        i, m_config.displays[i].name, m_config.displays[i].type,
                        m_config.displays[i].enabled ? "yes" : "no",
                        m_config.displays[i].brightness);
        }
    }
    DEBUG_LOG_CONFIG("Total displays: %d", displayCount);

    char buffer[2048];
    if (!toJSON(buffer, sizeof(buffer))) {
        setError("Failed to serialize config to JSON");
        DEBUG_LOG_CONFIG("Config save FAILED: Failed to serialize to JSON");
        DEBUG_LOG_CONFIG("=== Save Failed: JSON serialization error ===");
        return false;
    }

    File file = FILESYSTEM.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        setError("Failed to open config file for writing");
        DEBUG_LOG_CONFIG("Config save FAILED: Cannot open file for writing");
        DEBUG_LOG_CONFIG("=== Save Failed: Cannot open file ===");
        return false;
    }

    size_t written = file.print(buffer);
    file.close();

    if (written == 0) {
        setError("Failed to write config to file");
        DEBUG_LOG_CONFIG("Config save FAILED: Write returned 0 bytes");
        DEBUG_LOG_CONFIG("=== Save Failed: Write error ===");
        return false;
    }

    DEBUG_LOG_CONFIG("ConfigManager: Config saved successfully (%u bytes)", written);
    DEBUG_LOG_CONFIG("Config saved successfully (%u bytes)", written);
    DEBUG_LOG_CONFIG("=== Configuration Saved Successfully (%u bytes) ===", written);
    return true;
}

bool ConfigManager::reset(bool save) {
    DEBUG_LOG_CONFIG("ConfigManager: Resetting to factory defaults");
    loadDefaults();

    if (save) {
        return this->save();
    }

    return true;
}

bool ConfigManager::validate() {
    if (!validateParameters()) {
        return false;
    }

    DEBUG_LOG_CONFIG("ConfigManager: Configuration validated");
    return true;
}

bool ConfigManager::validateAndCorrect() {
    bool hadErrors = false;
    uint8_t sensorErrorCount = 0;
    uint8_t displayErrorCount = 0;

    DEBUG_LOG_CONFIG("=== Configuration Validation Check ===");

    // Validate and correct sensor configurations
    for (uint8_t i = 0; i < 4; i++) {
        SensorSlotConfig& sensor = m_config.sensors[i];

        if (!sensor.active) {
            continue;  // Skip inactive slots
        }

        bool sensorHadError = false;

        // Validate sensor type
        if (sensor.type < SENSOR_TYPE_PIR || sensor.type > SENSOR_TYPE_ULTRASONIC_GROVE) {
            DEBUG_LOG_CONFIG("Sensor[%u]: INVALID type %u (valid: 0=PIR, 3=Ultrasonic, 4=Grove), DISABLING slot", i, sensor.type);
            sensor.active = false;
            sensorErrorCount++;
            continue;
        }

        // Validate detection threshold (100mm to 5000mm reasonable range)
        if (sensor.detectionThreshold < 100 || sensor.detectionThreshold > 5000) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED detectionThreshold: %u mm → 1100 mm (out of range 100-5000)",
                     i, sensor.detectionThreshold);
            sensor.detectionThreshold = 1100;
            sensorHadError = true;
        }

        // Validate max detection distance (200mm to 5000mm)
        if (sensor.maxDetectionDistance < 200 || sensor.maxDetectionDistance > 5000) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED maxDetectionDistance: %u mm → 3000 mm (out of range 200-5000)",
                     i, sensor.maxDetectionDistance);
            sensor.maxDetectionDistance = 3000;
            sensorHadError = true;
        }

        // Validate debounce (10ms to 1000ms) - only for sensors that use it (not PIR)
        if (sensor.type != SENSOR_TYPE_PIR) {
            if (sensor.debounceMs < 10 || sensor.debounceMs > 1000) {
                DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED debounceMs: %u ms → 75 ms (out of range 10-1000)",
                         i, sensor.debounceMs);
                sensor.debounceMs = 75;
                sensorHadError = true;
            }
        }

        // Validate warmup time (0 to 120000ms = 2 minutes)
        if (sensor.warmupMs > 120000) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED warmupMs: %u ms → 60000 ms (max 120000)",
                     i, sensor.warmupMs);
            sensor.warmupMs = 60000;
            sensorHadError = true;
        }

        // Validate direction trigger mode (0=approaching, 1=receding, 2=both)
        if (sensor.directionTriggerMode > 2) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED directionTriggerMode: %u → 0 (must be 0=approaching, 1=receding, or 2=both)",
                     i, sensor.directionTriggerMode);
            sensor.directionTriggerMode = 0;
            sensorHadError = true;
        }

        // Validate direction sensitivity (0=auto, or 10mm to 1000mm)
        if (sensor.directionSensitivity > 1000) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED directionSensitivity: %u mm → 0 mm (auto) (max 1000)",
                     i, sensor.directionSensitivity);
            sensor.directionSensitivity = 0;
            sensorHadError = true;
        }

        // Validate sample window size (3 to 20)
        if (sensor.sampleWindowSize < 3 || sensor.sampleWindowSize > 20) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED sampleWindowSize: %u → 3 (range 3-20)",
                     i, sensor.sampleWindowSize);
            sensor.sampleWindowSize = 3;
            sensorHadError = true;
        }

        // Validate sample rate (50ms to 1000ms)
        if (sensor.sampleRateMs < 50 || sensor.sampleRateMs > 1000) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED sampleRateMs: %u ms → 75 ms (range 50-1000)",
                     i, sensor.sampleRateMs);
            sensor.sampleRateMs = 75;
            sensorHadError = true;
        }

        // Validate GPIO pins (ESP32-C3 has GPIO 0-21)
        if (sensor.primaryPin > 21) {
            DEBUG_LOG_CONFIG("Sensor[%u]: INVALID primaryPin %u (max 21), DISABLING slot",
                     i, sensor.primaryPin);
            sensor.active = false;
            sensorErrorCount++;
            continue;
        }

        // Validate secondary pin (0 means disabled, or valid GPIO 0-21)
        // Note: Condition simplified - if secondaryPin > 21, it's already != 0
        if (sensor.secondaryPin > 21) {
            DEBUG_LOG_CONFIG("Sensor[%u]: CORRECTED secondaryPin: %u → 0 (max 21)",
                     i, sensor.secondaryPin);
            sensor.secondaryPin = 0;
            sensorHadError = true;
        }

        if (sensorHadError) {
            sensorErrorCount++;
        }
    }

    // Validate and correct display configurations
    for (uint8_t i = 0; i < 2; i++) {
        DisplaySlotConfig& display = m_config.displays[i];

        if (!display.active) {
            continue;  // Skip inactive slots
        }

        bool displayHadError = false;

        // Validate display type
        if (display.type < DISPLAY_TYPE_SINGLE_LED || display.type > DISPLAY_TYPE_MATRIX_8X8) {
            DEBUG_LOG_CONFIG("Display[%u]: INVALID type %u (corrupted), DISABLING slot", i, display.type);
            display.active = false;
            displayErrorCount++;
            continue;
        }

        // Validate I2C address (0x00 to 0x7F, typically 0x70-0x77 for HT16K33)
        if (display.i2cAddress > 0x7F) {
            DEBUG_LOG_CONFIG("Display[%u]: CORRECTED i2cAddress: 0x%02X → 0x70 (max 0x7F)",
                     i, display.i2cAddress);
            display.i2cAddress = 0x70;
            displayHadError = true;
        }

        // Validate I2C pins (GPIO 0-21)
        if (display.sdaPin > 21) {
            DEBUG_LOG_CONFIG("Display[%u]: CORRECTED sdaPin: %u → 7 (max 21)",
                     i, display.sdaPin);
            display.sdaPin = 7;
            displayHadError = true;
        }

        if (display.sclPin > 21) {
            DEBUG_LOG_CONFIG("Display[%u]: CORRECTED sclPin: %u → 10 (max 21)",
                     i, display.sclPin);
            display.sclPin = 10;
            displayHadError = true;
        }

        // Validate brightness (0-15 for matrix, 0-255 for LED)
        if (display.type == DISPLAY_TYPE_MATRIX_8X8) {
            if (display.brightness > 15) {
                DEBUG_LOG_CONFIG("Display[%u]: CORRECTED brightness: %u → 15 (8x8 matrix max 15)",
                         i, display.brightness);
                display.brightness = 15;
                displayHadError = true;
            }
        } else {
            if (display.brightness > 255) {
                DEBUG_LOG_CONFIG("Display[%u]: CORRECTED brightness: %u → 255 (max 255)",
                         i, display.brightness);
                display.brightness = 255;
                displayHadError = true;
            }
        }

        // Validate rotation (0-3)
        if (display.rotation > 3) {
            DEBUG_LOG_CONFIG("Display[%u]: CORRECTED rotation: %u → 0 (max 3)",
                     i, display.rotation);
            display.rotation = 0;
            displayHadError = true;
        }

        if (displayHadError) {
            displayErrorCount++;
        }
    }

    // Validate fusion mode (0-2: ANY, ALL, PRIMARY_ONLY)
    if (m_config.fusionMode > 2) {
        DEBUG_LOG_CONFIG("CORRECTED fusionMode: %u → 0 (must be 0=ANY, 1=ALL, or 2=PRIMARY_ONLY)", m_config.fusionMode);
        m_config.fusionMode = 0;
        hadErrors = true;
    }

    // Validate direction detector configuration
    if (m_config.directionDetector.enabled) {
        // Sensor slots must be valid and different
        if (m_config.directionDetector.farSensorSlot >= 4) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector.farSensorSlot: %u → 1 (must be 0-3)",
                           m_config.directionDetector.farSensorSlot);
            m_config.directionDetector.farSensorSlot = 1;
            hadErrors = true;
        }
        if (m_config.directionDetector.nearSensorSlot >= 4) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector.nearSensorSlot: %u → 0 (must be 0-3)",
                           m_config.directionDetector.nearSensorSlot);
            m_config.directionDetector.nearSensorSlot = 0;
            hadErrors = true;
        }
        if (m_config.directionDetector.farSensorSlot == m_config.directionDetector.nearSensorSlot) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector: far and near slots must be different (both were %u)",
                           m_config.directionDetector.farSensorSlot);
            m_config.directionDetector.nearSensorSlot = 0;
            m_config.directionDetector.farSensorSlot = 1;
            hadErrors = true;
        }

        // Timing bounds
        if (m_config.directionDetector.confirmationWindowMs < 1000 || m_config.directionDetector.confirmationWindowMs > 30000) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector.confirmationWindowMs: %u → 5000 (must be 1000-30000)",
                           m_config.directionDetector.confirmationWindowMs);
            m_config.directionDetector.confirmationWindowMs = 5000;
            hadErrors = true;
        }
        if (m_config.directionDetector.simultaneousThresholdMs > 2000) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector.simultaneousThresholdMs: %u → 150 (must be ≤2000)",
                           m_config.directionDetector.simultaneousThresholdMs);
            m_config.directionDetector.simultaneousThresholdMs = 150;
            hadErrors = true;
        }
        if (m_config.directionDetector.patternTimeoutMs < 5000 || m_config.directionDetector.patternTimeoutMs > 60000) {
            DEBUG_LOG_CONFIG("CORRECTED directionDetector.patternTimeoutMs: %u → 10000 (must be 5000-60000)",
                           m_config.directionDetector.patternTimeoutMs);
            m_config.directionDetector.patternTimeoutMs = 10000;
            hadErrors = true;
        }
    }

    // Report results
    if (sensorErrorCount == 0 && displayErrorCount == 0 && !hadErrors) {
        DEBUG_LOG_CONFIG("Configuration validation: PASSED (no errors detected)");
        return true;
    } else {
        DEBUG_LOG_CONFIG("Configuration validation: FAILED - corrected %u sensor errors, %u display errors",
                 sensorErrorCount, displayErrorCount);
        hadErrors = true;

        // Save corrected configuration
        if (save()) {
            DEBUG_LOG_CONFIG("Corrected configuration saved to filesystem");
        } else {
            DEBUG_LOG_CONFIG("Failed to save corrected configuration");
        }

        return false;
    }
}

void ConfigManager::autoConfigureDirectionDetector() {
    // Scan all PIR sensors for distance zone settings
    int8_t nearSlot = -1;
    int8_t farSlot = -1;

    for (uint8_t i = 0; i < 4; i++) {
        if (m_config.sensors[i].active && m_config.sensors[i].enabled &&
            m_config.sensors[i].type == SENSOR_TYPE_PIR) {

            if (m_config.sensors[i].distanceZone == 1) {  // Near
                if (nearSlot == -1) {
                    nearSlot = i;
                }
            } else if (m_config.sensors[i].distanceZone == 2) {  // Far
                if (farSlot == -1) {
                    farSlot = i;
                }
            }
        }
    }

    // Configure direction detector based on findings
    if (nearSlot != -1 && farSlot != -1) {
        // One near and one far sensor found - enable direction detection
        m_config.directionDetector.enabled = true;
        m_config.directionDetector.nearSensorSlot = nearSlot;
        m_config.directionDetector.farSensorSlot = farSlot;
        m_config.directionDetector.triggerOnApproaching = true;

        // Set defaults if not already configured
        if (m_config.directionDetector.confirmationWindowMs == 0) {
            m_config.directionDetector.confirmationWindowMs = 5000;
        }
        if (m_config.directionDetector.simultaneousThresholdMs == 0) {
            m_config.directionDetector.simultaneousThresholdMs = 150;
        }
        if (m_config.directionDetector.patternTimeoutMs == 0) {
            m_config.directionDetector.patternTimeoutMs = 10000;
        }

        DEBUG_LOG_CONFIG("Auto-configured direction detector: Near=Slot%d, Far=Slot%d", nearSlot, farSlot);
    } else {
        // No valid near+far combination - disable direction detection
        // (allows normal multi-sensor operation)
        m_config.directionDetector.enabled = false;
        DEBUG_LOG_CONFIG("Direction detector disabled (no near+far sensor pair found)");
    }
}

const ConfigManager::Config& ConfigManager::getConfig() const {
    return m_config;
}

bool ConfigManager::setConfig(const Config& config) {
    m_config = config;

    if (!validate()) {
        // Restore defaults on invalid config
        loadDefaults();
        return false;
    }

    return true;
}

bool ConfigManager::toJSON(char* buffer, size_t bufferSize) {
    // Use DynamicJsonDocument to avoid stack overflow with large configs
    DynamicJsonDocument doc(8192);  // Allocates on heap instead of stack

    // Motion Detection
    JsonObject motion = doc.createNestedObject("motion");
    motion["warningDuration"] = m_config.motionWarningDuration;
    motion["pirWarmup"] = m_config.pirWarmupTime;

    // Button
    JsonObject button = doc.createNestedObject("button");
    button["debounceMs"] = m_config.buttonDebounceMs;
    button["longPressMs"] = m_config.buttonLongPressMs;

    // LED
    JsonObject led = doc.createNestedObject("led");
    led["brightnessFull"] = m_config.ledBrightnessFull;
    led["brightnessMedium"] = m_config.ledBrightnessMedium;
    led["brightnessDim"] = m_config.ledBrightnessDim;
    led["blinkFastMs"] = m_config.ledBlinkFastMs;
    led["blinkSlowMs"] = m_config.ledBlinkSlowMs;
    led["blinkWarningMs"] = m_config.ledBlinkWarningMs;

    // Battery
    JsonObject battery = doc.createNestedObject("battery");
    battery["voltageFull"] = m_config.batteryVoltageFull;
    battery["voltageLow"] = m_config.batteryVoltageLow;
    battery["voltageCritical"] = m_config.batteryVoltageCritical;

    // Light Sensor
    JsonObject light = doc.createNestedObject("light");
    light["thresholdDark"] = m_config.lightThresholdDark;
    light["thresholdBright"] = m_config.lightThresholdBright;

    // Distance Sensor
    JsonObject sensor = doc.createNestedObject("sensor");
    sensor["minDistance"] = m_config.sensorMinDistance;
    sensor["maxDistance"] = m_config.sensorMaxDistance;
    sensor["directionEnabled"] = m_config.sensorDirectionEnabled;
    sensor["rapidSampleCount"] = m_config.sensorRapidSampleCount;
    sensor["rapidSampleMs"] = m_config.sensorRapidSampleMs;

    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = m_config.wifiSSID;
    wifi["password"] = m_config.wifiPassword;
    wifi["enabled"] = m_config.wifiEnabled;

    // Device
    JsonObject device = doc.createNestedObject("device");
    device["name"] = m_config.deviceName;
    device["defaultMode"] = m_config.defaultMode;
    device["rememberMode"] = m_config.rememberLastMode;

    // Power
    JsonObject power = doc.createNestedObject("power");
    power["savingEnabled"] = m_config.powerSavingEnabled;
    power["deepSleepAfterMs"] = m_config.deepSleepAfterMs;

    // Logging
    JsonObject logging = doc.createNestedObject("logging");
    logging["level"] = m_config.logLevel;
    logging["serialEnabled"] = m_config.serialLoggingEnabled;
    logging["fileEnabled"] = m_config.fileLoggingEnabled;

    // Multi-Sensor Configuration
    JsonArray sensorsArray = doc.createNestedArray("sensors");
    for (int i = 0; i < 4; i++) {
        if (m_config.sensors[i].active) {
            JsonObject sensorObj = sensorsArray.createNestedObject();
            sensorObj["slot"] = i;
            sensorObj["name"] = m_config.sensors[i].name;
            sensorObj["type"] = m_config.sensors[i].type;
            sensorObj["primaryPin"] = m_config.sensors[i].primaryPin;
            sensorObj["secondaryPin"] = m_config.sensors[i].secondaryPin;
            sensorObj["enabled"] = m_config.sensors[i].enabled;
            sensorObj["isPrimary"] = m_config.sensors[i].isPrimary;
            sensorObj["detectionThreshold"] = m_config.sensors[i].detectionThreshold;
            sensorObj["maxDetectionDistance"] = m_config.sensors[i].maxDetectionDistance;
            sensorObj["debounceMs"] = m_config.sensors[i].debounceMs;
            sensorObj["warmupMs"] = m_config.sensors[i].warmupMs;
            sensorObj["enableDirectionDetection"] = m_config.sensors[i].enableDirectionDetection;
            sensorObj["directionTriggerMode"] = m_config.sensors[i].directionTriggerMode;
            sensorObj["directionSensitivity"] = m_config.sensors[i].directionSensitivity;
            sensorObj["sampleWindowSize"] = m_config.sensors[i].sampleWindowSize;
            sensorObj["sampleRateMs"] = m_config.sensors[i].sampleRateMs;
            sensorObj["distanceZone"] = m_config.sensors[i].distanceZone;
        }
    }

    doc["fusionMode"] = m_config.fusionMode;

    // Multi-Display Configuration
    JsonArray displaysArray = doc.createNestedArray("displays");
    for (int i = 0; i < 2; i++) {
        if (m_config.displays[i].active) {
            JsonObject displayObj = displaysArray.createNestedObject();
            displayObj["slot"] = i;
            displayObj["name"] = m_config.displays[i].name;
            displayObj["type"] = m_config.displays[i].type;
            displayObj["i2cAddress"] = m_config.displays[i].i2cAddress;
            displayObj["sdaPin"] = m_config.displays[i].sdaPin;
            displayObj["sclPin"] = m_config.displays[i].sclPin;
            displayObj["enabled"] = m_config.displays[i].enabled;
            displayObj["brightness"] = m_config.displays[i].brightness;
            displayObj["rotation"] = m_config.displays[i].rotation;
            displayObj["useForStatus"] = m_config.displays[i].useForStatus;
        }
    }

    doc["primaryDisplaySlot"] = m_config.primaryDisplaySlot;

    // Direction Detection Configuration
    JsonObject dirObj = doc.createNestedObject("directionDetector");
    dirObj["enabled"] = m_config.directionDetector.enabled;
    dirObj["farSensorSlot"] = m_config.directionDetector.farSensorSlot;
    dirObj["nearSensorSlot"] = m_config.directionDetector.nearSensorSlot;
    dirObj["confirmationWindowMs"] = m_config.directionDetector.confirmationWindowMs;
    dirObj["simultaneousThresholdMs"] = m_config.directionDetector.simultaneousThresholdMs;
    dirObj["patternTimeoutMs"] = m_config.directionDetector.patternTimeoutMs;
    dirObj["triggerOnApproaching"] = m_config.directionDetector.triggerOnApproaching;

    // Metadata
    JsonObject meta = doc.createNestedObject("metadata");
    meta["version"] = m_config.version;
    meta["lastModified"] = m_config.lastModified;

    // Serialize to buffer
    size_t size = serializeJson(doc, buffer, bufferSize);

    if (size == 0 || size >= bufferSize) {
        setError("JSON buffer too small");
        return false;
    }

    return true;
}

bool ConfigManager::fromJSON(const char* json) {
    DEBUG_LOG_CONFIG("Loading config from JSON (%u bytes)", strlen(json));

    // Use DynamicJsonDocument to avoid stack overflow with large configs
    DynamicJsonDocument doc(8192);  // Allocates on heap instead of stack
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        setError(error.c_str());
        DEBUG_LOG_CONFIG("Config load FAILED: JSON parse error: %s", error.c_str());
        DEBUG_LOG_CONFIG("JSON input (%u bytes): %s", strlen(json), json);
        return false;
    }

    // Motion Detection
    if (doc.containsKey("motion")) {
        m_config.motionWarningDuration = doc["motion"]["warningDuration"] | MOTION_WARNING_DURATION_MS;
        m_config.pirWarmupTime = doc["motion"]["pirWarmup"] | PIR_WARMUP_TIME_MS;
    }

    // Button
    if (doc.containsKey("button")) {
        m_config.buttonDebounceMs = doc["button"]["debounceMs"] | BUTTON_DEBOUNCE_MS;
        m_config.buttonLongPressMs = doc["button"]["longPressMs"] | 1000;
    }

    // LED
    if (doc.containsKey("led")) {
        m_config.ledBrightnessFull = doc["led"]["brightnessFull"] | LED_BRIGHTNESS_FULL;
        m_config.ledBrightnessMedium = doc["led"]["brightnessMedium"] | LED_BRIGHTNESS_MEDIUM;
        m_config.ledBrightnessDim = doc["led"]["brightnessDim"] | LED_BRIGHTNESS_DIM;
        m_config.ledBlinkFastMs = doc["led"]["blinkFastMs"] | LED_BLINK_FAST_MS;
        m_config.ledBlinkSlowMs = doc["led"]["blinkSlowMs"] | LED_BLINK_SLOW_MS;
        m_config.ledBlinkWarningMs = doc["led"]["blinkWarningMs"] | LED_BLINK_WARNING_MS;
    }

    // Battery
    if (doc.containsKey("battery")) {
        m_config.batteryVoltageFull = doc["battery"]["voltageFull"] | BATTERY_VOLTAGE_FULL;
        m_config.batteryVoltageLow = doc["battery"]["voltageLow"] | BATTERY_VOLTAGE_LOW;
        m_config.batteryVoltageCritical = doc["battery"]["voltageCritical"] | BATTERY_VOLTAGE_CRITICAL;
    }

    // Light Sensor
    if (doc.containsKey("light")) {
        m_config.lightThresholdDark = doc["light"]["thresholdDark"] | LIGHT_THRESHOLD_DARK;
        m_config.lightThresholdBright = doc["light"]["thresholdBright"] | LIGHT_THRESHOLD_BRIGHT;
    }

    // Distance Sensor
    if (doc.containsKey("sensor")) {
        m_config.sensorMinDistance = doc["sensor"]["minDistance"] | SENSOR_MIN_DISTANCE_CM;
        m_config.sensorMaxDistance = doc["sensor"]["maxDistance"] | SENSOR_MAX_DISTANCE_CM;
        m_config.sensorDirectionEnabled = doc["sensor"]["directionEnabled"] | SENSOR_DIRECTION_ENABLED;
        m_config.sensorRapidSampleCount = doc["sensor"]["rapidSampleCount"] | SENSOR_RAPID_SAMPLE_COUNT;
        m_config.sensorRapidSampleMs = doc["sensor"]["rapidSampleMs"] | SENSOR_RAPID_SAMPLE_MS;
    }

    // WiFi
    if (doc.containsKey("wifi")) {
        strlcpy(m_config.wifiSSID, doc["wifi"]["ssid"] | "", sizeof(m_config.wifiSSID));
        strlcpy(m_config.wifiPassword, doc["wifi"]["password"] | "", sizeof(m_config.wifiPassword));
        m_config.wifiEnabled = doc["wifi"]["enabled"] | false;
    }

    // Device
    if (doc.containsKey("device")) {
        strlcpy(m_config.deviceName, doc["device"]["name"] | "StepAware", sizeof(m_config.deviceName));
        m_config.defaultMode = doc["device"]["defaultMode"] | 2;  // MOTION_DETECT
        m_config.rememberLastMode = doc["device"]["rememberMode"] | false;
    }

    // Power
    if (doc.containsKey("power")) {
        m_config.powerSavingEnabled = doc["power"]["savingEnabled"] | false;
        m_config.deepSleepAfterMs = doc["power"]["deepSleepAfterMs"] | 3600000;  // 1 hour
    }

    // Logging
    if (doc.containsKey("logging")) {
        m_config.logLevel = doc["logging"]["level"] | LOG_LEVEL_INFO;
        m_config.serialLoggingEnabled = doc["logging"]["serialEnabled"] | true;
        m_config.fileLoggingEnabled = doc["logging"]["fileEnabled"] | false;
    }

    // Multi-Sensor Configuration
    if (doc.containsKey("sensors")) {
        // Clear all slots first
        for (int i = 0; i < 4; i++) {
            m_config.sensors[i].active = false;
        }

        JsonArray sensorsArray = doc["sensors"];
        for (JsonObject sensorObj : sensorsArray) {
            int slot = sensorObj["slot"] | -1;
            if (slot >= 0 && slot < 4) {
                m_config.sensors[slot].active = true;
                strlcpy(m_config.sensors[slot].name, sensorObj["name"] | "", sizeof(m_config.sensors[slot].name));
                m_config.sensors[slot].type = (SensorType)(sensorObj["type"] | 0);
                m_config.sensors[slot].primaryPin = sensorObj["primaryPin"] | 0;
                m_config.sensors[slot].secondaryPin = sensorObj["secondaryPin"] | 0;
                m_config.sensors[slot].enabled = sensorObj["enabled"] | true;
                m_config.sensors[slot].isPrimary = sensorObj["isPrimary"] | false;
                m_config.sensors[slot].detectionThreshold = sensorObj["detectionThreshold"] | 1100;  // 1100mm warn threshold
                m_config.sensors[slot].maxDetectionDistance = sensorObj["maxDetectionDistance"] | 3000;  // 3000mm max range
                m_config.sensors[slot].debounceMs = sensorObj["debounceMs"] | 75;  // 75ms for ultrasonic/IR (not used by PIR)
                m_config.sensors[slot].warmupMs = sensorObj["warmupMs"] | 0;
                m_config.sensors[slot].enableDirectionDetection = sensorObj["enableDirectionDetection"] | true;  // Direction enabled by default
                m_config.sensors[slot].directionTriggerMode = sensorObj["directionTriggerMode"] | 0;  // APPROACHING
                m_config.sensors[slot].directionSensitivity = sensorObj["directionSensitivity"] | 0;  // 0 = auto (adaptive threshold)
                m_config.sensors[slot].sampleWindowSize = sensorObj["sampleWindowSize"] | 3;  // 3 samples default
                m_config.sensors[slot].sampleRateMs = sensorObj["sampleRateMs"] | 75;  // 75ms sample interval (adaptive threshold)
                m_config.sensors[slot].distanceZone = sensorObj["distanceZone"] | 0;  // 0 = None (default)
            }
        }
    }

    if (doc.containsKey("fusionMode")) {
        m_config.fusionMode = doc["fusionMode"] | 0;
    }

    // Multi-Display Configuration
    if (doc.containsKey("displays")) {
        // Clear all slots first
        for (int i = 0; i < 2; i++) {
            m_config.displays[i].active = false;
        }

        JsonArray displaysArray = doc["displays"];
        for (JsonObject displayObj : displaysArray) {
            int slot = displayObj["slot"] | -1;
            if (slot >= 0 && slot < 2) {
                m_config.displays[slot].active = true;
                strlcpy(m_config.displays[slot].name, displayObj["name"] | "", sizeof(m_config.displays[slot].name));
                m_config.displays[slot].type = (DisplayType)(displayObj["type"] | 0);
                m_config.displays[slot].i2cAddress = displayObj["i2cAddress"] | 0x70;
                m_config.displays[slot].sdaPin = displayObj["sdaPin"] | I2C_SDA_PIN;
                m_config.displays[slot].sclPin = displayObj["sclPin"] | I2C_SCL_PIN;
                m_config.displays[slot].enabled = displayObj["enabled"] | true;
                m_config.displays[slot].brightness = displayObj["brightness"] | MATRIX_BRIGHTNESS_DEFAULT;
                m_config.displays[slot].rotation = displayObj["rotation"] | MATRIX_ROTATION;
                m_config.displays[slot].useForStatus = displayObj["useForStatus"] | true;
            }
        }
    }

    if (doc.containsKey("primaryDisplaySlot")) {
        m_config.primaryDisplaySlot = doc["primaryDisplaySlot"] | 0;
    }

    // Direction Detection Configuration
    if (doc.containsKey("directionDetector")) {
        JsonObject dirObj = doc["directionDetector"];
        m_config.directionDetector.enabled = dirObj["enabled"] | false;
        m_config.directionDetector.farSensorSlot = dirObj["farSensorSlot"] | 1;
        m_config.directionDetector.nearSensorSlot = dirObj["nearSensorSlot"] | 0;
        m_config.directionDetector.confirmationWindowMs = dirObj["confirmationWindowMs"] | 5000;
        m_config.directionDetector.simultaneousThresholdMs = dirObj["simultaneousThresholdMs"] | 150;
        m_config.directionDetector.patternTimeoutMs = dirObj["patternTimeoutMs"] | 10000;
        m_config.directionDetector.triggerOnApproaching = dirObj["triggerOnApproaching"] | true;
    }

    // Metadata
    if (doc.containsKey("metadata")) {
        strlcpy(m_config.version, doc["metadata"]["version"] | FIRMWARE_VERSION, sizeof(m_config.version));
        m_config.lastModified = doc["metadata"]["lastModified"] | 0;
    }

    bool valid = validate();
    if (valid) {
        DEBUG_LOG_CONFIG("Config loaded successfully");
        DEBUG_LOG_CONFIG("Device: %s, Default mode: %d", m_config.deviceName, m_config.defaultMode);

        // Log detailed config at INFO level for debugging
        DEBUG_LOG_CONFIG("=== Configuration Loaded ===");
        DEBUG_LOG_CONFIG("Device: %s, Mode: %d, WiFi: %s", m_config.deviceName, m_config.defaultMode,
                 m_config.wifiEnabled ? "enabled" : "disabled");
        DEBUG_LOG_CONFIG("Motion: duration=%ums, PIR warmup=%ums", m_config.motionWarningDuration, m_config.pirWarmupTime);
        DEBUG_LOG_CONFIG("Battery: full=%umV, low=%umV, critical=%umV",
                 m_config.batteryVoltageFull, m_config.batteryVoltageLow, m_config.batteryVoltageCritical);
        DEBUG_LOG_CONFIG("Logging: level=%u, serial=%s, file=%s", m_config.logLevel,
                 m_config.serialLoggingEnabled ? "enabled" : "disabled",
                 m_config.fileLoggingEnabled ? "enabled" : "disabled");

        // Count and log active sensors
        int sensorCount = 0;
        for (int i = 0; i < 4; i++) {
            if (m_config.sensors[i].active) {
                sensorCount++;
                DEBUG_LOG_CONFIG("Sensor[%d]: %s, type=%d, enabled=%s, primary=%s", i,
                         m_config.sensors[i].name,
                         m_config.sensors[i].type,
                         m_config.sensors[i].enabled ? "yes" : "no",
                         m_config.sensors[i].isPrimary ? "yes" : "no");
                DEBUG_LOG_CONFIG("  Pins: primary=%u, secondary=%u",
                         m_config.sensors[i].primaryPin, m_config.sensors[i].secondaryPin);
                DEBUG_LOG_CONFIG("  Detection: threshold=%umm, maxDist=%umm, debounce=%ums, warmup=%ums",
                         m_config.sensors[i].detectionThreshold,
                         m_config.sensors[i].maxDetectionDistance,
                         m_config.sensors[i].debounceMs,
                         m_config.sensors[i].warmupMs);
                DEBUG_LOG_CONFIG("  Direction: enabled=%s, triggerMode=%u, sensitivity=%u, window=%u, sampleRate=%ums",
                         m_config.sensors[i].enableDirectionDetection ? "yes" : "no",
                         m_config.sensors[i].directionTriggerMode,
                         m_config.sensors[i].directionSensitivity,
                         m_config.sensors[i].sampleWindowSize,
                         m_config.sensors[i].sampleRateMs);
            }
        }
        DEBUG_LOG_CONFIG("Total sensors: %d, Fusion mode: %d", sensorCount, m_config.fusionMode);

        // Count and log active displays
        int displayCount = 0;
        for (int i = 0; i < 2; i++) {
            if (m_config.displays[i].active) {
                displayCount++;
                DEBUG_LOG_CONFIG("Display[%d]: %s, type=%d, enabled=%s, i2c=0x%02X", i,
                         m_config.displays[i].name,
                         m_config.displays[i].type,
                         m_config.displays[i].enabled ? "yes" : "no",
                         m_config.displays[i].i2cAddress);
                DEBUG_LOG_CONFIG("  I2C pins: SDA=%u, SCL=%u", m_config.displays[i].sdaPin, m_config.displays[i].sclPin);
                DEBUG_LOG_CONFIG("  Settings: brightness=%u, rotation=%u, useForStatus=%s",
                         m_config.displays[i].brightness,
                         m_config.displays[i].rotation,
                         m_config.displays[i].useForStatus ? "yes" : "no");
            }
        }
        DEBUG_LOG_CONFIG("Total displays: %d, Primary display slot: %d", displayCount, m_config.primaryDisplaySlot);
        DEBUG_LOG_CONFIG("=== End Configuration ===");

        DEBUG_LOG_CONFIG("Active sensors: %d, Fusion mode: %d", sensorCount, m_config.fusionMode);
    } else {
        DEBUG_LOG_CONFIG("Config validation FAILED");
    }

    return valid;
}

void ConfigManager::print() {
    Serial.println("\n========================================");
    Serial.println("Configuration");
    Serial.println("========================================");

    Serial.printf("Device Name: %s\n", m_config.deviceName);
    Serial.printf("Version: %s\n", m_config.version);
    Serial.println();

    Serial.println("Motion Detection:");
    Serial.printf("  Warning Duration: %u ms\n", m_config.motionWarningDuration);
    Serial.printf("  PIR Warmup: %u ms\n", m_config.pirWarmupTime);
    Serial.println();

    Serial.println("Button:");
    Serial.printf("  Debounce: %u ms\n", m_config.buttonDebounceMs);
    Serial.printf("  Long Press: %u ms\n", m_config.buttonLongPressMs);
    Serial.println();

    Serial.println("LED:");
    Serial.printf("  Brightness Full: %u\n", m_config.ledBrightnessFull);
    Serial.printf("  Brightness Medium: %u\n", m_config.ledBrightnessMedium);
    Serial.printf("  Brightness Dim: %u\n", m_config.ledBrightnessDim);
    Serial.println();

    Serial.println("Battery:");
    Serial.printf("  Full: %u mV\n", m_config.batteryVoltageFull);
    Serial.printf("  Low: %u mV\n", m_config.batteryVoltageLow);
    Serial.printf("  Critical: %u mV\n", m_config.batteryVoltageCritical);
    Serial.println();

    Serial.println("Distance Sensor:");
    Serial.printf("  Min Distance: %u cm\n", m_config.sensorMinDistance);
    Serial.printf("  Max Distance: %u cm\n", m_config.sensorMaxDistance);
    Serial.printf("  Direction Enabled: %s\n", m_config.sensorDirectionEnabled ? "YES" : "NO");
    Serial.printf("  Rapid Sample Count: %u\n", m_config.sensorRapidSampleCount);
    Serial.printf("  Rapid Sample Interval: %u ms\n", m_config.sensorRapidSampleMs);
    Serial.println();

    Serial.println("WiFi:");
    Serial.printf("  Enabled: %s\n", m_config.wifiEnabled ? "YES" : "NO");
    Serial.printf("  SSID: %s\n", m_config.wifiSSID[0] ? m_config.wifiSSID : "(not set)");
    Serial.println();

    Serial.println("Power:");
    Serial.printf("  Saving Enabled: %s\n", m_config.powerSavingEnabled ? "YES" : "NO");
    Serial.printf("  Deep Sleep After: %u ms\n", m_config.deepSleepAfterMs);
    Serial.println();

    Serial.println("Logging:");
    Serial.printf("  Level: %u\n", m_config.logLevel);
    Serial.printf("  Serial: %s\n", m_config.serialLoggingEnabled ? "YES" : "NO");
    Serial.printf("  File: %s\n", m_config.fileLoggingEnabled ? "YES" : "NO");

    Serial.println("========================================\n");
}

const char* ConfigManager::getLastError() const {
    return m_lastError;
}

void ConfigManager::loadDefaults() {
    DEBUG_LOG_CONFIG("ConfigManager: Loading factory defaults");

    // Motion Detection
    m_config.motionWarningDuration = 5000;  // 5 seconds (Issue #9 requirement)
    m_config.pirWarmupTime = PIR_WARMUP_TIME_MS;

    // Button
    m_config.buttonDebounceMs = BUTTON_DEBOUNCE_MS;
    m_config.buttonLongPressMs = 1000;

    // LED
    m_config.ledBrightnessFull = LED_BRIGHTNESS_FULL;
    m_config.ledBrightnessMedium = LED_BRIGHTNESS_MEDIUM;
    m_config.ledBrightnessDim = LED_BRIGHTNESS_DIM;
    m_config.ledBlinkFastMs = LED_BLINK_FAST_MS;
    m_config.ledBlinkSlowMs = LED_BLINK_SLOW_MS;
    m_config.ledBlinkWarningMs = LED_BLINK_WARNING_MS;

    // Battery
    m_config.batteryVoltageFull = BATTERY_VOLTAGE_FULL;
    m_config.batteryVoltageLow = BATTERY_VOLTAGE_LOW;
    m_config.batteryVoltageCritical = BATTERY_VOLTAGE_CRITICAL;

    // Light Sensor
    m_config.lightThresholdDark = LIGHT_THRESHOLD_DARK;
    m_config.lightThresholdBright = LIGHT_THRESHOLD_BRIGHT;

    // Distance Sensor
    m_config.sensorMinDistance = SENSOR_MIN_DISTANCE_CM;
    m_config.sensorMaxDistance = SENSOR_MAX_DISTANCE_CM;
    m_config.sensorDirectionEnabled = SENSOR_DIRECTION_ENABLED;
    m_config.sensorRapidSampleCount = SENSOR_RAPID_SAMPLE_COUNT;
    m_config.sensorRapidSampleMs = SENSOR_RAPID_SAMPLE_MS;

    // WiFi
    strlcpy(m_config.wifiSSID, "", sizeof(m_config.wifiSSID));
    strlcpy(m_config.wifiPassword, "", sizeof(m_config.wifiPassword));
    m_config.wifiEnabled = false;

    // Device
    strlcpy(m_config.deviceName, "StepAware", sizeof(m_config.deviceName));
    m_config.defaultMode = 2;  // MOTION_DETECT mode
    m_config.rememberLastMode = false;

    // Power
    m_config.powerSavingEnabled = false;
    m_config.deepSleepAfterMs = 3600000;  // 1 hour

    // Logging
    m_config.logLevel = LOG_LEVEL_INFO;
    m_config.serialLoggingEnabled = true;
    m_config.fileLoggingEnabled = false;

    // Multi-Sensor Configuration - Default PIR sensor
    for (int i = 0; i < 4; i++) {
        m_config.sensors[i].active = false;
        m_config.sensors[i].enabled = false;
        strlcpy(m_config.sensors[i].name, "", sizeof(m_config.sensors[i].name));
    }

    // Sensor slot 0: Default PIR sensor
    m_config.sensors[0].active = true;
    m_config.sensors[0].enabled = true;
    strlcpy(m_config.sensors[0].name, "PIR Motion", sizeof(m_config.sensors[0].name));
    m_config.sensors[0].type = SENSOR_TYPE_PIR;
    m_config.sensors[0].primaryPin = PIN_PIR_SENSOR;
    m_config.sensors[0].secondaryPin = 0;
    m_config.sensors[0].isPrimary = true;
    m_config.sensors[0].detectionThreshold = 0;  // N/A for PIR
    m_config.sensors[0].maxDetectionDistance = 0;  // N/A for PIR
    m_config.sensors[0].debounceMs = 0;  // Not used by PIR sensors
    m_config.sensors[0].warmupMs = PIR_WARMUP_TIME_MS;
    m_config.sensors[0].enableDirectionDetection = false;
    m_config.sensors[0].directionTriggerMode = 0;  // Approaching
    m_config.sensors[0].directionSensitivity = 0;  // 0 = auto (adaptive threshold)
    m_config.sensors[0].sampleWindowSize = 3;   // Global default window size
    m_config.sensors[0].sampleRateMs = 75;      // Global default sample interval (adaptive threshold)

    m_config.fusionMode = 0;  // FUSION_MODE_ANY

    // Direction Detection Configuration - Disabled by default
    m_config.directionDetector.enabled = false;
    m_config.directionDetector.farSensorSlot = 1;      // Slot 1 = far PIR
    m_config.directionDetector.nearSensorSlot = 0;     // Slot 0 = near PIR
    m_config.directionDetector.confirmationWindowMs = DIR_CONFIRMATION_WINDOW_MS;        // 5000ms
    m_config.directionDetector.simultaneousThresholdMs = DIR_SIMULTANEOUS_THRESHOLD_MS;  // 150ms
    m_config.directionDetector.patternTimeoutMs = DIR_PATTERN_TIMEOUT_MS;                // 10000ms
    m_config.directionDetector.triggerOnApproaching = true;  // Only trigger on approaching

    // Multi-Display Configuration - No displays by default
    for (int i = 0; i < 2; i++) {
        m_config.displays[i].active = false;
        m_config.displays[i].enabled = false;
        strlcpy(m_config.displays[i].name, "", sizeof(m_config.displays[i].name));
        m_config.displays[i].type = DISPLAY_TYPE_NONE;
        m_config.displays[i].i2cAddress = MATRIX_I2C_ADDRESS;
        m_config.displays[i].sdaPin = I2C_SDA_PIN;
        m_config.displays[i].sclPin = I2C_SCL_PIN;
        m_config.displays[i].brightness = MATRIX_BRIGHTNESS_DEFAULT;
        m_config.displays[i].rotation = MATRIX_ROTATION;
        m_config.displays[i].useForStatus = true;
    }

    m_config.primaryDisplaySlot = 0;

    // Metadata
    strlcpy(m_config.version, FIRMWARE_VERSION, sizeof(m_config.version));
    m_config.lastModified = 0;
}

bool ConfigManager::validateParameters() {
    // Motion Detection
    if (m_config.motionWarningDuration < 1000 || m_config.motionWarningDuration > 300000) {
        setError("Invalid motion warning duration (1-300s)");
        return false;
    }

    // Button
    if (m_config.buttonDebounceMs < 10 || m_config.buttonDebounceMs > 500) {
        setError("Invalid button debounce time (10-500ms)");
        return false;
    }

    if (m_config.buttonLongPressMs < 500 || m_config.buttonLongPressMs > 10000) {
        setError("Invalid long press duration (500-10000ms)");
        return false;
    }

    // LED Brightness (0-255)
    if (m_config.ledBrightnessFull > 255 || m_config.ledBrightnessMedium > 255 || m_config.ledBrightnessDim > 255) {
        setError("Invalid LED brightness (0-255)");
        return false;
    }

    // LED Blink timings
    if (m_config.ledBlinkFastMs < 50 || m_config.ledBlinkFastMs > 5000) {
        setError("Invalid LED blink fast time (50-5000ms)");
        return false;
    }

    // Battery voltages
    if (m_config.batteryVoltageCritical >= m_config.batteryVoltageLow ||
        m_config.batteryVoltageLow >= m_config.batteryVoltageFull) {
        setError("Invalid battery voltage thresholds");
        return false;
    }

    // Log level (0=VERBOSE to 5=NONE)
    if (m_config.logLevel > LOG_LEVEL_NONE) {
        setError("Invalid log level");
        return false;
    }

    // Distance sensor
    if (m_config.sensorMinDistance < 10 || m_config.sensorMinDistance > 500) {
        setError("Invalid sensor min distance (10-500cm)");
        return false;
    }

    if (m_config.sensorMaxDistance < 20 || m_config.sensorMaxDistance > 500) {
        setError("Invalid sensor max distance (20-500cm)");
        return false;
    }

    if (m_config.sensorMinDistance >= m_config.sensorMaxDistance) {
        setError("Sensor min distance must be less than max distance");
        return false;
    }

    if (m_config.sensorRapidSampleCount < 2 || m_config.sensorRapidSampleCount > 20) {
        setError("Invalid rapid sample count (2-20)");
        return false;
    }

    if (m_config.sensorRapidSampleMs < 50 || m_config.sensorRapidSampleMs > 1000) {
        setError("Invalid rapid sample interval (50-1000ms)");
        return false;
    }

    return true;
}

void ConfigManager::setError(const char* error) {
    strlcpy(m_lastError, error, sizeof(m_lastError));
}

// NOTE: setWiFiCredentials() removed as unused (Wave 3b cleanup)
// WiFi credentials are set via getConfig/setConfig pattern in serial_config.cpp
// This function was never called anywhere in the codebase.
