#ifndef STEPAWARE_POWER_MANAGER_H
#define STEPAWARE_POWER_MANAGER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Power Manager for Battery Monitoring and Power Optimization
 *
 * Manages battery monitoring, power states, sleep modes, and power optimization
 * to maximize battery life while maintaining system responsiveness.
 *
 * Features:
 * - Battery voltage monitoring via ADC
 * - Charge percentage calculation
 * - Charging detection (VBUS)
 * - Light sleep and deep sleep management
 * - Power state machine (ACTIVE, LIGHT_SLEEP, DEEP_SLEEP, LOW_BATTERY, CRITICAL_BATTERY, CHARGING)
 * - RTC memory state persistence
 * - Power statistics tracking
 *
 * Usage:
 * ```cpp
 * PowerManager power;
 * power.begin();
 *
 * void loop() {
 *     power.update();  // Monitor battery, manage sleep
 *     // ... other code
 * }
 * ```
 */
class PowerManager {
public:
    /**
     * @brief Power states
     */
    enum PowerState {
        STATE_ACTIVE,          ///< Full power, WiFi enabled, all features
        STATE_MOTION_ALERT,    ///< Motion response only, WiFi off (battery saving)
        STATE_LIGHT_SLEEP,     ///< WiFi off, CPU 80MHz, quick wake
        STATE_DEEP_SLEEP,      ///< Deep sleep, wake on motion/button
        STATE_LOW_BATTERY,     ///< Battery < 20%, reduced features
        STATE_CRITICAL_BATTERY,///< Battery < 5%, shutdown imminent
        STATE_CHARGING         ///< USB powered, charging
    };

    /**
     * @brief Battery status information
     */
    struct BatteryStatus {
        float voltage;          ///< Battery voltage (V)
        uint8_t percentage;     ///< Charge percentage (0-100%)
        bool charging;          ///< Charging state
        bool low;               ///< Low battery flag (<20%)
        bool critical;          ///< Critical battery flag (<5%)
        uint32_t timeToEmpty;   ///< Estimated time to empty (seconds)
    };

    /**
     * @brief Power statistics
     */
    struct PowerStats {
        uint32_t uptime;        ///< Total uptime (seconds)
        uint32_t activeTime;    ///< Time in active state (seconds)
        uint32_t sleepTime;     ///< Time in sleep states (seconds)
        uint32_t wakeCount;     ///< Total wake-up count
        uint32_t deepSleepCount;///< Deep sleep count
        float avgCurrent;       ///< Average current consumption (mA)
    };

    /**
     * @brief Configuration
     */
    struct Config {
        uint32_t idleToLightSleepMs;         ///< Idle time before light sleep (ms, default: 180000 = 3min, range: 60000-600000)
        uint32_t lightSleepToDeepSleepMs;    ///< Time in light sleep before deep sleep (ms, default: 60000 = 1min, 0 = skip light sleep)
        float lowBatteryThreshold;           ///< Low battery voltage (V, default: 3.4V ~20%)
        float criticalBatteryThreshold;      ///< Critical battery voltage (V, default: 3.2V ~5%)
        uint32_t batteryCheckInterval;       ///< Battery check interval (ms, default: 10000)
        bool enableAutoSleep;                ///< Enable automatic sleep (default: true)
        bool enableDeepSleep;                ///< Enable deep sleep mode (default: true)
        float voltageCalibrationOffset;      ///< Voltage calibration offset (V, default: 0.0)
        uint8_t lowBatteryLEDBrightness;     ///< LED brightness when low battery (0-255, default: 128 = 50%)
    };

    /**
     * @brief Callback function types
     */
    typedef void (*LowBatteryCallback)();
    typedef void (*CriticalBatteryCallback)();
    typedef void (*ChargingCallback)();
    typedef void (*WakeCallback)();

    /**
     * @brief Construct power manager
     */
    PowerManager();

    /**
     * @brief Destructor
     */
    ~PowerManager();

    /**
     * @brief Initialize power manager
     *
     * @param config Optional configuration (uses defaults if nullptr)
     * @return True if initialization successful
     */
    bool begin(const Config* config = nullptr);

    /**
     * @brief Update power manager (call every loop)
     *
     * Monitors battery, manages sleep states, and optimizes power consumption.
     */
    void update();

    /**
     * @brief Get current power state
     *
     * @return Current power state
     */
    PowerState getState() const { return m_state; }

    /**
     * @brief Get battery status
     *
     * @return Battery status information
     */
    const BatteryStatus& getBatteryStatus() const { return m_batteryStatus; }

    /**
     * @brief Get power statistics
     *
     * @return Power statistics
     */
    const PowerStats& getStats() const { return m_stats; }

    /**
     * @brief Get battery voltage
     *
     * @return Battery voltage in volts
     */
    float getBatteryVoltage();

    /**
     * @brief Get battery percentage
     *
     * @return Battery charge percentage (0-100)
     */
    uint8_t getBatteryPercentage();

    /**
     * @brief Check if charging
     *
     * @return True if USB power connected
     */
    bool isCharging();

