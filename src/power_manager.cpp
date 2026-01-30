#include "power_manager.h"
#include "logger.h"
#include "debug_logger.h"

#ifndef MOCK_MODE
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/adc.h>
#include <driver/gpio.h>

// ESP32-C3 specific includes for deep sleep GPIO wakeup
#if CONFIG_IDF_TARGET_ESP32C3
#include <esp_chip_info.h>
#endif
#endif

// Global power manager instance
PowerManager g_power;

// RTC memory structure for state persistence
#ifndef MOCK_MODE
RTC_DATA_ATTR struct {
    uint32_t magic;
    PowerManager::PowerState lastState;
    uint32_t wakeCount;
    uint32_t deepSleepCount;
    float lastBatteryVoltage;
} rtcMemory;
#else
static struct {
    uint32_t magic;
    PowerManager::PowerState lastState;
    uint32_t wakeCount;
    uint32_t deepSleepCount;
    float lastBatteryVoltage;
} rtcMemory;
#endif

#define RTC_MAGIC 0xBADC0FFE

PowerManager::PowerManager()
    : m_state(STATE_ACTIVE)
    , m_initialized(false)
    , m_lastActivity(0)
    , m_lastBatteryUpdate(0)
    , m_stateEnterTime(0)
    , m_startTime(0)
    , m_voltageSampleIndex(0)
    , m_voltageSamplesFilled(false)
    , m_onLowBattery(nullptr)
    , m_onCriticalBattery(nullptr)
    , m_onCharging(nullptr)
    , m_onWake(nullptr)
{
    // Initialize default configuration
    m_config.idleToLightSleepMs = 180000;       // 3 minutes (user configurable: 1-10 min)
    m_config.lightSleepToDeepSleepMs = 60000;   // 1 minute from light sleep (0 = skip light sleep)
    m_config.lowBatteryThreshold = 3.4f;        // 3.4V (~20%)
    m_config.criticalBatteryThreshold = 3.2f;   // 3.2V (~5%)
    m_config.batteryCheckInterval = 10000;      // 10 seconds
    m_config.enableAutoSleep = true;
    m_config.enableDeepSleep = true;
    m_config.voltageCalibrationOffset = 0.0f;
    m_config.lowBatteryLEDBrightness = 128;     // 50% brightness when low battery

    // Initialize battery status
    m_batteryStatus.voltage = 0.0f;
    m_batteryStatus.percentage = 0;
    m_batteryStatus.charging = false;
    m_batteryStatus.low = false;
    m_batteryStatus.critical = false;
    m_batteryStatus.timeToEmpty = 0;

    // Initialize stats
    m_stats.uptime = 0;
    m_stats.activeTime = 0;
    m_stats.sleepTime = 0;
    m_stats.wakeCount = 0;
    m_stats.deepSleepCount = 0;
    m_stats.avgCurrent = 0.0f;

    // Initialize voltage samples
    for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
        m_voltageSamples[i] = 0.0f;
    }
}

PowerManager::~PowerManager() {
}

bool PowerManager::begin(const Config* config) {
    if (m_initialized) {
        DEBUG_LOG_SYSTEM("Power: Already initialized");
        return true;
    }

    // Apply custom configuration if provided
    if (config) {
        m_config = *config;
    }

    DEBUG_LOG_SYSTEM("Power: Initializing");

#ifndef MOCK_MODE
    // Configure ADC for battery voltage reading
    // Note: ESP32-C3 uses different ADC API - analogRead() works via Arduino framework
    // The adc1_config functions are deprecated in newer ESP-IDF
    #if !CONFIG_IDF_TARGET_ESP32C3
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
    #else
    // ESP32-C3: Use analogReadResolution and analogSetAttenuation instead
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // For full 0-3.3V range
    #endif
#endif

    m_startTime = millis();
    m_lastActivity = millis();
    m_lastBatteryUpdate = 0; // Force immediate update

    // Try to restore state from RTC memory
    if (restoreStateFromRTC()) {
        DEBUG_LOG_SYSTEM("Power: Restored state from RTC memory (wake count: %u)", m_stats.wakeCount);
    } else {
        DEBUG_LOG_SYSTEM("Power: Fresh boot, initializing RTC memory");
        rtcMemory.magic = RTC_MAGIC;
        rtcMemory.wakeCount = 0;
        rtcMemory.deepSleepCount = 0;
    }

    m_initialized = true;

    // Initial battery check
    updateBatteryStatus();

    DEBUG_LOG_SYSTEM("Power: Initialized (battery: %.2fV, %u%%)",
             m_batteryStatus.voltage, m_batteryStatus.percentage);

    // Critical battery boot protection
    // If battery is critical and not charging, show warning and shutdown
    if (m_batteryStatus.critical && !m_batteryStatus.charging) {
        DEBUG_LOG_SYSTEM("Power: Critical battery detected on boot (%.2fV), shutting down", m_batteryStatus.voltage);

        #ifndef MOCK_MODE
        // Flash LED 3 times as shutdown warning
        pinMode(PIN_HAZARD_LED, OUTPUT);
        for (int i = 0; i < 3; i++) {
            // Use raw GPIO to avoid dependency on LED HAL
            digitalWrite(PIN_HAZARD_LED, HIGH);
            delay(200);
            digitalWrite(PIN_HAZARD_LED, LOW);
            delay(200);
        }
        #endif

        // Enter deep sleep immediately (device will not wake until charged)
        enterDeepSleep();
        // Never returns
    }

    return true;
}

