# Power Manager Design Specification

## Overview

The Power Manager handles battery monitoring, power optimization, and deep sleep management for StepAware. It ensures maximum battery life while maintaining system responsiveness and functionality.

## Design Goals

1. **Long Battery Life**: Maximize runtime on single charge (target: 7+ days)
2. **Smart Power Management**: Automatic sleep/wake based on usage patterns with configurable timeouts
3. **Battery Monitoring**: Accurate charge level tracking and warnings
4. **Graceful Degradation**: Reduce functionality when battery low while maintaining core features
5. **Wake-on-Motion**: Deep sleep with PIR wake capability, optimized for battery savings
6. **Charge Detection**: Detect and handle charging state
7. **Wake Source Intelligence**: Route PIR wakes without WiFi activation to save power
8. **Boot Protection**: Prevent boot on critical battery to avoid over-discharge damage

## Use Cases

### Use Case 1: Normal Operation (Battery Powered)
```
1. Device boots with full battery (4.2V)
2. Power manager monitors battery voltage
3. Normal operation in active mode
4. When idle for 3 minutes (configurable: 1-10min) → Light sleep (WiFi off, CPU slowed)
5. After 1 minute in light sleep (configurable: 0-10min, 0=skip) → Deep sleep
6. PIR motion detected → Wake to MOTION_ALERT state (WiFi stays OFF, saves ~200mA)
7. Flash hazard warning → Return to light sleep after timeout
8. Button press → Wake to ACTIVE state (WiFi enabled for full functionality)
```

### Use Case 2: Low Battery Warning
```
1. Battery drops below 3.4V (20%)
2. Power manager enters LOW_BATTERY mode
3. Reduce LED brightness to 50% (configurable via lowBatteryLEDBrightness)
4. Disable WiFi to conserve power
5. Continue motion detection and hazard warning (core functionality maintained)
6. More aggressive sleep (shorter idle timeouts)
7. Flash status LED every 10 seconds (battery warning)
```

### Use Case 3: Critical Battery Shutdown
```
1. Battery drops below 3.2V (5%)
2. Power manager enters CRITICAL_BATTERY mode
3. Flash hazard LED 3 times (shutdown warning)
4. Save current state to SPIFFS
5. Disable all peripherals
6. Enter deep sleep until charged
7. Wake only on USB power detected

BOOT PROTECTION:
1. Device powered on with critical battery (<3.2V) and not charging
2. Power manager detects critical battery during begin()
3. Flash hazard LED 3 times rapidly (shutdown warning)
4. Enter deep sleep immediately (prevents boot damage from over-discharge)
5. Device will not boot until battery is charged
```

### Use Case 4: Charging Detected
```
1. USB power connected (VBUS detected)
2. Power manager enters CHARGING mode
3. Flash status LED (charging indicator)
4. Enable WiFi for web interface access
5. Monitor charge progress
6. When full (4.2V) → Flash green LED 3 times
7. Resume normal operation
```

### Use Case 5: Deep Sleep Wake-up (Wake Source Routing)
```
PIR WAKE (Battery Optimized):
1. Device in deep sleep (motion wake enabled)
2. PIR sensor triggers EXT0 interrupt
3. ESP32 wakes from deep sleep
4. Power manager detects PIR wake source (ESP_SLEEP_WAKEUP_EXT0)
5. Enter MOTION_ALERT state (WiFi stays OFF, saves ~200mA)
6. Flash hazard warning for motion
7. Return to deep sleep after idle timeout

BUTTON WAKE (Full Functionality):
1. Device in deep sleep
2. User presses button (EXT1 interrupt)
3. ESP32 wakes from deep sleep
4. Power manager detects button wake source (ESP_SLEEP_WAKEUP_EXT1)
5. Enter ACTIVE state (WiFi enabled for web interface)
6. Full functionality available
7. Return to light sleep → deep sleep after idle timeout
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     PowerManager                             │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ├─► Power States
                        │   - ACTIVE (full power, WiFi enabled, all features)
                        │   - MOTION_ALERT (motion response only, WiFi OFF for battery savings)
                        │   - LIGHT_SLEEP (WiFi off, CPU 80MHz, quick wake)
                        │   - DEEP_SLEEP (wake on motion/button)
                        │   - LOW_BATTERY (reduced features, core functionality maintained)
                        │   - CRITICAL_BATTERY (shutdown imminent)
                        │   - CHARGING (USB powered)
                        │
                        ├─► Battery Monitoring
                        │   - Voltage measurement (ADC)
                        │   - Charge percentage (0-100%)
                        │   - Charging detection (VBUS pin)
                        │   - Low battery detection
                        │   - Voltage smoothing (moving average)
                        │
                        ├─► Sleep Management
                        │   - Light sleep timer
                        │   - Deep sleep timer
                        │   - Wake sources (motion, button, timer)
                        │   - State save/restore
                        │   - RTC memory usage
                        │
                        ├─► Power Optimization
                        │   - WiFi power saving
                        │   - CPU frequency scaling
                        │   - LED brightness reduction
                        │   - Peripheral gating
                        │   - Wake-up time optimization
                        │
                        └─► Energy Statistics
                            - Uptime tracking
                            - Sleep time tracking
                            - Wake count
                            - Battery discharge rate
```

