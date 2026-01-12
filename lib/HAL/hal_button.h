#ifndef STEPAWARE_HAL_BUTTON_H
#define STEPAWARE_HAL_BUTTON_H

#include <Arduino.h>
#include <config.h>

/**
 * @brief Hardware Abstraction Layer for Button Input
 *
 * This class provides debounced button input with support for:
 * - Single press detection
 * - Long press detection
 * - Press and release events
 * - Mock mode for testing
 *
 * The button is expected to be connected with a pull-up resistor,
 * reading LOW when pressed and HIGH when released.
 */
class HAL_Button {
public:
    /**
     * @brief Button event types
     */
    enum ButtonEvent {
        EVENT_NONE,         ///< No event
        EVENT_PRESSED,      ///< Button was pressed
        EVENT_RELEASED,     ///< Button was released
        EVENT_CLICK,        ///< Short click (press + release)
        EVENT_LONG_PRESS    ///< Button held for long duration
    };

    /**
     * @brief Construct a new HAL_Button object
     *
     * @param pin GPIO pin connected to button (default: PIN_BUTTON)
     * @param debounceTime Debounce time in milliseconds (default: BUTTON_DEBOUNCE_MS)
     * @param longPressTime Long press threshold in milliseconds (default: 1000ms)
     * @param mock Enable mock mode for testing (default: MOCK_HARDWARE)
     */
    HAL_Button(uint8_t pin = PIN_BUTTON,
               uint32_t debounceTime = BUTTON_DEBOUNCE_MS,
               uint32_t longPressTime = 1000,
               bool mock = MOCK_HARDWARE);

    /**
     * @brief Destructor
     */
    ~HAL_Button();

    /**
     * @brief Initialize the button
     *
     * Sets up GPIO pin with pull-up resistor.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update button state (call regularly in loop)
     *
     * This method handles debouncing and event detection.
     * Must be called every loop iteration for proper operation.
     */
    void update();

    /**
     * @brief Check if button is currently pressed
     *
     * @return true if button is pressed (after debouncing)
     */
    bool isPressed();

    /**
     * @brief Get the last button event
     *
     * This method returns the most recent button event and
     * clears it after reading (one-shot behavior).
     *
     * @return ButtonEvent The last event that occurred
     */
    ButtonEvent getEvent();

    /**
     * @brief Check if a specific event occurred
     *
     * @param event Event type to check for
     * @return true if the event occurred since last check
     */
    bool hasEvent(ButtonEvent event);

    /**
     * @brief Clear all pending events
     */
    void clearEvents();

    /**
     * @brief Get the duration the button has been held
     *
     * @return uint32_t Duration in milliseconds (0 if not pressed)
     */
    uint32_t getPressedDuration();

    /**
     * @brief Get total number of clicks
     *
     * @return uint32_t Total click count since initialization
     */
    uint32_t getClickCount();

    /**
     * @brief Reset the click counter
     */
    void resetClickCount();

    /**
     * @brief Set debounce time
     *
     * @param ms Debounce time in milliseconds
     */
    void setDebounceTime(uint32_t ms);

    /**
     * @brief Set long press threshold
     *
     * @param ms Long press time in milliseconds
     */
    void setLongPressTime(uint32_t ms);

    // ========================================================================
    // Mock Hardware Methods
    // ========================================================================

#if MOCK_HARDWARE
    /**
     * @brief Simulate button press (mock mode only)
     */
    void mockPress();

    /**
     * @brief Simulate button release (mock mode only)
     */
    void mockRelease();

    /**
     * @brief Simulate a complete button click (press + release)
     */
    void mockClick();

    /**
     * @brief Simulate a long press
     *
     * @param duration_ms Duration to hold button (ms)
     */
    void mockLongPress(uint32_t duration_ms = 1500);

    /**
     * @brief Get mock button state
     *
     * @return true if mock button is pressed
     */
    bool mockIsPressed();
#endif

private:
    uint8_t m_pin;                  ///< GPIO pin number
    bool m_mock;                    ///< Mock mode enabled
    bool m_initialized;             ///< Initialization state

    // Debouncing
    uint32_t m_debounceTime;        ///< Debounce time (ms)
    uint32_t m_lastDebounceTime;    ///< Last time input changed (ms)
    bool m_lastReading;             ///< Last raw pin reading
    bool m_buttonState;             ///< Current debounced state
    bool m_lastButtonState;         ///< Previous debounced state

    // Event detection
    ButtonEvent m_lastEvent;        ///< Last detected event
    bool m_eventPending;            ///< Event waiting to be read

    // Press timing
    uint32_t m_pressStartTime;      ///< When button was pressed (ms)
    uint32_t m_longPressTime;       ///< Long press threshold (ms)
    bool m_longPressTriggered;      ///< Long press already detected

    // Statistics
    uint32_t m_clickCount;          ///< Total clicks

#if MOCK_HARDWARE
    bool m_mockPressed;             ///< Mock button state
    uint32_t m_mockPressTime;       ///< When mock press started
#endif

    /**
     * @brief Read the raw button state
     *
     * @return bool true if button pressed (LOW)
     */
    bool readButton();

    /**
     * @brief Set a new event
     *
     * @param event Event to set
     */
    void setEvent(ButtonEvent event);
};

#endif // STEPAWARE_HAL_BUTTON_H
