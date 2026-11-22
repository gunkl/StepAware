/**
 * @file step_state_machine.h
 * @brief StepAware operating-mode state machine (port of StateMachine, Phase 1 modes).
 *
 * Dependency-free: time comes from an injected clock, hardware is reached through
 * StepLedPattern (hazard LED), StepButton (mode button), StepDirectionDetector and the
 * optional StepMatrixDisplay interface; notifications go out via StepStateMachineListener.
 *
 * Call order each loop (mirrors main.cpp): caller updates the direction detector with
 * the PIR levels, passes the per-sensor levels / WiFi / battery state to the setters
 * below, then calls update(motionDetected) with the fused "any sensor" state.
 *
 * The state machine also owns every "what does the LED matrix show when" decision of
 * the old firmware (mode indicators, reboot bitmap, post-warning WiFi/battery sequence,
 * per-sensor status pixels, first-connect WiFi emblem, battery-low notice), so all of it
 * is natively testable. See step_state_machine.cpp for deliberate behaviour changes.
 */
#ifndef STEPAWARE_CORE_STEP_STATE_MACHINE_H
#define STEPAWARE_CORE_STEP_STATE_MACHINE_H

#include <stdint.h>
#include "step_clock.h"
#include "step_button.h"
#include "step_direction_detector.h"
#include "step_led_pattern.h"

namespace step_logic {

const uint32_t MOTION_WARNING_DURATION_MS = 5000;  // ConfigManager default (Issue #9)
const uint32_t PIR_WARMUP_TIME_MS = 60000;
const uint32_t REBOOT_FEEDBACK_DURATION_MS = 2000;
/// Hold the button at least this long, then release = dormant (0 disables the gesture).
const uint32_t DORMANT_HOLD_MS = 5000;
/// The shutdown (power-off icon) display gives up after this if the device did not sleep
/// (power component refused, e.g. OTA in progress).
const uint32_t SHUTDOWN_DISPLAY_TIMEOUT_MS = 10000;
/// Countdown bar while holding: top row of the matrix (x = 0..7).
const uint8_t HOLD_PROGRESS_ROW = 0;

// Matrix UI timing (src/state_machine.cpp, include/config.h, src/main.cpp)
const uint32_t MODE_INDICATOR_DURATION_MS = 2000;   // mode bitmap on screen
const uint32_t MATRIX_POST_WARN_WIFI_MS = 2000;     // WiFi status after a motion alert
const uint32_t MATRIX_POST_WARN_BATTERY_MS = 2000;  // battery level after the WiFi status
const uint32_t MATRIX_WIFI_EMBLEM_MS = 2000;        // first WiFi connect per boot
const uint32_t BATTERY_NOTICE_DURATION_MS = 2000;   // showBatteryStatus() in main.cpp
const uint8_t BATTERY_LOW_DISPLAY_PERCENT = 30;     // below this the drain animation shows

// Mode / reboot indicator bitmaps (8 rows, bit 7 = leftmost pixel), verbatim from
// src/state_machine.cpp.
extern const uint8_t MODE_INDICATOR_OFF[8];
extern const uint8_t MODE_INDICATOR_CONTINUOUS[8];
extern const uint8_t MODE_INDICATOR_MOTION[8];
extern const uint8_t MODE_INDICATOR_REBOOT[8];
/// IEC power symbol: shown while a release would enter dormant, and before sleeping.
extern const uint8_t POWER_OFF_ICON[8];

// Per-sensor status pixels (updateSensorStatusLEDs): rightmost column, two pixels each.
const uint8_t SENSOR_PIXEL_X = 7;
const uint8_t SENSOR_PIXEL_NEAR_Y1 = 6;  // near PIR -> bottom-right
const uint8_t SENSOR_PIXEL_NEAR_Y2 = 7;
const uint8_t SENSOR_PIXEL_FAR_Y1 = 0;   // far PIR -> top-right
const uint8_t SENSOR_PIXEL_FAR_Y2 = 1;

/// LED-matrix surface the state machine needs (implemented by the glue).
/// The first four are required; the rest default to no-ops so a minimal display
/// (e.g. a test double) only implements what it cares about.
class StepMatrixDisplay {
public:
    virtual ~StepMatrixDisplay() {}
    /// Start the motion-alert (hazard arrow) animation; durationMs 0 = loop forever.
    virtual void startMotionAlert(uint32_t durationMs) = 0;
    virtual void stopAnimation() = 0;
    virtual void clear() = 0;
    virtual bool isAnimating() const = 0;

