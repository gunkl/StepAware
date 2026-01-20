#include "hal_button.h"
#include "logger.h"

HAL_Button::HAL_Button(uint8_t pin, uint32_t debounce_ms,
                       uint32_t long_press_ms, bool mock_mode)
    : m_pin(pin)
    , m_debounceMs(debounce_ms)
    , m_longPressMs(long_press_ms)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_state(STATE_RELEASED)
    , m_isPressed(false)
    , m_rawState(false)
    , m_pressTime(0)
    , m_releaseTime(0)
    , m_lastStateChange(0)
    , m_longPressTriggered(false)
    , m_clickCount(0)
    , m_eventQueueHead(0)
    , m_eventQueueTail(0)
{
    // Initialize event queue
    for (uint8_t i = 0; i < EVENT_QUEUE_SIZE; i++) {
        m_eventQueue[i] = EVENT_NONE;
    }
}

HAL_Button::~HAL_Button() {
    // No cleanup needed
}

bool HAL_Button::begin() {
    if (m_initialized) {
        return true;
    }

    LOG_DEBUG("HAL_Button: Initializing button on pin %d", m_pin);

    if (!m_mockMode) {
        // Configure GPIO with internal pull-up (active LOW)
        pinMode(m_pin, INPUT_PULLUP);
        DEBUG_PRINTF("[HAL_Button] Pin %d configured as INPUT_PULLUP\n", m_pin);
    } else {
        DEBUG_PRINTLN("[HAL_Button] MOCK MODE: Simulating button");
    }

    DEBUG_PRINTF("[HAL_Button] Debounce: %ums, Long press: %ums\n",
                 m_debounceMs, m_longPressMs);

    m_initialized = true;
    DEBUG_PRINTLN("[HAL_Button] ✓ Initialization complete");

    return true;
}

void HAL_Button::update() {
    if (!m_initialized) {
        return;
    }

    bool currentRawState = readRawState();
    uint32_t now = millis();

    // State machine for debouncing and event detection
    switch (m_state) {
        case STATE_RELEASED:
            if (currentRawState) {
                // Button pressed, start debounce
                m_state = STATE_DEBOUNCING_PRESS;
                m_lastStateChange = now;
                DEBUG_PRINTLN("[HAL_Button] Pressed");
            }
            break;

        case STATE_DEBOUNCING_PRESS:
            if (!currentRawState) {
                // False press, back to released
                m_state = STATE_RELEASED;
            } else if (now - m_lastStateChange >= m_debounceMs) {
                // Debounce time elapsed, press confirmed
                m_state = STATE_PRESSED;
                m_isPressed = true;
                m_pressTime = now;
                m_longPressTriggered = false;
                pushEvent(EVENT_PRESSED);
                DEBUG_PRINTLN("[HAL_Button] Press confirmed");
            }
            break;

        case STATE_PRESSED:
            if (!currentRawState) {
                // Button released, start debounce
                m_state = STATE_DEBOUNCING_RELEASE;
                m_lastStateChange = now;
                DEBUG_PRINTLN("[HAL_Button] Released");
            } else if (!m_longPressTriggered &&
                       now - m_pressTime >= m_longPressMs) {
                // Long press detected
                m_state = STATE_LONG_PRESS;
                m_longPressTriggered = true;
                pushEvent(EVENT_LONG_PRESS);
                DEBUG_PRINTLN("[HAL_Button] Long press detected");
            }
            break;

        case STATE_LONG_PRESS:
            if (!currentRawState) {
                // Button released after long press
                m_state = STATE_DEBOUNCING_RELEASE;
                m_lastStateChange = now;
                DEBUG_PRINTLN("[HAL_Button] Long press released");
            }
            break;

        case STATE_DEBOUNCING_RELEASE:
            if (currentRawState) {
                // False release, back to pressed
                if (m_longPressTriggered) {
                    m_state = STATE_LONG_PRESS;
                } else {
                    m_state = STATE_PRESSED;
                }
            } else if (now - m_lastStateChange >= m_debounceMs) {
                // Debounce time elapsed, release confirmed
                m_state = STATE_RELEASED;
                m_isPressed = false;
                m_releaseTime = now;
                pushEvent(EVENT_RELEASED);

                // If it wasn't a long press, it's a click
                if (!m_longPressTriggered) {
                    pushEvent(EVENT_CLICK);
                    m_clickCount++;
                    DEBUG_PRINTF("[HAL_Button] Click (count: %u)\n", m_clickCount);
                } else {
                    DEBUG_PRINTLN("[HAL_Button] Release confirmed");
                }
            }
            break;
    }

    m_rawState = currentRawState;
}

