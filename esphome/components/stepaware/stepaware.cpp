#include "stepaware.h"
#include "stepaware_entities.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#include <cmath>

namespace esphome {
namespace stepaware {

static const char* const TAG = "stepaware";

using step_logic::StepStateMachine;

static const char* const MODE_LABELS[] = {"Off", "Continuous", "Motion"};
static const size_t MODE_COUNT = 3;

uint32_t StepAware::clockMs() { return millis(); }

StepAware::StepAware()
    : m_led(&StepAware::clockMs)
    , m_button(&StepAware::clockMs, 0, step_logic::BUTTON_LONG_PRESS_MS)
    , m_dir(&StepAware::clockMs)
    , m_sm(&StepAware::clockMs) {
    // The binary_sensor input is already debounced (delayed_on/off), so no core debounce.
    m_sm.setHazardLed(&m_led);
    m_sm.setButton(&m_button);
    m_sm.setDirectionDetector(&m_dir);
    m_sm.setListener(this);
}

#ifdef USE_STEPAWARE_MATRIX
void StepAware::setMatrix(stepaware_matrix::StepAwareMatrix* matrix) {
    m_matrix = matrix;
    m_matrixAdapter.setMatrix(matrix);
    m_sm.setMatrix(&m_matrixAdapter);
}
#endif

void StepAware::setDirectionTiming(uint32_t windowMs, uint32_t simultaneousMs,
                                   uint32_t timeoutMs) {
    m_dir.setConfirmationWindowMs(windowMs);
    m_dir.setSimultaneousThresholdMs(simultaneousMs);
    m_dir.setPatternTimeoutMs(timeoutMs);
}

void StepAware::addNumber(StepAwareNumber* n) {
#ifdef USE_NUMBER
    if (n->getKind() == NumberKind::WARNING_DURATION) {
        m_warningNumber = n;
    } else {
        m_brightnessNumber = n;
    }
#else
    (void) n;
#endif
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------

void StepAware::setup() {
    m_restoring = true;

    // Numbers: restore (or initial) value, then apply to the core
    float warningS = static_cast<float>(m_initialWarningMs) / 1000.0f;
    float brightnessPct = m_initialBrightness * 100.0f;
#ifdef USE_NUMBER
    if (m_warningNumber) {
        warningS = m_warningNumber->restoreValue(warningS);
    }
    if (m_brightnessNumber) {
        brightnessPct = m_brightnessNumber->restoreValue(brightnessPct);
    }
#endif
    onNumberChanged(NumberKind::WARNING_DURATION, warningS);
    onNumberChanged(NumberKind::LED_BRIGHTNESS, brightnessPct);

    // Direction filter switch
#ifdef USE_SWITCH
    if (m_directionSwitch) {
        optional<bool> initial = m_directionSwitch->get_initial_state_with_restore_mode();
        bool on = initial.has_value() ? *initial : m_directionEnabled;
        m_directionSwitch->publish_state(on);
        m_directionEnabled = on;
    }
#endif
    m_sm.setDirectionFilterEnabled(m_directionEnabled);

    // Operating mode: restored select value, else default_mode
    size_t mode = m_defaultMode < MODE_COUNT ? m_defaultMode : StepStateMachine::MOTION_DETECT;
#ifdef USE_SELECT
    if (m_modeSelect) {
        mode = m_modeSelect->restoreIndex(mode);
    }
#endif

    bool nearActive = m_motionNear->state;
    bool farActive = m_motionFar->state;
    m_dir.begin(farActive, nearActive);
    m_button.setRawState(m_buttonInput->state);
    feedDisplayInputs(nearActive, farActive);

    m_sm.begin(static_cast<StepStateMachine::OperatingMode>(mode));
    m_restoring = false;

#ifdef USE_SELECT
    if (m_modeSelect) {
        m_modeSelect->publishAndSave(mode);
    }
#endif
    if (m_approachingSensor) {
        m_approachingSensor->publish_initial_state(false);
    }
    if (m_leavingSensor) {
        m_leavingSensor->publish_initial_state(false);
    }
    writeHazardOutput();
    publishDerivedEntities();

    ESP_LOGI(TAG, "Started in mode %s", StepStateMachine::getModeName(m_sm.getMode()));
}

void StepAware::loop() {
    bool nearActive = m_motionNear->state;
    bool farActive = m_motionFar->state;
    m_button.setRawState(m_buttonInput->state);

    // Same order as main.cpp: direction detector, then state machine
    m_dir.update(farActive, nearActive);
    feedDisplayInputs(nearActive, farActive);
    m_sm.update(nearActive || farActive || distanceDetected());

    writeHazardOutput();
    publishDerivedEntities();
}

void StepAware::dump_config() {
    ESP_LOGCONFIG(TAG, "StepAware core:");
    ESP_LOGCONFIG(TAG, "  Mode: %s", StepStateMachine::getModeName(m_sm.getMode()));
    ESP_LOGCONFIG(TAG, "  Motion warning: %u ms", (unsigned) m_sm.getMotionWarningDurationMs());
    ESP_LOGCONFIG(TAG, "  LED on level: %u/255", (unsigned) m_led.getOnBrightness());
    ESP_LOGCONFIG(TAG, "  Long press: %u ms (reboot on release: %s)",
                  (unsigned) m_button.getLongPressMs(), YESNO(m_rebootOnLongPress));
    if (m_sm.isDormantGestureEnabled()) {
        ESP_LOGCONFIG(TAG, "  Dormant gesture: hold %u ms, then release",
                      (unsigned) m_sm.getDormantHoldMs());
    } else {
        ESP_LOGCONFIG(TAG, "  Dormant gesture: disabled");
    }
    ESP_LOGCONFIG(TAG, "  Direction filter: %s (window %u ms, simultaneous %u ms, timeout %u ms)",
                  ONOFF(m_directionEnabled), (unsigned) m_dir.getConfirmationWindowMs(),
                  (unsigned) m_dir.getSimultaneousThresholdMs(),
                  (unsigned) m_dir.getPatternTimeoutMs());
#ifdef USE_SENSOR
    if (m_distance) {
        ESP_LOGCONFIG(TAG, "  Distance trigger range: %.1f - %.1f", m_distanceMin, m_distanceMax);
    }
#endif
#ifdef USE_STEPAWARE_MATRIX
    ESP_LOGCONFIG(TAG, "  LED matrix: %s", m_matrix ? "yes" : "no");
#endif
    ESP_LOGCONFIG(TAG, "  Battery inputs: low=%s level=%s", YESNO(m_batteryLowInput != nullptr),
                  YESNO(m_batteryLevel != nullptr));
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

void StepAware::handleMotionWake() {
    ESP_LOGI(TAG, "Motion wake: %s", m_sm.isInitialized() ? "handling" : "queued until setup");
    m_sm.handleMotionWake();
    if (m_sm.isInitialized()) {
        writeHazardOutput();
    }
}

void StepAware::clearDisplay() {
#ifdef USE_STEPAWARE_MATRIX
    if (m_matrix) {
        m_matrix->stopAnimation();
        m_matrix->clear();
    }
#endif
    m_sm.resetDisplayTracking();
}

void StepAware::prepareForSleep() {
    clearDisplay();
#ifdef USE_STEPAWARE_MATRIX
    if (m_matrix) {
        m_matrix->standby();
    }
#endif
    if (m_hazardOutput != nullptr) {
        m_hazardOutput->set_level(0.0f);
    }
}

void StepAware::showShutdown() {
    m_sm.showShutdown();
    writeHazardOutput();
    publishDerivedEntities();
}

void StepAware::setModeIndex(size_t index) {
    if (index >= MODE_COUNT) {
        return;
    }
    if (!m_sm.isInitialized()) {
        m_defaultMode = static_cast<uint8_t>(index);
        return;
    }
    auto mode = static_cast<StepStateMachine::OperatingMode>(index);
    if (mode == m_sm.getMode()) {
#ifdef USE_SELECT
        // No change: re-publish so HA reflects the actual state
        if (m_modeSelect) {
            m_modeSelect->publishAndSave(index);
        }
#endif
        return;
    }
    m_sm.setMode(mode);  // onModeChanged() publishes and persists
    writeHazardOutput();
}

void StepAware::setDirectionEnabled(bool enabled) {
    m_directionEnabled = enabled;
    m_sm.setDirectionFilterEnabled(enabled);
    ESP_LOGI(TAG, "Direction filter %s", ONOFF(enabled));
}

void StepAware::onNumberChanged(NumberKind kind, float value) {
    if (std::isnan(value)) {
        return;
    }
    switch (kind) {
        case NumberKind::WARNING_DURATION: {
            uint32_t ms = static_cast<uint32_t>(lroundf(value * 1000.0f));
            m_sm.setMotionWarningDurationMs(ms);
            ESP_LOGD(TAG, "Motion warning duration: %u ms", (unsigned) ms);
            break;
        }
        case NumberKind::LED_BRIGHTNESS: {
            float pct = clamp(value, 0.0f, 100.0f);
            uint8_t level = static_cast<uint8_t>(lroundf(pct * 2.55f));
            m_led.setOnBrightness(level);
            ESP_LOGD(TAG, "Hazard LED on level: %u/255", (unsigned) level);
            writeHazardOutput();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------

void StepAware::onModeChanged(uint8_t oldMode, uint8_t newMode) {
    ESP_LOGI(TAG, "Mode change: %s -> %s",
             StepStateMachine::getModeName(static_cast<StepStateMachine::OperatingMode>(oldMode)),
             StepStateMachine::getModeName(static_cast<StepStateMachine::OperatingMode>(newMode)));
#ifdef USE_SELECT
    if (m_modeSelect && !m_restoring) {
        m_modeSelect->publishAndSave(newMode);
    }
#endif
}

void StepAware::onWarningChanged(bool active) {
    if (active) {
        ESP_LOGI(TAG, "Hazard warning started (%u ms)", (unsigned) m_sm.getWarningRemainingMs());
    } else {
        ESP_LOGI(TAG, "Hazard warning ended");
    }
}

void StepAware::onRebootRequested() {
    if (m_rebootOnLongPress) {
        ESP_LOGW(TAG, "Button long press: rebooting in %u ms",
                 (unsigned) step_logic::REBOOT_FEEDBACK_DURATION_MS);
    } else {
        ESP_LOGI(TAG, "Button long press (reboot disabled)");
    }
}

void StepAware::onRebootDue() {
    if (m_rebootOnLongPress) {
        App.safe_reboot();
    }
}

void StepAware::onDormantRequested() {
    ESP_LOGW(TAG, "Button held %u ms: shutting down (dormant)",
             (unsigned) m_button.getLastPressDurationMs());
    m_dormantTrigger.trigger();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void StepAware::feedDisplayInputs(bool nearActive, bool farActive) {
    m_sm.setSensorStates(nearActive, farActive);
#ifdef USE_WIFI
    if (wifi::global_wifi_component != nullptr) {
        m_sm.setWifiConnected(wifi::global_wifi_component->is_connected());
    }
#endif
    if (m_batteryLowInput == nullptr && m_batteryLevel == nullptr) {
        return;
    }
    bool low = m_batteryLowInput != nullptr && m_batteryLowInput->has_state() &&
               m_batteryLowInput->state;
    int16_t percent = StepStateMachine::BATTERY_PERCENT_UNKNOWN;
#ifdef USE_SENSOR
    if (m_batteryLevel != nullptr && m_batteryLevel->has_state() &&
        !std::isnan(m_batteryLevel->state)) {
        percent = static_cast<int16_t>(lroundf(clamp(m_batteryLevel->state, 0.0f, 100.0f)));
    }
#endif
    m_sm.setBatteryState(low, percent);
}

bool StepAware::distanceDetected() const {
#ifdef USE_SENSOR
    if (m_distance == nullptr || !m_distance->has_state()) {
        return false;
    }
    float d = m_distance->state;
    return !std::isnan(d) && d >= m_distanceMin && d <= m_distanceMax;
#else
    return false;
#endif
}

const char* StepAware::stateName() const {
    if (m_sm.isShutdownPending()) {
        return "Shutting down";
    }
    if (m_sm.isRebootPending()) {
        return "Rebooting";
    }
    switch (m_sm.getMode()) {
        case StepStateMachine::OFF:
            return "Off";
        case StepStateMachine::CONTINUOUS_ON:
            return "Continuous";
        case StepStateMachine::MOTION_DETECT:
            if (m_sm.isWarningActive()) {
                return "Motion (warning)";
            }
            return m_sm.isSensorReady() ? "Motion (watching)" : "Motion (warming up)";
    }
    return "Unknown";
}

void StepAware::writeHazardOutput() {
    if (m_hazardOutput != nullptr && m_led.levelChanged()) {
        m_hazardOutput->set_level(static_cast<float>(m_led.getLevel()) / 255.0f);
    }
}

void StepAware::publishDerivedEntities() {
    bool approaching = m_dir.isApproaching();
    if (approaching != m_lastApproaching) {
        m_lastApproaching = approaching;
        if (m_approachingSensor) {
            m_approachingSensor->publish_state(approaching);
        }
    }
    bool leaving = m_dir.isLeaving();
    if (leaving != m_lastLeaving) {
        m_lastLeaving = leaving;
        if (m_leavingSensor) {
            m_leavingSensor->publish_state(leaving);
        }
    }
    const char* name = stateName();
    if (name != m_lastStateName) {
        m_lastStateName = name;
#ifdef USE_TEXT_SENSOR
        if (m_stateText) {
            m_stateText->publish_state(name);
        }
#endif
    }
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

#ifdef USE_SELECT
size_t StepAwareModeSelect::restoreIndex(size_t fallback) {
    if (!m_restoreValue) {
        return fallback;
    }
    m_pref = this->make_entity_preference<uint32_t>();
    uint32_t stored = 0;
    if (m_pref.load(&stored) && stored < MODE_COUNT) {
        ESP_LOGD(TAG, "Restored mode: %s", MODE_LABELS[stored]);
        return stored;
    }
    return fallback;
}

void StepAwareModeSelect::publishAndSave(size_t index) {
    if (index >= MODE_COUNT) {
        return;
    }
    this->publish_state(index);
    if (m_restoreValue) {
        uint32_t stored = static_cast<uint32_t>(index);
        m_pref.save(&stored);
    }
}

void StepAwareModeSelect::control(size_t index) { this->parent_->setModeIndex(index); }
#endif

#ifdef USE_NUMBER
float StepAwareNumber::restoreValue(float fallback) {
    float value = fallback;
    if (m_restoreValue) {
        m_pref = this->make_entity_preference<float>();
        float stored;
        if (m_pref.load(&stored) && !std::isnan(stored)) {
            value = stored;
        }
    }
    value = clamp(value, this->traits.get_min_value(), this->traits.get_max_value());
    this->publish_state(value);
    return value;
}

void StepAwareNumber::control(float value) {
    this->publish_state(value);
    if (m_restoreValue) {
        m_pref.save(&value);
    }
    this->parent_->onNumberChanged(m_kind, value);
}
#endif

#ifdef USE_SWITCH
void StepAwareDirectionSwitch::write_state(bool state) {
    this->parent_->setDirectionEnabled(state);
    this->publish_state(state);
}
#endif

}  // namespace stepaware
}  // namespace esphome