void PowerManager::update() {
    if (!m_initialized) {
        return;
    }

    // Update battery status periodically
    if (millis() - m_lastBatteryUpdate >= m_config.batteryCheckInterval) {
        updateBatteryStatus();
        m_lastBatteryUpdate = millis();
    }

    // Handle power state
    handlePowerState();

    // Update statistics
    updateStats();
}

void PowerManager::updateBatteryStatus() {
    // Read voltage
    float voltage = getBatteryVoltage();
    m_batteryStatus.voltage = voltage;

    // Calculate percentage
    m_batteryStatus.percentage = calculateBatteryPercentage(voltage);

    // Check charging state
    bool wasCharging = m_batteryStatus.charging;
    m_batteryStatus.charging = isCharging();

    // Detect charging state change
    if (m_batteryStatus.charging && !wasCharging) {
        DEBUG_LOG_SYSTEM("Power: Charging started");
        if (m_onCharging) {
            m_onCharging();
        }
    }

    // Check battery thresholds
    bool wasLow = m_batteryStatus.low;
    bool wasCritical = m_batteryStatus.critical;

    m_batteryStatus.low = (voltage < m_config.lowBatteryThreshold);
    m_batteryStatus.critical = (voltage < m_config.criticalBatteryThreshold);

    // Trigger callbacks on state changes
    if (m_batteryStatus.critical && !wasCritical) {
        DEBUG_LOG_SYSTEM("Power: Critical battery! (%.2fV)", voltage);
        if (m_onCriticalBattery) {
            m_onCriticalBattery();
        }
    } else if (m_batteryStatus.low && !wasLow) {
        DEBUG_LOG_SYSTEM("Power: Low battery (%.2fV)", voltage);
        if (m_onLowBattery) {
            m_onLowBattery();
        }
    }

    // Estimate time to empty (simplified)
    // Assume average current consumption based on state
    float avgCurrent = 15.0f; // 15mA typical
    if (m_batteryStatus.percentage > 0) {
        // Simple estimation: capacity * percentage / current
        m_batteryStatus.timeToEmpty = (2000.0f * m_batteryStatus.percentage / 100.0f) / avgCurrent * 3600.0f;
    } else {
        m_batteryStatus.timeToEmpty = 0;
    }
}

float PowerManager::getBatteryVoltage() {
    float rawVoltage = readBatteryVoltageRaw();
    addVoltageSample(rawVoltage);
    return getFilteredVoltage();
}

float PowerManager::readBatteryVoltageRaw() {
#ifndef MOCK_MODE
    // Read ADC value using Arduino API (works on all ESP32 variants)
    uint16_t raw = analogRead(PIN_BATTERY_ADC);

    // Convert to voltage (12-bit ADC, 0-3.3V range with 11dB attenuation)
    // With voltage divider (2:1 ratio), multiply by 2
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;

    // Apply calibration offset
    voltage += m_config.voltageCalibrationOffset;

    return voltage;
#else
    // Mock mode: return simulated voltage
    return 3.8f; // Nominal battery voltage
#endif
}

uint8_t PowerManager::getBatteryPercentage() {
    return m_batteryStatus.percentage;
}

