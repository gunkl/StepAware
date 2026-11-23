#include "stepaware_power.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef STEPAWARE_POWER_HAS_SAFE_MODE
#include "esphome/components/safe_mode/safe_mode.h"
#endif

#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <soc/soc_caps.h>

namespace esphome::stepaware_power {

static const char *const TAG = "stepaware_power";

// ---------------------------------------------------------------------------------------------
// RTC state (survives deep sleep and software/crash resets; garbage after power loss, so it is
// guarded by a magic + check word). Mirrors the rtcMemory block in src/power_manager.cpp.
// ---------------------------------------------------------------------------------------------
struct RtcState {
    uint32_t magic;
    uint32_t deepSleepCount;
    uint32_t lastWakeCause;  ///< WakeCause of the previous boot
    uint32_t check;
};

static constexpr uint32_t RTC_MAGIC = 0x53415057UL;  // "SAPW"
static constexpr uint32_t RTC_CHECK_SALT = 0xA5C3F00DUL;

static RTC_NOINIT_ATTR RtcState s_rtc;

static uint32_t rtcCheck(const RtcState &s) {
    return s.magic ^ (s.deepSleepCount * 2654435761UL) ^ (s.lastWakeCause << 24) ^ RTC_CHECK_SALT;
}

static void rtcCommit() { s_rtc.check = rtcCheck(s_rtc); }

static bool rtcValid() { return s_rtc.magic == RTC_MAGIC && s_rtc.check == rtcCheck(s_rtc); }

// Battery + recalibration state, kept separate so the original block's layout is unchanged.
struct RtcExtState {
    uint32_t magic;
    uint8_t batteryLevel;
    uint8_t batteryValidCount;
    uint8_t batteryCriticalCount;
    uint8_t flags;
    int64_t lastMotionEpoch;  ///< 0 = unknown
    int64_t lastRecalEpoch;   ///< 0 = never
    uint32_t check;
};

static constexpr uint32_t RTC_EXT_MAGIC = 0x53414258UL;  // "SABX"
static constexpr uint8_t RTC_FLAG_CRITICAL_SLEEP = 0x01;

static RTC_NOINIT_ATTR RtcExtState s_rtcExt;

static uint32_t rtcExtCheck(const RtcExtState &s) {
    uint32_t h = 2166136261UL;  // FNV-1a over everything before 'check'
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
    for (size_t i = 0; i < offsetof(RtcExtState, check); i++) {
        h = (h ^ p[i]) * 16777619UL;
    }
    return h ^ RTC_CHECK_SALT;
}

static bool rtcExtValid() {
    return s_rtcExt.magic == RTC_EXT_MAGIC && s_rtcExt.check == rtcExtCheck(s_rtcExt);
}

static const char *const MODE_NAMES[] = {"Off", "Deep sleep"};

// Dormant record preference key (fixed, not tied to an entity: survives renames).
static const char *const DORMANT_PREF_NAME = "stepaware_power_dormant";
// Poll period while verifying a "hold to wake".
static constexpr uint32_t WAKE_HOLD_POLL_MS = 5;
// "Button held at boot" must read pressed for this long (pull-up just enabled after reset).
static constexpr uint32_t BUTTON_HELD_STABLE_MS = 30;

// ---------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------
void PowerModeSelect::control(size_t index) {
    this->publish_state(index);
    this->parent_->onModeChanged(index);
}

void PowerNumber::restore() {
    m_pref = this->make_entity_preference<float>();
    float value;
    if (!m_pref.load(&value) || std::isnan(value)) {
        value = m_initialValue;
    }
    this->publish_state(value);
}

void PowerNumber::control(float value) {
    this->publish_state(value);
    m_pref.save(&value);
    this->parent_->onSettingChanged();
}

void PowerSwitch::write_state(bool state) {
    this->publish_state(state);
    this->parent_->onSettingChanged();
}

#ifdef USE_BUTTON
void RecalButton::press_action() { this->parent_->startRecalibration("button"); }

void ShutdownButton::press_action() {
    this->parent_->requestDormant(step_power::DORMANT_REASON_HOME_ASSISTANT);
}
#endif

// ---------------------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------------------
void StepAwarePower::setBatteryConfig(float low, float critical, float hysteresis,
                                      float minValid, float maxValid, uint8_t minSamples,
                                      uint8_t criticalSamples) {
    step_power::BatteryConfig cfg;
    cfg.lowV = low;
    cfg.criticalV = critical;
    cfg.hysteresisV = hysteresis;
    cfg.minValidV = minValid;
    cfg.maxValidV = maxValid;
    cfg.minSamples = minSamples;
    cfg.criticalSamples = criticalSamples;
    m_battery.setConfig(cfg);
}

void StepAwarePower::setRecalSchedule(uint8_t startHour, uint8_t endHour, uint32_t quiescenceS,
                                      uint32_t cooldownS) {
    step_power::RecalConfig cfg;
    cfg.windowStartHour = startHour;
    cfg.windowEndHour = endHour;
    cfg.quiescenceS = quiescenceS;
    cfg.cooldownS = cooldownS;
    m_recalScheduler.setConfig(cfg);
}

// ---------------------------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------------------------
// Must run before: WiFiComponent::setup() (reads enable_on_boot), the gpio binary sensors
// (HARDWARE), the PIR rail output, and the stepaware.yaml on_boot hold-release (priority 1000).
float StepAwarePower::get_setup_priority() const { return setup_priority::BUS + 50.0f; }

bool StepAwarePower::isDeepSleepMode() const {
    if (m_modeSelect == nullptr) {
        return false;
    }
    auto idx = m_modeSelect->active_index();
    return idx.has_value() && *idx == POWER_MODE_DEEP_SLEEP;
}

bool StepAwarePower::readPinLevel(uint8_t pin) const {
    return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
}

bool StepAwarePower::isUsbPowered() const {
    return m_usbPowerPin != PIN_NONE && readPinLevel(m_usbPowerPin);
}

bool StepAwarePower::isApiConnected() const {
#ifdef USE_API
    return api::global_api_server != nullptr && api::global_api_server->is_connected();
#else
    return false;
#endif
}

bool StepAwarePower::anyWakePinActive() const {
    // GPIO wake is level-triggered: sleeping with a source already active wakes instantly.
    if (m_buttonPin != PIN_NONE && !readPinLevel(m_buttonPin)) {
        return true;
    }
    for (uint8_t pin : m_pirPins) {
        if (readPinLevel(pin)) {
            return true;
        }
    }
    return false;
}

void StepAwarePower::releaseWakeHolds() {
    // Wake pins: IDF's gpio_deep_sleep_wakeup_prepare() latched them with gpio_hold_en().
    // Release so the binary_sensor gpio setup can reconfigure them; keep them readable now.
    for (uint8_t pin : m_pirPins) {
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
        gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
    }
    if (m_buttonPin != PIN_NONE) {
        auto pin = static_cast<gpio_num_t>(m_buttonPin);
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_en(pin);
    }
    if (m_usbPowerPin != PIN_NONE) {
        // VBUS divider; pulldown matches stepaware.yaml (INPUT_PULLDOWN) so a floating pad
        // can never read as "on USB" before the binary_sensor is set up.
        auto pin = static_cast<gpio_num_t>(m_usbPowerPin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pulldown_en(pin);
    }
}

void StepAwarePower::releaseRailHold() {
    // Separate from releaseWakeHolds(): a dormant re-entry never powers the rail.
    if (m_pirPowerPin != PIN_NONE) {
        // PIR rail was held HIGH through deep sleep. Drive the output register HIGH *before*
        // releasing the hold so the rail never glitches low (a glitch restarts PIR warm-up).
        auto pin = static_cast<gpio_num_t>(m_pirPowerPin);
        gpio_set_level(pin, 1);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_hold_dis(pin);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
        gpio_deep_sleep_hold_dis();
#endif
    }
}

void StepAwarePower::classifyWake() {
    m_resetReason = static_cast<int>(esp_reset_reason());
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (m_resetReason == ESP_RST_DEEPSLEEP) {
        switch (cause) {
            case ESP_SLEEP_WAKEUP_GPIO: {
                // The C3 reports one GPIO cause for all pins; the RTC wake-status register says
                // which pin(s) fired. Fall back to live levels in case the register is empty.
                m_gpioWakeMask = esp_sleep_get_gpio_wakeup_status();
                uint64_t pirMask = 0;
                for (uint8_t pin : m_pirPins) {
                    pirMask |= (1ULL << pin);
                }
                bool buttonFired = m_buttonPin != PIN_NONE &&
                                   ((m_gpioWakeMask & (1ULL << m_buttonPin)) != 0 ||
                                    !readPinLevel(m_buttonPin));
                bool pirFired = (m_gpioWakeMask & pirMask) != 0;
                for (uint8_t pin : m_pirPins) {
                    pirFired = pirFired || readPinLevel(pin);
                }
                if (buttonFired) {
                    m_wakeCause = WAKE_BUTTON;  // button wins over PIR
                } else if (pirFired) {
                    m_wakeCause = WAKE_PIR;
                } else {
                    m_wakeCause = WAKE_SPURIOUS_GPIO;
                }
                break;
            }
            case ESP_SLEEP_WAKEUP_TIMER:
                m_wakeCause = WAKE_TIMER;
                break;
            default:
                m_wakeCause = WAKE_OTHER_SLEEP;
                break;
        }
    } else {
        switch (m_resetReason) {
            case ESP_RST_SW:
                m_wakeCause = WAKE_SOFTWARE_RESET;
                break;
            case ESP_RST_PANIC:
            case ESP_RST_INT_WDT:
            case ESP_RST_TASK_WDT:
            case ESP_RST_WDT:
            case ESP_RST_BROWNOUT:
            case ESP_RST_CPU_LOCKUP:
            case ESP_RST_PWR_GLITCH:
                m_wakeCause = WAKE_CRASH;
                break;
            default:
                m_wakeCause = WAKE_POWER_ON;
                break;
        }
    }

    m_usbAtWake = isUsbPowered();

    switch (m_wakeCause) {
        case WAKE_PIR:
        case WAKE_SPURIOUS_GPIO:
            m_session = SESSION_RADIO_OFF;
            break;
        case WAKE_TIMER:
            m_session = SESSION_CHECK_IN;
            break;
        default:
            m_session = SESSION_INTERACTIVE;
            break;
    }
    if (m_usbAtWake) {
        m_session = SESSION_INTERACTIVE;
    }

    // Boot grace only for real (re)boots, not for motion/timer/button sleep wakes.
    m_hasBootGrace = m_wakeCause == WAKE_POWER_ON || m_wakeCause == WAKE_SOFTWARE_RESET ||
                     m_wakeCause == WAKE_CRASH || m_wakeCause == WAKE_OTHER_SLEEP;

    // Idle baseline: interactive and motion sessions count the wake itself as activity; a timer
    // check-in only waits for real activity (plus the HA check-in window).
    if (m_session != SESSION_CHECK_IN) {
        recordActivity();
    }
}

// ---------------------------------------------------------------------------------------------
// Dormant mode
// ---------------------------------------------------------------------------------------------
void StepAwarePower::saveDormant() {
    uint32_t raw = m_dormantRecord.encode();
    if (!m_dormantPref.save(&raw) || !global_preferences->sync()) {
        ESP_LOGE(TAG, "Dormant flag: flash write failed");
    }
}

void StepAwarePower::handleDormantBoot() {
    m_dormantPref = global_preferences->make_preference<uint32_t>(fnv1_hash(DORMANT_PREF_NAME),
                                                                  true);
    uint32_t raw = 0;
    if (m_dormantPref.load(&raw)) {
        m_dormantRecord = step_power::DormantRecord::decode(raw);
    }

    step_power::DormantBootInput in;
    in.flagActive = m_dormantRecord.active;
    in.kind = step_power::dormantBootKindFor(m_wakeCause);
    in.buttonHeld = false;
    if (m_dormantRecord.active && m_buttonPin != PIN_NONE) {
        // The pull-up was enabled microseconds ago (releaseWakeHolds()) on a reset boot: let
        // the pad charge, then require a stable press so a slow edge cannot wake it.
        delayMicroseconds(100);
        in.buttonHeld = !readPinLevel(m_buttonPin);
        uint32_t start = millis();
        while (in.buttonHeld && millis() - start < BUTTON_HELD_STABLE_MS) {
            delay(1);
            in.buttonHeld = !readPinLevel(m_buttonPin);
        }
    }
    in.usb = m_usbAtWake;
    in.wakeHoldMs = m_wakeHoldMs;
    step_power::DormantBootAction action = step_power::decideDormantBoot(in);
    if (action == step_power::DORMANT_ACTION_NORMAL) {
        return;
    }
    ESP_LOGW(TAG, "Dormant flag set (%s), wake %s, button %s, USB %s -> %s",
             step_power::dormantReasonName(m_dormantRecord.reason), wakeCauseName(),
             in.buttonHeld ? "held" : "up", in.usb ? "yes" : "no",
             step_power::dormantBootActionName(action));

    if (action == step_power::DORMANT_ACTION_VERIFY_HOLD) {
        action = verifyWakeHold() ? step_power::DORMANT_ACTION_WAKE
                                  : step_power::DORMANT_ACTION_REENTER;
    }
    if (action == step_power::DORMANT_ACTION_REENTER) {
        reenterDormantNow();
    }

    // Woken: clear the flag (keep the reason for "Last Shutdown").
    m_wokeFromDormant = true;
    m_dormantRecord.active = false;
    saveDormant();
    ESP_LOGW(TAG, "Woken from dormant (was: %s)",
             step_power::dormantReasonName(m_dormantRecord.reason));
    if (m_buttonPin != PIN_NONE && !readPinLevel(m_buttonPin)) {
        // The waking press is still held: it must not reach the core as a click.
        m_dormantWakeTrigger.trigger();
    }
}

bool StepAwarePower::verifyWakeHold() {
    if (m_buttonPin == PIN_NONE) {
        return true;
    }
    step_power::WakeHoldVerifier verifier(m_wakeHoldMs);
    verifier.start(millis());
    while (verifier.update(millis(), !readPinLevel(m_buttonPin)) ==
           step_power::WakeHoldVerifier::PENDING) {
        App.feed_wdt();
        delay(WAKE_HOLD_POLL_MS);
    }
    bool ok = verifier.getResult() == step_power::WakeHoldVerifier::CONFIRMED;
    ESP_LOGW(TAG, "Hold-to-wake (%u ms): %s", static_cast<unsigned>(m_wakeHoldMs),
             ok ? "confirmed" : "released early, staying dormant");
    return ok;
}

void StepAwarePower::reenterDormantNow() {
    // Minimal path: nothing else has been set up (no WiFi, no I2C, no entities). The matrix
    // was put into standby before the dormant sleep (and powers up in standby after power
    // loss), the hazard LED pad is an input after reset, the PIR rail is not driven.
    ESP_LOGW(TAG, "Dormant: back to sleep until the button is pressed");
#ifdef STEPAWARE_POWER_HAS_SAFE_MODE
    // safe_mode counted this boot as an attempt and only clears the counter on a clean
    // shutdown / after boot_is_good_after. A dormant re-entry is not a failed boot: without
    // this, ~10 re-entries (USB plug cycles, a brownout loop on a flat battery) would bring
    // the device up in safe mode with WiFi on and this component not running.
    {
        ESPPreferenceObject counter =
            global_preferences->make_preference<uint32_t>(safe_mode::RTC_KEY, false);
        uint32_t attempts = 0;
        if (counter.load(&attempts) && attempts != 0) {
            attempts = 0;
            counter.save(&attempts);
            global_preferences->sync();
        }
    }
#endif
    s_rtc.lastWakeCause = m_wakeCause;
    s_rtc.deepSleepCount++;
    rtcCommit();
    armDormantWakeSources();
    esp_deep_sleep_start();
}

bool StepAwarePower::requestDormant(uint8_t reason) {
    if (m_otaActive) {
        ESP_LOGW(TAG, "Shut down (%s) refused: OTA in progress",
                 step_power::dormantReasonName(reason));
        return false;
    }
    if (m_deepSleep == nullptr || m_buttonPin == PIN_NONE) {
        ESP_LOGE(TAG, "Shut down (%s) refused: %s", step_power::dormantReasonName(reason),
                 m_deepSleep == nullptr ? "no deep_sleep component" : "no button wake pin");
        return false;
    }
    if (m_dormantPending) {
        return true;
    }
    m_dormantPending = true;
    m_dormantPendingReason = reason;
    m_dormantRequestMs = millis();
    m_dormantWaitLogged = false;
    ESP_LOGW(TAG, "Shut down requested (%s): dormant in %u ms",
             step_power::dormantReasonName(reason), static_cast<unsigned>(m_dormantFeedbackMs));
    m_shutdownTrigger.trigger();
    return true;
}

void StepAwarePower::updateDormantRequest(uint32_t now) {
    if (m_otaActive) {
        ESP_LOGW(TAG, "Shut down cancelled: OTA started");
        m_dormantPending = false;
        return;
    }
    if (now - m_dormantRequestMs < m_dormantFeedbackMs) {
        return;
    }
    if (!readPinLevel(m_buttonPin)) {
        if (!m_dormantWaitLogged) {
            ESP_LOGI(TAG, "Shut down: waiting for the button to be released");
            m_dormantWaitLogged = true;
        }
        return;  // level wake would fire immediately
    }
    enterDormant(m_dormantPendingReason);
}

void StepAwarePower::enterDormant(uint8_t reason) {
    ESP_LOGW(TAG, "Entering DORMANT (%s): deep sleep until the button is pressed "
             "(no PIR / timer / USB wake, PIR rail off)",
             step_power::dormantReasonName(reason));
    // Blank + matrix standby first, so a crash after the flag write leaves the display off.
    m_sleepTrigger.trigger();

    m_dormantRecord = step_power::DormantRecord(true, reason);
    saveDormant();
    m_dormantSleep = true;
    m_criticalSleep = reason == step_power::DORMANT_REASON_CRITICAL_BATTERY;
    saveExtRtc();
    s_rtc.deepSleepCount++;
    rtcCommit();

    m_sleepRequested = true;
    m_deepSleep->begin_sleep(true);

    // Only reached if deep_sleep refused to sleep.
    m_sleepRequested = false;
    m_dormantSleep = false;
    m_dormantPending = false;
    m_criticalSleep = false;
    saveExtRtc();
    m_dormantRecord.active = false;
    saveDormant();
    s_rtc.deepSleepCount--;
    rtcCommit();
    ESP_LOGW(TAG, "deep_sleep did not enter dormant sleep");
    recordActivity();
}

void StepAwarePower::armDormantWakeSources() {
    // Button only. Nothing else may wake a dormant device: no PIR pins, no timer, and USB
    // (GPIO6, not an RTC pad) cannot wake the C3 from deep sleep at all.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t err = ESP_FAIL;
    if (m_buttonPin != PIN_NONE) {
        auto pin = static_cast<gpio_num_t>(m_buttonPin);
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_en(pin);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        err = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << m_buttonPin,
                                                                  ESP_GPIO_WAKEUP_GPIO_LOW);
#else
        err = esp_deep_sleep_enable_gpio_wakeup(1ULL << m_buttonPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
    }
    if (err != ESP_OK) {
        // Never sleep with no wake source at all: hourly re-check (the boot re-enters
        // dormant immediately, so this only costs a short wake per hour).
        ESP_LOGE(TAG, "Button wake arm failed (%d): arming 1 h timer fallback", err);
        esp_sleep_enable_timer_wakeup(60ULL * 60ULL * 1000000ULL);
    }
    // PIR outputs: no pull-up into the unpowered sensors (would back-feed them); pull down.
    for (uint8_t p : m_pirPins) {
        auto pin = static_cast<gpio_num_t>(p);
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_dis(pin);
        gpio_pulldown_en(pin);
    }
    // PIR rail: driven LOW and not held, so the PIRs are unpowered while dormant. setup()
    // drives it HIGH again after a wake (PIRs then warm up as after a cold boot).
    if (m_pirPowerPin != PIN_NONE) {
        auto pin = static_cast<gpio_num_t>(m_pirPowerPin);
        gpio_hold_dis(pin);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
        gpio_deep_sleep_hold_dis();
#endif
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
    }
    ESP_LOGW(TAG, "Dormant wake source armed: button GPIO%d only",
             m_buttonPin == PIN_NONE ? -1 : static_cast<int>(m_buttonPin));
}

const char *StepAwarePower::wakeCauseName() const {
    switch (m_wakeCause) {
        case WAKE_POWER_ON:
            return "Power on";
        case WAKE_SOFTWARE_RESET:
            return "Software reset";
        case WAKE_CRASH:
            switch (m_resetReason) {
                case ESP_RST_BROWNOUT:
                    return "Brownout reset";
                case ESP_RST_PANIC:
                    return "Crash (panic)";
                case ESP_RST_INT_WDT:
                case ESP_RST_TASK_WDT:
                case ESP_RST_WDT:
                    return "Watchdog reset";
                default:
                    return "Crash reset";
            }
        case WAKE_PIR:
            return "Motion (PIR)";
        case WAKE_BUTTON:
            return "Button";
        case WAKE_TIMER:
            return "Check-in timer";
        case WAKE_SPURIOUS_GPIO:
            return "Spurious GPIO wake";
        default:
            return "Other sleep wake";
    }
}

void StepAwarePower::setup() {
    uint32_t prevCause = 0xFF;
    if (rtcValid()) {
        prevCause = s_rtc.lastWakeCause;
    } else {
        s_rtc.magic = RTC_MAGIC;
        s_rtc.deepSleepCount = 0;
    }

    releaseWakeHolds();
    classifyWake();

    // Dormant first: a dormant device goes back to sleep here (no WiFi, rail stays off).
    handleDormantBoot();
    releaseRailHold();

    s_rtc.lastWakeCause = m_wakeCause;
    rtcCommit();

    // Battery / recalibration RTC state. Garbage after power loss -> start clean.
    if (rtcExtValid()) {
        step_power::BatteryPersist bp;
        bp.level = s_rtcExt.batteryLevel;
        bp.validCount = s_rtcExt.batteryValidCount;
        bp.criticalCount = s_rtcExt.batteryCriticalCount;
        m_battery.restore(bp);
        m_wokeFromCritical = (s_rtcExt.flags & RTC_FLAG_CRITICAL_SLEEP) != 0;
        m_recalScheduler.setLastMotionEpoch(s_rtcExt.lastMotionEpoch);
        m_recalScheduler.setLastRecalEpoch(s_rtcExt.lastRecalEpoch);
    }
    m_battery.setUsb(isUsbPowered());
    saveExtRtc();  // also clears the critical-sleep flag

    // Motion / button wakes are motion for the recalibration quiescence check.
    if (m_wakeCause == WAKE_PIR || m_wakeCause == WAKE_BUTTON) {
        noteMotion(millis());
    }

    // Restore settings before deciding anything.
    if (m_modeSelect != nullptr) {
        m_modePref = m_modeSelect->make_entity_preference<uint8_t>();
        uint8_t mode;
        if (!m_modePref.load(&mode) || mode > POWER_MODE_DEEP_SLEEP) {
            mode = m_defaultMode;
        }
        m_modeSelect->publish_state(static_cast<size_t>(mode));
    }
    for (PowerNumber *n : {m_idleTimeout, m_motionWakeIdle, m_checkinInterval}) {
        if (n != nullptr) {
            n->restore();
        }
    }

    bool motionWifi = false;
    if (m_motionWakeWifi != nullptr) {
        motionWifi = m_motionWakeWifi->get_initial_state_with_restore_mode().value_or(false);
        m_motionWakeWifi->publish_state(motionWifi);
    }

    // WiFi decision. WiFiComponent::setup() (priority WIFI, later) reads enable_on_boot, so set
    // it either way: no delay on WiFi-on boots, no radio on motion wakes unless "WiFi on Motion
    // Wake" is on. A motion wake with WiFi keeps the motion session's short idle limit, but
    // first gives Home Assistant its check-in window (or the WiFi timeout). Spurious GPIO wakes
    // never turn the radio on.
    bool wifiOn = !isDeepSleepMode() || m_usbAtWake || m_session != SESSION_RADIO_OFF ||
                  (motionWifi && m_wakeCause == WAKE_PIR);
    if (wifi::global_wifi_component != nullptr) {
        wifi::global_wifi_component->set_enable_on_boot(wifiOn);
    }
    m_wifiOn = wifiOn;
    m_wifiOnSinceMs = millis();

    m_motionTriggerPending = (m_wakeCause == WAKE_PIR);

    ESP_LOGI(TAG, "Wake: %s (reset reason %d, gpio mask 0x%llx, USB %s, prev wake %u) -> WiFi %s",
             wakeCauseName(), m_resetReason, static_cast<unsigned long long>(m_gpioWakeMask),
             m_usbAtWake ? "yes" : "no", static_cast<unsigned>(prevCause), wifiOn ? "ON" : "OFF");

    // Hooks.
    if (m_stayAwake != nullptr) {
        m_stayAwake->add_full_state_callback([this](optional<bool> prev, optional<bool> cur) {
            // Leaving stay-awake starts a fresh idle period. The first report after an API
            // connect (prev empty) is not activity, or every check-in would stay up for minutes.
            if (prev.has_value() && *prev && cur.has_value() && !*cur) {
                recordActivity();
            }
        });
    }
    for (auto *bs : m_activitySensors) {
        bool isButton = (bs == m_buttonSensor);
        bs->add_full_state_callback([this, isButton](optional<bool> prev, optional<bool> cur) {
            if (prev.has_value() && cur.has_value() && *prev != *cur) {
                // PIR edges while the rail is power-cycled / re-warming are not motion.
                if (!isButton && m_recal.isActive()) {
                    return;
                }
                recordActivity();
                noteMotion(millis());
            }
        });
    }
    if (m_batterySensor != nullptr) {
        m_batterySensor->add_on_state_callback([this](float v) { onBatteryReading(v); });
    }
    if (m_buttonSensor != nullptr) {
        m_buttonSensor->add_full_state_callback([this](optional<bool> prev, optional<bool> cur) {
            if (prev.has_value() && cur.has_value() && !*prev && *cur) {
                requestWifi("button press");
            }
        });
    }
#ifdef USE_OTA_STATE_LISTENER
    ota::get_global_ota_callback()->add_global_state_listener(this);
#endif

    if (m_wakeReasonSensor != nullptr) {
        std::string s = wakeCauseName();
        if (m_wokeFromDormant) {
            s += " (from dormant)";
        } else if (m_wokeFromCritical) {
            s += " (after critical battery)";
        }
        if (m_usbAtWake) {
            s += " (USB)";
        }
        m_wakeReasonSensor->publish_state(s);
    }
    if (m_deepSleepCountSensor != nullptr) {
        m_deepSleepCountSensor->publish_state(static_cast<float>(s_rtc.deepSleepCount));
    }
    if (m_lastShutdownSensor != nullptr) {
        m_lastShutdownSensor->publish_state(
            step_power::dormantReasonName(m_dormantRecord.reason));
    }
    if (m_wokeFromCritical) {
        ESP_LOGW(TAG, "Previous sleep was a critical-battery shutdown");
    }

    if (m_batterySensor != nullptr) {
        publishBattery(true);
    }
    if (m_pirPowerPin != PIN_NONE) {
        m_recal.setRail(&StepAwarePower::setRailThunk, this);
    }
    publishRecal();
}

void StepAwarePower::dump_config() {
    ESP_LOGCONFIG(TAG, "StepAware power policy:");
    ESP_LOGCONFIG(TAG, "  Mode: %s", isDeepSleepMode() ? "Deep sleep" : "Off");
    ESP_LOGCONFIG(TAG, "  Wake cause: %s (reset reason %d)", wakeCauseName(), m_resetReason);
    for (uint8_t pin : m_pirPins) {
        ESP_LOGCONFIG(TAG, "  PIR wake pin: GPIO%u (HIGH)", pin);
    }
    if (m_buttonPin != PIN_NONE) {
        ESP_LOGCONFIG(TAG, "  Button wake pin: GPIO%u (LOW)", m_buttonPin);
    }
    if (m_usbPowerPin != PIN_NONE) {
        ESP_LOGCONFIG(TAG, "  USB detect pin: GPIO%u", m_usbPowerPin);
    }
    if (m_pirPowerPin != PIN_NONE) {
        ESP_LOGCONFIG(TAG, "  PIR power hold pin: GPIO%u", m_pirPowerPin);
    }
    ESP_LOGCONFIG(TAG, "  Boot grace: %u ms, check-in min awake: %u ms, WiFi timeout: %u ms",
                  static_cast<unsigned>(m_bootGraceMs), static_cast<unsigned>(m_checkinMinAwakeMs),
                  static_cast<unsigned>(m_wifiTimeoutMs));
    ESP_LOGCONFIG(TAG, "  Deep sleeps (since power loss): %u",
                  static_cast<unsigned>(s_rtc.deepSleepCount));
    ESP_LOGCONFIG(TAG, "  Dormant: feedback %u ms, wake %s; last shutdown: %s%s",
                  static_cast<unsigned>(m_dormantFeedbackMs),
                  m_wakeHoldMs > 0 ? "needs a hold" : "single press",
                  step_power::dormantReasonName(m_dormantRecord.reason),
                  m_wokeFromDormant ? " (woke from it this boot)" : "");
    if (m_batterySensor != nullptr) {
        const auto &bc = m_battery.getConfig();
        ESP_LOGCONFIG(TAG,
                      "  Battery: low < %.2f V, critical < %.2f V, hysteresis %.2f V, "
                      "plausible %.2f-%.2f V",
                      bc.lowV, bc.criticalV, bc.hysteresisV, bc.minValidV, bc.maxValidV);
        ESP_LOGCONFIG(TAG, "  Battery: %u samples to report, critical shutdown %s after %u "
                      "consecutive readings (button wake only)",
                      bc.minSamples, m_criticalShutdown ? "ON" : "OFF", bc.criticalSamples);
        ESP_LOGCONFIG(TAG, "  Battery state: %s (critical count %u)", m_battery.getStateName(),
                      m_battery.getCriticalCount());
    }
    if (m_recalEnabled) {
        const auto &rc = m_recalScheduler.getConfig();
        bool hasTime = false;
#ifdef USE_TIME
        hasTime = m_time != nullptr;
#endif
        ESP_LOGCONFIG(TAG, "  PIR recalibration: off %u ms + warm-up %u ms",
                      static_cast<unsigned>(m_recal.getPowerOffMs()),
                      static_cast<unsigned>(m_recal.getWarmupMs()));
        if (hasTime) {
            ESP_LOGCONFIG(TAG,
                          "  PIR recalibration schedule: %02u:00-%02u:00, quiet %u s, "
                          "cooldown %u s, once per night",
                          rc.windowStartHour, rc.windowEndHour,
                          static_cast<unsigned>(rc.quiescenceS),
                          static_cast<unsigned>(rc.cooldownS));
        } else {
            ESP_LOGCONFIG(TAG, "  PIR recalibration schedule: disabled (no time_id)");
        }
    }
#ifndef USE_OTA_STATE_LISTENER
    ESP_LOGCONFIG(TAG, "  OTA listener: not compiled (no ota component)");
#endif
}

void StepAwarePower::recordActivity() {
    m_activityMs = millis();
    m_hasActivity = true;
}

void StepAwarePower::requestWifi(const char *why) {
    recordActivity();
    m_session = SESSION_INTERACTIVE;
    if (m_wifiOn) {
        return;
    }
    ESP_LOGI(TAG, "Enabling WiFi (%s)", why);
    m_wifiOn = true;
    m_wifiOnSinceMs = millis();
    if (wifi::global_wifi_component != nullptr) {
        wifi::global_wifi_component->enable();
    }
}

void StepAwarePower::onModeChanged(size_t index) {
    uint8_t mode = static_cast<uint8_t>(index);
    m_modePref.save(&mode);
    ESP_LOGI(TAG, "Power saving mode -> %s", index < 2 ? MODE_NAMES[index] : "?");
    if (index == POWER_MODE_OFF) {
        requestWifi("power saving off");
    } else {
        recordActivity();
    }
}

void StepAwarePower::onSettingChanged() { recordActivity(); }

#ifdef USE_OTA_STATE_LISTENER
void StepAwarePower::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                         ota::OTAComponent *comp) {
    if (state == ota::OTA_STARTED || state == ota::OTA_IN_PROGRESS) {
        m_otaActive = true;
    } else if (state == ota::OTA_ABORT || state == ota::OTA_ERROR) {
        m_otaActive = false;
        recordActivity();
    }
    // OTA_COMPLETED: keep blocking; the device reboots next.
}
#endif

const char *StepAwarePower::evaluateBlocker(uint32_t now) {
    if (!isDeepSleepMode()) {
        return "Power saving off";
    }
    if (m_deepSleep == nullptr) {
        return "No deep_sleep component";
    }
    if (isUsbPowered()) {
        return "On USB power";
    }
    if (m_otaActive) {
        return "OTA in progress";
    }
    if (m_stayAwake != nullptr && m_stayAwake->has_state() && m_stayAwake->state) {
        return "Stay awake";
    }
    if (m_recal.isActive()) {
        return "PIR recalibration";
    }
    if (m_busyCondition && m_busyCondition()) {
        recordActivity();  // idle time counts from the end of the warning
        return "Busy";
    }
    if (m_hasBootGrace && now < m_bootGraceMs) {
        return "Boot grace period";
    }
    if (m_wifiOn) {
        if (isApiConnected()) {
            if (now - m_apiConnectedSinceMs < m_checkinMinAwakeMs) {
                return "Home Assistant check-in";
            }
        } else if (now - m_wifiOnSinceMs < m_wifiTimeoutMs) {
            return "Waiting for Home Assistant";
        }
    }

    bool hasActivity = m_hasActivity;
    uint32_t last = m_activityMs;
    if (m_lastActivity) {
        uint32_t ext = m_lastActivity();
        // 0 = "no activity since boot"; ignore values from the future.
        if (ext != 0 && ext <= now && (!hasActivity || static_cast<int32_t>(ext - last) > 0)) {
            last = ext;
            hasActivity = true;
        }
    }
    PowerNumber *limit = (m_session == SESSION_RADIO_OFF) ? m_motionWakeIdle : m_idleTimeout;
    uint32_t limitMs = limit != nullptr ? static_cast<uint32_t>(limit->state * 1000.0f) : 0;
    if (hasActivity && now - last < limitMs) {
        return "Recent activity";
    }
    if (anyWakePinActive()) {
        return "Wake pin active";
    }
    return nullptr;
}

void StepAwarePower::publishBlocker(const char *blocker) {
    // Blocker strings are literals, so pointer comparison detects changes.
    if (m_hasPublished && blocker == m_publishedBlocker) {
        return;
    }
    ESP_LOGD(TAG, "Sleep blocker: %s", blocker != nullptr ? blocker : "none");
    m_hasPublished = true;
    m_publishedBlocker = blocker;
    if (m_sleepBlockedSensor != nullptr) {
        m_sleepBlockedSensor->publish_state(blocker != nullptr);
    }
    if (m_blockReasonSensor != nullptr) {
        m_blockReasonSensor->publish_state(blocker != nullptr ? blocker : "None");
    }
}

void StepAwarePower::loop() {
    uint32_t now = millis();

    if (m_motionTriggerPending) {
        // First loop: every component (incl. the core state machine) has finished setup().
        m_motionTriggerPending = false;
        ESP_LOGI(TAG, "Motion wake: firing on_motion_wake");
        m_motionWakeTrigger.trigger();
    }

    // A requested shutdown owns the device: no normal / critical sleep decisions meanwhile.
    if (m_dormantPending) {
        updateDormantRequest(now);
        return;
    }

    if (isApiConnected()) {
        if (m_apiConnectedSinceMs == 0) {
            m_apiConnectedSinceMs = now | 1u;
        }
    } else {
        m_apiConnectedSinceMs = 0;
    }

    bool usb = isUsbPowered();
    if (!m_wifiOn && usb) {
        requestWifi("USB power");
    }

    if (m_recal.update()) {
        if (m_recal.isActive()) {
            ESP_LOGI(TAG, "PIR recalibration: rail restored, warming up %u ms",
                     static_cast<unsigned>(m_recal.getWarmupMs()));
        } else {
            ESP_LOGI(TAG, "PIR recalibration complete");
            publishRecal();
            m_recalEndTrigger.trigger();
        }
    }

    if (now - m_lastEvalMs < EVAL_INTERVAL_MS) {
        return;
    }
    m_lastEvalMs = now;

    if (m_batterySensor != nullptr && m_battery.setUsb(usb)) {
        saveExtRtc();
        publishBattery(false);
    }

    updateRecal(now);

    // Critical battery: its own (shorter) blocker list, independent of Power Saving Mode.
    if (m_batterySensor != nullptr && m_criticalShutdown && m_battery.isShutdownConfirmed()) {
        const char *cb = evaluateCriticalBlocker(now);
        if (!m_criticalPendingLogged || cb != m_criticalBlocker) {
            ESP_LOGW(TAG, "Critical battery confirmed (%u readings): %s%s",
                     m_battery.getCriticalCount(), cb != nullptr ? "shutdown deferred: " : "",
                     cb != nullptr ? cb : "shutting down");
            m_criticalPendingLogged = true;
            m_criticalBlocker = cb;
        }
        if (cb == nullptr) {
            // Critical battery -> the same persistent dormant state: after a flat battery the
            // device stays off while charging until the button is pressed.
            enterDormant(step_power::DORMANT_REASON_CRITICAL_BATTERY);
            return;
        }
    } else {
        m_criticalPendingLogged = false;
        m_criticalBlocker = nullptr;
    }

    m_blocker = evaluateBlocker(now);
    publishBlocker(m_blocker);
    if (m_blocker == nullptr) {
        enterSleep();
    }
}

void StepAwarePower::enterSleep() {
    uint32_t minutes = (m_checkinInterval != nullptr && m_checkinInterval->state > 0.0f)
                           ? static_cast<uint32_t>(m_checkinInterval->state)
                           : 0;
    ESP_LOGI(TAG, "Entering deep sleep (#%u, check-in %u min, session %u)",
             static_cast<unsigned>(s_rtc.deepSleepCount + 1), static_cast<unsigned>(minutes),
             static_cast<unsigned>(m_session));
    m_sleepTrigger.trigger();

    s_rtc.deepSleepCount++;
    rtcCommit();

    m_sleepRequested = true;
    // Runs safe-shutdown hooks, tears components down (API clean disconnect), runs
    // on_powerdown() (where we arm the wake sources), then esp_deep_sleep_start().
    m_deepSleep->begin_sleep(true);

    // Only reached if deep_sleep refused to sleep.
    m_sleepRequested = false;
    s_rtc.deepSleepCount--;
    rtcCommit();
    ESP_LOGW(TAG, "deep_sleep did not enter sleep");
    recordActivity();
}

void StepAwarePower::on_powerdown() {
    // Armed unconditionally: if anything calls deep_sleep.enter directly (bypassing this
    // policy) the device must still have its button/PIR wake sources, otherwise only a reset
    // could wake it. On safe_reboot() (OTA / restart) this is harmless: the sleep config lives
    // in RAM and is discarded, and the PIR rail hold keeps the PIRs powered (no re-warm-up)
    // until setup() releases it.
    if (!m_sleepRequested) {
        ESP_LOGD(TAG, "Powerdown without a policy sleep (reboot or direct deep_sleep.enter)");
    }
    if (m_dormantSleep && m_sleepRequested) {
        armDormantWakeSources();
    } else {
        armWakeSources();
    }
}

void StepAwarePower::armWakeSources() {
    // ESPHome's deep_sleep_() runs after every on_powerdown(). With no sleep_duration /
    // wakeup_pin configured (enforced in __init__.py) it only calls esp_deep_sleep_start(),
    // so nothing below gets overwritten.
    esp_err_t err;
    uint32_t minutes = (m_checkinInterval != nullptr && m_checkinInterval->state > 0.0f)
                           ? static_cast<uint32_t>(m_checkinInterval->state)
                           : 0;
    if (minutes > 0) {
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL);
    }

