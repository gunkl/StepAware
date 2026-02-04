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
        STATE_USB_POWER        ///< USB power connected
    };

    /**
     * @brief Battery status information
     */
    struct BatteryStatus {
        float voltage;          ///< Battery voltage (V)
        uint8_t percentage;     ///< Charge percentage (0-100%)
        bool usbPower;          ///< USB power connected
        bool low;               ///< Low battery flag (<20%)
        bool critical;          ///< Critical battery flag (<5%)
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
        uint32_t lightSleepTime;    ///< Time in light sleep this boot cycle (seconds)
        uint32_t deepSleepTime;     ///< Accumulated deep-sleep time across reboots (seconds)
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
    typedef void (*UsbPowerCallback)();
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
     * @brief Check if USB power is connected
     *
     * @return True if USB power connected (VBUS detected on GPIO6)
     */
    bool isUsbPower();

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
     * @brief Enable or disable battery monitoring at runtime
     *
     * When disabled, battery voltage reads return a fixed nominal value
     * and battery-based power management is inactive.
     *
     * @param enabled True to enable battery monitoring
     */
    void setBatteryMonitoringEnabled(bool enabled);

    /**
     * @brief Check if battery monitoring is enabled
     *
     * @return True if battery monitoring is active (requires external voltage divider)
     */
    bool isBatteryMonitoringEnabled() const { return m_batteryMonitoringEnabled; }

    /**
     * @brief Set power saving mode at runtime
     *
     * Mode 0 = Disabled (no auto-sleep)
     * Mode 1 = Light Sleep only (auto-sleep, no deep sleep)
     * Mode 2 = Deep Sleep + ULP (auto-sleep, deep sleep with ULP PIR monitor)
     *
     * @param mode Power saving mode (0, 1, or 2; values > 2 are clamped to 0)
     */
    void setPowerSavingMode(uint8_t mode);

    /**
     * @brief Get current power saving mode
     *
     * @return Power saving mode (0, 1, or 2)
     */
    uint8_t getPowerSavingMode() const { return m_powerSavingMode; }

    /**
     * @brief Enable power saving even when on USB power (for debugging)
     *
     * When enabled, power saving modes work normally even when USB is connected.
     * IMPORTANT: Always resets to false on boot for safety.
     *
     * @param enable True to allow power saving on USB, false to disable (default)
     */
    void setEnablePowerSavingOnUSB(bool enable);

    /**
     * @brief Get USB power override status
     *
     * @return True if power saving is allowed on USB, false otherwise
     */
    bool getEnablePowerSavingOnUSB() const { return m_enablePowerSavingOnUSB; }

    /**
     * @brief Register GPIO pins that should wake the device from sleep.
     *
     * Must be called once during setup after sensors are loaded from
     * config.  Only PIR-type sensor pins (active-HIGH output) should be
     * passed.  The array is copied; the caller does not need to keep it
     * alive.
     *
     * @param pins  Array of GPIO pin numbers
     * @param count Number of entries (clamped to MAX_MOTION_WAKE_PINS)
     */
    void setMotionWakePins(const uint8_t* pins, uint8_t count);

    /**
     * @brief Enter light sleep mode
     *
     * WiFi off, CPU 80MHz, wake on motion/button/timer.
     *
     * @param duration_ms Sleep duration (0 = indefinite)
     */
    void enterLightSleep(uint32_t duration_ms = 0, const char* reason = nullptr);

    /**
     * @brief Enter deep sleep mode
     *
     * Deep sleep, wake on motion/button/timer.
     * System will reboot on wake.
     *
     * @param duration_ms Sleep duration (0 = indefinite)
     */
    void enterDeepSleep(uint32_t duration_ms = 0, const char* reason = nullptr);

    /**
     * @brief Wake from sleep
     *
     * Called automatically after sleep wake-up.
     */
    void wakeUp(uint32_t sleepDurationMs = 0);

    /**
     * @brief Record activity (resets idle timer)
     *
     * Call when system is actively used to prevent sleep.
     * @param source Optional description of what triggered the activity
     */
    void recordActivity(const char* source = nullptr);

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
     * @brief Register USB power callback
     *
     * @param callback Function to call when USB power is connected
     */
    void onUsbPower(UsbPowerCallback callback) { m_onUsbPower = callback; }

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
    bool m_batteryMonitoringEnabled;    ///< Battery monitoring enabled (runtime)
    uint8_t m_powerSavingMode;          ///< Power saving mode (0=off, 1=light sleep, 2=deep+ULP)
    bool m_enablePowerSavingOnUSB;      ///< Enable power saving even on USB (debug only, resets to false on boot)

    uint32_t m_lastActivity;            ///< Last activity timestamp
    uint32_t m_lastBatteryUpdate;       ///< Last battery update timestamp
    uint32_t m_stateEnterTime;          ///< Time when current state entered
    uint32_t m_lastStatsUpdate;         ///< Last stats accumulation timestamp
    uint32_t m_startTime;               ///< System start time

    // Voltage filtering
    static const uint8_t VOLTAGE_SAMPLES = 10;
    float m_voltageSamples[VOLTAGE_SAMPLES];
    uint8_t m_voltageSampleIndex;
    bool m_voltageSamplesFilled;

    // Callbacks
    LowBatteryCallback m_onLowBattery;
    CriticalBatteryCallback m_onCriticalBattery;
    UsbPowerCallback m_onUsbPower;
    WakeCallback m_onWake;

    // Motion wake-pin list — populated at boot from sensor config via
    // setMotionWakePins().  enterLightSleep/enterDeepSleep iterate this
    // array instead of using a hardcoded single pin.
    static const uint8_t MAX_MOTION_WAKE_PINS = 4;
    uint8_t m_motionWakePins[MAX_MOTION_WAKE_PINS];
    uint8_t m_motionWakePinCount;

    /**
     * @brief Handle power state machine
     */
    void handlePowerState();

    /**
     * @brief Log periodic power state summary
     *
     * Logs comprehensive state info every 5 minutes for diagnostics
     */
    void logStateSummary();

    /**
     * @brief Transition to new power state
     *
     * @param newState State to transition to
     */
    void setState(PowerState newState, const char* reason = nullptr);

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
     * @brief Detect wake source and route to the appropriate power state.
     *
     * On ESP32-C3 both PIR and button report as ESP_SLEEP_WAKEUP_GPIO.
     * The method reads the button GPIO to distinguish the two: button held
     * LOW means user interaction (route to ACTIVE); otherwise route to
     * MOTION_ALERT (WiFi off, battery-saving motion response).
     *
     * Called from begin() after a deep-sleep RTC restore, and from wakeUp()
     * after light sleep returns.
     */
    void detectAndRouteWakeSource(uint32_t sleepDurationMs = 0);

    /**
     * @brief Check if should enter sleep
     *
     * @return True if idle timeout reached
     */
    bool shouldEnterSleep();

    /**
     * @brief Load and start the ULP RISC-V program for PIR monitoring in deep sleep.
     * Only called when m_powerSavingMode == 2, immediately before esp_deep_sleep_start().
     */
    void startULPPirMonitor();

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
