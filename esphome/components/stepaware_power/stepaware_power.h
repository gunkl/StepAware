#pragma once

// StepAware deep-sleep power policy (ESPHome external component).
//
// Ported from the Arduino firmware's PowerManager (src/power_manager.cpp):
//   - classifies the wake cause (PIR / button / timer / power-on / crash / USB),
//   - decides whether WiFi comes up on this boot (before WiFiComponent::setup runs),
//   - decides when it is safe to deep sleep, and
//   - arms the three mixed-level GPIO wake sources + PIR rail hold that ESPHome's
//     built-in deep_sleep component cannot express on the ESP32-C3.
//
// The actual shutdown is delegated to deep_sleep::DeepSleepComponent::begin_sleep() so all
// components tear down cleanly; the wake sources are armed from on_powerdown(), which runs
// after teardown and immediately before esp_deep_sleep_start().
//
// Dormant mode ("switched off"): requestDormant() (button gesture, "Shut Down" button, or a
// confirmed critical battery) persists a flag in flash (NVS) and deep-sleeps with the button
// as the only wake source, PIR rail off. Every boot checks the flag first thing in setup():
// anything but a button press (or the button held at power-on) goes straight back to sleep,
// so USB power, a brownout or a restored battery never bring the device back up.

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/deep_sleep/deep_sleep_component.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#ifdef USE_BUTTON  // auto-loaded only with pir_recalibration
#include "esphome/components/button/button.h"
#endif

#include "power_logic.h"

#include <functional>
#include <vector>

namespace esphome::stepaware_power {

// Wake cause lives in power_logic.h (unit-tested dormant decision uses it).
using step_power::WakeCause;
using step_power::WAKE_POWER_ON;
using step_power::WAKE_SOFTWARE_RESET;
using step_power::WAKE_CRASH;
using step_power::WAKE_PIR;
using step_power::WAKE_BUTTON;
using step_power::WAKE_TIMER;
using step_power::WAKE_SPURIOUS_GPIO;
using step_power::WAKE_OTHER_SLEEP;

/// Power-saving mode select options (index order matters: persisted).
enum PowerMode : uint8_t {
    POWER_MODE_OFF = 0,
    POWER_MODE_DEEP_SLEEP = 1,
};

/// What kind of session this boot is, which picks the sleep rules.
enum SessionKind : uint8_t {
    SESSION_RADIO_OFF = 0,  ///< PIR / spurious wake: WiFi stays off, short idle
    SESSION_CHECK_IN,       ///< Timer wake: WiFi on, sleep once HA has had its window
    SESSION_INTERACTIVE,    ///< Button / boot / crash / USB / runtime WiFi enable
};

class StepAwarePower;

/// "Power Saving Mode" select ("Off" / "Deep sleep"), restored from flash.
class PowerModeSelect : public select::Select, public Parented<StepAwarePower> {
 protected:
    void control(size_t index) override;
};

/// Tunable number entity (seconds / minutes), restored from flash.
class PowerNumber : public number::Number, public Parented<StepAwarePower> {
 public:
    void setInitialValue(float value) { m_initialValue = value; }
    void restore();

 protected:
    void control(float value) override;

    ESPPreferenceObject m_pref;
    float m_initialValue{0.0f};
};

/// "WiFi on Motion Wake" switch, restored from flash (switch restore_mode).
class PowerSwitch : public switch_::Switch, public Parented<StepAwarePower> {
 protected:
    void write_state(bool state) override;
};

#ifdef USE_BUTTON
/// "Recalibrate PIRs" button: power-cycles the PIR rail now.
class RecalButton : public button::Button, public Parented<StepAwarePower> {
 protected:
    void press_action() override;
};

/// "Shut Down" button: enter dormant (button-only wake).
class ShutdownButton : public button::Button, public Parented<StepAwarePower> {
 protected:
    void press_action() override;
};
#endif

class StepAwarePower : public Component
#ifdef USE_OTA_STATE_LISTENER
    ,
                       public ota::OTAGlobalStateListener
#endif
{
 public:
    // ---- ESPHome overrides --------------------------------------------------------------
    void setup() override;
    void loop() override;
    void dump_config() override;
    void on_powerdown() override;
    float get_setup_priority() const override;

#ifdef USE_OTA_STATE_LISTENER
    void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                             ota::OTAComponent *comp) override;
#endif

