#include "state_machine.h"
#include "sensor_manager.h"
#include "config_manager.h"
#include "logger.h"
#include "debug_logger.h"

// ─── Mode-indicator bitmaps (8×8, MSB = leftmost pixel) ───

// OFF: bold X — clearly distinct from the circle-based MOTION eye
static const uint8_t MODE_INDICATOR_OFF[] = {
    0b10000001,  // #      #
    0b01000010,  //  #    #
    0b00100100,  //   #  #
    0b00011000,  //    ##
    0b00011000,  //    ##
    0b00100100,  //   #  #
    0b01000010,  //  #    #
    0b10000001   // #      #
};

// CONTINUOUS_ON: filled circle — always-on beacon
static const uint8_t MODE_INDICATOR_CONTINUOUS[] = {
    0b00011000,  //    ##
    0b01111110,  //  ######
    0b11111111,  // ########
    0b11111111,  // ########
    0b11111111,  // ########
    0b11111111,  // ########
    0b01111110,  //  ######
    0b00011000   //    ##
};

// MOTION_DETECT: eye with pupil — sensing/watching
static const uint8_t MODE_INDICATOR_MOTION[] = {
    0b00000000,  //
    0b00111100,  //   ####
    0b01000010,  //  #    #
    0b10011001,  // #  ##  #
    0b10011001,  // #  ##  #
    0b01000010,  //  #    #
    0b00111100,  //   ####
    0b00000000   //
};

// REBOOT: open circle with arrowhead pointing up into gap (refresh symbol)
// Gap at top, arrow at left end points up-right toward the opening
static const uint8_t MODE_INDICATOR_REBOOT[] = {
    0b00000000,  //
    0b00010000,  //    # #         arrow tip
    0b00110000,  //   # ##         arrowhead
    0b01110010,  //  #             arrow base + right arc (2-px gap)
    0b01000010,  //  #    #        circle sides
    0b01000010,  //  #    #        circle sides
    0b00100100,  //   #  #         circle narrows
    0b00011000   //    ##          circle bottom
};

#define MODE_INDICATOR_DURATION_MS   2000  // How long indicator bitmap stays on-screen
#define REBOOT_FEEDBACK_DURATION_MS  2000  // How long reboot bitmap shows before restart

StateMachine::StateMachine(SensorManager* sensorManager,
                           HAL_LED* hazardLED,
                           HAL_LED* statusLED,
                           HAL_Button* button,
                           ConfigManager* config)
    : m_sensorManager(sensorManager)
    , m_hazardLED(hazardLED)
    , m_statusLED(statusLED)
    , m_button(button)
    , m_ledMatrix(nullptr)
    , m_directionDetector(nullptr)
    , m_config(config)
    , m_currentMode(OFF)
    , m_previousMode(OFF)
    , m_initialized(false)
    , m_warningActive(false)
    , m_warningStartTime(0)
    , m_warningDuration(MOTION_WARNING_DURATION_MS)
    , m_startTime(0)
    , m_motionEvents(0)
    , m_modeChanges(0)
    , m_lastMotionState(false)
    , m_sensorReady(false)
    , m_lastApproachingState(false)
    , m_modeIndicatorActive(false)
    , m_modeIndicatorEndTime(0)
    , m_rebootPending(false)
    , m_rebootTime(0)
    , m_lastMatrixWasAnimating(false)
{
    memset(m_lastSensorDisplayState, 0, sizeof(m_lastSensorDisplayState));
}

StateMachine::~StateMachine() {
    // Cleanup: turn off all LEDs
    if (m_hazardLED) {
        m_hazardLED->off();
    }
    if (m_statusLED) {
        m_statusLED->off();
    }
}

bool StateMachine::begin(OperatingMode initialMode) {
    if (m_initialized) {
        return true;
    }

    DEBUG_LOG_STATE("StateMachine: Initializing...");

    // Validate hardware pointers
    if (!m_sensorManager || !m_hazardLED || !m_statusLED || !m_button) {
        DEBUG_LOG_STATE("StateMachine: Hardware HAL not initialized");
        return false;
    }

    m_startTime = millis();
    m_currentMode = OFF;

    // Enter initial mode
    setMode(initialMode);

    m_initialized = true;

    DEBUG_LOG_STATE("StateMachine: Initialized in mode: %s", getModeName(initialMode));

    return true;
}