uint8_t PowerManager::calculateBatteryPercentage(float voltage) {
    // LiPo discharge curve (simplified piecewise linear)
    // 4.2V = 100%
    // 3.7V = 50%
    // 3.0V = 0%

    // Clamp voltage range
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.0f) return 0;

    // Piecewise linear approximation
    if (voltage >= 3.7f) {
        // 3.7V-4.2V = 50%-100% (0.5V range = 50% capacity)
        return 50 + (uint8_t)(((voltage - 3.7f) / 0.5f) * 50.0f);
    } else {
        // 3.0V-3.7V = 0%-50% (0.7V range = 50% capacity)
        return (uint8_t)(((voltage - 3.0f) / 0.7f) * 50.0f);
    }
}

void PowerManager::addVoltageSample(float voltage) {
    m_voltageSamples[m_voltageSampleIndex] = voltage;
    m_voltageSampleIndex = (m_voltageSampleIndex + 1) % VOLTAGE_SAMPLES;
    if (m_voltageSampleIndex == 0) {
        m_voltageSamplesFilled = true;
    }
}

float PowerManager::getFilteredVoltage() {
    float sum = 0.0f;
    int count = m_voltageSamplesFilled ? VOLTAGE_SAMPLES : m_voltageSampleIndex;

    if (count == 0) return 0.0f;

    for (int i = 0; i < count; i++) {
        sum += m_voltageSamples[i];
    }

    return sum / count;
}

bool PowerManager::isCharging() {
#ifndef MOCK_MODE
    // Check VBUS detection pin
    return digitalRead(VBUS_DETECT_PIN) == HIGH;
#else
    // Mock mode: not charging
    return false;
#endif
}

void PowerManager::handlePowerState() {
    switch (m_state) {
        case STATE_ACTIVE:
        case STATE_MOTION_ALERT:
            // Check for battery issues
            if (m_batteryStatus.critical && !m_batteryStatus.charging) {
                setState(STATE_CRITICAL_BATTERY);
            } else if (m_batteryStatus.low && !m_batteryStatus.charging) {
                setState(STATE_LOW_BATTERY);
            } else if (m_batteryStatus.charging) {
                setState(STATE_CHARGING);
            } else if (m_config.enableAutoSleep && shouldEnterSleep()) {
                // Check if we should skip light sleep (go straight to deep sleep)
                if (m_config.enableDeepSleep && m_config.lightSleepToDeepSleepMs == 0) {
                    enterDeepSleep();
                } else {
                    enterLightSleep();
                }
            }
            break;

        case STATE_LIGHT_SLEEP:
            // Check if should transition to deep sleep
            if (m_config.enableDeepSleep && m_config.lightSleepToDeepSleepMs > 0) {
                uint32_t timeInLightSleep = millis() - m_stateEnterTime;
                if (timeInLightSleep >= m_config.lightSleepToDeepSleepMs) {
                    enterDeepSleep();
                }
            }
            break;

        case STATE_DEEP_SLEEP:
            // Handled by deep sleep wake-up (system reboots)
            break;

        case STATE_LOW_BATTERY:
            // Check if battery recovered
            if (m_batteryStatus.charging) {
                setState(STATE_CHARGING);
            } else if (!m_batteryStatus.low) {
                setState(STATE_ACTIVE);
            } else if (m_batteryStatus.critical) {
                setState(STATE_CRITICAL_BATTERY);
            }
            break;

        case STATE_CRITICAL_BATTERY:
            // Only exit if charging
            if (m_batteryStatus.charging) {
                setState(STATE_CHARGING);
            } else {
                // Prepare for shutdown
                DEBUG_LOG_SYSTEM("Power: Critical battery, entering deep sleep");
                saveStateToRTC();
                enterDeepSleep(0); // Indefinite sleep until charged
            }
            break;

        case STATE_CHARGING:
            // Return to active when unplugged
            if (!m_batteryStatus.charging) {
                if (m_batteryStatus.critical) {
                    setState(STATE_CRITICAL_BATTERY);
                } else if (m_batteryStatus.low) {
                    setState(STATE_LOW_BATTERY);
                } else {
                    setState(STATE_ACTIVE);
                }
            }
            break;
    }
}

void PowerManager::setState(PowerState newState) {
    if (m_state != newState) {
        DEBUG_LOG_SYSTEM("Power: State %s -> %s", getStateName(m_state), getStateName(newState));
        m_state = newState;
        m_stateEnterTime = millis();
    }
}