## Power States

```
                    ┌──────────────┐
            ┌───────│    ACTIVE    │◄──── Button wake (WiFi enabled)
            │       └──────┬───────┘
            │              │
            │   idle 3min  │ (configurable: 1-10min via idleToLightSleepMs)
            │              ▼
            │       ┌──────────────┐
            │   ┌──►│ LIGHT_SLEEP  │◄──── WiFi off, CPU 80MHz
            │   │   └──────┬───────┘
            │   │          │
            │   │  1min in │ light sleep (configurable: 0-10min via lightSleepToDeepSleepMs)
            │   │          │ (0 = skip light sleep, go straight to deep)
            │   │          ▼
            │   │   ┌──────────────┐
            │   │   │  DEEP_SLEEP  │◄──── Wake on motion (EXT0) or button (EXT1)
            │   │   └───┬────┬─────┘
            │   │       │    │
            │   │  PIR  │    │  Button
            │   │  wake │    │  wake
            │   │       │    │
            │   │       ▼    └───────────┐
            │   │   ┌──────────────┐     │
            │   └───│MOTION_ALERT  │     │  ◄──── PIR wake (WiFi OFF, saves ~200mA)
            │       └──────────────┘     │
            └────────────────────────────┘

            ┌──────────────┐
            │ LOW_BATTERY  │◄──── Battery < 20% (reduced features, core maintained)
            └──────┬───────┘
                   │
        battery    │ critical
                   ▼
            ┌──────────────┐
            │   CRITICAL   │◄──── Battery < 5% (shutdown + boot protection)
            │   BATTERY    │
            └──────────────┘

            ┌──────────────┐
            │   CHARGING   │◄──── USB power detected
            └──────────────┘
```

## Battery Monitoring

### Voltage Measurement

The ESP32 ADC measures battery voltage through a voltage divider:

```cpp
// Hardware: Battery → 100kΩ → ADC_PIN → 100kΩ → GND
// Divider ratio: 2:1 (max input voltage = 6.6V)
// ESP32 ADC range: 0-3.3V (12-bit: 0-4095)

float readBatteryVoltage() {
    uint16_t raw = analogRead(BATTERY_ADC_PIN);

    // Convert ADC to voltage (with 2:1 divider)
    float voltage = (raw / 4095.0) * 3.3 * 2.0;

    // Apply calibration offset (if needed)
    voltage += VOLTAGE_CALIBRATION_OFFSET;

    return voltage;
}
```

### Battery Percentage Calculation

```cpp
// LiPo voltage ranges:
// - 4.2V = 100% (fully charged)
// - 3.7V = 50% (nominal)
// - 3.4V = 20% (low battery warning)
// - 3.2V = 5% (critical shutdown)
// - 3.0V = 0% (absolute minimum)

uint8_t calculateBatteryPercentage(float voltage) {
    // Clamp voltage range
    if (voltage >= 4.2) return 100;
    if (voltage <= 3.0) return 0;

    // Non-linear discharge curve approximation
    // Using piecewise linear segments
    if (voltage >= 3.7) {
        // 3.7V-4.2V = 50%-100% (0.5V range = 50% capacity)
        return 50 + ((voltage - 3.7) / 0.5) * 50;
    } else {
        // 3.0V-3.7V = 0%-50% (0.7V range = 50% capacity)
        return ((voltage - 3.0) / 0.7) * 50;
    }
}
```

