#pragma once

#ifdef USE_ESP32_VARIANT_ESP32C3

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"
#include "esphome/components/sensor/sensor.h"
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include "battery_calibration.h"

#include <esp_adc/adc_cali.h>

#include <cstdint>

namespace esphome::stepaware_battery {

/**
 * @brief Battery voltage on ESP32-C3 ADC2 channel 0 (GPIO5), read without the esp_adc driver.
 *
 * Port of StepAware v0.6.4 PowerManager::readBatteryVoltageRaw(). The IDF oneshot driver cannot
 * be used: while WiFi is up the PHY holds the ADC2 software lock (adc2_wifi_acquire()), so
 * adc_oneshot_read() returns ESP_ERR_TIMEOUT for every read even with
 * CONFIG_ADC_ONESHOT_FORCE_USE_ADC2_ON_C3. The SAR ADC itself is shared through a hardware
 * arbiter, so a register-level oneshot conversion still works; a conversion pre-empted by the
 * WiFi power detector comes back with an invalid-data flag, which is checked here.
 *
 * Publishes battery volts: pin volts x divider_ratio x the full-charge calibration factor
 * (battery_calibration.h; 1.0 until the first full charge, or when calibration is not
 * configured). Everything downstream (low / critical thresholds, percentage) therefore sees the
 * corrected voltage.
 */
class StepAwareBatterySensor : public sensor::Sensor, public PollingComponent {
public:
    void setup() override;
    void update() override;
    void dump_config() override;
    float get_setup_priority() const override;

    void set_samples(uint8_t samples) { m_samples = samples; }
    void set_divider_ratio(float ratio) { m_dividerRatio = ratio; }
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    void set_usb_sensor(binary_sensor::BinarySensor *usb) { m_usbSensor = usb; }
    void set_factor_sensor(sensor::Sensor *factor) { m_factorSensor = factor; }
    void set_calibration(float fullV, uint32_t stableMs, float maxSlopeVPerHour,
                         float maxSpreadV);
#endif

    /// Forget the learned factor (back to 1.0) and erase it from flash.
    void resetCalibration();

protected:
    /// One oneshot conversion. Returns false on hardware timeout or arbiter-invalidated data.
    bool readRawOnce(uint16_t &raw);
    /// Raw 12-bit count -> millivolts at the pin (eFuse curve fitting, or linear fallback).
    float rawToMillivolts(float raw) const;
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    /// Feed the uncorrected reading to the calibrator; persist and report a new factor.
    void updateCalibration(float measuredV);
    void saveCalibration();
    void publishFactor();
#endif

    adc_cali_handle_t m_caliHandle{nullptr};
    uint8_t m_samples{8};
    uint32_t m_failedUpdates{0};
    float m_dividerRatio{1.0f};
    step_battery::FullChargeCalibrator m_calibrator;
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    binary_sensor::BinarySensor *m_usbSensor{nullptr};
    sensor::Sensor *m_factorSensor{nullptr};
    ESPPreferenceObject m_calPref;
#endif
};

template<typename... Ts>
class ResetCalibrationAction : public Action<Ts...>, public Parented<StepAwareBatterySensor> {
public:
    explicit ResetCalibrationAction(StepAwareBatterySensor *parent)
        : Parented<StepAwareBatterySensor>(parent) {}
    void play(const Ts &...x) override { this->parent_->resetCalibration(); }
};

}  // namespace esphome::stepaware_battery

#endif  // USE_ESP32_VARIANT_ESP32C3
