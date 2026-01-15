#ifndef STEPAWARE_STATE_MACHINE_H
#define STEPAWARE_STATE_MACHINE_H

#include <Arduino.h>
#include "config.h"
#include "hal_motion_sensor.h"
#include "hal_led.h"
#include "hal_button.h"

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
 * - CHARGING: Battery charging indication
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
        CHARGING                ///< Battery charging
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
        EVENT_CHARGING_START,   ///< Charging started
        EVENT_CHARGING_STOP,    ///< Charging stopped
        EVENT_LIGHT_DARK,       ///< Ambient light dark
        EVENT_LIGHT_BRIGHT      ///< Ambient light bright
    };

    /**
     * @brief Construct a new StateMachine object
     *
     * @param motionSensor Pointer to motion sensor HAL (polymorphic)
     * @param hazardLED Pointer to hazard LED HAL
     * @param statusLED Pointer to status LED HAL
     * @param button Pointer to button HAL
     */
    StateMachine(HAL_MotionSensor* motionSensor,
                 HAL_LED* hazardLED,
                 HAL_LED* statusLED,
                 HAL_Button* button);

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

private:
    // Hardware interfaces
    HAL_MotionSensor* m_motionSensor; ///< Motion sensor (polymorphic)
    HAL_LED* m_hazardLED;             ///< Hazard warning LED
    HAL_LED* m_statusLED;             ///< Status indicator LED
    HAL_Button* m_button;             ///< Mode button

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
};

#endif // STEPAWARE_STATE_MACHINE_H
