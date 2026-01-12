#include "hal_button.h"
#include <config.h>

HAL_Button::HAL_Button(uint8_t pin, uint32_t debounceTime, uint32_t longPressTime, bool mock)
    : m_pin(pin)
    , m_mock(mock)
    , m_initialized(false)
    , m_debounceTime(debounceTime)
    , m_lastDebounceTime(0)
    , m_lastReading(HIGH)
    , m_buttonState(HIGH)
    , m_lastButtonState(HIGH)
    , m_lastEvent(EVENT_NONE)
    , m_eventPending(false)
    , m_pressStartTime(0)
    , m_longPressTime(longPressTime)
    , m_longPressTriggered(false)
    , m_clickCount(0)
#if MOCK_HARDWARE
    , m_mockPressed(false)
    , m_mockPressTime(0)
#endif
{
}

HAL_Button::~HAL_Button() {
    // Nothing to clean up
}

bool HAL_Button::begin() {
    if (m_initialized) {
        return true;
    }

    if (!m_mock) {
        // Configure GPIO pin as input with pull-up
        pinMode(m_pin, INPUT_PULLUP);

        DEBUG_PRINTF("[HAL_Button] Initialized on GPIO%d (real hardware, pull-up)\n", m_pin);
    } else {
#if MOCK_HARDWARE
        DEBUG_PRINTF("[HAL_Button] Initialized on GPIO%d (MOCK mode)\n", m_pin);
#endif
    }

    m_initialized = true;
    m_buttonState = HIGH;  // Released state (pull-up)
    m_lastButtonState = HIGH;

    return true;
}

void HAL_Button::update() {
    if (!m_initialized) {
        return;
    }

    // Read current button state
    bool reading = readButton();

    // Check if state changed (for debouncing)
    if (reading != m_lastReading) {
        m_lastDebounceTime = millis();
    }

    m_lastReading = reading;

    // Check if enough time has passed for debouncing
    if ((millis() - m_lastDebounceTime) > m_debounceTime) {
        // Reading is stable, update button state if it changed
        if (reading != m_buttonState) {
            m_buttonState = reading;

            // Detect events
            if (m_buttonState == LOW && m_lastButtonState == HIGH) {
                // Button pressed
                m_pressStartTime = millis();
                m_longPressTriggered = false;
                setEvent(EVENT_PRESSED);
                DEBUG_PRINTLN("[HAL_Button] Pressed");

            } else if (m_buttonState == HIGH && m_lastButtonState == LOW) {
                // Button released
                setEvent(EVENT_RELEASED);
                DEBUG_PRINTLN("[HAL_Button] Released");

                // Check if it was a click (not a long press)
                if (!m_longPressTriggered) {
                    setEvent(EVENT_CLICK);
                    m_clickCount++;
                    DEBUG_PRINTF("[HAL_Button] Click (count: %u)\n", m_clickCount);
                }

                m_pressStartTime = 0;
            }

            m_lastButtonState = m_buttonState;
        }
    }

    // Check for long press
    if (m_buttonState == LOW && !m_longPressTriggered) {
        if (millis() - m_pressStartTime >= m_longPressTime) {
            m_longPressTriggered = true;
            setEvent(EVENT_LONG_PRESS);
            DEBUG_PRINTLN("[HAL_Button] Long press detected");
        }
    }
}

bool HAL_Button::isPressed() {
    return (m_buttonState == LOW);
}

HAL_Button::ButtonEvent HAL_Button::getEvent() {
    ButtonEvent event = m_lastEvent;
    m_lastEvent = EVENT_NONE;
    m_eventPending = false;
    return event;
}

bool HAL_Button::hasEvent(ButtonEvent event) {
    if (m_lastEvent == event && m_eventPending) {
        m_eventPending = false;
        return true;
    }
    return false;
}

void HAL_Button::clearEvents() {
    m_lastEvent = EVENT_NONE;
    m_eventPending = false;
}

uint32_t HAL_Button::getPressedDuration() {
    if (!isPressed()) {
        return 0;
    }

    return millis() - m_pressStartTime;
}

uint32_t HAL_Button::getClickCount() {
    return m_clickCount;
}

void HAL_Button::resetClickCount() {
    m_clickCount = 0;
    DEBUG_PRINTLN("[HAL_Button] Click counter reset");
}

void HAL_Button::setDebounceTime(uint32_t ms) {
    m_debounceTime = ms;
    DEBUG_PRINTF("[HAL_Button] Debounce time set to %u ms\n", ms);
}

void HAL_Button::setLongPressTime(uint32_t ms) {
    m_longPressTime = ms;
    DEBUG_PRINTF("[HAL_Button] Long press time set to %u ms\n", ms);
}

bool HAL_Button::readButton() {
#if MOCK_HARDWARE
    if (m_mock) {
        // In mock mode, button pressed = LOW (simulating pull-up)
        return m_mockPressed ? LOW : HIGH;
    }
#endif

    // Read real hardware (pull-up: pressed = LOW)
    return digitalRead(m_pin);
}

void HAL_Button::setEvent(ButtonEvent event) {
    m_lastEvent = event;
    m_eventPending = true;
}

// ============================================================================
// Mock Hardware Methods
// ============================================================================

#if MOCK_HARDWARE

void HAL_Button::mockPress() {
    if (!m_mock) {
        return;
    }

    if (!m_mockPressed) {
        m_mockPressed = true;
        m_mockPressTime = millis();
        DEBUG_PRINTLN("[HAL_Button] MOCK: Button pressed");
    }
}

void HAL_Button::mockRelease() {
    if (!m_mock) {
        return;
    }

    if (m_mockPressed) {
        m_mockPressed = false;
        DEBUG_PRINTLN("[HAL_Button] MOCK: Button released");
    }
}

void HAL_Button::mockClick() {
    if (!m_mock) {
        return;
    }

    DEBUG_PRINTLN("[HAL_Button] MOCK: Simulating click");

    mockPress();
    // Allow update cycle to process press
    for (int i = 0; i < 5; i++) {
        delay(m_debounceTime / 5);
        update();
    }

    mockRelease();
    // Allow update cycle to process release
    for (int i = 0; i < 5; i++) {
        delay(m_debounceTime / 5);
        update();
    }
}

void HAL_Button::mockLongPress(uint32_t duration_ms) {
    if (!m_mock) {
        return;
    }

    DEBUG_PRINTF("[HAL_Button] MOCK: Simulating long press (%u ms)\n", duration_ms);

    mockPress();

    // Hold for specified duration
    uint32_t startTime = millis();
    while (millis() - startTime < duration_ms) {
        delay(100);
        update();
    }

    mockRelease();
    delay(m_debounceTime);
    update();
}

bool HAL_Button::mockIsPressed() {
    if (!m_mock) {
        return false;
    }

    return m_mockPressed;
}

#endif // MOCK_HARDWARE