bool HAL_Button::isPressed() {
    return m_isPressed;
}

bool HAL_Button::hasEvent(ButtonEvent event) {
    // Search event queue for specific event
    uint8_t index = m_eventQueueTail;

    while (index != m_eventQueueHead) {
        if (m_eventQueue[index] == event) {
            // Found it! Remove this event and return true
            // Shift remaining events down
            while (index != m_eventQueueHead) {
                uint8_t next = (index + 1) % EVENT_QUEUE_SIZE;
                m_eventQueue[index] = m_eventQueue[next];
                index = next;
            }
            // Move head back
            m_eventQueueHead = (m_eventQueueHead + EVENT_QUEUE_SIZE - 1) % EVENT_QUEUE_SIZE;
            return true;
        }
        index = (index + 1) % EVENT_QUEUE_SIZE;
    }

    return false;
}

HAL_Button::ButtonEvent HAL_Button::getNextEvent() {
    if (!hasQueuedEvents()) {
        return EVENT_NONE;
    }

    ButtonEvent event = m_eventQueue[m_eventQueueTail];
    m_eventQueueTail = (m_eventQueueTail + 1) % EVENT_QUEUE_SIZE;

    return event;
}

void HAL_Button::clearEvents() {
    m_eventQueueHead = 0;
    m_eventQueueTail = 0;
    DEBUG_PRINTLN("[HAL_Button] Event queue cleared");
}

uint32_t HAL_Button::getClickCount() {
    return m_clickCount;
}

void HAL_Button::resetClickCount() {
    m_clickCount = 0;
    DEBUG_PRINTLN("[HAL_Button] Click count reset");
}

uint32_t HAL_Button::getPressedDuration() {
    if (!m_isPressed) {
        return 0;
    }
    return millis() - m_pressTime;
}

// Mock/Test Methods

void HAL_Button::mockClick() {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Button] WARNING: mockClick() called but mock mode not enabled");
        return;
    }

    DEBUG_PRINTLN("[HAL_Button] MOCK: Simulating click");
    mockPress();
    // Simulate time passing for debounce
    m_lastStateChange -= (m_debounceMs + 10);
    update();  // Process press
    delay(10);  // Short delay
    mockRelease();
    // Simulate time passing for debounce
    m_lastStateChange -= (m_debounceMs + 10);
    update();  // Process release
}

void HAL_Button::mockPress() {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Button] WARNING: mockPress() called but mock mode not enabled");
        return;
    }

    DEBUG_PRINTLN("[HAL_Button] MOCK: Button pressed");
    m_rawState = true;
}

void HAL_Button::mockRelease() {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Button] WARNING: mockRelease() called but mock mode not enabled");
        return;
    }

    DEBUG_PRINTLN("[HAL_Button] MOCK: Button released");
    m_rawState = false;
}

void HAL_Button::mockLongPress() {
    if (!m_mockMode) {
        DEBUG_PRINTLN("[HAL_Button] WARNING: mockLongPress() called but mock mode not enabled");
        return;
    }

    DEBUG_PRINTLN("[HAL_Button] MOCK: Simulating long press");
    mockPress();
    // Simulate time passing for debounce + long press
    m_lastStateChange -= (m_debounceMs + 10);
    update();  // Process press
    m_pressTime -= (m_longPressMs + 10);
    update();  // Process long press
    delay(10);
    mockRelease();
    m_lastStateChange -= (m_debounceMs + 10);
    update();  // Process release
}

bool HAL_Button::readRawState() {
    if (!m_mockMode) {
        // Read hardware pin (active LOW - pressed = LOW)
        return digitalRead(m_pin) == LOW;
    } else {
        // Mock mode: return stored raw state
        return m_rawState;
    }
}

void HAL_Button::pushEvent(ButtonEvent event) {
    uint8_t nextHead = (m_eventQueueHead + 1) % EVENT_QUEUE_SIZE;

    // Check if queue is full
    if (nextHead == m_eventQueueTail) {
        DEBUG_PRINTLN("[HAL_Button] WARNING: Event queue full, dropping oldest event");
        // Drop oldest event by moving tail forward
        m_eventQueueTail = (m_eventQueueTail + 1) % EVENT_QUEUE_SIZE;
    }

    m_eventQueue[m_eventQueueHead] = event;
    m_eventQueueHead = nextHead;
}

bool HAL_Button::hasQueuedEvents() {
    return m_eventQueueHead != m_eventQueueTail;
}
