#include "config_manager.h"
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

    DEBUG_PRINTLN("[ConfigManager] Initializing...");

    // Mount FILESYSTEM
    if (!FILESYSTEM.begin(true)) {  // true = format on fail
        setError("Failed to mount FILESYSTEM");
        DEBUG_PRINTLN("[ConfigManager] ERROR: Failed to mount FILESYSTEM");
        return false;
    }

    DEBUG_PRINTLN("[ConfigManager] FILESYSTEM mounted");

    // Load defaults first
    loadDefaults();

    // Try to load config from file
    if (FILESYSTEM.exists(CONFIG_FILE_PATH)) {
        DEBUG_PRINTLN("[ConfigManager] Config file found, loading...");
        if (!load()) {
            DEBUG_PRINTLN("[ConfigManager] WARNING: Failed to load config, using defaults");
            // Continue with defaults
        }
    } else {
        DEBUG_PRINTLN("[ConfigManager] No config file found, using defaults");
        // Save defaults
        save();
    }

    m_initialized = true;
    DEBUG_PRINTLN("[ConfigManager] ✓ Initialization complete");

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
        DEBUG_PRINTLN("[ConfigManager] ✓ Config loaded successfully");
    } else {
        DEBUG_PRINTLN("[ConfigManager] ERROR: Failed to parse config");
    }

    return result;
}

bool ConfigManager::save() {
    char buffer[2048];
    if (!toJSON(buffer, sizeof(buffer))) {
        setError("Failed to serialize config to JSON");
        return false;
    }

    File file = FILESYSTEM.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        setError("Failed to open config file for writing");
        return false;
    }

    size_t written = file.print(buffer);
    file.close();

    if (written == 0) {
        setError("Failed to write config to file");
        return false;
    }

    DEBUG_PRINTLN("[ConfigManager] ✓ Config saved successfully");
    return true;
}

bool ConfigManager::reset(bool save) {
    DEBUG_PRINTLN("[ConfigManager] Resetting to factory defaults");
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

    DEBUG_PRINTLN("[ConfigManager] ✓ Configuration validated");
    return true;
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
    StaticJsonDocument<2048> doc;

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
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        setError(error.c_str());
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

    // Metadata
    if (doc.containsKey("metadata")) {
        strlcpy(m_config.version, doc["metadata"]["version"] | FIRMWARE_VERSION, sizeof(m_config.version));
        m_config.lastModified = doc["metadata"]["lastModified"] | 0;
    }

    return validate();
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
    DEBUG_PRINTLN("[ConfigManager] Loading factory defaults");

    // Motion Detection
    m_config.motionWarningDuration = MOTION_WARNING_DURATION_MS;
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

    // Log level
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

bool ConfigManager::setWiFiCredentials(const char* ssid, const char* password) {
    if (!m_initialized) {
        setError("Config manager not initialized");
        return false;
    }

    // Update WiFi SSID
    if (ssid) {
        strlcpy(m_config.wifiSSID, ssid, sizeof(m_config.wifiSSID));
    } else {
        m_config.wifiSSID[0] = '\0';
    }

    // Update WiFi password
    if (password) {
        strlcpy(m_config.wifiPassword, password, sizeof(m_config.wifiPassword));
    } else {
        m_config.wifiPassword[0] = '\0';
    }

    // Update metadata
    m_config.lastModified = millis();

    // Save to FILESYSTEM
    return save();
}
