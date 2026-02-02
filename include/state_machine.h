#ifndef STEPAWARE_STATE_MACHINE_H
#define STEPAWARE_STATE_MACHINE_H

#include <Arduino.h>
#include "config.h"
#include "hal_motion_sensor.h"
#include "hal_led.h"
#include "hal_button.h"
#include "hal_ledmatrix_8x8.h"
#include "direction_detector.h"

/**
 * @brief State Machine for StepAware Operating Modes
 *
 * This class manages the overall operating state of the StepAware system,
 * handling mode transitions, event processing, and LED control patterns.
 *
 * Operating Modes (Phase 1 - MVP):
 * - OFF: Deep sleep, button wake only
 * - CONTINUOUS_ON: Always flashing hazard warning
 * - MOTION_DETECT: Flash LED when motion detected (default)
 *
 * Future Modes (Phases 5-6):
 * - MOTION_LIGHT: Motion detection only in darkness
 * - NIGHTLIGHT_STEADY: Low brightness always on
 * - NIGHTLIGHT_FLASH: Low brightness flashing
 * - NIGHTLIGHT_MOTION: Low brightness on motion
 * - LOW_BATTERY: Special state for battery warning
 * - USB_POWER: USB power connected
 */
class StateMachine {
public:
    /**
     * @brief Operating modes
     */
    enum OperatingMode {
        OFF,                    ///< System off (deep sleep)
        CONTINUOUS_ON,          ///< Always flash hazard LED
        MOTION_DETECT,          ///< Flash on motion detection
        // Future modes (Phases 5-6)
        MOTION_LIGHT,           ///< Motion + darkness detection
        NIGHTLIGHT_STEADY,      ///< Low brightness steady
        NIGHTLIGHT_FLASH,       ///< Low brightness flashing
        NIGHTLIGHT_MOTION,      ///< Low brightness on motion
        LOW_BATTERY,            ///< Battery warning state
        USB_POWER               ///< USB power connected
    };

    /**
     * @brief System events
     */
    enum SystemEvent {
        EVENT_NONE,             ///< No event
        EVENT_BUTTON_PRESS,     ///< Mode button pressed
        EVENT_MOTION_DETECTED,  ///< Motion sensor triggered
        EVENT_MOTION_CLEARED,   ///< Motion no longer detected
        EVENT_TIMER_EXPIRED,    ///< Warning timer expired
        EVENT_BATTERY_LOW,      ///< Battery below threshold
        EVENT_BATTERY_OK,       ///< Battery above threshold
        EVENT_USB_POWER_CONNECTED,   ///< USB power connected
        EVENT_USB_POWER_DISCONNECTED,///< USB power disconnected
        EVENT_LIGHT_DARK,       ///< Ambient light dark
        EVENT_LIGHT_BRIGHT,     ///< Ambient light bright
        EVENT_BUTTON_LONG_PRESS ///< Mode button held >1 s (reboot)
    };

    /**
     * @brief Construct a new StateMachine object
     *
     * @param sensorManager Pointer to sensor manager (multi-sensor support, Issue #17)
     * @param hazardLED Pointer to hazard LED HAL
     * @param statusLED Pointer to status LED HAL
     * @param button Pointer to button HAL
     * @param config Pointer to config manager (for warning duration, etc.)
     */
    StateMachine(class SensorManager* sensorManager,
                 HAL_LED* hazardLED,
                 HAL_LED* statusLED,
                 HAL_Button* button,
                 class ConfigManager* config = nullptr);

    /**
     * @brief Destructor
     */
    ~StateMachine();

    /**
     * @brief Initialize the state machine
     *
     * Sets initial mode and initializes all hardware.
     *
     * @param initialMode Starting operating mode (default: MOTION_DETECT)
     * @return true if initialization successful
     */
    bool begin(OperatingMode initialMode = MOTION_DETECT);

    /**
     * @brief Update state machine (call in main loop)
     *
     * Processes events, updates LED patterns, and handles state transitions.
     * Must be called every loop iteration.
     */
    void update();

    /**
     * @brief Handle a system event
     *
     * @param event Event to process
     */
    void handleEvent(SystemEvent event);

    /**
     * @brief Get current operating mode
     *
     * @return OperatingMode Current mode
     */
    OperatingMode getMode();

    /**
     * @brief Set operating mode
     *
     * @param mode New operating mode
     */
    void setMode(OperatingMode mode);

    /**
     * @brief Cycle to next operating mode
     *
     * Called when button is pressed to switch modes.
     */
    void cycleMode();

    /**
     * @brief Get mode name as string
     *
     * @param mode Operating mode
     * @return const char* Mode name
     */
    static const char* getModeName(OperatingMode mode);

    /**
     * @brief Check if motion warning is active
     *
     * @return true if currently displaying hazard warning
     */
    bool isWarningActive();

    /**
     * @brief Manually trigger hazard warning
     *
     * @param duration_ms Warning duration (default: MOTION_WARNING_DURATION_MS)
     */
    void triggerWarning(uint32_t duration_ms = MOTION_WARNING_DURATION_MS);

    /**
     * @brief Stop active warning
     */
    void stopWarning();

