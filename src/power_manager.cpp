#include "power_manager.h"
#include "logger.h"

#ifndef MOCK_MODE
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/adc.h>
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
    m_config.lightSleepTimeout = 30000;      // 30 seconds
    m_config.deepSleepTimeout = 300000;      // 5 minutes
    m_config.lowBatteryThreshold = 3.4f;     // 3.4V (~20%)
    m_config.criticalBatteryThreshold = 3.2f;// 3.2V (~5%)
    m_config.batteryCheckInterval = 10000;   // 10 seconds
    m_config.enableAutoSleep = true;
    m_config.enableDeepSleep = true;
    m_config.voltageCalibrationOffset = 0.0f;

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
        LOG_WARN("Power: Already initialized");
        return true;
    }

    // Apply custom configuration if provided
    if (config) {
        m_config = *config;
    }

    LOG_INFO("Power: Initializing");

#ifndef MOCK_MODE
    // Configure ADC for battery voltage reading
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
#endif

    m_startTime = millis();
    m_lastActivity = millis();
    m_lastBatteryUpdate = 0; // Force immediate update

    // Try to restore state from RTC memory
    if (restoreStateFromRTC()) {
        LOG_INFO("Power: Restored state from RTC memory (wake count: %u)", m_stats.wakeCount);
    } else {
        LOG_INFO("Power: Fresh boot, initializing RTC memory");
        rtcMemory.magic = RTC_MAGIC;
        rtcMemory.wakeCount = 0;
        rtcMemory.deepSleepCount = 0;
    }

    m_initialized = true;

    // Initial battery check
    updateBatteryStatus();

    LOG_INFO("Power: Initialized (battery: %.2fV, %u%%)",
             m_batteryStatus.voltage, m_batteryStatus.percentage);

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
        LOG_INFO("Power: Charging started");
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
        LOG_ERROR("Power: Critical battery! (%.2fV)", voltage);
        if (m_onCriticalBattery) {
            m_onCriticalBattery();
        }
    } else if (m_batteryStatus.low && !wasLow) {
        LOG_WARN("Power: Low battery (%.2fV)", voltage);
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
    // Read ADC value
    uint16_t raw = adc1_get_raw(BATTERY_ADC_CHANNEL);

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
            // Check for battery issues
            if (m_batteryStatus.critical && !m_batteryStatus.charging) {
                setState(STATE_CRITICAL_BATTERY);
            } else if (m_batteryStatus.low && !m_batteryStatus.charging) {
                setState(STATE_LOW_BATTERY);
            } else if (m_batteryStatus.charging) {
                setState(STATE_CHARGING);
            } else if (m_config.enableAutoSleep && shouldEnterSleep()) {
                // Enter light sleep if idle
                enterLightSleep(m_config.lightSleepTimeout);
            }
            break;

        case STATE_LIGHT_SLEEP:
            // Handled by sleep wake-up
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
                LOG_ERROR("Power: Critical battery, entering deep sleep");
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
        LOG_INFO("Power: State %s -> %s", getStateName(m_state), getStateName(newState));
        m_state = newState;
        m_stateEnterTime = millis();
    }
}

bool PowerManager::shouldEnterSleep() {
    uint32_t idleTime = millis() - m_lastActivity;

    // Check light sleep timeout
    if (m_config.enableAutoSleep && idleTime >= m_config.lightSleepTimeout) {
        return true;
    }

    return false;
}

void PowerManager::enterLightSleep(uint32_t duration_ms) {
    LOG_INFO("Power: Entering light sleep (%ums)", duration_ms);

    saveStateToRTC();
    setState(STATE_LIGHT_SLEEP);

#ifndef MOCK_MODE
    // Configure wake sources
    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL); // Convert to µs
    }

    // Wake on PIR motion
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, HIGH);

    // Wake on button press
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

    // Reduce CPU frequency
    setCPUFrequency(80);

    // Enter light sleep
    esp_light_sleep_start();

    // Restore after wake
    setCPUFrequency(240);
#endif

    // Wake up
    wakeUp();
}

void PowerManager::enterDeepSleep(uint32_t duration_ms) {
    LOG_INFO("Power: Entering deep sleep (%ums)", duration_ms);

    saveStateToRTC();
    setState(STATE_DEEP_SLEEP);

#ifndef MOCK_MODE
    // Configure wake sources
    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL);
    }

    // Wake on PIR motion
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, HIGH);

    // Wake on button press
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

    // Enter deep sleep (no return)
    esp_deep_sleep_start();
#endif
}

void PowerManager::wakeUp() {
    LOG_INFO("Power: Wake up");

    m_stats.wakeCount++;
    m_lastActivity = millis();

    setState(STATE_ACTIVE);

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
    setCpuFrequencyMhz(mhz);
    LOG_INFO("Power: CPU frequency set to %uMHz", mhz);
#endif
}

void PowerManager::setLEDBrightness(uint8_t percentage) {
    // This will be called by LED HAL implementations
    // Just log for now
    LOG_INFO("Power: LED brightness set to %u%%", percentage);
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

    LOG_INFO("Power: Restored from deep sleep (wake #%u)", m_stats.wakeCount);

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
    LOG_INFO("Power: Statistics reset");
}

const char* PowerManager::getStateName(PowerState state) {
    switch (state) {
        case STATE_ACTIVE: return "ACTIVE";
        case STATE_LIGHT_SLEEP: return "LIGHT_SLEEP";
        case STATE_DEEP_SLEEP: return "DEEP_SLEEP";
        case STATE_LOW_BATTERY: return "LOW_BATTERY";
        case STATE_CRITICAL_BATTERY: return "CRITICAL_BATTERY";
        case STATE_CHARGING: return "CHARGING";
        default: return "UNKNOWN";
    }
}