    /// False while the display hardware is not responding (draws are dropped).
    virtual bool isReady() const { return true; }
    /// Show a static 8x8 bitmap (bit 7 = leftmost pixel).
    virtual void drawBitmap(const uint8_t* /*frame*/) {}
    /// Set or clear one pixel of the current frame.
    virtual void setPixel(uint8_t /*x*/, uint8_t /*y*/, bool /*on*/) {}
    /// Static WiFi connected / disconnected bitmap.
    virtual void showWifiStatus(bool /*connected*/) {}
    /// Static battery level bitmap (0-100 %).
    virtual void showBatteryLevel(uint8_t /*percent*/) {}
    /// Battery-drain animation for durationMs.
    virtual void startBatteryLow(uint32_t /*durationMs*/) {}
    /// WiFi-connected emblem animation for durationMs.
    virtual void startWifiEmblem(uint32_t /*durationMs*/) {}
};

/// Notifications from the state machine (all optional).
class StepStateMachineListener {
public:
    virtual ~StepStateMachineListener() {}
    virtual void onModeChanged(uint8_t /*oldMode*/, uint8_t /*newMode*/) {}
    virtual void onWarningChanged(bool /*active*/) {}
    /// Long press seen; feedback period started (reboot follows after the delay).
    /// Also called when reboot-on-long-press is disabled (then nothing else happens).
    virtual void onRebootRequested() {}
    /// Feedback period elapsed: perform the reboot now.
    virtual void onRebootDue() {}
    /// Hold gesture released past the dormant threshold: the power-off icon is up; the
    /// listener should now put the device into dormant (button-only wake) deep sleep.
    virtual void onDormantRequested() {}
};

class StepStateMachine {
public:
    enum OperatingMode {
        OFF = 0,            ///< LEDs off (device may sleep, button wake only)
        CONTINUOUS_ON = 1,  ///< Always flashing hazard warning
        MOTION_DETECT = 2   ///< Flash on motion (default)
    };

    enum SystemEvent {
        EVENT_NONE,
        EVENT_BUTTON_PRESS,
        EVENT_MOTION_DETECTED,
        EVENT_MOTION_CLEARED,
        EVENT_TIMER_EXPIRED,
        EVENT_BUTTON_LONG_PRESS
    };

    /// Post-warning info sequence (MOTION_DETECT, after a warning ends).
    enum PostWarnPhase {
        POST_WARN_IDLE = 0,
        POST_WARN_WIFI,     ///< WiFi status bitmap
        POST_WARN_BATTERY   ///< battery level bitmap or drain animation
    };

    /// Battery percentage value meaning "unknown / no battery sensor".
    static const int16_t BATTERY_PERCENT_UNKNOWN = -1;

    explicit StepStateMachine(StepClockFn clock);

    // --- Wiring (all optional) ---------------------------------------------
    void setHazardLed(StepLedPattern* led) { m_hazardLed = led; }
    void setButton(StepButton* button) { m_button = button; }
    void setMatrix(StepMatrixDisplay* matrix) { m_matrix = matrix; }
    void setDirectionDetector(StepDirectionDetector* detector) { m_directionDetector = detector; }
    void setListener(StepStateMachineListener* listener) { m_listener = listener; }

