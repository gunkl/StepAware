#include "stepaware_battery_sensor.h"

#ifdef USE_ESP32_VARIANT_ESP32C3

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <driver/gpio.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_private/adc_share_hw_ctrl.h>
#include <esp_private/sar_periph_ctrl.h>
#include <esp_rom_sys.h>
#include <hal/adc_ll.h>
#include <soc/apb_saradc_struct.h>

#include <cmath>

namespace esphome::stepaware_battery {

static const char *const TAG = "stepaware_battery";

// Fixed wiring: battery divider on GPIO5 = ADC2 channel 0 (the only ADC2 channel on the C3).
static constexpr adc_unit_t BATTERY_ADC_UNIT = ADC_UNIT_2;
static constexpr adc_channel_t BATTERY_ADC_CHANNEL = ADC_CHANNEL_0;
static constexpr gpio_num_t BATTERY_GPIO = GPIO_NUM_5;
static constexpr adc_atten_t BATTERY_ADC_ATTEN = ADC_ATTEN_DB_12;
static constexpr uint32_t ONESHOT_DONE_EVENT = ADC_LL_EVENT_ADC2_ONESHOT_DONE;

// The onetime_start edge must be held >= 3 ADC controller clocks. Controller clock is
// APB 80 MHz / (ADC_LL_CLKM_DIV_NUM_DEFAULT + 1) = 5 MHz, so 3 cycles = 0.6 us; IDF's
// adc_hal_onetime_start() computes 3 us for this divider, use the same.
static constexpr uint32_t START_PULSE_DELAY_US = 3;
// A conversion takes a few us; anything longer means the arbiter never granted the SAR.
static constexpr uint32_t CONVERSION_TIMEOUT_US = 100;
// Retries per sample when WiFi's power detector wins the arbiter.
static constexpr uint8_t MAX_RETRIES_PER_SAMPLE = 3;
static constexpr uint32_t RETRY_DELAY_US = 50;
// Linear fallback when no eFuse calibration is burnt: StepAware v0.6.x formula
// (raw / 4095 * 3.3 V). Approximate only; the C3 12 dB range saturates below 3.3 V.
static constexpr float FALLBACK_FULL_SCALE_MV = 3300.0f;
static constexpr float RAW_FULL_SCALE = 4095.0f;
static constexpr uint32_t FAILURE_LOG_EVERY = 10;
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
// Flash key for the learned factor. Changing it forgets every device's calibration.
static const char *const CAL_PREF_NAME = "stepaware_battery_cal";
#endif

void StepAwareBatterySensor::setup() {
    // GPIO5 as a plain analog input: no digital input buffer, no pulls.
    gpio_config_t ioConf{};
    ioConf.pin_bit_mask = 1ULL << BATTERY_GPIO;
    ioConf.mode = GPIO_MODE_DISABLE;
    ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
    ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ioConf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&ioConf);

    // Un-gate and reset the APB_SARADC peripheral (ref-counted, shared with the IDF driver).
    adc_apb_periph_claim();

    // Load the per-chip SAR "initial code" from eFuse into ADC2. Done once here, which runs
    // before the wifi component starts (setup priority DATA > WIFI). It is deliberately not
    // rewritten per read: with WiFi up the PHY owns ADC2 and StepAware v0.6.4 never touched it.
    adc_calc_hw_calibration_code(BATTERY_ADC_UNIT, BATTERY_ADC_ATTEN);
    sar_periph_ctrl_adc_oneshot_power_acquire();
    adc_set_hw_calibration_code(BATTERY_ADC_UNIT, BATTERY_ADC_ATTEN);
    sar_periph_ctrl_adc_oneshot_power_release();

    // raw -> mV calibration. IDF 5.5 accepts ADC_UNIT_2 here on the C3: esp_efuse_rtc_calib
    // has no separate ADC2 data on this chip and uses the ADC1 eFuse reference points (see
    // components/efuse/esp32c3/esp_efuse_rtc_calib.c). This replaces IDF 4.4's
    // esp_adc_cal_characterize() that v0.6.4 used, which did the same thing.
    adc_cali_curve_fitting_config_t caliConf{};
    caliConf.unit_id = BATTERY_ADC_UNIT;
    caliConf.chan = BATTERY_ADC_CHANNEL;
    caliConf.atten = BATTERY_ADC_ATTEN;
    caliConf.bitwidth = ADC_BITWIDTH_12;
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&caliConf, &m_caliHandle);
    if (err != ESP_OK) {
        m_caliHandle = nullptr;
        ESP_LOGW(TAG, "eFuse curve-fitting calibration unavailable (%s); using linear fallback",
                 esp_err_to_name(err));
    }