void StateMachine::update() {
    if (!m_initialized) {
        return;
    }

    // Update hardware HALs
    m_hazardLED->update();
    m_statusLED->update();
    m_button->update();

    // Update sensor manager
    m_sensorManager->update();

    // Check if all sensors are ready
    if (!m_sensorReady && m_sensorManager->allSensorsReady()) {
        m_sensorReady = true;
        DEBUG_LOG_STATE("StateMachine: All sensors ready (%d active)",
                 m_sensorManager->getActiveSensorCount());
    }

    // Check for button events
    if (m_button->hasEvent(HAL_Button::EVENT_CLICK)) {
        handleEvent(EVENT_BUTTON_PRESS);
    }

    // Check for long press (reboot trigger)
    if (m_button->hasEvent(HAL_Button::EVENT_LONG_PRESS)) {
        handleEvent(EVENT_BUTTON_LONG_PRESS);
    }

    // Mode-indicator expiry: clear matrix or start CONTINUOUS_ON arrow loop
    if (m_modeIndicatorActive && millis() >= m_modeIndicatorEndTime) {
        m_modeIndicatorActive = false;
        if (m_currentMode == CONTINUOUS_ON && m_ledMatrix && m_ledMatrix->isReady()) {
            m_ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT, 0); // 0 = loop forever
            LOG_INFO("CONTINUOUS_ON: started looping arrow animation on matrix");
        } else if (m_ledMatrix && m_ledMatrix->isReady()) {
            m_ledMatrix->clear();  // OFF / MOTION_DETECT: indicator done, go dark
        }
    }

    // Reboot countdown expiry
    if (m_rebootPending && millis() >= m_rebootTime) {
        LOG_INFO("Reboot: executing ESP.restart()");
#ifndef MOCK_HARDWARE
        ESP.restart();
#else
        m_rebootPending = false;
        LOG_INFO("Reboot: MOCK_HARDWARE — skipped actual restart");
#endif
    }

    // Check for motion events
    handleMotionDetection();

    // Process mode-specific logic
    processMode();

    // Update warning LED if active
    updateWarning();

    // Update status LED
    updateStatusLED();

    // Check for state transitions
    checkTransitions();

    // Update per-sensor status LEDs (runs after all animation/mode logic has settled)
    updateSensorStatusLEDs();
}

void StateMachine::handleEvent(SystemEvent event) {
    switch (event) {
        case EVENT_BUTTON_PRESS:
            DEBUG_LOG_STATE("StateMachine: Event BUTTON_PRESS");
            cycleMode();
            break;

        case EVENT_BUTTON_LONG_PRESS:
            LOG_INFO("Button: long press detected — reboot requested");
            DEBUG_LOG_STATE("StateMachine: Event BUTTON_LONG_PRESS");
            if (m_ledMatrix && m_ledMatrix->isReady()) {
                m_ledMatrix->stopAnimation();
                m_ledMatrix->drawBitmap(MODE_INDICATOR_REBOOT);
                LOG_INFO("Reboot: displaying reboot indicator on matrix");
            }
            m_rebootPending = true;
            m_rebootTime = millis() + REBOOT_FEEDBACK_DURATION_MS;
            break;

        case EVENT_MOTION_DETECTED:
            DEBUG_LOG_STATE("StateMachine: Event MOTION_DETECTED");
            DEBUG_LOG_STATE("Motion detected - event count: %u", m_motionEvents + 1);
            m_motionEvents++;

            // Trigger warning in appropriate modes
            if (m_currentMode == MOTION_DETECT && m_sensorReady) {
                // Use warning duration from config if available
                uint32_t duration = MOTION_WARNING_DURATION_MS;
                if (m_config) {
                    duration = m_config->getConfig().motionWarningDuration;
                }
                DEBUG_LOG_STATE("Triggering warning (duration: %u ms)", duration);
                triggerWarning(duration);
            }
            break;

        case EVENT_MOTION_CLEARED:
            DEBUG_LOG_STATE("StateMachine: Event MOTION_CLEARED");
            DEBUG_LOG_STATE("Motion cleared");
            // Motion cleared, warning will time out naturally
            break;

        case EVENT_TIMER_EXPIRED:
            DEBUG_LOG_STATE("StateMachine: Event TIMER_EXPIRED");
            stopWarning();
            break;

        case EVENT_BATTERY_LOW:
            DEBUG_LOG_STATE("StateMachine: Event BATTERY_LOW");
            // Future: Switch to LOW_BATTERY mode
            break;

        case EVENT_BATTERY_OK:
            DEBUG_LOG_STATE("StateMachine: Event BATTERY_OK");
            // Battery recovered
            break;

        case EVENT_USB_POWER_CONNECTED:
            DEBUG_LOG_STATE("StateMachine: Event USB_POWER_CONNECTED");
            // Future: Switch to USB_POWER mode
            break;

        case EVENT_USB_POWER_DISCONNECTED:
            DEBUG_LOG_STATE("StateMachine: Event USB_POWER_DISCONNECTED");
            // Exit USB power mode
            break;

        case EVENT_LIGHT_DARK:
        case EVENT_LIGHT_BRIGHT:
            // Future: Handle light sensing events
            break;

        case EVENT_NONE:
        default:
            break;
    }
}