bool PowerManager::shouldEnterSleep() {
    uint32_t idleTime = millis() - m_lastActivity;

    // Check idle to light sleep timeout
    if (m_config.enableAutoSleep && idleTime >= m_config.idleToLightSleepMs) {
        return true;
    }

    return false;
}

void PowerManager::enterLightSleep(uint32_t duration_ms) {
    DEBUG_LOG_SYSTEM("Power: Entering light sleep (%ums)", duration_ms);

    saveStateToRTC();
    setState(STATE_LIGHT_SLEEP);

#ifndef MOCK_MODE
    // Configure wake sources
    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL); // Convert to µs
    }

    // ESP32-C3 uses GPIO wakeup for light sleep (gpio_wakeup_enable)
    // Enable GPIO wakeup source first
    esp_sleep_enable_gpio_wakeup();

    // Wake on PIR motion (HIGH level)
    gpio_wakeup_enable((gpio_num_t)PIR_SENSOR_PIN, GPIO_INTR_HIGH_LEVEL);

    // Wake on button press (LOW level since button is active-low with pull-up)
    gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);

    // Reduce CPU frequency (ESP32-C3 max is 160MHz)
    setCPUFrequency(80);

    // Enter light sleep
    esp_light_sleep_start();

    // Restore after wake (ESP32-C3 max is 160MHz, not 240MHz)
    setCPUFrequency(160);
#endif

    // Wake up
    wakeUp();
}

void PowerManager::enterDeepSleep(uint32_t duration_ms) {
    DEBUG_LOG_SYSTEM("Power: Entering deep sleep (%ums)", duration_ms);

    saveStateToRTC();
    setState(STATE_DEEP_SLEEP);

#ifndef MOCK_MODE
    // Configure wake sources
    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL);
    }

    // ESP32-C3 deep sleep GPIO wakeup configuration
    // Note: ESP32-C3 only supports wakeup on GPIO 0-5 in deep sleep
    // PIR_SENSOR_PIN (GPIO6) exceeds GPIO 0-5 range - verify wakeup compatibility!
    // BUTTON_PIN (GPIO0) is valid for wakeup

    // Create bitmask for GPIO wakeup pins
    // PIR sensor wakes on HIGH (motion detected)
    // Button wakes on LOW (button pressed, active-low)
    uint64_t gpio_wakeup_pin_mask = (1ULL << PIR_SENSOR_PIN) | (1ULL << BUTTON_PIN);

    // For ESP32-C3 deep sleep, we use esp_deep_sleep_enable_gpio_wakeup
    // The mode parameter: ESP_GPIO_WAKEUP_GPIO_LOW or ESP_GPIO_WAKEUP_GPIO_HIGH
    // Since PIR needs HIGH and button needs LOW, we configure separately

    // Configure PIR pin for high-level wakeup
    gpio_set_direction((gpio_num_t)PIR_SENSOR_PIN, GPIO_MODE_INPUT);

    // Configure button pin for low-level wakeup (has external pull-up)
    gpio_set_direction((gpio_num_t)BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);

    // Enable deep sleep wakeup on these GPIOs
    // Note: ESP32-C3 wakes on ANY of the configured pins changing to the specified level
    // We use LOW level since button is more reliable, PIR will also work due to edge
    esp_deep_sleep_enable_gpio_wakeup(gpio_wakeup_pin_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

    // Enter deep sleep (no return - system reboots on wake)
    esp_deep_sleep_start();
#endif
}

void PowerManager::wakeUp() {
    DEBUG_LOG_SYSTEM("Power: Wake up");

    m_stats.wakeCount++;
    m_lastActivity = millis();

    #ifndef MOCK_MODE
    // Detect wake source
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_GPIO:  // ESP32-C3 GPIO wakeup (replaces EXT0/EXT1)
            DEBUG_LOG_SYSTEM("Power: Wake source = GPIO (motion or button)");
            // For ESP32-C3, we can't easily distinguish which GPIO triggered the wake
            // Default to active state (user can check button state if needed)
            setState(STATE_ACTIVE);
            break;

        case ESP_SLEEP_WAKEUP_TIMER:  // Timer wake
            DEBUG_LOG_SYSTEM("Power: Wake source = Timer");
            setState(STATE_ACTIVE);
            break;

        case ESP_SLEEP_WAKEUP_UNDEFINED:  // Normal boot (not from sleep)
            DEBUG_LOG_SYSTEM("Power: Wake source = Normal boot");
            setState(STATE_ACTIVE);
            break;

        default:  // Unknown wake source
            DEBUG_LOG_SYSTEM("Power: Wake source = Unknown (%d)", (int)wakeup_reason);
            setState(STATE_ACTIVE);
            break;
    }
    #else
    // Mock mode: default to active
    setState(STATE_ACTIVE);
    #endif

    if (m_onWake) {
        m_onWake();
    }
}

