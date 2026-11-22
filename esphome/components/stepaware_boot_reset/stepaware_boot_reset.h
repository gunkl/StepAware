#pragma once

// StepAware boot-time button-hold resets (port of src/main.cpp handleBootButtonHold()).
//
// If the button is held when the device boots (any reset except a deep-sleep wake), setup()
// blocks while it stays held and shows the pending action on the feedback LED:
//   0 .. wifi hold       slow pulse    release = cancel
//   wifi .. factory hold fast blink    release = WiFi reset (runtime WiFi credentials erased)
//   factory hold ..      solid on      release = factory reset (all preferences erased)
//   max wait reached     off           stuck button: cancel and boot normally
// Both resets reboot the device. The button is read straight from the GPIO, and this
// component sets up before the GPIO binary sensors, so the stepaware core never sees the
// boot-time hold.

#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"

#include <cstdint>

namespace esphome::stepaware_boot_reset {

class StepAwareBootReset : public Component {
 public:
    void setup() override;
    void dump_config() override;
    /// Before the GPIO binary sensors and ledc outputs (both HARDWARE), after the I2C bus,
    /// logger and stepaware_power (BUS + 50).
    float get_setup_priority() const override { return setup_priority::HARDWARE + 10.0f; }

    void setButtonPin(uint8_t pin) { m_buttonPin = pin; }
    void setOutput(output::FloatOutput *out) { m_output = out; }
    void setOutputComponent(Component *comp) { m_outputComponent = comp; }
    void setHoldTimes(uint32_t wifiMs, uint32_t factoryMs, uint32_t maxWaitMs) {
        m_wifiHoldMs = wifiMs;
        m_factoryHoldMs = factoryMs;
        m_maxWaitMs = maxWaitMs;
    }

 protected:
    enum Stage : uint8_t {
        STAGE_NONE = 0,
        STAGE_WIFI_PENDING,
        STAGE_FACTORY_PENDING,
    };

    bool isPressed() const;
    /// True if the button reads pressed for the whole debounce window.
    bool isHeldStable(uint32_t windowMs) const;
    void prepareOutput();
    void setLed(float level);
    void updateLed(Stage stage, uint32_t heldMs);
    void blinkConfirm(uint8_t count, uint32_t onMs, uint32_t offMs);
    void waitMs(uint32_t ms);

    void performWifiReset();
    void performFactoryReset();
    [[noreturn]] void rebootNow();

    output::FloatOutput *m_output{nullptr};
    Component *m_outputComponent{nullptr};
    uint8_t m_buttonPin{0};
    uint32_t m_wifiHoldMs{15000};
    uint32_t m_factoryHoldMs{30000};
    uint32_t m_maxWaitMs{40000};
    bool m_checked{false};   ///< Boot hold check ran (false on deep-sleep wakes)
    bool m_wasHeld{false};   ///< Button was held at boot
};

}  // namespace esphome::stepaware_boot_reset