    // --- Configuration -------------------------------------------------------
    void setMotionWarningDurationMs(uint32_t ms) { m_warningDurationCfg = ms; }
    uint32_t getMotionWarningDurationMs() const { return m_warningDurationCfg; }
    /// Direction filter (ConfigManager directionDetector.enabled && triggerOnApproaching).
    void setDirectionFilterEnabled(bool enabled) { m_directionFilterEnabled = enabled; }
    bool isDirectionFilterEnabled() const { return m_directionFilterEnabled; }
    /// PIR warm-up after begin(); motion does not trigger warnings until it elapses.
    void setSensorWarmupMs(uint32_t ms) { m_sensorWarmupMs = ms; }
    void setRebootFeedbackMs(uint32_t ms) { m_rebootFeedbackMs = ms; }
    /// false: releasing between the long-press and dormant thresholds does nothing
    /// (onRebootRequested() is still notified); with dormant also disabled, a long press
    /// shows nothing at all.
    void setRebootOnLongPress(bool enabled) { m_rebootOnLongPress = enabled; }
    /// Hold time for the dormant gesture (release after >= this). 0 disables it. Must be
    /// longer than the button's long-press time.
    void setDormantHoldMs(uint32_t ms) { m_dormantHoldMs = ms; }
    uint32_t getDormantHoldMs() const { return m_dormantHoldMs; }
    bool isDormantGestureEnabled() const;
    /// Also blink the hazard LED for motion warnings when a matrix is present
    /// (original: matrix replaces the LED for motion warnings).
    void setHazardLedWithMatrix(bool enabled) { m_hazardLedWithMatrix = enabled; }
    /// Per-sensor status pixels on the matrix (ConfigManager sensorStatusDisplay).
    void setSensorStatusPixelsEnabled(bool enabled) { m_sensorPixelsEnabled = enabled; }

    // --- Lifecycle -----------------------------------------------------------
    bool begin(OperatingMode initialMode = MOTION_DETECT);
    void update(bool motionDetected);
    void handleEvent(SystemEvent event);

    // --- Inputs for the matrix UI (call before update()) ---------------------
    /// Raw per-PIR levels, used for the status pixels.
    void setSensorStates(bool nearActive, bool farActive) {
        m_nearActive = nearActive;
        m_farActive = farActive;
    }
    /// WiFi link state. The first false->true per boot shows the WiFi emblem.
    void setWifiConnected(bool connected);
    bool isWifiConnected() const { return m_wifiConnected; }
    /// Battery state. `low` rising edge shows the battery-low notice (once per edge);
    /// percent 0-100 or BATTERY_PERCENT_UNKNOWN.
    void setBatteryState(bool low, int16_t percent);

    // --- Modes ---------------------------------------------------------------
    OperatingMode getMode() const { return m_currentMode; }
    void setMode(OperatingMode mode);
    void cycleMode();
    static const char* getModeName(OperatingMode mode);

    // --- Warnings ------------------------------------------------------------
    bool isWarningActive() const { return m_warningActive; }
    void triggerWarning(uint32_t durationMs);
    void triggerWarning() { triggerWarning(m_warningDurationCfg); }
    void stopWarning();
    uint32_t getWarningRemainingMs() const;

    // --- Hold gesture / shutdown -------------------------------------------------
    /// What releasing the button now would do (HOLD_NONE when no long press is held).
    enum HoldAction { HOLD_NONE = 0, HOLD_REBOOT, HOLD_CANCEL, HOLD_DORMANT };
    HoldAction getHoldAction() const;
    bool isHoldActive() const { return m_holdActive; }
    /// Lit pixels of the countdown bar (0-8).
    uint8_t getHoldProgress() const { return m_holdProgress; }
    /// Show the power-off icon and hold the display / stay busy until the device sleeps
    /// (or SHUTDOWN_DISPLAY_TIMEOUT_MS passes). Used for every dormant entry (gesture, HA).
    void showShutdown();
    void cancelShutdown();
    bool isShutdownPending() const { return m_shutdownPending; }

    /// Boot after a PIR wake: start a warning now (MOTION_DETECT only), ignoring warm-up.
    /// Safe to call before begin(); it is then applied at the end of begin().
    void handleMotionWake();

    // --- Power-policy queries ---------------------------------------------------
    /// True while sleep must not happen.
    bool isBusy() const;
    uint32_t getLastActivityMs() const { return m_lastActivityMs; }
    void recordActivity();
    /// Anything the core put on the matrix that is still showing (hasDisplayActivity()).
    bool hasDisplayActivity() const;
    /// The matrix was blanked behind the core's back (e.g. pre-sleep clear): forget the
    /// status pixels so they are redrawn (clearLEDDisplay()).
    void resetDisplayTracking();