    // Button: wake on LOW (pressed), pull-up as in the old enterDeepSleep().
    if (m_buttonPin != PIN_NONE) {
        auto pin = static_cast<gpio_num_t>(m_buttonPin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_en(pin);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        err = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << m_buttonPin,
                                                                  ESP_GPIO_WAKEUP_GPIO_LOW);
#else
        err = esp_deep_sleep_enable_gpio_wakeup(1ULL << m_buttonPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Button wake arm failed: %d", err);
        }
    }

    // PIRs: wake on HIGH, input with no pull change (old enterDeepSleep() did the same; note
    // CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS lets IDF apply a pulldown at sleep entry).
    // Successive calls accumulate (s_config.gpio_wakeup_mask |= / per-pin trigger mode).
    uint64_t pirMask = 0;
    for (uint8_t p : m_pirPins) {
        gpio_set_direction(static_cast<gpio_num_t>(p), GPIO_MODE_INPUT);
        pirMask |= (1ULL << p);
    }
    if (pirMask != 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        err = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(pirMask,
                                                                  ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
        err = esp_deep_sleep_enable_gpio_wakeup(pirMask, ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PIR wake arm failed: %d", err);
        }
    }

    // PIR rail: keep powered so the sensors can wake us. Digital pad on the C3 needs both the
    // per-pin hold and the global deep-sleep hold.
    if (m_pirPowerPin != PIN_NONE) {
        auto pin = static_cast<gpio_num_t>(m_pirPowerPin);
        gpio_set_level(pin, 1);
        gpio_hold_en(pin);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
        gpio_deep_sleep_hold_en();
#endif
    }
    ESP_LOGI(TAG, "Wake sources armed (timer %u min, PIR mask 0x%llx, button GPIO%d)",
             static_cast<unsigned>(minutes), static_cast<unsigned long long>(pirMask),
             m_buttonPin == PIN_NONE ? -1 : static_cast<int>(m_buttonPin));
}

// ---------------------------------------------------------------------------------------------
// Battery state + critical-battery shutdown
// ---------------------------------------------------------------------------------------------
void StepAwarePower::saveExtRtc() {
    step_power::BatteryPersist bp = m_battery.persist();
    std::memset(&s_rtcExt, 0, sizeof(s_rtcExt));
    s_rtcExt.magic = RTC_EXT_MAGIC;
    s_rtcExt.batteryLevel = bp.level;
    s_rtcExt.batteryValidCount = bp.validCount;
    s_rtcExt.batteryCriticalCount = bp.criticalCount;
    s_rtcExt.flags = m_criticalSleep ? RTC_FLAG_CRITICAL_SLEEP : 0;
    s_rtcExt.lastMotionEpoch = m_recalScheduler.getLastMotionEpoch();
    s_rtcExt.lastRecalEpoch = m_recalScheduler.getLastRecalEpoch();
    s_rtcExt.check = rtcExtCheck(s_rtcExt);
}

void StepAwarePower::onBatteryReading(float volts) {
    bool usb = isUsbPowered();
    step_power::BatteryLevel before = m_battery.getLevel();
    bool changed = m_battery.onReading(volts, usb);
    saveExtRtc();

    const auto &bc = m_battery.getConfig();
    if (!(volts >= bc.minValidV && volts <= bc.maxValidV)) {
        ESP_LOGW(TAG, "Battery reading %.3f V implausible: ignored (critical count reset)", volts);
    } else if (m_battery.getCriticalCount() > 0) {
        ESP_LOGW(TAG, "Battery %.3f V below critical %.2f V (%u/%u consecutive)", volts,
                 bc.criticalV, m_battery.getCriticalCount(), bc.criticalSamples);
    } else {
        ESP_LOGV(TAG, "Battery %.3f V -> %s", volts, m_battery.getStateName());
    }
    if (m_battery.getLevel() != before) {
        ESP_LOGI(TAG, "Battery level -> %s (%.3f V%s)", m_battery.getStateName(), volts,
                 usb ? ", USB" : "");
    }
    if (changed) {
        publishBattery(false);
    }
}

void StepAwarePower::publishBattery(bool force) {
    // Binary sensors stay without state until the level is known (or USB makes it moot).
    bool known = m_battery.isKnown() || m_battery.isUsb();
    if (known) {
        if (m_batteryLowSensor != nullptr) {
            m_batteryLowSensor->publish_state(m_battery.isLow());
        }
        if (m_batteryCriticalSensor != nullptr) {
            m_batteryCriticalSensor->publish_state(m_battery.isCritical());
        }
    }
    if (m_batteryStateSensor != nullptr) {
        const char *name = m_battery.getStateName();
        if (force || !m_batteryStateSensor->has_state() || m_batteryStateSensor->state != name) {
            m_batteryStateSensor->publish_state(name);
        }
    }
}

const char *StepAwarePower::evaluateCriticalBlocker(uint32_t now) {
    // Deliberately NOT checked: Power Saving Mode, idle timeout, recent activity, PIR
    // recalibration (the old firmware shut down from any active state, regardless of autoSleep).
    if (m_deepSleep == nullptr) {
        return "No deep_sleep component";
    }
    if (m_buttonPin == PIN_NONE) {
        return "No button wake pin";  // never sleep without a way to wake
    }
    if (isUsbPowered()) {
        return "On USB power";
    }
    if (m_otaActive) {
        return "OTA in progress";
    }
    if (m_stayAwake != nullptr && m_stayAwake->has_state() && m_stayAwake->state) {
        return "Stay awake";
    }
    if (m_busyCondition && m_busyCondition()) {
        return "Busy";
    }
    if (m_hasBootGrace && now < m_bootGraceMs) {
        return "Boot grace period";
    }
    // Give Home Assistant the chance to deliver stay-awake before an interactive session ends.
    if (m_wifiOn) {
        if (isApiConnected()) {
            if (now - m_apiConnectedSinceMs < m_checkinMinAwakeMs) {
                return "Home Assistant check-in";
            }
        } else if (now - m_wifiOnSinceMs < m_wifiTimeoutMs) {
            return "Waiting for Home Assistant";
        }
    }
    if (!readPinLevel(m_buttonPin)) {
        return "Button held";  // level wake would fire immediately
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// PIR recalibration
// ---------------------------------------------------------------------------------------------
void StepAwarePower::setRailThunk(void *ctx, bool on) {
    static_cast<StepAwarePower *>(ctx)->setRail(on);
}

void StepAwarePower::setRail(bool on) {
    if (m_pirPowerPin == PIN_NONE) {
        return;
    }
    auto pin = static_cast<gpio_num_t>(m_pirPowerPin);
    gpio_hold_dis(pin);  // a held pad ignores writes (released in setup(), but be sure)
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, on ? 1 : 0);
}

bool StepAwarePower::startRecalibration(const char *why) {
    if (m_pirPowerPin == PIN_NONE) {
        ESP_LOGW(TAG, "PIR recalibration (%s) ignored: no pir_power_pin", why);
        return false;
    }
    if (m_recal.isActive()) {
        ESP_LOGD(TAG, "PIR recalibration (%s): already in progress", why);
        return true;
    }
    if (!m_recal.start()) {
        return false;
    }
    ESP_LOGI(TAG, "PIR recalibration (%s): rail GPIO%u off for %u ms, then %u ms warm-up", why,
             m_pirPowerPin, static_cast<unsigned>(m_recal.getPowerOffMs()),
             static_cast<unsigned>(m_recal.getWarmupMs()));
    publishRecal();
    m_recalStartTrigger.trigger();
    return true;
}

void StepAwarePower::publishRecal() {
    if (m_recalSensor != nullptr) {
        m_recalSensor->publish_state(m_recal.isActive());
    }
}

void StepAwarePower::noteMotion(uint32_t atMs) {
    m_lastMotionMs = atMs;
    m_hasMotion = true;
}

bool StepAwarePower::readWallTime(step_power::WallTime &out) {
    out.valid = false;
    out.epoch = 0;
    out.hour = out.minute = out.second = 0;
#ifdef USE_TIME
    if (m_time == nullptr) {
        return false;
    }
    ESPTime t = m_time->now();
    if (!t.is_valid()) {
        return false;
    }
    out.valid = true;
    out.epoch = static_cast<int64_t>(t.timestamp);
    out.hour = t.hour;
    out.minute = t.minute;
    out.second = t.second;
    return true;
#else
    return false;
#endif
}

void StepAwarePower::updateRecal(uint32_t now) {
    if (!m_recalEnabled || m_pirPowerPin == PIN_NONE) {
        return;
    }
    // Core's last activity (millis) counts as motion; skipped while recalibrating because the
    // power-cycled PIRs produce bogus edges.
    if (m_lastActivity && !m_recal.isActive()) {
        uint32_t ext = m_lastActivity();
        if (ext != 0 && ext <= now &&
            (!m_hasMotion || static_cast<int32_t>(ext - m_lastMotionMs) > 0)) {
            noteMotion(ext);
        }
    }

    step_power::WallTime wt;
    if (!readWallTime(wt)) {
        m_lastDecision = step_power::RECAL_NO_TIME;
        return;
    }
    if (m_hasMotion) {
        int64_t before = m_recalScheduler.getLastMotionEpoch();
        m_recalScheduler.noteMotion(wt.epoch - static_cast<int64_t>((now - m_lastMotionMs) / 1000));
        if (m_recalScheduler.getLastMotionEpoch() != before) {
            saveExtRtc();
        }
    }
    if (m_batterySensor != nullptr && m_battery.isCritical()) {
        return;  // no point; the critical path owns the rail
    }

    step_power::RecalDecision d = m_recalScheduler.evaluate(wt, m_recal.isActive());
    if (d != m_lastDecision) {
        ESP_LOGD(TAG, "PIR recalibration schedule: %s (%02u:%02u)",
                 step_power::RecalScheduler::decisionName(d), wt.hour, wt.minute);
        m_lastDecision = d;
    }
    if (d == step_power::RECAL_GO && startRecalibration("nightly schedule")) {
        m_recalScheduler.markRecalibrated(wt.epoch);
        saveExtRtc();
    }
}

}  // namespace esphome::stepaware_power
