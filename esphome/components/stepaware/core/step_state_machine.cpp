/**
 * @file step_state_machine.cpp
 * @brief Port of src/state_machine.cpp (OFF / CONTINUOUS_ON / MOTION_DETECT) including
 *        the LED-matrix UI decisions of StateMachine and main.cpp.
 *
 * Deliberate changes vs. the Arduino StateMachine:
 *  - The direction filter is applied whenever a detector is attached and the filter is
 *    enabled; main.cpp only attached the detector when the LED matrix was ready.
 *  - Hold gestures act on RELEASE (the Arduino firmware rebooted while still held):
 *    the long press (1 s) only starts the hold display - reboot icon plus a countdown bar
 *    in the top row, hazard LED fast blink. At the dormant threshold (5 s) the icon turns
 *    into the power-off symbol and the LED goes solid. Release before the threshold =
 *    reboot (delegated to the listener, onRebootDue(), after the feedback period);
 *    release after it = dormant (onDormantRequested(); the power-off icon stays up via
 *    showShutdown() until the device sleeps). A running warning, mode indicator or
 *    post-warning sequence is cancelled so nothing replaces the hold / reboot bitmaps; if
 *    the listener does not reboot, the bitmap is cleared. With reboot-on-long-press
 *    disabled, a release before the dormant threshold just cancels; with both disabled a
 *    long press shows nothing.
 *  - Status LED patterns are dropped (ESPHome's status_led owns GPIO2).
 *  - Activity (getLastActivityMs) is recorded on motion events, button events, mode
 *    changes, motion wakes and status pixels lighting; main.cpp/PowerManager recorded
 *    motion + web requests + status pixels.
 *  - isBusy() additionally counts "button held", "reboot pending" and any matrix UI
 *    activity (hasDisplayActivity()) as busy.
 *  - Leaving MOTION_DETECT during a warning does not start the post-warning sequence
 *    (the original's comment intended this, but exitMode() ran before the mode changed).
 *    A new warning, mode change or long press cancels a running sequence.
 *  - A new warning cancels the mode indicator (the original kept its timer running and
 *    blanked the arrow mid-animation when it expired).
 *  - Post-warning battery phase: skipped when no battery percentage is known and the
 *    battery is not low (the original showed a fake 50 % bitmap without a PowerManager).
 *  - WiFi emblem (main.cpp onWiFiConnected): skipped when an animation or warning is
 *    running, as before; while only a static mode indicator / post-warning bitmap /
 *    reboot bitmap is showing it waits for it to finish instead of overwriting it.
 *  - Battery-low notice (main.cpp onBatteryLowCallback): a deferred notice waits for
 *    the whole post-warning sequence, not only for the arrow animation. Without a
 *    matrix the hazard LED slow-blink fallback also waits for a running warning.
 */
#include "step_state_machine.h"