    // --- Status --------------------------------------------------------------
    bool isInitialized() const { return m_initialized; }
    bool isSensorReady() const { return m_sensorReady; }
    bool isRebootPending() const { return m_rebootPending; }
    bool isModeIndicatorActive() const { return m_modeIndicatorActive; }
    PostWarnPhase getPostWarnPhase() const { return m_postWarnPhase; }
    bool isWifiEmblemShown() const { return m_wifiEmblemShown; }
    bool isBatteryNoticePending() const { return m_batteryNoticePending; }
    bool isNearPixelLit() const { return m_pixelNear; }
    bool isFarPixelLit() const { return m_pixelFar; }
    uint32_t getUptimeSeconds() const;
    uint32_t getMotionEventCount() const { return m_motionEvents; }
    uint32_t getModeChangeCount() const { return m_modeChanges; }

private:
    StepClockFn m_clock;
    StepLedPattern* m_hazardLed;
    StepButton* m_button;
    StepMatrixDisplay* m_matrix;
    StepDirectionDetector* m_directionDetector;
    StepStateMachineListener* m_listener;

    uint32_t m_warningDurationCfg;
    bool m_directionFilterEnabled;
    uint32_t m_sensorWarmupMs;
    uint32_t m_rebootFeedbackMs;
    bool m_rebootOnLongPress;
    bool m_hazardLedWithMatrix;
    bool m_sensorPixelsEnabled;
    uint32_t m_dormantHoldMs;

    OperatingMode m_currentMode;
    OperatingMode m_previousMode;
    bool m_initialized;

    bool m_warningActive;
    uint32_t m_warningStartTime;
    uint32_t m_warningDuration;

    uint32_t m_startTime;
    uint32_t m_motionEvents;
    uint32_t m_modeChanges;
    uint32_t m_lastActivityMs;

    bool m_lastMotionState;
    bool m_sensorReady;
    bool m_lastApproachingState;

    bool m_rebootPending;
    uint32_t m_rebootTime;
    bool m_pendingMotionWake;

    // Hold gesture / shutdown
    bool m_holdActive;
    HoldAction m_holdShown;  ///< icon currently drawn for the hold
    uint8_t m_holdProgress;
    bool m_shutdownPending;
    uint32_t m_shutdownStart;

    // Matrix UI
    bool m_modeIndicatorActive;
    uint32_t m_modeIndicatorStart;
    PostWarnPhase m_postWarnPhase;
    uint32_t m_postWarnPhaseStart;
    uint32_t m_postWarnPhaseDuration;
    bool m_nearActive;
    bool m_farActive;
    bool m_pixelNear;
    bool m_pixelFar;
    bool m_lastDisplayBusy;
    bool m_wifiConnected;
    bool m_wifiEmblemPending;
    bool m_wifiEmblemShown;
    bool m_batteryLow;
    int16_t m_batteryPercent;
    bool m_batteryNoticePending;

    void enterMode(OperatingMode mode);
    void exitMode(OperatingMode mode);
    void processMode();
    void handleMotionDetection(bool motionDetected);
    void updateWarning();
    void applyMotionWake();
    void stopWarningInternal(bool startPostWarn);
    void startHold();
    void updateHold();
    void finishHold(uint32_t heldMs);
    void startReboot();
    void updateShutdown();
    /// Hold icon / countdown / power-off icon owns the matrix and LED.
    bool gestureOwnsDisplay() const { return m_holdActive || m_shutdownPending; }

    bool matrixReady() const { return m_matrix && m_matrix->isReady(); }
    void showModeIndicator(const uint8_t* bitmap);
    void updateModeIndicator();
    void updatePostWarningPhase();
    void startPostWarnPhase(PostWarnPhase phase, uint32_t durationMs);
    void endPostWarnSequence();
    void updateWifiEmblem();
    void updateBatteryNotice();
    void showBatteryNotice();
    void updateSensorStatusPixels();
};

}  // namespace step_logic

#endif  // STEPAWARE_CORE_STEP_STATE_MACHINE_H