    /**
     * @brief Get uptime in seconds
     *
     * @return uint32_t System uptime in seconds
     */
    uint32_t getUptimeSeconds();

    /**
     * @brief Get number of motion events
     *
     * @return uint32_t Total motion events detected
     */
    uint32_t getMotionEventCount();

    /**
     * @brief Get number of mode changes
     *
     * @return uint32_t Total mode changes
     */
    uint32_t getModeChangeCount();

    /**
     * @brief Set LED matrix display (Issue #12)
     *
     * Allows dynamic assignment of LED matrix display for enhanced visual feedback.
     * If set, matrix will be used for warnings instead of single LED.
     *
     * @param matrix Pointer to LED matrix HAL (nullptr to disable)
     */
    void setLEDMatrix(HAL_LEDMatrix_8x8* matrix);

    /**
     * @brief Get LED matrix display
     *
     * @return HAL_LEDMatrix_8x8* Pointer to matrix or nullptr if not set
     */
    HAL_LEDMatrix_8x8* getLEDMatrix() const { return m_ledMatrix; }

    /**
     * @brief Set direction detector (dual-PIR)
     *
     * Allows dynamic assignment of direction detector for directional motion filtering.
     * If set and enabled in config, state machine will only trigger on approaching motion.
     *
     * @param detector Pointer to DirectionDetector (nullptr to disable)
     */
    void setDirectionDetector(DirectionDetector* detector);

    /**
     * @brief Get direction detector
     *
     * @return DirectionDetector* Pointer to detector or nullptr if not set
     */
    DirectionDetector* getDirectionDetector() const { return m_directionDetector; }

private:
    // Hardware interfaces
    class SensorManager* m_sensorManager; ///< Sensor manager (multi-sensor support, Issue #17)
    HAL_LED* m_hazardLED;                 ///< Hazard warning LED
    HAL_LED* m_statusLED;                 ///< Status indicator LED
    HAL_Button* m_button;                 ///< Mode button
    HAL_LEDMatrix_8x8* m_ledMatrix;       ///< LED matrix display (Issue #12, optional)
    DirectionDetector* m_directionDetector; ///< Direction detector (dual-PIR, optional)
    class ConfigManager* m_config;        ///< Config manager (for runtime config access)

    // State
    OperatingMode m_currentMode; ///< Current operating mode
    OperatingMode m_previousMode;///< Previous mode (for restore)
    bool m_initialized;          ///< Initialization state

    // Warning control
    bool m_warningActive;        ///< Warning currently displayed
    uint32_t m_warningStartTime; ///< When warning started (ms)
    uint32_t m_warningDuration;  ///< Warning duration (ms)

    // Statistics
    uint32_t m_startTime;        ///< System start time (ms)
    uint32_t m_motionEvents;     ///< Total motion events
    uint32_t m_modeChanges;      ///< Total mode changes

    // Motion sensor state tracking
    bool m_lastMotionState;      ///< Previous motion sensor state
    bool m_sensorReady;          ///< Motion sensor ready (warmup complete)
    bool m_lastApproachingState; ///< Previous approaching state (for edge detection)

    // Mode indicator & reboot timing
    bool     m_modeIndicatorActive;      ///< Indicator bitmap currently on-screen
    uint32_t m_modeIndicatorEndTime;     ///< millis() when indicator should clear / transition
    bool     m_rebootPending;            ///< Reboot requested, waiting for feedback delay
    uint32_t m_rebootTime;               ///< millis() when ESP.restart() fires

    // Sensor status display tracking
    bool m_lastSensorDisplayState[4];    ///< Last-drawn motion state per sensor slot (avoids redundant I2C writes)
    bool m_lastMatrixWasAnimating;       ///< Detects busy→idle transition to force redraw

    /**
     * @brief Enter a new operating mode
     *
     * Performs all actions needed when entering a mode.
     *
     * @param mode Mode to enter
     */
    void enterMode(OperatingMode mode);

    /**
     * @brief Exit current operating mode
     *
     * Performs all cleanup needed when leaving a mode.
     *
     * @param mode Mode being exited
     */
    void exitMode(OperatingMode mode);

    /**
     * @brief Process mode-specific logic
     *
     * Called every update() to handle mode-specific behavior.
     */
    void processMode();

    /**
     * @brief Check for mode transitions
     *
     * Evaluates conditions that might trigger mode changes.
     */
    void checkTransitions();

    /**
     * @brief Update status LED based on current mode
     */
    void updateStatusLED();

    /**
     * @brief Handle motion detection in current mode
     */
    void handleMotionDetection();

    /**
     * @brief Update hazard warning LED
     *
     * Manages warning LED pattern and duration.
     */
    void updateWarning();

    /**
     * @brief Update per-sensor status LEDs on matrix
     *
     * Draws two LEDs per active PIR sensor when the matrix is idle.
     * Near sensors (distanceZone==1) use bottom-right pixels (7,6)+(7,7).
     * Far sensors  (distanceZone==2) use top-right pixels  (7,0)+(7,1).
     * Suppressed while any animation, mode indicator, or reboot bitmap is active.
     */
    void updateSensorStatusLEDs();
};

#endif // STEPAWARE_STATE_MACHINE_H
