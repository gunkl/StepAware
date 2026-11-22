#pragma once
//
// StepAware core decision logic - ESPHome glue.
//
// Owns the hazard LED output and runs the ported state machine / direction detector /
// button classifier from ./core (plain C++, unit-tested natively). This file only
// adapts ESPHome entities to that core.
//
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/output/float_output.h"

#include "core/step_button.h"
#include "core/step_direction_detector.h"
#include "core/step_led_pattern.h"
#include "core/step_state_machine.h"

#ifdef USE_STEPAWARE_MATRIX
#include "esphome/components/stepaware_matrix/stepaware_matrix.h"
#endif

namespace esphome {

namespace sensor {
class Sensor;
}
namespace text_sensor {
class TextSensor;
}

namespace stepaware {

class StepAwareModeSelect;
class StepAwareNumber;
class StepAwareDirectionSwitch;

enum class NumberKind : uint8_t {
    WARNING_DURATION,  ///< seconds
    LED_BRIGHTNESS     ///< percent of full PWM
};

#ifdef USE_STEPAWARE_MATRIX
/// Maps the core's matrix interface onto the stepaware_matrix component.
class MatrixAdapter : public step_logic::StepMatrixDisplay {
public:
    void setMatrix(stepaware_matrix::StepAwareMatrix* matrix) { m_matrix = matrix; }
    void startMotionAlert(uint32_t durationMs) override {
        m_matrix->startAnimation(stepaware_matrix::ANIM_MOTION_ALERT, durationMs);
    }
    void stopAnimation() override { m_matrix->stopAnimation(); }
    void clear() override { m_matrix->clear(); }
    bool isAnimating() const override { return m_matrix->isAnimating(); }
    bool isReady() const override { return m_matrix->isReady(); }
    void drawBitmap(const uint8_t* frame) override { m_matrix->drawFrame(frame); }
    void setPixel(uint8_t x, uint8_t y, bool on) override { m_matrix->setPixel(x, y, on); }
    void showWifiStatus(bool connected) override {
        if (connected) {
            m_matrix->showWifiConnected();
        } else {
            m_matrix->showWifiDisconnected();
        }
    }
    void showBatteryLevel(uint8_t percent) override { m_matrix->showBatteryBitmap(percent); }
    void startBatteryLow(uint32_t durationMs) override {
        m_matrix->startAnimation(stepaware_matrix::ANIM_BATTERY_LOW, durationMs);
    }
    void startWifiEmblem(uint32_t durationMs) override {
        m_matrix->startAnimation(stepaware_matrix::ANIM_WIFI_CONNECTED, durationMs);
    }

private:
    stepaware_matrix::StepAwareMatrix* m_matrix{nullptr};
};
#endif

class StepAware : public Component, public step_logic::StepStateMachineListener {
public:
    StepAware();

    // ---- ESPHome --------------------------------------------------------------------
    void setup() override;
    void loop() override;
    void dump_config() override;
    /// After hardware outputs/inputs and the matrix (DATA), before WiFi/API.
    float get_setup_priority() const override { return setup_priority::DATA - 10.0f; }

    // ---- Public API for the power component (names fixed) ------------------------------
    /// True while sleep must not happen (warning/animation active, CONTINUOUS_ON, button
    /// held, reboot pending, or a motion wake not yet processed).
    bool isBusy() const { return m_sm.isBusy(); }
    /// millis() of the last motion event, button event, mode change or motion wake.
    uint32_t getLastActivityMs() const { return m_sm.getLastActivityMs(); }
    /// Boot after a PIR wake: start a warning immediately (MOTION_DETECT only), bypassing
    /// the PIR warm-up. Safe to call before this component's setup().
    void handleMotionWake();

    // ---- Runtime API (lambdas, HA entities) --------------------------------------------
    uint8_t getMode() const { return static_cast<uint8_t>(m_sm.getMode()); }
    void setModeIndex(size_t index);
    void cycleMode() { m_sm.cycleMode(); }
    bool isWarningActive() const { return m_sm.isWarningActive(); }
    void triggerWarning() { m_sm.triggerWarning(); }
    void stopWarning() { m_sm.stopWarning(); }
    bool isApproaching() const { return m_dir.isApproaching(); }
    bool isLeaving() const { return m_dir.isLeaving(); }
    void setDirectionEnabled(bool enabled);
    void onNumberChanged(NumberKind kind, float value);
    step_logic::StepStateMachine& getStateMachine() { return m_sm; }
    /// Blank the matrix and forget the status pixels (use before sleep instead of
    /// stepaware_matrix.clear so the pixels are redrawn afterwards).
    void clearDisplay();
    /// Every deep-sleep entry (stepaware_power on_sleep): blank the matrix and put the
    /// HT16K33 into standby, hazard LED off.
    void prepareForSleep();
    /// Show the power-off icon until the device sleeps (stepaware_power on_shutdown).
    void showShutdown();
    /// Take the power-off icon down again (shutdown refused, e.g. OTA in progress).
    void cancelShutdown() { m_sm.cancelShutdown(); }
    /// The press that woke the device from dormant must not count as a click / long press
    /// (stepaware_power on_dormant_wake). windowMs: how long to wait for that press.
    void ignoreButtonUntilReleased(uint32_t windowMs = 1500) {
        m_button.suppressNextPress(windowMs);
    }
    /// Fired when the hold-to-dormant gesture is released (power-off icon already up).
    Trigger<> *getDormantTrigger() { return &m_dormantTrigger; }