    /**
     * @brief Check if battery is low
     *
     * @return True if battery below low threshold
     */
    bool isBatteryLow() const { return m_batteryStatus.low; }

    /**
     * @brief Check if battery is critical
     *
     * @return True if battery below critical threshold
     */
    bool isBatteryCritical() const { return m_batteryStatus.critical; }

    /**
     * @brief Enter light sleep mode
     *
     * WiFi off, CPU 80MHz, wake on motion/button/timer.
     *
     * @param duration_ms Sleep duration (0 = indefinite)
     */
    void enterLightSleep(uint32_t duration_ms = 0);

    /**
     * @brief Enter deep sleep mode
     *
     * Deep sleep, wake on motion/button/timer.
     * System will reboot on wake.
     *
     * @param duration_ms Sleep duration (0 = indefinite)
     */
    void enterDeepSleep(uint32_t duration_ms = 0);

    /**
     * @brief Wake from sleep
     *
     * Called automatically after sleep wake-up.
     */
    void wakeUp();

    /**
     * @brief Record activity (resets idle timer)
     *
     * Call when system is actively used to prevent sleep.
     */
    void recordActivity();

    /**
     * @brief Set CPU frequency
     *
     * @param mhz Frequency in MHz (80, 160, 240)
     */
    void setCPUFrequency(uint8_t mhz);

    /**
     * @brief Set LED brightness percentage
     *
     * @param percentage Brightness (0-100%)
     */
    void setLEDBrightness(uint8_t percentage);

    /**
     * @brief Reset power statistics
     */
    void resetStats();

    /**
     * @brief Get time since last activity
     *
     * @return Time in milliseconds
     */
    uint32_t getTimeSinceActivity() const;

    /**
     * @brief Get last battery update time
     *
     * @return Timestamp in milliseconds
     */
    uint32_t getLastUpdateTime() const { return m_lastBatteryUpdate; }

    /**
     * @brief Force battery status update
     */
    void updateBatteryStatus();

    /**
     * @brief Register low battery callback
     *
     * @param callback Function to call when battery is low
     */
    void onLowBattery(LowBatteryCallback callback) { m_onLowBattery = callback; }

    /**
     * @brief Register critical battery callback
     *
     * @param callback Function to call when battery is critical
     */
    void onCriticalBattery(CriticalBatteryCallback callback) { m_onCriticalBattery = callback; }

    /**
     * @brief Register charging callback
     *
     * @param callback Function to call when charging starts
     */
    void onCharging(ChargingCallback callback) { m_onCharging = callback; }

    /**
     * @brief Register wake callback
     *
     * @param callback Function to call when waking from sleep
     */
    void onWake(WakeCallback callback) { m_onWake = callback; }

    /**
     * @brief Get power state name
     *
     * @param state Power state
     * @return State name string
     */
    static const char* getStateName(PowerState state);

private:
    Config m_config;                    ///< Configuration
    PowerState m_state;                 ///< Current power state
    BatteryStatus m_batteryStatus;      ///< Battery status
    PowerStats m_stats;                 ///< Power statistics
    bool m_initialized;                 ///< Initialization flag

    uint32_t m_lastActivity;            ///< Last activity timestamp
    uint32_t m_lastBatteryUpdate;       ///< Last battery update timestamp
    uint32_t m_stateEnterTime;          ///< Time when current state entered
    uint32_t m_startTime;               ///< System start time

    // Voltage filtering
    static const uint8_t VOLTAGE_SAMPLES = 10;
    float m_voltageSamples[VOLTAGE_SAMPLES];
    uint8_t m_voltageSampleIndex;
    bool m_voltageSamplesFilled;

    // Callbacks
    LowBatteryCallback m_onLowBattery;
    CriticalBatteryCallback m_onCriticalBattery;
    ChargingCallback m_onCharging;
    WakeCallback m_onWake;

    /**
     * @brief Handle power state machine
     */
    void handlePowerState();

    /**
     * @brief Transition to new power state
     *
     * @param newState State to transition to
     */
    void setState(PowerState newState);

    /**
     * @brief Read raw battery voltage
     *
     * @return Voltage in volts
     */
    float readBatteryVoltageRaw();

    /**
     * @brief Calculate battery percentage from voltage
     *
     * @param voltage Battery voltage (V)
     * @return Percentage (0-100)
     */
    uint8_t calculateBatteryPercentage(float voltage);

    /**
     * @brief Add voltage sample to filter
     *
     * @param voltage Voltage sample
     */
    void addVoltageSample(float voltage);

    /**
     * @brief Get filtered voltage average
     *
     * @return Averaged voltage
     */
    float getFilteredVoltage();

    /**
     * @brief Check if should enter sleep
     *
     * @return True if idle timeout reached
     */
    bool shouldEnterSleep();

    /**
     * @brief Save state to RTC memory
     */
    void saveStateToRTC();

    /**
     * @brief Restore state from RTC memory
     *
     * @return True if state restored successfully
     */
    bool restoreStateFromRTC();

    /**
     * @brief Update power statistics
     */
    void updateStats();
};

// Global power manager instance
extern PowerManager g_power;

#endif // STEPAWARE_POWER_MANAGER_H
