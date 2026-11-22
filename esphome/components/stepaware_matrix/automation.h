#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "stepaware_matrix.h"

namespace esphome {
namespace stepaware_matrix {

template<typename... Ts>
class StartAnimationAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit StartAnimationAction(StepAwareMatrix *parent) : Parented<StepAwareMatrix>(parent) {}
    void set_type(AnimationType type) { m_type = type; }
    TEMPLATABLE_VALUE(uint32_t, duration)

    void play(const Ts &...x) override {
        this->parent_->startAnimation(m_type, this->duration_.value(x...));
    }

protected:
    AnimationType m_type{ANIM_NONE};
};

template<typename... Ts>
class StopAnimationAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit StopAnimationAction(StepAwareMatrix *parent) : Parented<StepAwareMatrix>(parent) {}
    void play(const Ts &...x) override { this->parent_->stopAnimation(); }
};

/// Stops any running animation and blanks the display.
template<typename... Ts>
class ClearAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit ClearAction(StepAwareMatrix *parent) : Parented<StepAwareMatrix>(parent) {}
    void play(const Ts &...x) override {
        this->parent_->stopAnimation();
        this->parent_->clear();
    }
};

/// HT16K33 standby (oscillator off) - use right before deep sleep.
template<typename... Ts>
class StandbyAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit StandbyAction(StepAwareMatrix *parent) : Parented<StepAwareMatrix>(parent) {}
    void play(const Ts &...x) override { this->parent_->standby(); }
};

template<typename... Ts>
class SetBrightnessAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit SetBrightnessAction(StepAwareMatrix *parent) : Parented<StepAwareMatrix>(parent) {}
    TEMPLATABLE_VALUE(uint8_t, brightness)

    void play(const Ts &...x) override {
        this->parent_->setBrightness(this->brightness_.value(x...));
    }
};

template<typename... Ts>
class SetSnakeProgressAction : public Action<Ts...>, public Parented<StepAwareMatrix> {
public:
    explicit SetSnakeProgressAction(StepAwareMatrix *parent)
        : Parented<StepAwareMatrix>(parent) {}
    TEMPLATABLE_VALUE(uint8_t, pixels)

    void play(const Ts &...x) override {
        this->parent_->setSnakeProgress(this->pixels_.value(x...));
    }
};

}  // namespace stepaware_matrix
}  // namespace esphome