    // ---- Code-generation setters -------------------------------------------------------
    void setMotionNear(binary_sensor::BinarySensor* bs) { m_motionNear = bs; }
    void setMotionFar(binary_sensor::BinarySensor* bs) { m_motionFar = bs; }
    void setButton(binary_sensor::BinarySensor* bs) { m_buttonInput = bs; }
    void setHazardOutput(output::FloatOutput* out) { m_hazardOutput = out; }
    void setDistanceSensor(sensor::Sensor* s) { m_distance = s; }
    void setDistanceRange(float minValue, float maxValue) {
        m_distanceMin = minValue;
        m_distanceMax = maxValue;
    }
#ifdef USE_STEPAWARE_MATRIX
    void setMatrix(stepaware_matrix::StepAwareMatrix* matrix);
#endif
    void setDefaultMode(uint8_t mode) { m_defaultMode = mode; }
    void setMotionWarningDurationMs(uint32_t ms) { m_initialWarningMs = ms; }
    void setLedBrightness(float fraction) { m_initialBrightness = fraction; }
    void setPirWarmupMs(uint32_t ms) { m_sm.setSensorWarmupMs(ms); }
    void setLongPressMs(uint32_t ms) { m_button.setLongPressMs(ms); }
    void setDormantHoldMs(uint32_t ms) { m_sm.setDormantHoldMs(ms); }
    void setRebootOnLongPress(bool enabled) {
        m_rebootOnLongPress = enabled;
        m_sm.setRebootOnLongPress(enabled);
    }
    void setBlinkIntervals(uint32_t fastMs, uint32_t slowMs, uint32_t warningMs) {
        m_led.setBlinkIntervals(fastMs, slowMs, warningMs);
    }
    void setHazardLedWithMatrix(bool enabled) { m_sm.setHazardLedWithMatrix(enabled); }
    void setDirectionTiming(uint32_t windowMs, uint32_t simultaneousMs, uint32_t timeoutMs);
    void setBatteryLowSensor(binary_sensor::BinarySensor* bs) { m_batteryLowInput = bs; }
    void setBatteryLevelSensor(sensor::Sensor* s) { m_batteryLevel = s; }
    void setSensorStatusPixels(bool enabled) { m_sm.setSensorStatusPixelsEnabled(enabled); }

    void setModeSelect(StepAwareModeSelect* s) { m_modeSelect = s; }
    void addNumber(StepAwareNumber* n);
    void setDirectionSwitch(StepAwareDirectionSwitch* s) { m_directionSwitch = s; }
    void setApproachingSensor(binary_sensor::BinarySensor* bs) { m_approachingSensor = bs; }
    void setLeavingSensor(binary_sensor::BinarySensor* bs) { m_leavingSensor = bs; }
    void setStateTextSensor(text_sensor::TextSensor* ts) { m_stateText = ts; }

protected:
    // StepStateMachineListener
    void onModeChanged(uint8_t oldMode, uint8_t newMode) override;
    void onWarningChanged(bool active) override;
    void onRebootRequested() override;
    void onRebootDue() override;
    void onDormantRequested() override;

    static uint32_t clockMs();
    bool distanceDetected() const;
    const char* stateName() const;
    void writeHazardOutput();
    void publishDerivedEntities();
    void feedDisplayInputs(bool nearActive, bool farActive);

    // Core logic
    step_logic::StepLedPattern m_led;
    step_logic::StepButton m_button;
    step_logic::StepDirectionDetector m_dir;
    step_logic::StepStateMachine m_sm;
#ifdef USE_STEPAWARE_MATRIX
    MatrixAdapter m_matrixAdapter;
    stepaware_matrix::StepAwareMatrix* m_matrix{nullptr};
#endif

    // Inputs / outputs
    binary_sensor::BinarySensor* m_motionNear{nullptr};
    binary_sensor::BinarySensor* m_motionFar{nullptr};
    binary_sensor::BinarySensor* m_buttonInput{nullptr};
    output::FloatOutput* m_hazardOutput{nullptr};
    sensor::Sensor* m_distance{nullptr};
    binary_sensor::BinarySensor* m_batteryLowInput{nullptr};
    sensor::Sensor* m_batteryLevel{nullptr};
    float m_distanceMin{30.0f};
    float m_distanceMax{200.0f};

    // Configuration
    uint8_t m_defaultMode{step_logic::StepStateMachine::MOTION_DETECT};
    uint32_t m_initialWarningMs{step_logic::MOTION_WARNING_DURATION_MS};
    float m_initialBrightness{1.0f};
    bool m_rebootOnLongPress{true};
    bool m_directionEnabled{true};

    // HA entities
    StepAwareModeSelect* m_modeSelect{nullptr};
    StepAwareNumber* m_warningNumber{nullptr};
    StepAwareNumber* m_brightnessNumber{nullptr};
    StepAwareDirectionSwitch* m_directionSwitch{nullptr};
    binary_sensor::BinarySensor* m_approachingSensor{nullptr};
    binary_sensor::BinarySensor* m_leavingSensor{nullptr};
    text_sensor::TextSensor* m_stateText{nullptr};

    Trigger<> m_dormantTrigger;

    // Publish-on-change tracking
    const char* m_lastStateName{nullptr};
    bool m_lastApproaching{false};
    bool m_lastLeaving{false};
    bool m_restoring{false};
};

}  // namespace stepaware
}  // namespace esphome
