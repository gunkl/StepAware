/**
 * @file step_button.h
 * @brief Mode-button press classifier (port of HAL_Button's debounce/event state machine).
 *
 * The caller feeds the logical pressed state (setRawState()) and calls update() every
 * loop. Events are queued like HAL_Button: PRESSED, RELEASED, CLICK (release without a
 * long press) and LONG_PRESS (fired while still held, once per press). Added for the
 * release-based hold gestures: LONG_RELEASE (released after a long press;
 * getLastPressDurationMs() says how long it was held).
 *
 * suppressNextPress(): the next press that starts within the window (or is already in
 * progress) produces no events at all - used for the button press that woke the device
 * from dormant, so it is not also a click / long press.
 *
 * On ESPHome the binary_sensor already debounces (delayed_on/off 50 ms), so the glue
 * uses debounceMs = 0; the debounce stage is kept so the original behaviour stays testable.
 */
#ifndef STEPAWARE_CORE_STEP_BUTTON_H
#define STEPAWARE_CORE_STEP_BUTTON_H

#include <stdint.h>
#include "step_clock.h"

namespace step_logic {

const uint32_t BUTTON_DEBOUNCE_MS = 50;
const uint32_t BUTTON_LONG_PRESS_MS = 1000;  // ConfigManager default (config.h says 3000)

class StepButton {
public:
    enum ButtonEvent {
        EVENT_NONE,
        EVENT_PRESSED,
        EVENT_RELEASED,
        EVENT_CLICK,
        EVENT_LONG_PRESS,
        EVENT_LONG_RELEASE  ///< Released after LONG_PRESS (see getLastPressDurationMs())
    };

    StepButton(StepClockFn clock,
               uint32_t debounceMs = BUTTON_DEBOUNCE_MS,
               uint32_t longPressMs = BUTTON_LONG_PRESS_MS);

    /// Logical input: true = pressed.
    void setRawState(bool pressed) { m_rawState = pressed; }
    bool getRawState() const { return m_rawState; }

    /// Run the debounce / long-press state machine. Call every loop.
    void update();

    bool isPressed() const { return m_isPressed; }

    /// Consume one queued instance of the given event; true if found.
    bool hasEvent(ButtonEvent event);
    ButtonEvent getNextEvent();
    bool hasQueuedEvents() const { return m_queueHead != m_queueTail; }
    void clearEvents();

    uint32_t getClickCount() const { return m_clickCount; }
    void resetClickCount() { m_clickCount = 0; }
    uint32_t getPressedDuration() const;
    /// Hold time of the last completed press (press confirmed -> release started).
    uint32_t getLastPressDurationMs() const { return m_lastPressDuration; }

    /**
     * @brief Swallow the next press entirely (no PRESSED / LONG_PRESS / CLICK / RELEASED).
     *
     * Applies to a press already in progress or one confirmed within @p windowMs from now;
     * if none happens in the window the suppression lapses. isPressed() still reports the
     * held button (so the device stays awake while it is held).
     */
    void suppressNextPress(uint32_t windowMs);
    bool isSuppressing() const { return m_suppressArmed || m_pressSuppressed; }

    void setDebounceMs(uint32_t ms) { m_debounceMs = ms; }
    void setLongPressMs(uint32_t ms) { m_longPressMs = ms; }
    uint32_t getLongPressMs() const { return m_longPressMs; }

private:
    enum State {
        STATE_RELEASED,
        STATE_DEBOUNCING_PRESS,
        STATE_PRESSED,
        STATE_LONG_PRESS,
        STATE_DEBOUNCING_RELEASE
    };

    static const uint8_t EVENT_QUEUE_SIZE = 8;

    StepClockFn m_clock;
    uint32_t m_debounceMs;
    uint32_t m_longPressMs;
    State m_state;
    bool m_isPressed;
    bool m_rawState;
    uint32_t m_pressTime;
    uint32_t m_lastStateChange;
    bool m_longPressTriggered;
    uint32_t m_clickCount;
    uint32_t m_lastPressDuration;
    bool m_suppressArmed;
    uint32_t m_suppressStart;
    uint32_t m_suppressWindowMs;
    bool m_pressSuppressed;
    ButtonEvent m_eventQueue[EVENT_QUEUE_SIZE];
    uint8_t m_queueHead;
    uint8_t m_queueTail;

    void pushEvent(ButtonEvent event);
};

}  // namespace step_logic

#endif  // STEPAWARE_CORE_STEP_BUTTON_H