    // ---- Configuration (called from generated code) -------------------------------------
    void setDeepSleep(deep_sleep::DeepSleepComponent *ds) { m_deepSleep = ds; }
    void addPirPin(uint8_t pin) { m_pirPins.push_back(pin); }
    void setButtonPin(uint8_t pin) { m_buttonPin = pin; }
    void setUsbPowerPin(uint8_t pin) { m_usbPowerPin = pin; }
    void setPirPowerPin(uint8_t pin) { m_pirPowerPin = pin; }
    void setStayAwakeSensor(binary_sensor::BinarySensor *bs) { m_stayAwake = bs; }
    void setButtonSensor(binary_sensor::BinarySensor *bs) { m_buttonSensor = bs; }
    void addActivitySensor(binary_sensor::BinarySensor *bs) { m_activitySensors.push_back(bs); }
    void setBusyCondition(std::function<bool()> &&f) { m_busyCondition = std::move(f); }
    void setLastActivity(std::function<uint32_t()> &&f) { m_lastActivity = std::move(f); }
    void setBootGraceMs(uint32_t ms) { m_bootGraceMs = ms; }
    void setCheckinMinAwakeMs(uint32_t ms) { m_checkinMinAwakeMs = ms; }
    void setWifiTimeoutMs(uint32_t ms) { m_wifiTimeoutMs = ms; }
    void setDefaultMode(uint8_t mode) { m_defaultMode = mode; }

    void setModeSelect(PowerModeSelect *s) { m_modeSelect = s; }
    void setIdleTimeoutNumber(PowerNumber *n) { m_idleTimeout = n; }
    void setMotionWakeIdleNumber(PowerNumber *n) { m_motionWakeIdle = n; }
    void setCheckinIntervalNumber(PowerNumber *n) { m_checkinInterval = n; }
    void setMotionWakeWifiSwitch(PowerSwitch *s) { m_motionWakeWifi = s; }
    void setWakeReasonSensor(text_sensor::TextSensor *s) { m_wakeReasonSensor = s; }
    void setDeepSleepCountSensor(sensor::Sensor *s) { m_deepSleepCountSensor = s; }
    void setSleepBlockedSensor(binary_sensor::BinarySensor *s) { m_sleepBlockedSensor = s; }
    void setSleepBlockReasonSensor(text_sensor::TextSensor *s) { m_blockReasonSensor = s; }

    // Battery state (optional "battery:" block).
    void setBatterySensor(sensor::Sensor *s) { m_batterySensor = s; }
    void setBatteryConfig(float low, float critical, float hysteresis, float minValid,
                          float maxValid, uint8_t minSamples, uint8_t criticalSamples);
    void setCriticalShutdown(bool enabled) { m_criticalShutdown = enabled; }
    void setBatteryLowSensor(binary_sensor::BinarySensor *s) { m_batteryLowSensor = s; }
    void setBatteryCriticalSensor(binary_sensor::BinarySensor *s) { m_batteryCriticalSensor = s; }
    void setBatteryStateSensor(text_sensor::TextSensor *s) { m_batteryStateSensor = s; }

    // PIR recalibration (optional "pir_recalibration:" block).
    void setRecalEnabled(bool enabled) { m_recalEnabled = enabled; }
#ifdef USE_TIME
    void setTime(time::RealTimeClock *t) { m_time = t; }
#endif
    void setRecalSchedule(uint8_t startHour, uint8_t endHour, uint32_t quiescenceS,
                          uint32_t cooldownS);
    void setRecalTiming(uint32_t powerOffMs, uint32_t warmupMs) {
        m_recal.setTiming(powerOffMs, warmupMs);
    }
#ifdef USE_BUTTON
    void setRecalButton(RecalButton *b) { m_recalButton = b; }
#endif
    void setRecalibratingSensor(binary_sensor::BinarySensor *s) { m_recalSensor = s; }

    // Dormant (optional "dormant:" block for entities / options; the logic is always on).
    void setDormantFeedbackMs(uint32_t ms) { m_dormantFeedbackMs = ms; }
    void setWakeHoldMs(uint32_t ms) { m_wakeHoldMs = ms; }
#ifdef USE_BUTTON
    void setShutdownButton(ShutdownButton *b) { m_shutdownButton = b; }
#endif
    void setLastShutdownSensor(text_sensor::TextSensor *s) { m_lastShutdownSensor = s; }
    /// Fired by requestDormant() before the feedback delay (show the power-off icon).
    Trigger<> *getShutdownTrigger() { return &m_shutdownTrigger; }
    /// Fired in setup() after a dormant wake while the waking button is still held
    /// (core: swallow that press so it is not also a click / long press).
    Trigger<> *getDormantWakeTrigger() { return &m_dormantWakeTrigger; }

