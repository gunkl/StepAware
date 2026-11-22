#include "step_button.h"

namespace step_logic {

StepButton::StepButton(StepClockFn clock, uint32_t debounceMs, uint32_t longPressMs)
    : m_clock(clock)
    , m_debounceMs(debounceMs)
    , m_longPressMs(longPressMs)
    , m_state(STATE_RELEASED)
    , m_isPressed(false)
    , m_rawState(false)
    , m_pressTime(0)
    , m_lastStateChange(0)
    , m_longPressTriggered(false)
    , m_clickCount(0)
    , m_lastPressDuration(0)
    , m_suppressArmed(false)
    , m_suppressStart(0)
    , m_suppressWindowMs(0)
    , m_pressSuppressed(false)
    , m_queueHead(0)
    , m_queueTail(0)
{
    for (uint8_t i = 0; i < EVENT_QUEUE_SIZE; i++) {
        m_eventQueue[i] = EVENT_NONE;
    }
}

void StepButton::update() {
    bool raw = m_rawState;
    uint32_t now = m_clock();

    // An armed suppression lapses if no press starts within its window.
    if (m_suppressArmed && !m_isPressed && m_state == STATE_RELEASED &&
        now - m_suppressStart > m_suppressWindowMs) {
        m_suppressArmed = false;
    }

    switch (m_state) {
        case STATE_RELEASED:
            if (raw) {
                m_state = STATE_DEBOUNCING_PRESS;
                m_lastStateChange = now;
            }
            break;

        case STATE_DEBOUNCING_PRESS:
            if (!raw) {
                m_state = STATE_RELEASED;  // bounce
            } else if (now - m_lastStateChange >= m_debounceMs) {
                m_state = STATE_PRESSED;
                m_isPressed = true;
                m_pressTime = now;
                m_longPressTriggered = false;
                if (m_suppressArmed) {
                    m_suppressArmed = false;
                    m_pressSuppressed = true;
                }
                pushEvent(EVENT_PRESSED);
            }
            break;

        case STATE_PRESSED:
            if (!raw) {
                m_state = STATE_DEBOUNCING_RELEASE;
                m_lastStateChange = now;
            } else if (!m_longPressTriggered && now - m_pressTime >= m_longPressMs) {
                m_state = STATE_LONG_PRESS;
                m_longPressTriggered = true;
                pushEvent(EVENT_LONG_PRESS);
            }
            break;

        case STATE_LONG_PRESS:
            if (!raw) {
                m_state = STATE_DEBOUNCING_RELEASE;
                m_lastStateChange = now;
            }
            break;

        case STATE_DEBOUNCING_RELEASE:
            if (raw) {
                m_state = m_longPressTriggered ? STATE_LONG_PRESS : STATE_PRESSED;
            } else if (now - m_lastStateChange >= m_debounceMs) {
                m_state = STATE_RELEASED;
                m_isPressed = false;
                m_lastPressDuration = m_lastStateChange - m_pressTime;
                pushEvent(EVENT_RELEASED);
                if (m_longPressTriggered) {
                    pushEvent(EVENT_LONG_RELEASE);
                } else {
                    pushEvent(EVENT_CLICK);
                    if (!m_pressSuppressed) {
                        m_clickCount++;
                    }
                }
                m_pressSuppressed = false;
            }
            break;
    }
}

bool StepButton::hasEvent(ButtonEvent event) {
    uint8_t index = m_queueTail;
    while (index != m_queueHead) {
        if (m_eventQueue[index] == event) {
            // Remove this entry by shifting the rest down
            while (index != m_queueHead) {
                uint8_t next = (index + 1) % EVENT_QUEUE_SIZE;
                m_eventQueue[index] = m_eventQueue[next];
                index = next;
            }
            m_queueHead = (m_queueHead + EVENT_QUEUE_SIZE - 1) % EVENT_QUEUE_SIZE;
            return true;
        }
        index = (index + 1) % EVENT_QUEUE_SIZE;
    }
    return false;
}

StepButton::ButtonEvent StepButton::getNextEvent() {
    if (!hasQueuedEvents()) {
        return EVENT_NONE;
    }
    ButtonEvent event = m_eventQueue[m_queueTail];
    m_queueTail = (m_queueTail + 1) % EVENT_QUEUE_SIZE;
    return event;
}

void StepButton::clearEvents() {
    m_queueHead = 0;
    m_queueTail = 0;
}

uint32_t StepButton::getPressedDuration() const {
    if (!m_isPressed) {
        return 0;
    }
    return m_clock() - m_pressTime;
}

void StepButton::suppressNextPress(uint32_t windowMs) {
    if (m_isPressed) {
        m_pressSuppressed = true;  // already held: swallow the rest of this press
        m_suppressArmed = false;
        return;
    }
    m_suppressArmed = true;
    m_suppressStart = m_clock();
    m_suppressWindowMs = windowMs;
}

void StepButton::pushEvent(ButtonEvent event) {
    if (m_pressSuppressed) {
        return;
    }
    uint8_t nextHead = (m_queueHead + 1) % EVENT_QUEUE_SIZE;
    if (nextHead == m_queueTail) {
        // Queue full: drop the oldest event
        m_queueTail = (m_queueTail + 1) % EVENT_QUEUE_SIZE;
    }
    m_eventQueue[m_queueHead] = event;
    m_queueHead = nextHead;
}

}  // namespace step_logic
