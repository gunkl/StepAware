#ifndef STEPAWARE_HAL_BUTTON_H
#define STEPAWARE_HAL_BUTTON_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Hardware Abstraction Layer for Button Input
 *
 * Provides debounced button handling with click and long-press detection,
 * event queuing, and mock mode for testing.
 *
 * Features:
 * - Hardware debouncing (configurable delay)
 * - Click detection
 * - Long-press detection (configurable duration)
 * - Event queue for reliable event handling
 * - Mock mode for testing
 * - Click counting for statistics
 *
 * Button Connection:
 * - Active LOW (pressed = LOW, released = HIGH)
 * - Internal pull-up resistor enabled
 * - Connect button between GPIO and GND
 */
class HAL_Button {
public:
    /**
     * @brief Button events
     */
    enum ButtonEvent {
        EVENT_NONE,             ///< No event
        EVENT_PRESSED,          ///< Button pressed down
        EVENT_RELEASED,         ///< Button released
        EVENT_CLICK,            ///< Short click detected
        EVENT_LONG_PRESS        ///< Long press detected
    };

    /**
     * @brief Construct a new HAL_Button object
     *
     * @param pin GPIO pin number for button
     * @param debounce_ms Debounce time in milliseconds (default: 50ms)
     * @param long_press_ms Long press duration in milliseconds (default: 1000ms)
     * @param mock_mode True to enable mock/simulation mode for testing
     */
    HAL_Button(uint8_t pin, uint32_t debounce_ms = 50,
               uint32_t long_press_ms = 1000, bool mock_mode = false);

    /**
     * @brief Destructor
     */
    ~HAL_Button();

    /**
     * @brief Initialize the button
     *
     * Configures GPIO pin with internal pull-up.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update button state (call in main loop)
     *
     * Reads pin state, handles debouncing, detects events, and updates queue.
     */
    void update();

    /**
     * @brief Check if button is currently pressed
     *
     * @return true if button is pressed right now
     */
    bool isPressed();

    /**
     * @brief Check if specific event has occurred
     *
     * Checks event queue and returns true if event is present.
     * Consumes the event from the queue.
     *
     * @param event Event type to check for
     * @return true if event occurred since last check
     */
    bool hasEvent(ButtonEvent event);

    /**
     * @brief Get next event from queue
     *
     * @return ButtonEvent Next event (EVENT_NONE if queue empty)
     */
    ButtonEvent getNextEvent();

    /**
     * @brief Clear all pending events
     */
    void clearEvents();

    /**
     * @brief Get total click count
     *
     * @return uint32_t Number of clicks detected
     */
    uint32_t getClickCount();

    /**
     * @brief Reset click counter
     */
    void resetClickCount();

    /**
     * @brief Get time button has been pressed
     *
     * @return uint32_t Milliseconds button has been held down (0 if not pressed)
     */
    uint32_t getPressedDuration();

    // Mock/Test Methods (only active if mock_mode = true)

    /**
     * @brief Simulate button click
     *
     * Simulates a quick press and release.
     * Only works when mock_mode is enabled.
     */
    void mockClick();

    /**
     * @brief Simulate button press
     *
     * Simulates button being pressed down.
     * Only works when mock_mode is enabled.
     */
    void mockPress();

    /**
     * @brief Simulate button release
     *
     * Simulates button being released.
     * Only works when mock_mode is enabled.
     */
    void mockRelease();

    /**
     * @brief Simulate long press
     *
     * Simulates holding button for long press duration.
     * Only works when mock_mode is enabled.
     */
    void mockLongPress();

private:
    uint8_t m_pin;                  ///< GPIO pin number
    uint32_t m_debounceMs;          ///< Debounce time (ms)
    uint32_t m_longPressMs;         ///< Long press duration (ms)
    bool m_mockMode;                ///< Mock mode enabled
    bool m_initialized;             ///< Initialization complete

    // State machine
    enum State {
        STATE_RELEASED,             ///< Button not pressed
        STATE_DEBOUNCING_PRESS,     ///< Waiting to confirm press
        STATE_PRESSED,              ///< Button confirmed pressed
        STATE_LONG_PRESS,           ///< Long press detected
        STATE_DEBOUNCING_RELEASE    ///< Waiting to confirm release
    } m_state;

    // Button state
    bool m_isPressed;               ///< Current debounced button state
    bool m_rawState;                ///< Raw pin state
    uint32_t m_pressTime;           ///< When button was pressed (millis)
    uint32_t m_releaseTime;         ///< When button was released (millis)
    uint32_t m_lastStateChange;     ///< Last state change time (for debouncing)
    bool m_longPressTriggered;      ///< Long press event already fired

    // Statistics
    uint32_t m_clickCount;          ///< Total clicks detected

    // Event queue (simple circular buffer)
    static const uint8_t EVENT_QUEUE_SIZE = 8;
    ButtonEvent m_eventQueue[EVENT_QUEUE_SIZE];
    uint8_t m_eventQueueHead;       ///< Write index
    uint8_t m_eventQueueTail;       ///< Read index

    /**
     * @brief Read raw button state from pin
     *
     * @return true if button is pressed (pin is LOW)
     */
    bool readRawState();

    /**
     * @brief Add event to queue
     *
     * @param event Event to add
     */
    void pushEvent(ButtonEvent event);

    /**
     * @brief Check if event queue has events
     *
     * @return true if queue is not empty
     */
    bool hasQueuedEvents();
};

#endif // STEPAWARE_HAL_BUTTON_H
