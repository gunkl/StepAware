#include "grove_ultrasonic_sensor.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome::stepaware_grove_ultrasonic {

static const char *const TAG = "stepaware_grove_ultrasonic";

// Ignore edges within this window of each other (noise / ringing).
static constexpr uint32_t DEBOUNCE_US = 50;
// Ignore edges this soon after the trigger: our own trigger edges are latched by the
// interrupt controller and delivered when the InterruptLock is released.
static constexpr uint32_t START_DELAY_US = 100;
// The Grove module starts the echo pulse well under 1 ms after the trigger (8 x 40 kHz
// burst first). NewPing allows 5.8 ms; give up after 10 ms if no echo started.
static constexpr uint32_t START_TIMEOUT_US = 10000;
// Low settle time before the trigger pulse (Seeed reference driver: 2 us).
static constexpr uint32_t TRIGGER_SETTLE_US = 2;
static constexpr float SPEED_OF_SOUND_M_PER_S = 343.0f;
static constexpr uint32_t TIMEOUT_LOG_EVERY = 50;

void IRAM_ATTR GroveUltrasonicStore::gpioIntr(GroveUltrasonicStore *arg) {
    uint32_t now = micros();
    if (arg->echoEnd || (now - arg->measurementStartUs) <= START_DELAY_US) {
        return;
    }
    if (!arg->echoStart || (now - arg->echoStartUs) <= DEBOUNCE_US) {
        arg->echoStartUs = now;
        arg->echoStart = true;
    } else {
        arg->echoEndUs = now;
        arg->echoEnd = true;
    }
}

void GroveUltrasonicSensor::setup() {
    m_pin->setup();
    m_inputFlags = m_pin->get_flags();
    m_pinIsr = m_pin->to_isr();
    // Sync the ISR pin's cached mode with the configured (input) mode.
    m_pinIsr.pin_mode(m_inputFlags);
    m_pin->attach_interrupt(GroveUltrasonicStore::gpioIntr, &m_store, gpio::INTERRUPT_ANY_EDGE);
}

void IRAM_ATTR GroveUltrasonicSensor::sendTriggerPulse() {
    // ~15 us with interrupts off: direction switch + LOW settle + HIGH pulse + back to input.
    InterruptLock lock;
    m_store.echoStart = false;
    m_store.echoEnd = false;
    m_store.measurementStartUs = micros();

    m_pinIsr.digital_write(false);
    m_pinIsr.pin_mode(static_cast<gpio::Flags>(m_inputFlags | gpio::FLAG_OUTPUT));
    delayMicroseconds(TRIGGER_SETTLE_US);
    m_pinIsr.digital_write(true);
    delayMicroseconds(m_pulseTimeUs);
    m_pinIsr.digital_write(false);
    // Release the line so the module can drive the echo pulse on SIG.
    m_pinIsr.pin_mode(m_inputFlags);

    m_measurementPending = true;
    m_measurementStartUs = m_store.measurementStartUs;
}

void GroveUltrasonicSensor::update() {
    if (m_measurementPending) {
        return;
    }
    this->sendTriggerPulse();
}

void GroveUltrasonicSensor::loop() {
    if (!m_measurementPending) {
        return;
    }

    if (!m_store.echoStart) {
        if (micros() - m_measurementStartUs >= START_TIMEOUT_US) {
            m_timeoutCount++;
            if (m_timeoutCount == 1 || m_timeoutCount % TIMEOUT_LOG_EVERY == 0) {
                ESP_LOGW(TAG, "'%s' - no echo started (timeouts: %" PRIu32 ")",
                         this->get_name().c_str(), m_timeoutCount);
            }
            this->finishMeasurement(NAN);
        }
        return;
    }

    uint32_t elapsed;
    if (m_store.echoEnd) {
        elapsed = m_store.echoEndUs - m_store.echoStartUs;
    } else {
        elapsed = micros() - m_store.echoStartUs;
    }
    if (elapsed >= m_timeoutUs) {
        // No object within the configured range (the module holds SIG high up to ~38 ms).
        ESP_LOGV(TAG, "'%s' - echo pulse timed out after %" PRIu32 " us",
                 this->get_name().c_str(), elapsed);
        this->finishMeasurement(NAN);
        return;
    }

    if (m_store.echoEnd) {
        float distanceM = echoUsToMeters(elapsed);
        ESP_LOGV(TAG, "'%s' - echo %" PRIu32 " us -> %.3f m", this->get_name().c_str(), elapsed,
                 distanceM);
        this->finishMeasurement(distanceM);
    }
}

void GroveUltrasonicSensor::finishMeasurement(float distanceM) {
    m_measurementPending = false;
    this->publish_state(distanceM);
}

void GroveUltrasonicSensor::dump_config() {
    LOG_SENSOR("", "Grove Ultrasonic v2.0", this);
    LOG_PIN("  SIG Pin: ", m_pin);
    ESP_LOGCONFIG(TAG,
                  "  Pulse time: %" PRIu32 " us\n"
                  "  Echo timeout: %" PRIu32 " us",
                  m_pulseTimeUs, m_timeoutUs);
    LOG_UPDATE_INTERVAL(this);
}

float GroveUltrasonicSensor::get_setup_priority() const { return setup_priority::DATA; }

float GroveUltrasonicSensor::echoUsToMeters(uint32_t echoUs) {
    return (echoUs / 1e6f) * SPEED_OF_SOUND_M_PER_S / 2.0f;
}

}  // namespace esphome::stepaware_grove_ultrasonic