    Trigger<> *getMotionWakeTrigger() { return &m_motionWakeTrigger; }
    Trigger<> *getSleepTrigger() { return &m_sleepTrigger; }
    Trigger<> *getRecalStartTrigger() { return &m_recalStartTrigger; }
    Trigger<> *getRecalEndTrigger() { return &m_recalEndTrigger; }

    // ---- Runtime API (lambdas / other components) ---------------------------------------
    /// Reset the idle timer (equivalent to PowerManager::recordActivity()).
    void recordActivity();
    /// Switch WiFi on for the rest of this boot and apply interactive-session rules.
    void requestWifi(const char *why);
    WakeCause getWakeCause() const { return m_wakeCause; }
    bool isMotionWake() const { return m_wakeCause == WAKE_PIR; }
    bool isDeepSleepMode() const;
    /// Human-readable reason sleep is currently blocked, or nullptr when it may sleep.
    const char *getSleepBlocker() const { return m_blocker; }

    /// True while the PIR rail is being power-cycled or the PIRs are re-warming. PIR edges
    /// are meaningless in this window and must be ignored by the core.
    bool isRecalibrating() const { return m_recal.isActive(); }
    /// Start a PIR power-cycle recalibration now. Returns false if no PIR power pin.
    bool startRecalibration(const char *why);

    bool isBatteryLow() const { return m_batterySensor != nullptr && m_battery.isLow(); }
    bool isBatteryCritical() const {
        return m_batterySensor != nullptr && m_battery.isCritical();
    }
    const char *getBatteryStateName() const { return m_battery.getStateName(); }

    /**
     * @brief Switch the device off: dormant deep sleep, button-only wake, persisted in flash.
     *
     * Overrides Power Saving Mode, USB power, stay-awake and the busy / idle rules. Refused
     * (returns false) while an OTA is in progress; an OTA starting during the feedback delay
     * cancels it. Sleep waits for the button to be released (it would wake instantly).
     * @param reason step_power::DormantReason
     */
    bool requestDormant(uint8_t reason);
    bool isShutdownPending() const { return m_dormantPending; }
    /// This boot cleared the dormant flag (button wake / button held at power-on).
    bool wokeFromDormant() const { return m_wokeFromDormant; }
    /// Reason of the most recent dormant entry (persisted), step_power::DormantReason.
    uint8_t getLastShutdownReason() const { return m_dormantRecord.reason; }

    // Called by the entity classes.
    void onModeChanged(size_t index);
    void onSettingChanged();

 protected:
    void classifyWake();
    void releaseWakeHolds();
    void releaseRailHold();
    bool readPinLevel(uint8_t pin) const;
    bool isUsbPowered() const;
    bool isApiConnected() const;
    bool anyWakePinActive() const;
    /// Evaluate the sleep policy; returns nullptr if sleep is allowed now.
    const char *evaluateBlocker(uint32_t now);
    void publishBlocker(const char *blocker);
    void enterSleep();
    void armWakeSources();
    const char *wakeCauseName() const;

    // Battery
    void onBatteryReading(float volts);
    void publishBattery(bool force);
    void saveExtRtc();
    /// Blockers for the critical-battery shutdown (nullptr = shut down now).
    const char *evaluateCriticalBlocker(uint32_t now);

    // Dormant
    void saveDormant();
    /// Dormant boot decision; returns only if this boot continues normally.
    void handleDormantBoot();
    bool verifyWakeHold();
    [[noreturn]] void reenterDormantNow();
    void enterDormant(uint8_t reason);
    void armDormantWakeSources();
    void updateDormantRequest(uint32_t now);

    // Recalibration
    static void setRailThunk(void *ctx, bool on);
    void setRail(bool on);
    void noteMotion(uint32_t atMs);
    bool readWallTime(step_power::WallTime &out);
    void updateRecal(uint32_t now);
    void publishRecal();

    static constexpr uint8_t PIN_NONE = 0xFF;
    static constexpr uint32_t EVAL_INTERVAL_MS = 1000;

    deep_sleep::DeepSleepComponent *m_deepSleep{nullptr};
    std::vector<uint8_t> m_pirPins;
    uint8_t m_buttonPin{PIN_NONE};
    uint8_t m_usbPowerPin{PIN_NONE};
    uint8_t m_pirPowerPin{PIN_NONE};

    binary_sensor::BinarySensor *m_stayAwake{nullptr};
    binary_sensor::BinarySensor *m_buttonSensor{nullptr};
    std::vector<binary_sensor::BinarySensor *> m_activitySensors;
    std::function<bool()> m_busyCondition;
    std::function<uint32_t()> m_lastActivity;