#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    // The learned full-charge factor survives reboots and deep sleep (flash, not RTC).
    m_calPref = global_preferences->make_preference<step_battery::CalibrationPersist>(
        fnv1_hash(CAL_PREF_NAME), true);
    step_battery::CalibrationPersist stored{};
    if (m_calPref.load(&stored)) {
        m_calibrator.restore(stored);
    }
    publishFactor();
#endif
}

bool StepAwareBatterySensor::readRawOnce(uint16_t &raw) {
    bool done;
    bool valid = false;
    {
        // Keep reconfiguration + conversion atomic (~10 us) so nothing on this core
        // reprograms the shared APB_SARADC oneshot registers in between.
        InterruptLock lock;

        // Controller clock: APB source, divider set *before* enabling (div 0 = no output),
        // then the clkm output enable bit, which v0.6.4 found is required on the C3.
        adc_ll_digi_clk_sel(ADC_DIGI_CLK_SRC_DEFAULT);
        adc_ll_digi_controller_clk_div(ADC_LL_CLKM_DIV_NUM_DEFAULT, ADC_LL_CLKM_DIV_B_DEFAULT,
                                       ADC_LL_CLKM_DIV_A_DEFAULT);
        APB_SARADC.apb_adc_clkm_conf.clk_en = 1;
        adc_ll_digi_set_clk_div(ADC_LL_DIGI_SAR_CLK_DIV_DEFAULT);

        adc_oneshot_ll_set_atten(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN);
        adc_oneshot_ll_set_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL);

        adc_oneshot_ll_clear_event(ONESHOT_DONE_EVENT);
        adc_oneshot_ll_disable_all_unit();
        adc_oneshot_ll_enable(BATTERY_ADC_UNIT);
        adc_oneshot_ll_start(false);
        esp_rom_delay_us(START_PULSE_DELAY_US);
        adc_oneshot_ll_start(true);

        uint32_t waitedUs = 0;
        while (!adc_oneshot_ll_get_event(ONESHOT_DONE_EVENT) &&
               waitedUs < CONVERSION_TIMEOUT_US) {
            esp_rom_delay_us(1);
            waitedUs++;
        }
        done = adc_oneshot_ll_get_event(ONESHOT_DONE_EVENT);
        if (done) {
            raw = static_cast<uint16_t>(adc_oneshot_ll_get_raw_result(BATTERY_ADC_UNIT));
            // ADC2 data carries the arbiter result in its channel field; WiFi pre-emption
            // marks the sample invalid.
            valid = adc_oneshot_ll_raw_check_valid(BATTERY_ADC_UNIT, raw);
        }
        adc_oneshot_ll_disable_all_unit();
    }
    return done && valid;
}

float StepAwareBatterySensor::rawToMillivolts(float raw) const {
    if (m_caliHandle != nullptr) {
        int mv = 0;
        int rawInt = static_cast<int>(raw + 0.5f);
        if (adc_cali_raw_to_voltage(m_caliHandle, rawInt, &mv) == ESP_OK) {
            return static_cast<float>(mv);
        }
    }
    return raw / RAW_FULL_SCALE * FALLBACK_FULL_SCALE_MV;
}

void StepAwareBatterySensor::update() {
    uint32_t rawSum = 0;
    uint8_t goodSamples = 0;
    uint32_t failedConversions = 0;

    // Hold the SAR powered for the batch; released back to FSM control afterwards.
    sar_periph_ctrl_adc_oneshot_power_acquire();
    for (uint8_t i = 0; i < m_samples; i++) {
        for (uint8_t attempt = 0; attempt < MAX_RETRIES_PER_SAMPLE; attempt++) {
            uint16_t raw = 0;
            if (readRawOnce(raw)) {
                rawSum += raw;
                goodSamples++;
                break;
            }
            failedConversions++;
            delayMicroseconds(RETRY_DELAY_US);
        }
    }
    sar_periph_ctrl_adc_oneshot_power_release();

    // Need at least half the requested samples; otherwise keep the last published value
    // rather than publishing 0 or a skewed average.
    if (goodSamples == 0 || goodSamples * 2 < m_samples) {
        m_failedUpdates++;
        if (m_failedUpdates == 1 || m_failedUpdates % FAILURE_LOG_EVERY == 0) {
            ESP_LOGW(TAG, "ADC2 read failed (%u/%u samples valid, %" PRIu32 " failed conversions,"
                     " %" PRIu32 " failed updates); keeping last value",
                     goodSamples, m_samples, failedConversions, m_failedUpdates);
        }
        return;
    }

    float rawAvg = static_cast<float>(rawSum) / goodSamples;
    float pinVolts = rawToMillivolts(rawAvg) / 1000.0f;
    float measured = pinVolts * m_dividerRatio;
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    updateCalibration(measured);
#endif
    float volts = m_calibrator.apply(measured);
    ESP_LOGD(TAG, "raw=%.1f (%u/%u samples, %" PRIu32 " retries) -> pin %.3f V, battery %.3f V"
             " (x%.4f)", rawAvg, goodSamples, m_samples, failedConversions, pinVolts, volts,
             m_calibrator.factor());
    this->publish_state(volts);
}

