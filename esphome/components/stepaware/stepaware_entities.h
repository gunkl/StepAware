#pragma once
//
// Home Assistant entities of the stepaware component. Each block is compiled only when
// the corresponding ESPHome entity domain is loaded (esphome.h includes every header).
//
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "stepaware.h"

#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace stepaware {

#ifdef USE_SELECT
/// Operating mode: Off / Continuous / Motion (index == OperatingMode value).
class StepAwareModeSelect : public select::Select, public Parented<StepAware> {
public:
    void setRestoreValue(bool restore) { m_restoreValue = restore; }
    /// Load the stored index (if restoring and valid), else return fallback.
    size_t restoreIndex(size_t fallback);
    /// Publish the index and persist it when restore_value is on.
    void publishAndSave(size_t index);

protected:
    void control(size_t index) override;

    bool m_restoreValue{true};
    ESPPreferenceObject m_pref;
};
#endif

#ifdef USE_NUMBER
class StepAwareNumber : public number::Number, public Parented<StepAware> {
public:
    void setKind(NumberKind kind) { m_kind = kind; }
    NumberKind getKind() const { return m_kind; }
    void setRestoreValue(bool restore) { m_restoreValue = restore; }
    /// Load the stored value (or fallback), publish it and return it.
    float restoreValue(float fallback);

protected:
    void control(float value) override;

    NumberKind m_kind{NumberKind::WARNING_DURATION};
    bool m_restoreValue{true};
    ESPPreferenceObject m_pref;
};
#endif

#ifdef USE_SWITCH
/// ON = only APPROACHING motion (far -> near) triggers warnings.
class StepAwareDirectionSwitch : public switch_::Switch, public Parented<StepAware> {
protected:
    void write_state(bool state) override;
};
#endif

}  // namespace stepaware
}  // namespace esphome
