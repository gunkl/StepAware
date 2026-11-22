#pragma once

#ifdef USE_ESP32_VARIANT_ESP32C3

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

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
 * Publishes the voltage at the pin, in volts. Any divider ratio is applied by YAML filters.
 */
class StepAwareBatterySensor : public sensor::Sensor, public PollingComponent {
public:
    void setup() override;
    void update() override;
    void dump_config() override;
    float get_setup_priority() const override;

    void set_samples(uint8_t samples) { m_samples = samples; }

protected:
    /// One oneshot conversion. Returns false on hardware timeout or arbiter-invalidated data.
    bool readRawOnce(uint16_t &raw);
    /// Raw 12-bit count -> millivolts at the pin (eFuse curve fitting, or linear fallback).
    float rawToMillivolts(float raw) const;

    adc_cali_handle_t m_caliHandle{nullptr};
    uint8_t m_samples{8};
    uint32_t m_failedUpdates{0};
};

}  // namespace esphome::stepaware_battery

#endif  // USE_ESP32_VARIANT_ESP32C3