#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
void StepAwareBatterySensor::set_calibration(float fullV, uint32_t stableMs,
                                             float maxSlopeVPerHour, float maxSpreadV) {
    step_battery::CalibrationConfig cfg;
    cfg.fullV = fullV;
    cfg.stableMs = stableMs;
    cfg.maxSlopeVPerHour = maxSlopeVPerHour;
    cfg.maxSpreadV = maxSpreadV;
    // Readings arrive once per update_interval; ask for 2/3 of the window's worth.
    uint32_t interval = this->get_update_interval();
    uint32_t expected = interval > 0 ? stableMs / interval + 1 : 6;
    uint32_t minSamples = expected * 2 / 3;
    cfg.minSamples = static_cast<uint8_t>(minSamples < 5 ? 5 : (minSamples > 60 ? 60 : minSamples));
    m_calibrator.setConfig(cfg);
}

void StepAwareBatterySensor::updateCalibration(float measuredV) {
    if (m_usbSensor == nullptr) {
        return;
    }
    bool usb = m_usbSensor->has_state() && m_usbSensor->state;
    step_battery::CalibrationEvent ev = m_calibrator.onReading(millis(), measuredV, usb);
    if (ev == step_battery::CAL_UPDATED) {
        ESP_LOGI(TAG, "Full-charge plateau at %.3f V measured -> calibration factor %.4f "
                 "(session %u)", m_calibrator.sessionPlateauV(), m_calibrator.factor(),
                 m_calibrator.sessions());
        saveCalibration();
        publishFactor();
    } else if (ev == step_battery::CAL_REJECTED) {
        ESP_LOGW(TAG, "Full-charge plateau found, but factor %.4f is outside %.2f-%.2f; "
                 "check the divider_ratio and wiring. Calibration unchanged.",
                 m_calibrator.lastRejectedFactor(), m_calibrator.getConfig().minFactor,
                 m_calibrator.getConfig().maxFactor);
    }
}

void StepAwareBatterySensor::saveCalibration() {
    step_battery::CalibrationPersist p = m_calibrator.persist();
    if (!m_calPref.save(&p) || !global_preferences->sync()) {
        ESP_LOGE(TAG, "Calibration: flash write failed");
    }
}

void StepAwareBatterySensor::publishFactor() {
    if (m_factorSensor != nullptr) {
        m_factorSensor->publish_state(m_calibrator.isCalibrated() ? m_calibrator.factor() : NAN);
    }
}
#endif  // USE_STEPAWARE_BATTERY_CALIBRATION

void StepAwareBatterySensor::resetCalibration() {
    m_calibrator.reset();
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    saveCalibration();
    publishFactor();
    ESP_LOGI(TAG, "Calibration reset; factor 1.0 until the next full charge");
#endif
}

void StepAwareBatterySensor::dump_config() {
    LOG_SENSOR("", "StepAware Battery (ADC2)", this);
    ESP_LOGCONFIG(TAG,
                  "  Channel: ADC2 CH0 (GPIO5), attenuation 12 dB\n"
                  "  Samples: %u\n"
                  "  ADC calibration: %s\n"
                  "  Divider ratio: %.3f",
                  m_samples, m_caliHandle != nullptr ? "eFuse curve fitting" : "linear fallback",
                  m_dividerRatio);
#ifdef USE_STEPAWARE_BATTERY_CALIBRATION
    const step_battery::CalibrationConfig &cfg = m_calibrator.getConfig();
    ESP_LOGCONFIG(TAG,
                  "  Full-charge calibration: %s\n"
                  "    Factor: %.4f (%u charge sessions)\n"
                  "    Full voltage: %.2f V, flat for %" PRIu32 " s (slope <= %.3f V/h,"
                  " spread <= %.3f V, >= %u readings)",
                  m_calibrator.isCalibrated() ? "calibrated" : "not yet calibrated",
                  m_calibrator.factor(), m_calibrator.sessions(), cfg.fullV, cfg.stableMs / 1000,
                  cfg.maxSlopeVPerHour, cfg.maxSpreadV, cfg.minSamples);
#endif
    LOG_UPDATE_INTERVAL(this);
}

float StepAwareBatterySensor::get_setup_priority() const { return setup_priority::DATA; }

}  // namespace esphome::stepaware_battery

#endif  // USE_ESP32_VARIANT_ESP32C3
