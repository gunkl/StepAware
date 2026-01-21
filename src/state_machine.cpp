#include "state_machine.h"
#include "logger.h"

StateMachine::StateMachine(HAL_MotionSensor* motionSensor,
                           HAL_LED* hazardLED,
                           HAL_LED* statusLED,
                           HAL_Button* button)
    : m_motionSensor(motionSensor)
    , m_hazardLED(hazardLED)
    , m_statusLED(statusLED)
    , m_button(button)
    , m_ledMatrix(nullptr)
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
{
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

    LOG_INFO("StateMachine: Initializing...");

    // Validate hardware pointers
    if (!m_motionSensor || !m_hazardLED || !m_statusLED || !m_button) {
        LOG_ERROR("StateMachine: Hardware HAL not initialized");
        return false;
    }

    m_startTime = millis();
    m_currentMode = OFF;

    // Enter initial mode
    setMode(initialMode);

    m_initialized = true;

    LOG_INFO("StateMachine: Initialized in mode: %s", getModeName(initialMode));

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

    // Check if motion sensor is ready
    if (!m_sensorReady && m_motionSensor->isReady()) {
        m_sensorReady = true;
        LOG_INFO("StateMachine: %s warm-up complete",
                 m_motionSensor->getCapabilities().sensorTypeName);
    }

    // Check for button events
    if (m_button->hasEvent(HAL_Button::EVENT_CLICK)) {
        handleEvent(EVENT_BUTTON_PRESS);
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
}

void StateMachine::handleEvent(SystemEvent event) {
    switch (event) {
        case EVENT_BUTTON_PRESS:
            LOG_DEBUG("StateMachine: Event BUTTON_PRESS");
            cycleMode();
            break;

        case EVENT_MOTION_DETECTED:
            LOG_INFO("StateMachine: Event MOTION_DETECTED");
            m_motionEvents++;

            // Trigger warning in appropriate modes
            if (m_currentMode == MOTION_DETECT && m_sensorReady) {
                triggerWarning();
            }
            break;

        case EVENT_MOTION_CLEARED:
            LOG_INFO("StateMachine: Event MOTION_CLEARED");
            // Motion cleared, warning will time out naturally
            break;

        case EVENT_TIMER_EXPIRED:
            LOG_DEBUG("StateMachine: Event TIMER_EXPIRED");
            stopWarning();
            break;

        case EVENT_BATTERY_LOW:
            LOG_WARN("StateMachine: Event BATTERY_LOW");
            // Future: Switch to LOW_BATTERY mode
            break;

        case EVENT_BATTERY_OK:
            LOG_INFO("StateMachine: Event BATTERY_OK");
            // Battery recovered
            break;

        case EVENT_CHARGING_START:
            LOG_INFO("StateMachine: Event CHARGING_START");
            // Future: Switch to CHARGING mode
            break;

        case EVENT_CHARGING_STOP:
            LOG_INFO("StateMachine: Event CHARGING_STOP");
            // Exit charging mode
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

    LOG_INFO("StateMachine: Mode change: %s -> %s",
             getModeName(m_currentMode), getModeName(mode));

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
        case CHARGING:           return "CHARGING";
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

    // Use LED matrix if available, otherwise fall back to hazard LED
    if (m_ledMatrix && m_ledMatrix->isReady()) {
        m_ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT, duration_ms);
        LOG_INFO("StateMachine: Warning triggered on matrix (%u ms)", duration_ms);
    } else {
        m_hazardLED->startPattern(HAL_LED::PATTERN_BLINK_WARNING, duration_ms);
        LOG_INFO("StateMachine: Warning triggered on LED (%u ms)", duration_ms);
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

    LOG_INFO("StateMachine: Warning stopped");
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
            // Future: Enter deep sleep
            break;

        case CONTINUOUS_ON:
            // Start continuous hazard warning
            m_hazardLED->startPattern(HAL_LED::PATTERN_BLINK_WARNING, 0);  // 0 = infinite
            m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_SLOW);
            break;

        case MOTION_DETECT:
            // Wait for motion events
            m_hazardLED->stopPattern();
            m_hazardLED->off();  // Defensive: ensure LED is physically off
            m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);
            break;

        case MOTION_LIGHT:
        case NIGHTLIGHT_STEADY:
        case NIGHTLIGHT_FLASH:
        case NIGHTLIGHT_MOTION:
        case LOW_BATTERY:
        case CHARGING:
            // Future modes (Phases 5-6)
            LOG_WARN("StateMachine: Mode not yet implemented");
            break;
    }
}

void StateMachine::exitMode(OperatingMode mode) {
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
    bool motionDetected = m_motionSensor->motionDetected();

    // Detect rising edge (motion started)
    if (motionDetected && !m_lastMotionState) {
        handleEvent(EVENT_MOTION_DETECTED);
    }

    // Detect falling edge (motion ended)
    if (!motionDetected && m_lastMotionState) {
        handleEvent(EVENT_MOTION_CLEARED);
    }

    m_lastMotionState = motionDetected;
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
        LOG_INFO("StateMachine: LED matrix display enabled");
    } else {
        LOG_INFO("StateMachine: LED matrix display disabled");
    }
}