### Voltage Smoothing

Use moving average to reduce ADC noise:

```cpp
#define VOLTAGE_SAMPLES 10

class VoltageFilter {
private:
    float samples[VOLTAGE_SAMPLES];
    uint8_t index;
    bool filled;

public:
    VoltageFilter() : index(0), filled(false) {
        for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
            samples[i] = 0;
        }
    }

    void addSample(float voltage) {
        samples[index] = voltage;
        index = (index + 1) % VOLTAGE_SAMPLES;
        if (index == 0) filled = true;
    }

    float getAverage() {
        float sum = 0;
        int count = filled ? VOLTAGE_SAMPLES : index;
        for (int i = 0; i < count; i++) {
            sum += samples[i];
        }
        return count > 0 ? sum / count : 0;
    }
};
```

## Sleep Management

### Light Sleep

Light sleep preserves RAM and allows fast wake-up (< 1ms):

```cpp
void enterLightSleep(uint32_t duration_ms) {
    // Configure wake sources
    esp_sleep_enable_timer_wakeup(duration_ms * 1000); // Convert to µs
    esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH);       // Motion wake
    esp_sleep_enable_ext1_wakeup(BUTTON_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH); // Button wake

    // Disable WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Reduce CPU frequency
    setCpuFrequencyMhz(80); // From 240MHz

    // Enter light sleep
    esp_light_sleep_start();

    // Restore CPU frequency
    setCpuFrequencyMhz(240);
}
```

### Deep Sleep

Deep sleep powers down most of the chip (wake-up ~300ms):

```cpp
void enterDeepSleep(uint32_t duration_ms = 0) {
    // Save state to RTC memory
    saveStateToRTC();

    // Configure wake sources
    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000);
    }
    esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH);       // Motion wake
    esp_sleep_enable_ext1_wakeup(BUTTON_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH); // Button wake

    // Disable all peripherals
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();

    // Enter deep sleep (no return from this function)
    esp_deep_sleep_start();
}
```

### Wake Source Detection and Routing

The power manager intelligently routes wake events to optimize battery life:

```cpp
void PowerManager::wakeUp() {
    LOG_INFO("Power: Wake up");

    m_stats.wakeCount++;
    m_lastActivity = millis();

    #ifndef MOCK_MODE
    // Detect wake source using ESP32 API
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:  // PIR sensor wake (GPIO)
            LOG_INFO("Power: Wake source = PIR motion");
            // PIR wake → Motion alert state (WiFi OFF to save ~200mA)
            // User only needs motion warning, not web interface
            setState(STATE_MOTION_ALERT);
            break;

        case ESP_SLEEP_WAKEUP_EXT1:  // Button wake (GPIO)
            LOG_INFO("Power: Wake source = Button press");
            // Button wake → Full active state (WiFi ON)
            // User interaction implies need for web interface
            setState(STATE_ACTIVE);
            break;

        case ESP_SLEEP_WAKEUP_TIMER:  // Timer wake
            LOG_INFO("Power: Wake source = Timer");
            setState(STATE_ACTIVE);
            break;

        default:  // Unknown wake or normal boot
            LOG_INFO("Power: Wake source = Normal boot");
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
```

**Power Savings:**
- PIR wake without WiFi: Saves ~200mA per wake event
- Battery impact: On devices with frequent motion (50 wakes/day), this saves ~2.4Ah/day
- Runtime improvement: ~20-30% longer battery life in motion-heavy environments

### RTC Memory for State Persistence

ESP32 RTC memory survives deep sleep:

```cpp
// RTC memory structure
RTC_DATA_ATTR struct {
    uint32_t magic;              // Magic number for validation
    OperatingMode lastMode;      // Last operating mode
    uint32_t motionEventCount;   // Total motion events
    uint32_t wakeCount;          // Total wake-ups
    uint32_t deepSleepCount;     // Total deep sleeps
    float lastBatteryVoltage;    // Last battery reading
} rtcMemory;

#define RTC_MAGIC 0xDEADBEEF

void saveStateToRTC() {
    rtcMemory.magic = RTC_MAGIC;
    rtcMemory.lastMode = g_stateMachine.getMode();
    rtcMemory.motionEventCount = g_stateMachine.getMotionEventCount();
    rtcMemory.deepSleepCount++;
    rtcMemory.lastBatteryVoltage = getBatteryVoltage();
}

bool restoreStateFromRTC() {
    if (rtcMemory.magic != RTC_MAGIC) {
        // First boot or RTC memory corrupted
        return false;
    }

    // Restore state
    g_stateMachine.setMode(rtcMemory.lastMode);
    // ... restore other state

    rtcMemory.wakeCount++;
    return true;
}
```