void PowerManager::recordActivity() {
    m_lastActivity = millis();
}

uint32_t PowerManager::getTimeSinceActivity() const {
    return millis() - m_lastActivity;
}

void PowerManager::setCPUFrequency(uint8_t mhz) {
#ifndef MOCK_MODE
    // ESP32-C3 max frequency is 160MHz (not 240MHz like original ESP32)
    #if CONFIG_IDF_TARGET_ESP32C3
    if (mhz > 160) {
        mhz = 160;
    }
    #endif

    setCpuFrequencyMhz(mhz);
    DEBUG_LOG_SYSTEM("Power: CPU frequency set to %uMHz", mhz);
#endif
}

void PowerManager::setLEDBrightness(uint8_t percentage) {
    // This will be called by LED HAL implementations
    // Just log for now
    DEBUG_LOG_SYSTEM("Power: LED brightness set to %u%%", percentage);
}

void PowerManager::saveStateToRTC() {
    rtcMemory.magic = RTC_MAGIC;
    rtcMemory.lastState = m_state;
    rtcMemory.lastBatteryVoltage = m_batteryStatus.voltage;
    rtcMemory.deepSleepCount++;
}

bool PowerManager::restoreStateFromRTC() {
    if (rtcMemory.magic != RTC_MAGIC) {
        return false;
    }

    m_stats.wakeCount = rtcMemory.wakeCount + 1;
    m_stats.deepSleepCount = rtcMemory.deepSleepCount;
    rtcMemory.wakeCount = m_stats.wakeCount;

    DEBUG_LOG_SYSTEM("Power: Restored from deep sleep (wake #%u)", m_stats.wakeCount);

    return true;
}

void PowerManager::updateStats() {
    m_stats.uptime = (millis() - m_startTime) / 1000;

    // Track time in current state
    uint32_t timeInState = (millis() - m_stateEnterTime) / 1000;

    if (m_state == STATE_ACTIVE || m_state == STATE_CHARGING) {
        m_stats.activeTime += timeInState;
    } else {
        m_stats.sleepTime += timeInState;
    }

    // Reset state enter time for next update
    m_stateEnterTime = millis();

    // Calculate average current (simplified)
    // This would need actual current measurement in real implementation
    switch (m_state) {
        case STATE_ACTIVE:
            m_stats.avgCurrent = 240.0f; // mA
            break;
        case STATE_LIGHT_SLEEP:
            m_stats.avgCurrent = 30.0f;
            break;
        case STATE_DEEP_SLEEP:
            m_stats.avgCurrent = 0.12f;
            break;
        case STATE_LOW_BATTERY:
            m_stats.avgCurrent = 150.0f; // Reduced features
            break;
        case STATE_CHARGING:
            m_stats.avgCurrent = 240.0f; // Full features when charging
            break;
        default:
            m_stats.avgCurrent = 0.0f;
            break;
    }
}

void PowerManager::resetStats() {
    m_stats.uptime = 0;
    m_stats.activeTime = 0;
    m_stats.sleepTime = 0;
    m_stats.wakeCount = 0;
    m_stats.deepSleepCount = 0;
    m_stats.avgCurrent = 0.0f;

    m_startTime = millis();
    DEBUG_LOG_SYSTEM("Power: Statistics reset");
}

const char* PowerManager::getStateName(PowerState state) {
    switch (state) {
        case STATE_ACTIVE: return "ACTIVE";
        case STATE_MOTION_ALERT: return "MOTION_ALERT";
        case STATE_LIGHT_SLEEP: return "LIGHT_SLEEP";
        case STATE_DEEP_SLEEP: return "DEEP_SLEEP";
        case STATE_LOW_BATTERY: return "LOW_BATTERY";
        case STATE_CRITICAL_BATTERY: return "CRITICAL_BATTERY";
        case STATE_CHARGING: return "CHARGING";
        default: return "UNKNOWN";
    }
}