    uint32_t m_bootGraceMs{60000};
    uint32_t m_checkinMinAwakeMs{10000};
    uint32_t m_wifiTimeoutMs{30000};
    uint8_t m_defaultMode{POWER_MODE_OFF};

    PowerModeSelect *m_modeSelect{nullptr};
    PowerNumber *m_idleTimeout{nullptr};
    PowerNumber *m_motionWakeIdle{nullptr};
    PowerNumber *m_checkinInterval{nullptr};
    PowerSwitch *m_motionWakeWifi{nullptr};
    text_sensor::TextSensor *m_wakeReasonSensor{nullptr};
    sensor::Sensor *m_deepSleepCountSensor{nullptr};
    binary_sensor::BinarySensor *m_sleepBlockedSensor{nullptr};
    text_sensor::TextSensor *m_blockReasonSensor{nullptr};

    Trigger<> m_motionWakeTrigger;
    Trigger<> m_sleepTrigger;
    ESPPreferenceObject m_modePref;

    // ---- Runtime state ------------------------------------------------------------------
    WakeCause m_wakeCause{WAKE_POWER_ON};
    SessionKind m_session{SESSION_INTERACTIVE};
    int m_resetReason{0};
    uint64_t m_gpioWakeMask{0};
    bool m_usbAtWake{false};
    bool m_wifiOn{false};
    bool m_otaActive{false};
    bool m_sleepRequested{false};
    bool m_motionTriggerPending{false};
    bool m_hasBootGrace{false};
    uint32_t m_wifiOnSinceMs{0};       ///< millis() when WiFi was switched on this boot
    uint32_t m_apiConnectedSinceMs{0}; ///< millis() when the current API session began (0 = none)
    uint32_t m_activityMs{0};          ///< Internal activity timestamp (0 = none this boot)
    bool m_hasActivity{false};
    uint32_t m_lastEvalMs{0};
    const char *m_blocker{"starting"};
    const char *m_publishedBlocker{nullptr};
    bool m_hasPublished{false};

    // ---- Battery ------------------------------------------------------------------------
    sensor::Sensor *m_batterySensor{nullptr};
    binary_sensor::BinarySensor *m_batteryLowSensor{nullptr};
    binary_sensor::BinarySensor *m_batteryCriticalSensor{nullptr};
    text_sensor::TextSensor *m_batteryStateSensor{nullptr};
    step_power::BatteryMonitor m_battery;
    bool m_criticalShutdown{true};
    bool m_criticalSleep{false};        ///< This powerdown is the critical-battery shutdown
    bool m_wokeFromCritical{false};     ///< Previous sleep was a critical-battery shutdown
    const char *m_criticalBlocker{nullptr};
    bool m_criticalPendingLogged{false};

    // ---- Dormant ------------------------------------------------------------------------
    ESPPreferenceObject m_dormantPref;
    step_power::DormantRecord m_dormantRecord;
    uint32_t m_dormantFeedbackMs{1500};
    uint32_t m_wakeHoldMs{0};
    bool m_wokeFromDormant{false};
    bool m_dormantPending{false};
    uint8_t m_dormantPendingReason{0};
    uint32_t m_dormantRequestMs{0};
    bool m_dormantWaitLogged{false};
    bool m_dormantSleep{false};  ///< This powerdown is a dormant sleep
#ifdef USE_BUTTON
    ShutdownButton *m_shutdownButton{nullptr};
#endif
    text_sensor::TextSensor *m_lastShutdownSensor{nullptr};
    Trigger<> m_shutdownTrigger;
    Trigger<> m_dormantWakeTrigger;

    // ---- PIR recalibration --------------------------------------------------------------
    bool m_recalEnabled{false};
#ifdef USE_TIME
    time::RealTimeClock *m_time{nullptr};
#endif
    step_power::RecalScheduler m_recalScheduler;
    step_power::RecalSequencer m_recal{&esphome::millis};
#ifdef USE_BUTTON
    RecalButton *m_recalButton{nullptr};
#endif
    binary_sensor::BinarySensor *m_recalSensor{nullptr};
    Trigger<> m_recalStartTrigger;
    Trigger<> m_recalEndTrigger;
    uint32_t m_lastMotionMs{0};
    bool m_hasMotion{false};
    step_power::RecalDecision m_lastDecision{step_power::RECAL_NO_TIME};
};

}  // namespace esphome::stepaware_power