## Power Optimization Techniques

### 1. CPU Frequency Scaling

```cpp
// Normal operation: 240 MHz (max performance)
setCpuFrequencyMhz(240);

// Light sleep: 80 MHz (balance power/performance)
setCpuFrequencyMhz(80);

// Deep sleep: CPU off
```

### 2. WiFi Power Saving

```cpp
// Modem sleep (DTIM-based wake)
WiFi.setSleep(WIFI_PS_MIN_MODEM);

// Complete WiFi disable
WiFi.mode(WIFI_OFF);
```

### 3. LED Brightness Reduction

```cpp
// Normal: 100% brightness
ledcWrite(LED_CHANNEL, 255);

// Low battery: 50% brightness
ledcWrite(LED_CHANNEL, 128);

// Critical: 25% brightness
ledcWrite(LED_CHANNEL, 64);
```

### 4. Peripheral Gating

```cpp
// Disable unused peripherals
periph_module_disable(PERIPH_I2C0_MODULE);
periph_module_disable(PERIPH_UART1_MODULE);
periph_module_disable(PERIPH_SPI_MODULE);
```

## Critical Battery Boot Protection

To prevent over-discharge damage, the power manager includes boot-time protection:

```cpp
bool PowerManager::begin(const Config* config) {
    // ... initialization code ...

    // Read initial battery status
    updateBatteryStatus();

    // Critical battery boot protection
    // If battery is critical and not charging, show warning and shutdown
    if (m_batteryStatus.critical && !m_batteryStatus.charging) {
        LOG_ERROR("Power: Critical battery detected on boot (%.2fV), shutting down",
                  m_batteryStatus.voltage);

        #ifndef MOCK_MODE
        // Flash LED 3 times as shutdown warning
        pinMode(PIN_HAZARD_LED, OUTPUT);
        for (int i = 0; i < 3; i++) {
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
```

**Protection Benefits:**
- Prevents boot loops on critically low battery
- Avoids over-discharge damage to LiPo cells (critical below 3.0V)
- Visual feedback (3 LED blinks) indicates shutdown reason
- Device remains in deep sleep until USB charging detected

## Charging Detection

```cpp
// VBUS detection (GPIO pin connected to USB VBUS via voltage divider)
#define VBUS_DETECT_PIN 34

bool isCharging() {
    return digitalRead(VBUS_DETECT_PIN) == HIGH;
}

// Monitor charging state
void updateChargingState() {
    bool charging = isCharging();

    if (charging && m_powerState != STATE_CHARGING) {
        LOG_INFO("Power: Charging started");
        setState(STATE_CHARGING);

        // Enable WiFi for web interface
        g_wifi.connect();

        // Start charge animation
        startChargeAnimation();
    } else if (!charging && m_powerState == STATE_CHARGING) {
        LOG_INFO("Power: Charging stopped");

        // Return to normal operation
        setState(STATE_ACTIVE);
    }
}
```

## Power Budget Analysis

### Active Mode (All Features Enabled)
- ESP32 CPU (240MHz): ~80mA
- WiFi active: ~120mA
- LEDs (2x max brightness): ~40mA
- PIR sensor: ~0.1mA
- **Total: ~240mA**
- Runtime on 2000mAh battery: ~8.3 hours

### Light Sleep Mode (WiFi Off)
- ESP32 CPU (80MHz): ~30mA
- WiFi off: 0mA
- LEDs off: 0mA
- PIR sensor: ~0.1mA
- **Total: ~30mA**
- Runtime on 2000mAh battery: ~66 hours (2.75 days)

### Deep Sleep Mode (Motion Wake)
- ESP32 deep sleep: ~0.01mA
- PIR sensor: ~0.1mA
- RTC + ULP: ~0.01mA
- **Total: ~0.12mA**
- Runtime on 2000mAh battery: ~16,667 hours (694 days / 1.9 years)