StateMachine::OperatingMode StateMachine::getMode() {
    return m_currentMode;
}

void StateMachine::setMode(OperatingMode mode) {
    if (mode == m_currentMode) {
        return;  // Already in this mode
    }

    DEBUG_LOG_STATE("StateMachine: Mode change: %s -> %s",
             getModeName(m_currentMode), getModeName(mode));
    LOG_INFO("Mode change: %s -> %s", getModeName(m_currentMode), getModeName(mode));

    // Log state transition
    g_debugLogger.logStateTransition(
        getModeName(m_currentMode),
        getModeName(mode),
        "mode change requested"
    );

    // Exit current mode
    exitMode(m_currentMode);

    // Update state
    m_previousMode = m_currentMode;
    m_currentMode = mode;
    m_modeChanges++;

    // Enter new mode
    enterMode(mode);
}

void StateMachine::cycleMode() {
    // Cycle through Phase 1 modes: OFF -> CONTINUOUS_ON -> MOTION_DETECT -> OFF
    OperatingMode nextMode;

    switch (m_currentMode) {
        case OFF:
            nextMode = CONTINUOUS_ON;
            break;

        case CONTINUOUS_ON:
            nextMode = MOTION_DETECT;
            break;

        case MOTION_DETECT:
        default:
            nextMode = OFF;
            break;
    }

    setMode(nextMode);
}

const char* StateMachine::getModeName(OperatingMode mode) {
    switch (mode) {
        case OFF:                return "OFF";
        case CONTINUOUS_ON:      return "CONTINUOUS_ON";
        case MOTION_DETECT:      return "MOTION_DETECT";
        case MOTION_LIGHT:       return "MOTION_LIGHT";
        case NIGHTLIGHT_STEADY:  return "NIGHTLIGHT_STEADY";
        case NIGHTLIGHT_FLASH:   return "NIGHTLIGHT_FLASH";
        case NIGHTLIGHT_MOTION:  return "NIGHTLIGHT_MOTION";
        case LOW_BATTERY:        return "LOW_BATTERY";
        case USB_POWER:          return "USB_POWER";
        default:                 return "UNKNOWN";
    }
}

bool StateMachine::isWarningActive() {
    return m_warningActive;
}

void StateMachine::triggerWarning(uint32_t duration_ms) {
    m_warningActive = true;
    m_warningStartTime = millis();
    m_warningDuration = duration_ms;

    DEBUG_LOG_STATE("StateMachine: triggerWarning() called - m_ledMatrix=%p", (void*)m_ledMatrix);

    // Use LED matrix if available, otherwise fall back to hazard LED
    if (m_ledMatrix && m_ledMatrix->isReady()) {
        DEBUG_LOG_STATE("StateMachine: LED matrix available and ready, starting MOTION_ALERT animation");
        m_ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT, duration_ms);
        DEBUG_LOG_STATE("StateMachine: Warning triggered on matrix (%u ms)", duration_ms);
    } else {
        if (m_ledMatrix) {
            DEBUG_LOG_STATE("StateMachine: LED matrix exists but not ready (isReady=%d)", m_ledMatrix->isReady());
        } else {
            DEBUG_LOG_STATE("StateMachine: LED matrix is NULL, using hazard LED instead");
        }
        m_hazardLED->startPattern(HAL_LED::PATTERN_BLINK_WARNING, duration_ms);
        DEBUG_LOG_STATE("StateMachine: Warning triggered on LED (%u ms)", duration_ms);
    }
}

void StateMachine::stopWarning() {
    if (!m_warningActive) {
        return;
    }

    m_warningActive = false;

    // Stop both LED matrix and hazard LED
    if (m_ledMatrix) {
        m_ledMatrix->stopAnimation();
    }
    m_hazardLED->stopPattern();

    DEBUG_LOG_STATE("StateMachine: Warning stopped");
}

uint32_t StateMachine::getUptimeSeconds() {
    return (millis() - m_startTime) / 1000;
}

uint32_t StateMachine::getMotionEventCount() {
    return m_motionEvents;
}

uint32_t StateMachine::getModeChangeCount() {
    return m_modeChanges;
}