namespace step_logic {

// ─── Mode-indicator bitmaps (8×8, MSB = leftmost pixel), from src/state_machine.cpp ───

// OFF: bold X
const uint8_t MODE_INDICATOR_OFF[8] = {
    0b10000001, 0b01000010, 0b00100100, 0b00011000,
    0b00011000, 0b00100100, 0b01000010, 0b10000001,
};

// CONTINUOUS_ON: filled circle (always-on beacon)
const uint8_t MODE_INDICATOR_CONTINUOUS[8] = {
    0b00011000, 0b01111110, 0b11111111, 0b11111111,
    0b11111111, 0b11111111, 0b01111110, 0b00011000,
};

// MOTION_DETECT: eye with pupil
const uint8_t MODE_INDICATOR_MOTION[8] = {
    0b00000000, 0b00111100, 0b01000010, 0b10011001,
    0b10011001, 0b01000010, 0b00111100, 0b00000000,
};

// REBOOT: open circle with arrowhead (refresh symbol)
const uint8_t MODE_INDICATOR_REBOOT[8] = {
    0b00000000, 0b00010000, 0b00110000, 0b01110010,
    0b01000010, 0b01000010, 0b00100100, 0b00011000,
};

// POWER OFF: IEC 5009 power symbol (stroke through an open ring)
const uint8_t POWER_OFF_ICON[8] = {
    0b00011000, 0b01011010, 0b10011001, 0b10011001,
    0b10000001, 0b10000001, 0b01000010, 0b00111100,
};

StepStateMachine::StepStateMachine(StepClockFn clock)
    : m_clock(clock)
    , m_hazardLed(nullptr)
    , m_button(nullptr)
    , m_matrix(nullptr)
    , m_directionDetector(nullptr)
    , m_listener(nullptr)
    , m_warningDurationCfg(MOTION_WARNING_DURATION_MS)
    , m_directionFilterEnabled(true)
    , m_sensorWarmupMs(PIR_WARMUP_TIME_MS)
    , m_rebootFeedbackMs(REBOOT_FEEDBACK_DURATION_MS)
    , m_rebootOnLongPress(true)
    , m_hazardLedWithMatrix(false)
    , m_sensorPixelsEnabled(true)
    , m_dormantHoldMs(DORMANT_HOLD_MS)
    , m_currentMode(OFF)
    , m_previousMode(OFF)
    , m_initialized(false)
    , m_warningActive(false)
    , m_warningStartTime(0)
    , m_warningDuration(MOTION_WARNING_DURATION_MS)
    , m_startTime(0)
    , m_motionEvents(0)
    , m_modeChanges(0)
    , m_lastActivityMs(0)
    , m_lastMotionState(false)
    , m_sensorReady(false)
    , m_lastApproachingState(false)
    , m_rebootPending(false)
    , m_rebootTime(0)
    , m_pendingMotionWake(false)
    , m_holdActive(false)
    , m_holdShown(HOLD_NONE)
    , m_holdProgress(0)
    , m_shutdownPending(false)
    , m_shutdownStart(0)
    , m_modeIndicatorActive(false)
    , m_modeIndicatorStart(0)
    , m_postWarnPhase(POST_WARN_IDLE)
    , m_postWarnPhaseStart(0)
    , m_postWarnPhaseDuration(0)
    , m_nearActive(false)
    , m_farActive(false)
    , m_pixelNear(false)
    , m_pixelFar(false)
    , m_lastDisplayBusy(false)
    , m_wifiConnected(false)
    , m_wifiEmblemPending(false)
    , m_wifiEmblemShown(false)
    , m_batteryLow(false)
    , m_batteryPercent(BATTERY_PERCENT_UNKNOWN)
    , m_batteryNoticePending(false)
{
}

bool StepStateMachine::begin(OperatingMode initialMode) {
    if (m_initialized) {
        return true;
    }
    m_startTime = m_clock();
    m_lastActivityMs = m_startTime;
    m_currentMode = OFF;

    setMode(initialMode);
    m_initialized = true;

    if (m_pendingMotionWake) {
        m_pendingMotionWake = false;
        applyMotionWake();
    }
    return true;
}

void StepStateMachine::update(bool motionDetected) {
    if (!m_initialized) {
        return;
    }
    uint32_t now = m_clock();

    if (m_hazardLed) {
        m_hazardLed->update();
    }

    // Sensor warm-up (SensorManager::allSensorsReady())
    if (!m_sensorReady && now - m_startTime >= m_sensorWarmupMs) {
        m_sensorReady = true;
    }

    // Button events
    if (m_button) {
        m_button->update();
        if (m_button->hasEvent(StepButton::EVENT_CLICK)) {
            handleEvent(EVENT_BUTTON_PRESS);
        }
        if (m_button->hasEvent(StepButton::EVENT_LONG_PRESS)) {
            handleEvent(EVENT_BUTTON_LONG_PRESS);
        }
        if (m_button->hasEvent(StepButton::EVENT_LONG_RELEASE)) {
            finishHold(m_button->getLastPressDurationMs());
        }
    }
    updateHold();
    updateShutdown();

    // Mode-indicator expiry: go dark, or start CONTINUOUS_ON's arrow loop
    updateModeIndicator();

    // Reboot countdown
    if (m_rebootPending && now - m_rebootTime < 0x80000000UL) {
        m_rebootPending = false;
        if (m_listener) {
            m_listener->onRebootDue();
        }
        // Still running (listener did not reboot): take the reboot bitmap down.
        if (matrixReady()) {
            m_matrix->clear();
        }
        resetDisplayTracking();
    }

    handleMotionDetection(motionDetected);
    processMode();
    updateWarning();  // also advances the post-warning sequence

    // main.cpp: deferred battery notice, first-connect WiFi emblem
    updateBatteryNotice();
    updateWifiEmblem();

    // Per-sensor status pixels last, after all animation/mode logic has settled
    updateSensorStatusPixels();
}

void StepStateMachine::handleEvent(SystemEvent event) {
    switch (event) {
        case EVENT_BUTTON_PRESS:
            recordActivity();
            if (m_shutdownPending) {
                break;  // about to sleep: do not change (and persist) the mode now
            }
            cycleMode();
            break;

        case EVENT_BUTTON_LONG_PRESS:
            recordActivity();
            if (!m_rebootOnLongPress && !isDormantGestureEnabled()) {
                if (m_listener) {
                    m_listener->onRebootRequested();  // legacy notice, nothing shown
                }
                break;
            }
            startHold();  // the action is decided on release
            break;

        case EVENT_MOTION_DETECTED:
            m_motionEvents++;
            recordActivity();
            if (m_currentMode == MOTION_DETECT && m_sensorReady && !gestureOwnsDisplay()) {
                triggerWarning(m_warningDurationCfg);
            }
            break;

        case EVENT_MOTION_CLEARED:
            // Warning times out on its own
            break;

        case EVENT_TIMER_EXPIRED:
            stopWarning();
            break;

        case EVENT_NONE:
        default:
            break;
    }
}

void StepStateMachine::setMode(OperatingMode mode) {
    if (mode == m_currentMode) {
        return;
    }
    OperatingMode oldMode = m_currentMode;

    exitMode(m_currentMode);
    m_previousMode = m_currentMode;
    m_currentMode = mode;
    m_modeChanges++;
    recordActivity();
    enterMode(mode);

    if (m_listener) {
        m_listener->onModeChanged(static_cast<uint8_t>(oldMode), static_cast<uint8_t>(mode));
    }
}

void StepStateMachine::cycleMode() {
    // OFF -> CONTINUOUS_ON -> MOTION_DETECT -> OFF
    OperatingMode next;
    switch (m_currentMode) {
        case OFF:           next = CONTINUOUS_ON; break;
        case CONTINUOUS_ON: next = MOTION_DETECT; break;
        case MOTION_DETECT:
        default:            next = OFF; break;
    }
    setMode(next);
}

const char* StepStateMachine::getModeName(OperatingMode mode) {
    switch (mode) {
        case OFF:           return "OFF";
        case CONTINUOUS_ON: return "CONTINUOUS_ON";
        case MOTION_DETECT: return "MOTION_DETECT";
    }
    return "UNKNOWN";
}

void StepStateMachine::triggerWarning(uint32_t durationMs) {
    bool wasActive = m_warningActive;
    m_warningActive = true;
    m_warningStartTime = m_clock();
    m_warningDuration = durationMs;

    // The alert owns the display now
    m_modeIndicatorActive = false;
    m_postWarnPhase = POST_WARN_IDLE;

    // Matrix if present, otherwise the hazard LED (optionally both)
    if (m_matrix) {
        m_matrix->startMotionAlert(durationMs);
    }
    if (m_hazardLed && (!m_matrix || m_hazardLedWithMatrix)) {
        m_hazardLed->startPattern(StepLedPattern::PATTERN_BLINK_WARNING, durationMs);
    }

    if (!wasActive && m_listener) {
        m_listener->onWarningChanged(true);
    }
}

void StepStateMachine::stopWarning() {
    stopWarningInternal(true);
}

void StepStateMachine::stopWarningInternal(bool startPostWarn) {
    if (!m_warningActive) {
        return;
    }
    m_warningActive = false;

    if (m_matrix) {
        m_matrix->stopAnimation();
    }
    if (m_hazardLed) {
        m_hazardLed->stopPattern();
    }

    // Post-warning info sequence: WiFi status, then battery (MOTION_DETECT only)
    if (startPostWarn && m_currentMode == MOTION_DETECT && !m_rebootPending && matrixReady()) {
        startPostWarnPhase(POST_WARN_WIFI, MATRIX_POST_WARN_WIFI_MS);
        m_matrix->showWifiStatus(m_wifiConnected);
    }

    if (m_listener) {
        m_listener->onWarningChanged(false);
    }
}

uint32_t StepStateMachine::getWarningRemainingMs() const {
    if (!m_warningActive || m_warningDuration == 0) {
        return 0;
    }
    uint32_t elapsed = m_clock() - m_warningStartTime;
    return elapsed >= m_warningDuration ? 0 : m_warningDuration - elapsed;
}

void StepStateMachine::handleMotionWake() {
    if (!m_initialized) {
        m_pendingMotionWake = true;
        return;
    }
    applyMotionWake();
}

void StepStateMachine::applyMotionWake() {
    // main.cpp: after a sleep-wake reboot the PIRs are in warm-up and the wake edge is
    // lost, so the warning is fired directly (MOTION_DETECT only) and activity recorded.
    if (m_currentMode == MOTION_DETECT && !gestureOwnsDisplay()) {
        triggerWarning(m_warningDurationCfg);
    }
    recordActivity();
}

// ---------------------------------------------------------------------------
// Hold gesture (reboot / dormant on release) and shutdown display
// ---------------------------------------------------------------------------

bool StepStateMachine::isDormantGestureEnabled() const {
    if (m_dormantHoldMs == 0) {
        return false;
    }
    return m_button == nullptr || m_dormantHoldMs > m_button->getLongPressMs();
}

StepStateMachine::HoldAction StepStateMachine::getHoldAction() const {
    if (!m_holdActive) {
        return HOLD_NONE;
    }
    uint32_t held = m_button ? m_button->getPressedDuration() : 0;
    if (isDormantGestureEnabled() && held >= m_dormantHoldMs) {
        return HOLD_DORMANT;
    }
    return m_rebootOnLongPress ? HOLD_REBOOT : HOLD_CANCEL;
}

void StepStateMachine::startHold() {
    m_holdActive = true;
    m_holdShown = HOLD_NONE;
    m_holdProgress = 0;
    // The hold display owns the matrix and LED now.
    m_modeIndicatorActive = false;
    m_postWarnPhase = POST_WARN_IDLE;
    if (m_warningActive) {
        stopWarningInternal(false);
    }
    if (m_matrix) {
        m_matrix->stopAnimation();
    }
    updateHold();
}

void StepStateMachine::updateHold() {
    if (!m_holdActive || !m_button || !m_button->isPressed()) {
        return;
    }
    HoldAction action = getHoldAction();
    if (action != m_holdShown) {
        m_holdShown = action;
        m_holdProgress = 0;
        if (matrixReady()) {
            if (action == HOLD_DORMANT) {
                m_matrix->drawBitmap(POWER_OFF_ICON);
            } else if (action == HOLD_REBOOT) {
                m_matrix->drawBitmap(MODE_INDICATOR_REBOOT);
            } else {
                m_matrix->clear();
            }
        }
        if (m_hazardLed) {
            StepLedPattern::Pattern p = StepLedPattern::PATTERN_BLINK_FAST;
            if (action == HOLD_DORMANT) {
                p = StepLedPattern::PATTERN_ON;
            } else if (action == HOLD_CANCEL) {
                p = StepLedPattern::PATTERN_BLINK_SLOW;
            }
            m_hazardLed->startPattern(p, 0);
        }
    }

    // Countdown to the dormant threshold: one more top-row pixel per eighth of the way.
    if (action == HOLD_DORMANT || !isDormantGestureEnabled() || !matrixReady()) {
        return;
    }
    uint32_t longPress = m_button->getLongPressMs();
    uint32_t held = m_button->getPressedDuration();
    uint32_t span = m_dormantHoldMs - longPress;
    uint32_t into = held > longPress ? held - longPress : 0;
    uint32_t target = 1 + static_cast<uint32_t>((static_cast<uint64_t>(into) * 8) / span);
    if (target > 8) {
        target = 8;
    }
    while (m_holdProgress < target) {
        m_matrix->setPixel(m_holdProgress, HOLD_PROGRESS_ROW, true);
        m_holdProgress++;
    }
}

void StepStateMachine::finishHold(uint32_t heldMs) {
    if (!m_holdActive) {
        return;  // long press without a gesture (both disabled): already notified
    }
    m_holdActive = false;
    m_holdShown = HOLD_NONE;
    m_holdProgress = 0;

    if (isDormantGestureEnabled() && heldMs >= m_dormantHoldMs) {
        showShutdown();
        if (m_listener) {
            m_listener->onDormantRequested();
        }
        return;
    }
    if (m_rebootOnLongPress) {
        startReboot();
        return;
    }
    // Reboot disabled, released before the dormant threshold: cancel.
    if (m_hazardLed) {
        m_hazardLed->stopPattern();
    }
    if (matrixReady()) {
        m_matrix->clear();
    }
    resetDisplayTracking();
    if (m_listener) {
        m_listener->onRebootRequested();  // "reboot disabled" notice
    }
}

void StepStateMachine::startReboot() {
    m_rebootPending = true;
    m_rebootTime = m_clock() + m_rebootFeedbackMs;
    m_modeIndicatorActive = false;
    m_postWarnPhase = POST_WARN_IDLE;
    if (m_warningActive) {
        stopWarningInternal(false);
    }
    if (m_hazardLed) {
        m_hazardLed->stopPattern();
    }
    if (matrixReady()) {
        m_matrix->stopAnimation();
        m_matrix->drawBitmap(MODE_INDICATOR_REBOOT);  // also drops the countdown bar
    }
    if (m_listener) {
        m_listener->onRebootRequested();
    }
}

void StepStateMachine::showShutdown() {
    m_shutdownPending = true;
    m_shutdownStart = m_clock();
    m_holdActive = false;
    m_holdShown = HOLD_NONE;
    m_modeIndicatorActive = false;
    m_postWarnPhase = POST_WARN_IDLE;
    if (m_warningActive) {
        stopWarningInternal(false);
    }
    if (m_hazardLed) {
        m_hazardLed->stopPattern();
    }
    if (matrixReady()) {
        m_matrix->stopAnimation();
        m_matrix->drawBitmap(POWER_OFF_ICON);
    }
    recordActivity();
}

void StepStateMachine::cancelShutdown() {
    if (!m_shutdownPending) {
        return;
    }
    m_shutdownPending = false;
    if (matrixReady()) {
        m_matrix->clear();
    }
    resetDisplayTracking();
    recordActivity();
}

void StepStateMachine::updateShutdown() {
    if (m_shutdownPending && m_clock() - m_shutdownStart >= SHUTDOWN_DISPLAY_TIMEOUT_MS) {
        cancelShutdown();  // still awake: the power component did not sleep
    }
}

bool StepStateMachine::isBusy() const {
    if (m_pendingMotionWake) return true;
    if (!m_initialized) return false;
    if (m_currentMode == CONTINUOUS_ON) return true;
    if (m_warningActive) return true;
    if (m_rebootPending) return true;
    if (gestureOwnsDisplay()) return true;
    if (hasDisplayActivity()) return true;
    if (m_button && m_button->isPressed()) return true;
    return false;
}

bool StepStateMachine::hasDisplayActivity() const {
    if (!m_matrix) return false;
    if (m_matrix->isAnimating()) return true;
    if (m_warningActive) return true;
    if (m_postWarnPhase != POST_WARN_IDLE) return true;
    if (m_modeIndicatorActive) return true;
    if (m_rebootPending) return true;
    if (gestureOwnsDisplay()) return true;
    return m_pixelNear || m_pixelFar;
}

void StepStateMachine::resetDisplayTracking() {
    m_pixelNear = false;
    m_pixelFar = false;
    m_lastDisplayBusy = false;
}

void StepStateMachine::recordActivity() {
    m_lastActivityMs = m_clock();
}

uint32_t StepStateMachine::getUptimeSeconds() const {
    return (m_clock() - m_startTime) / 1000;
}

void StepStateMachine::enterMode(OperatingMode mode) {
    switch (mode) {
        case OFF:
            if (m_hazardLed) {
                m_hazardLed->stopPattern();
            }
            showModeIndicator(MODE_INDICATOR_OFF);
            break;

        case CONTINUOUS_ON:
            if (m_hazardLed) {
                m_hazardLed->startPattern(StepLedPattern::PATTERN_BLINK_WARNING, 0);
            }
            // Indicator first; the arrow loop starts when it expires (updateModeIndicator).
            showModeIndicator(MODE_INDICATOR_CONTINUOUS);
            if (m_matrix && !m_modeIndicatorActive && !gestureOwnsDisplay()) {
                m_matrix->startMotionAlert(0);  // display not ready: self-heal retries
            }
            break;

        case MOTION_DETECT:
            if (m_hazardLed) {
                m_hazardLed->stopPattern();
                m_hazardLed->off();
            }
            showModeIndicator(MODE_INDICATOR_MOTION);
            break;
    }
}

void StepStateMachine::exitMode(OperatingMode mode) {
    m_modeIndicatorActive = false;
    m_postWarnPhase = POST_WARN_IDLE;
    if (m_warningActive) {
        stopWarningInternal(false);
    }
    if (mode == CONTINUOUS_ON) {
        if (m_hazardLed) {
            m_hazardLed->stopPattern();
        }
        if (m_matrix) {
            m_matrix->stopAnimation();
        }
    }
}

void StepStateMachine::processMode() {
    if (m_currentMode == CONTINUOUS_ON) {
        // Self-heal: restart the looping alert if something else ended it
        // (e.g. the battery notice finished), but not over the indicator / reboot bitmap /
        // hold gesture / power-off icon.
        if (gestureOwnsDisplay()) {
            return;
        }
        if (m_matrix && !m_modeIndicatorActive && !m_rebootPending && !m_matrix->isAnimating()) {
            m_matrix->startMotionAlert(0);
        }
        if (m_hazardLed && !m_hazardLed->isPatternActive()) {
            m_hazardLed->startPattern(StepLedPattern::PATTERN_BLINK_WARNING, 0);
        }
    }
}

void StepStateMachine::handleMotionDetection(bool motionDetected) {
    bool useDirectionFilter = m_directionDetector && m_directionFilterEnabled;

    if (useDirectionFilter) {
        bool isApproaching = m_directionDetector->isApproaching();
        if (isApproaching && !m_lastApproachingState) {
            handleEvent(EVENT_MOTION_DETECTED);
        }
        if (!isApproaching && m_lastApproachingState) {
            handleEvent(EVENT_MOTION_CLEARED);
        }
        m_lastApproachingState = isApproaching;
        // Keep the plain edge tracker current so toggling the filter off later
        // does not produce a spurious rising edge.
        m_lastMotionState = motionDetected;
    } else {
        if (motionDetected && !m_lastMotionState) {
            handleEvent(EVENT_MOTION_DETECTED);
        }
        if (!motionDetected && m_lastMotionState) {
            handleEvent(EVENT_MOTION_CLEARED);
        }
        m_lastMotionState = motionDetected;
        m_lastApproachingState = m_directionDetector ? m_directionDetector->isApproaching()
                                                     : false;
    }
}

void StepStateMachine::updateWarning() {
    if (!m_warningActive) {
        updatePostWarningPhase();
        return;
    }
    if (m_warningDuration > 0) {
        if (m_clock() - m_warningStartTime >= m_warningDuration) {
            handleEvent(EVENT_TIMER_EXPIRED);
        }
    }
}

// ---------------------------------------------------------------------------
// Matrix UI
// ---------------------------------------------------------------------------

void StepStateMachine::showModeIndicator(const uint8_t* bitmap) {
    m_modeIndicatorActive = false;
    if (!matrixReady() || gestureOwnsDisplay()) {
        return;
    }
    m_matrix->stopAnimation();
    m_matrix->drawBitmap(bitmap);
    m_modeIndicatorActive = true;
    m_modeIndicatorStart = m_clock();
}

void StepStateMachine::updateModeIndicator() {
    if (!m_modeIndicatorActive || m_clock() - m_modeIndicatorStart < MODE_INDICATOR_DURATION_MS) {
        return;
    }
    m_modeIndicatorActive = false;
    if (!matrixReady()) {
        return;
    }
    if (m_currentMode == CONTINUOUS_ON) {
        m_matrix->startMotionAlert(0);  // loop forever
    } else {
        m_matrix->clear();  // OFF / MOTION_DETECT: indicator done, go dark
    }
}

void StepStateMachine::startPostWarnPhase(PostWarnPhase phase, uint32_t durationMs) {
    m_postWarnPhase = phase;
    m_postWarnPhaseStart = m_clock();
    m_postWarnPhaseDuration = durationMs;
}

void StepStateMachine::endPostWarnSequence() {
    m_postWarnPhase = POST_WARN_IDLE;
    if (m_matrix->isAnimating()) {
        m_matrix->stopAnimation();  // clears the display too
    } else {
        m_matrix->clear();
    }
}

void StepStateMachine::updatePostWarningPhase() {
    if (m_postWarnPhase == POST_WARN_IDLE) {
        return;
    }
    if (!matrixReady()) {
        m_postWarnPhase = POST_WARN_IDLE;
        return;
    }
    if (m_clock() - m_postWarnPhaseStart < m_postWarnPhaseDuration) {
        return;  // phase still showing
    }

    if (m_postWarnPhase == POST_WARN_WIFI) {
        // WIFI -> BATTERY
        bool known = m_batteryPercent >= 0;
        if (!known && !m_batteryLow) {
            endPostWarnSequence();  // nothing meaningful to show
            return;
        }
        startPostWarnPhase(POST_WARN_BATTERY, MATRIX_POST_WARN_BATTERY_MS);
        if (m_batteryLow && !known) {
            m_matrix->startBatteryLow(MATRIX_POST_WARN_BATTERY_MS);
        } else if (m_batteryPercent < BATTERY_LOW_DISPLAY_PERCENT) {
            m_matrix->startBatteryLow(MATRIX_POST_WARN_BATTERY_MS);  // animated drain
        } else {
            m_matrix->showBatteryLevel(static_cast<uint8_t>(m_batteryPercent));
        }
    } else {
        // BATTERY -> IDLE
        endPostWarnSequence();
    }
}

void StepStateMachine::setWifiConnected(bool connected) {
    if (connected && !m_wifiConnected && !m_wifiEmblemShown) {
        m_wifiEmblemPending = true;
    }
    m_wifiConnected = connected;
}

void StepStateMachine::updateWifiEmblem() {
    if (!m_wifiEmblemPending) {
        return;
    }
    if (!matrixReady()) {
        // Nothing to show it on: treat as shown so it never pops up late.
        m_wifiEmblemPending = false;
        m_wifiEmblemShown = true;
        return;
    }
    if (m_matrix->isAnimating() || m_warningActive) {
        // main.cpp: don't interrupt an active animation; mark as shown anyway.
        m_wifiEmblemPending = false;
        m_wifiEmblemShown = true;
        return;
    }
    if (m_modeIndicatorActive || m_rebootPending || m_postWarnPhase != POST_WARN_IDLE ||
        gestureOwnsDisplay()) {
        return;  // a static bitmap is up; wait for it to finish
    }
    m_wifiEmblemPending = false;
    m_wifiEmblemShown = true;
    m_matrix->startWifiEmblem(MATRIX_WIFI_EMBLEM_MS);
}

void StepStateMachine::setBatteryState(bool low, int16_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    m_batteryPercent = percent < 0 ? BATTERY_PERCENT_UNKNOWN : percent;
    if (low && !m_batteryLow) {
        // showBatteryStatus(): the notice only shows below 30 % (unknown: show it).
        if (m_batteryPercent < 0 || m_batteryPercent < BATTERY_LOW_DISPLAY_PERCENT) {
            m_batteryNoticePending = true;
        }
    }
    m_batteryLow = low;
}

void StepStateMachine::showBatteryNotice() {
    m_batteryNoticePending = false;
    if (matrixReady()) {
        m_matrix->startBatteryLow(BATTERY_NOTICE_DURATION_MS);
    } else if (m_hazardLed) {
        // No matrix: slow blink (CONTINUOUS_ON's pattern self-heals afterwards)
        m_hazardLed->startPattern(StepLedPattern::PATTERN_BLINK_SLOW, BATTERY_NOTICE_DURATION_MS);
    }
}

void StepStateMachine::updateBatteryNotice() {
    if (!m_batteryNoticePending) {
        return;
    }
    if (m_rebootPending) {
        m_batteryNoticePending = false;
        return;
    }
    if (gestureOwnsDisplay()) {
        return;  // hold / power-off icon up: wait (dropped if the device sleeps)
    }
    if (!matrixReady()) {
        // Hazard LED fallback: don't cut a running warning short
        if (!m_warningActive) {
            showBatteryNotice();
        }
        return;
    }
    if (m_currentMode == CONTINUOUS_ON) {
        // Shown over the arrow loop directly; the loop self-heals after it.
        if (!m_modeIndicatorActive) {
            showBatteryNotice();
        }
        return;
    }
    // Other modes: defer behind the alert, indicator and post-warning sequence.
    if (m_warningActive || m_modeIndicatorActive || m_postWarnPhase != POST_WARN_IDLE ||
        m_matrix->isAnimating()) {
        return;
    }
    showBatteryNotice();
}

void StepStateMachine::updateSensorStatusPixels() {
    if (!m_matrix || !m_sensorPixelsEnabled) {
        return;
    }
    // Busy: an animation, mode indicator, reboot bitmap or post-warning bitmap owns it
    bool busy = !m_matrix->isReady() || m_matrix->isAnimating() || m_modeIndicatorActive ||
                m_rebootPending || m_postWarnPhase != POST_WARN_IDLE || gestureOwnsDisplay();
    if (busy || m_lastDisplayBusy) {
        // Whatever owns the display replaced the pixels (and clears when done):
        // redraw from scratch once it is idle again.
        m_pixelNear = false;
        m_pixelFar = false;
    }
    m_lastDisplayBusy = busy;
    if (busy) {
        return;
    }

    // Sensors only report once warmed up (HAL_PIR::isReady())
    bool nearOn = m_sensorReady && m_nearActive;
    bool farOn = m_sensorReady && m_farActive;
    if (nearOn != m_pixelNear) {
        m_matrix->setPixel(SENSOR_PIXEL_X, SENSOR_PIXEL_NEAR_Y1, nearOn);
        m_matrix->setPixel(SENSOR_PIXEL_X, SENSOR_PIXEL_NEAR_Y2, nearOn);
        m_pixelNear = nearOn;
        if (nearOn) {
            recordActivity();
        }
    }
    if (farOn != m_pixelFar) {
        m_matrix->setPixel(SENSOR_PIXEL_X, SENSOR_PIXEL_FAR_Y1, farOn);
        m_matrix->setPixel(SENSOR_PIXEL_X, SENSOR_PIXEL_FAR_Y2, farOn);
        m_pixelFar = farOn;
        if (farOn) {
            recordActivity();
        }
    }
}

}  // namespace step_logic
