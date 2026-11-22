#pragma once

#include <cmath>

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../stepaware_matrix.h"

namespace esphome {
namespace stepaware_matrix {

/// HA-facing brightness control (0-15).
///
/// Follows every brightness change (HA, stepaware_matrix.set_brightness, C++
/// setBrightness()) and, with restore_value, persists it and re-applies it on boot.
/// Without a stored value the initial state is the YAML `brightness`.
class BrightnessNumber : public number::Number, public Component,
                         public Parented<StepAwareMatrix> {
public:
    void setRestoreValue(bool restore) { m_restoreValue = restore; }

    // Run after the matrix so its configured brightness is the fallback state.
    float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

    void setup() override {
        uint8_t level = this->parent_->getBrightness();
        if (m_restoreValue) {
            m_pref = this->make_entity_preference<float>();
            float stored;
            if (m_pref.load(&stored) && !std::isnan(stored)) {
                level = static_cast<uint8_t>(clamp(lroundf(stored), 0L, 15L));
                m_saved = static_cast<float>(level);
            }
        }
        this->parent_->addOnBrightnessCallback(
            [this](uint8_t value) { this->onBrightness(value); });
        if (level != this->parent_->getBrightness()) {
            this->parent_->setBrightness(level);  // publishes via the callback
        } else {
            this->publish_state(level);
        }
    }

protected:
    void control(float value) override {
        if (std::isnan(value)) {
            return;
        }
        this->parent_->setBrightness(static_cast<uint8_t>(clamp(lroundf(value), 0L, 15L)));
    }

    void onBrightness(uint8_t level) {
        float value = static_cast<float>(level);
        this->publish_state(value);
        if (m_restoreValue && value != m_saved) {
            m_pref.save(&value);
            m_saved = value;
        }
    }

    bool m_restoreValue{true};
    float m_saved{NAN};
    ESPPreferenceObject m_pref;
};

}  // namespace stepaware_matrix
}  // namespace esphome