void StateMachine::enterMode(OperatingMode mode) {
    switch (mode) {
        case OFF:
            // Turn off all LEDs
            m_hazardLED->stopPattern();
            m_statusLED->setPattern(HAL_LED::PATTERN_OFF);
            // Show OFF indicator on matrix
            if (m_ledMatrix && m_ledMatrix->isReady()) {
                m_ledMatrix->stopAnimation();
                m_ledMatrix->drawBitmap(MODE_INDICATOR_OFF);
                m_modeIndicatorActive = true;
                m_modeIndicatorEndTime = millis() + MODE_INDICATOR_DURATION_MS;
                LOG_INFO("Mode indicator: showing OFF bitmap on matrix");
            }
            // Future: Enter deep sleep
            break;

        case CONTINUOUS_ON:
            // Start continuous hazard warning
            m_hazardLED->startPattern(HAL_LED::PATTERN_BLINK_WARNING, 0);  // 0 = infinite
            m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_SLOW);
            // Show CONTINUOUS_ON indicator; arrow loop starts after indicator expires (see update())
            if (m_ledMatrix && m_ledMatrix->isReady()) {
                m_ledMatrix->stopAnimation();
                m_ledMatrix->drawBitmap(MODE_INDICATOR_CONTINUOUS);
                m_modeIndicatorActive = true;
                m_modeIndicatorEndTime = millis() + MODE_INDICATOR_DURATION_MS;
                LOG_INFO("Mode indicator: showing CONTINUOUS_ON bitmap on matrix");
            }
            break;

        case MOTION_DETECT:
            // Wait for motion events
            m_hazardLED->stopPattern();
            m_hazardLED->off();  // Defensive: ensure LED is physically off
            m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);
            // Show MOTION_DETECT indicator on matrix
            if (m_ledMatrix && m_ledMatrix->isReady()) {
                m_ledMatrix->stopAnimation();
                m_ledMatrix->drawBitmap(MODE_INDICATOR_MOTION);
                m_modeIndicatorActive = true;
                m_modeIndicatorEndTime = millis() + MODE_INDICATOR_DURATION_MS;
                LOG_INFO("Mode indicator: showing MOTION_DETECT bitmap on matrix");
            }
            break;

        case MOTION_LIGHT:
        case NIGHTLIGHT_STEADY:
        case NIGHTLIGHT_FLASH:
        case NIGHTLIGHT_MOTION:
        case LOW_BATTERY:
        case USB_POWER:
            // Future modes (Phases 5-6)
            DEBUG_LOG_STATE("StateMachine: Mode not yet implemented");
            break;
    }
}

void StateMachine::exitMode(OperatingMode mode) {
    // Cancel any active mode indicator display
    m_modeIndicatorActive = false;

    // Stop any active warnings
    if (m_warningActive) {
        stopWarning();
    }

    // Mode-specific cleanup
    switch (mode) {
        case OFF:
            // Wake from sleep
            break;

        case CONTINUOUS_ON:
            // Stop continuous pattern
            m_hazardLED->stopPattern();
            // Stop matrix arrow loop if running
            if (m_ledMatrix && m_ledMatrix->isReady()) {
                m_ledMatrix->stopAnimation();
            }
            break;

        case MOTION_DETECT:
            // No special cleanup needed
            break;

        default:
            break;
    }
}

void StateMachine::processMode() {
    // Mode-specific processing
    switch (m_currentMode) {
        case OFF:
            // Minimal processing in OFF mode
            // Future: Sleep management
            break;

        case CONTINUOUS_ON:
            // LED pattern is handled by update()
            break;

        case MOTION_DETECT:
            // Motion detection handled by handleMotionDetection()
            break;

        default:
            break;
    }
}

void StateMachine::checkTransitions() {
    // Check for automatic mode transitions
    // (e.g., battery low, charging started, etc.)

    // Phase 1: No automatic transitions
    // Future phases will implement battery-based transitions
}

void StateMachine::updateStatusLED() {
    // Status LED provides visual feedback about current mode
    // Pattern is set in enterMode(), just needs update() to run
    // (update() is called in main update() method)
}

