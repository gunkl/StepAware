#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/sensor/sensor.h"

#include <cinttypes>

namespace esphome::stepaware_grove_ultrasonic {

/// Edge timestamps captured by the GPIO ISR.
struct GroveUltrasonicStore {
    static void gpioIntr(GroveUltrasonicStore *arg);

    volatile uint32_t measurementStartUs{0};
    volatile uint32_t echoStartUs{0};
    volatile uint32_t echoEndUs{0};
    volatile bool echoStart{false};
    volatile bool echoEnd{false};
};

/**
 * @brief Seeed Grove Ultrasonic Ranger v2.0 on one SIG pin (port of HAL_Ultrasonic_Grove).
 *
 * The SIG line is an input with an any-edge interrupt. update() briefly enables the output
 * driver for the trigger pulse and returns the pin to input before the echo starts; loop()
 * evaluates the ISR timestamps, so nothing blocks for the echo. Timing model and edge
 * filtering follow ESPHome's `ultrasonic` component.
 *
 * Publishes distance in metres (ESPHome `ultrasonic` convention), NAN on timeout. The
 * median/range filtering StepAware's DistanceSensorBase did is left to YAML sensor filters.
 */
class GroveUltrasonicSensor : public sensor::Sensor, public PollingComponent {
public:
    void set_pin(InternalGPIOPin *pin) { m_pin = pin; }
    /// Maximum echo pulse length in us (derived from the `timeout` distance).
    void set_timeout_us(uint32_t timeoutUs) { m_timeoutUs = timeoutUs; }
    /// Trigger pulse length in us.
    void set_pulse_time_us(uint32_t pulseTimeUs) { m_pulseTimeUs = pulseTimeUs; }

    void setup() override;
    void loop() override;
    void update() override;
    void dump_config() override;
    float get_setup_priority() const override;

protected:
    void sendTriggerPulse();
    void finishMeasurement(float distanceM);
    static float echoUsToMeters(uint32_t echoUs);

    InternalGPIOPin *m_pin{nullptr};
    ISRInternalGPIOPin m_pinIsr;
    gpio::Flags m_inputFlags{gpio::FLAG_INPUT};
    GroveUltrasonicStore m_store;
    uint32_t m_timeoutUs{20408};
    uint32_t m_pulseTimeUs{10};
    uint32_t m_measurementStartUs{0};
    bool m_measurementPending{false};
    uint32_t m_timeoutCount{0};
};

}  // namespace esphome::stepaware_grove_ultrasonic