### Typical Usage Pattern (Motion Detection Mode)
- Active 5% of time: 240mA × 0.05 = 12mA
- Light sleep 10% of time: 30mA × 0.10 = 3mA
- Deep sleep 85% of time: 0.12mA × 0.85 = 0.1mA
- **Average: ~15.1mA**
- Runtime on 2000mAh battery: ~132 hours (5.5 days)

## Interface Design

```cpp
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
     * @brief Battery status
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
        uint32_t sleepTime;     ///< Time in sleep state (seconds)
        uint32_t wakeCount;     ///< Total wake-up count
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

    bool begin(const Config* config = nullptr);
    void update();

    // Power state control
    PowerState getState() const;
    void enterLightSleep(uint32_t duration_ms = 0);
    void enterDeepSleep(uint32_t duration_ms = 0);
    void wakeUp();

    // Battery monitoring
    const BatteryStatus& getBatteryStatus() const;
    float getBatteryVoltage();
    uint8_t getBatteryPercentage();
    bool isCharging();
    bool isBatteryLow();

    // Power optimization
    void setCPUFrequency(uint8_t mhz);
    void enableWiFiPowerSaving(bool enable);
    void setLEDBrightness(uint8_t percentage);

    // Statistics
    const PowerStats& getStats() const;
    void resetStats();

    // Callbacks
    void onLowBattery(void (*callback)());
    void onCriticalBattery(void (*callback)());
    void onCharging(void (*callback)());

private:
    void updateBatteryStatus();
    void handlePowerState();
    void optimizePower();
    void saveState();
    void restoreState();
};
```

## Watchdog Integration

```cpp
WatchdogManager::HealthStatus checkPowerManagerHealth(const char** message) {
    PowerManager::BatteryStatus battery = g_power.getBatteryStatus();

    // Check critical battery
    if (battery.critical) {
        *message = "Critical battery";
        return WatchdogManager::HEALTH_CRITICAL;
    }

    // Check low battery
    if (battery.low) {
        *message = "Low battery";
        return WatchdogManager::HEALTH_WARNING;
    }

    // Check if battery status is being updated
    uint32_t timeSinceUpdate = millis() - g_power.getLastUpdateTime();
    if (timeSinceUpdate > 60000) { // Not updated in 1 minute
        *message = "Battery monitor not updating";
        return WatchdogManager::HEALTH_FAILED;
    }

    return WatchdogManager::HEALTH_OK;
}

bool recoverPowerManager(WatchdogManager::RecoveryAction action) {
    switch (action) {
        case WatchdogManager::RECOVERY_SOFT:
            // Force battery update
            g_power.updateBatteryStatus();
            return true;

        case WatchdogManager::RECOVERY_MODULE_RESTART:
            // Reinitialize power manager
            return g_power.begin();

        default:
            return false;
    }
}
```

## Testing Strategy

### Unit Tests
- Battery voltage calculation
- Percentage conversion
- Voltage filtering
- Power state transitions (including STATE_MOTION_ALERT)
- Sleep timer handling
- Wake source detection logic
- Critical battery boot protection
- Configurable sleep timing (idleToLightSleepMs, lightSleepToDeepSleepMs)

### Integration Tests
- Sleep/wake cycles
- Battery monitoring
- Charging detection
- Power optimization
- State save/restore

### Hardware Tests
- Real battery discharge curve
- Sleep current measurement
- Wake-up latency
- ADC accuracy
- Long-term reliability

## Benefits

1. **Extended Battery Life**: 5-7 days typical usage (20-30% improvement with wake source routing)
2. **Automatic Power Management**: No user intervention required
3. **Battery Protection**: Prevents over-discharge with boot-time protection
4. **Quick Wake-up**: Responsive to motion detection
5. **Charging Support**: Smart charging state handling
6. **Power Visibility**: Clear battery status reporting
7. **Intelligent Wake Routing**: PIR wakes without WiFi save ~200mA per event
8. **Configurable Sleep Timing**: User-adjustable timeouts (1-10 minutes)
9. **Aggressive Deep Sleep**: Option to skip light sleep entirely (lightSleepToDeepSleepMs=0)
10. **Core Functionality Maintained**: Even in low battery, motion detection continues

---

**Last Updated**: 2026-01-13
**Version**: 1.1
**Status**: Implemented (Issue #3 completed)