void StateMachine::handleMotionDetection() {
    // Check if direction detection is enabled with approaching-only mode
    bool useDirectionFilter = false;
    bool isApproaching = false;

    if (m_directionDetector && m_config) {
        const ConfigManager::DirectionDetectorConfig& dirCfg = m_config->getConfig().directionDetector;
        useDirectionFilter = dirCfg.enabled && dirCfg.triggerOnApproaching;

        if (useDirectionFilter) {
            isApproaching = m_directionDetector->isApproaching();
        }
    }

    if (useDirectionFilter) {
        // Direction detection mode: Trigger on approaching rising edge
        // This allows triggering even if sensor states have transitioned
        if (isApproaching && !m_lastApproachingState) {
            DEBUG_LOG_STATE("APPROACHING motion confirmed (dir confirmed: %s, confidence: %u ms)",
                          m_directionDetector->isDirectionConfirmed() ? "YES" : "NO",
                          m_directionDetector->getDirectionConfidenceMs());
            handleEvent(EVENT_MOTION_DETECTED);
        }

        // Detect falling edge (approaching ended)
        if (!isApproaching && m_lastApproachingState) {
            DEBUG_LOG_STATE("Approaching motion ended");
            handleEvent(EVENT_MOTION_CLEARED);
        }

        m_lastApproachingState = isApproaching;
    } else {
        // Standard motion detection mode: Use sensor manager state
        bool motionDetected = m_sensorManager->isMotionDetected();

        // Detect rising edge (motion started)
        if (motionDetected && !m_lastMotionState) {
            DEBUG_LOG_STATE("Motion detected");
            handleEvent(EVENT_MOTION_DETECTED);
        }

        // Detect falling edge (motion ended)
        if (!motionDetected && m_lastMotionState) {
            handleEvent(EVENT_MOTION_CLEARED);
        }

        m_lastMotionState = motionDetected;
    }
}

void StateMachine::updateWarning() {
    if (!m_warningActive) {
        return;
    }

    // Check if warning duration has expired
    if (m_warningDuration > 0) {
        if (millis() - m_warningStartTime >= m_warningDuration) {
            handleEvent(EVENT_TIMER_EXPIRED);
        }
    }
}

void StateMachine::setLEDMatrix(HAL_LEDMatrix_8x8* matrix) {
    m_ledMatrix = matrix;

    if (matrix) {
        DEBUG_LOG_STATE("StateMachine: LED matrix display enabled");
    } else {
        DEBUG_LOG_STATE("StateMachine: LED matrix display disabled");
    }
}

void StateMachine::setDirectionDetector(DirectionDetector* detector) {
    m_directionDetector = detector;

    if (detector) {
        DEBUG_LOG_STATE("StateMachine: Direction detector enabled (dual-PIR filtering)");
    } else {
        DEBUG_LOG_STATE("StateMachine: Direction detector disabled");
    }
}

void StateMachine::updateSensorStatusLEDs() {
    if (!m_ledMatrix || !m_ledMatrix->isReady() || !m_config || !m_initialized) {
        return;
    }

    // Matrix is "busy" when an animation, mode indicator, or reboot bitmap owns the display
    bool matrixBusy = m_ledMatrix->isAnimating() || m_modeIndicatorActive || m_rebootPending;

    // Animation just ended — matrix was cleared, force redraw of any active sensor indicators
    if (m_lastMatrixWasAnimating && !matrixBusy) {
        memset(m_lastSensorDisplayState, 0, sizeof(m_lastSensorDisplayState));
    }
    m_lastMatrixWasAnimating = matrixBusy;

    if (matrixBusy) {
        return;
    }

    const ConfigManager::Config& cfg = m_config->getConfig();

    for (uint8_t i = 0; i < 4; i++) {
        const ConfigManager::SensorSlotConfig& sensorCfg = cfg.sensors[i];

        // Only process active PIR sensors with status display enabled and a valid zone
        if (!sensorCfg.active || !sensorCfg.enabled ||
            sensorCfg.type != SENSOR_TYPE_PIR ||
            !sensorCfg.sensorStatusDisplay ||
            (sensorCfg.distanceZone != 1 && sensorCfg.distanceZone != 2)) {
            continue;
        }

        // Pixel positions: rightmost column, zone determines vertical position
        uint8_t x = 7;
        uint8_t y1, y2;
        if (sensorCfg.distanceZone == 1) {   // Near → bottom-right
            y1 = 6; y2 = 7;
        } else {                              // Far  → top-right
            y1 = 0; y2 = 1;
        }

        // Read current motion state from the sensor
        bool currentMotion = false;
        HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
        if (sensor && sensor->isReady()) {
            currentMotion = sensor->motionDetected();
        }

        // Only write to hardware when state actually changes (minimises I2C traffic)
        if (currentMotion != m_lastSensorDisplayState[i]) {
            m_ledMatrix->setPixel(x, y1, currentMotion);
            m_ledMatrix->setPixel(x, y2, currentMotion);
            m_lastSensorDisplayState[i] = currentMotion;
        }
    }
}
