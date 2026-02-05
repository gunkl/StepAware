# Voltage Monitoring — ESP32-C3 StepAware

StepAware monitors battery voltage via a 12-bit ADC on GPIO5 and detects USB
power via a digital input on GPIO6.  Battery voltage is factory-calibrated
using per-chip eFuse data burned during manufacturing.  USB input voltage is
NOT measured — GPIO6 has no ADC channel on ESP32-C3 — so only the
connected/disconnected state is reported.

---

## Battery Voltage Circuit

The battery voltage is sensed through a resistive voltage divider that scales
the LiPo cell voltage into the ADC input range.

| Component | Value | Notes |
|-----------|-------|-------|
| R1 (high side) | 100 kOhm | Connected to Battery+ |
| R2 (low side) | 100 kOhm | Connected to GND |
| ADC pin | GPIO5 (ADC2_CH0) | Mid-point of the divider |
| Attenuation | 12 dB | Gives a 0-3.3 V input range |

**Divider ratio:** 2:1.  A fully charged LiPo cell at 4.2 V appears as 2.1 V
at the GPIO5 pin, well within the 0-3.3 V range set by the 12 dB attenuation.

**Current draw through divider at 4.2 V:**
4.2 V / (100 k + 100 k) = 21 uA.  Negligible relative to system current.

**GPIO5 is RESERVED.** Do not connect other devices to it.  See also the GPIO
Pin Restrictions section in `memory.md`.

---

## ADC Calibration

This is the most important section in this document.  The ESP32-C3 ADC
reference voltage varies from chip to chip.  Espressif burns calibration
coefficients into each chip's eFuse array during manufacturing.  The firmware
reads those coefficients at boot and applies them to every raw ADC reading,
converting it directly to calibrated millivolts.

### Libraries used

**Initialisation (called once in `begin()`):**

| Function | Source | What it does |
|----------|--------|--------------|
| `adc_ll_digi_controller_clk_div()` | `hal/adc_ll.h` (ESP-IDF low-level HAL) | Sets the SAR ADC clock divider before enabling the clock |
| `adc_ll_digi_clk_sel()` | `hal/adc_ll.h` | Selects APB as the clock source and un-gates `sar_clk` |
| `adc_ll_digi_set_power_manage()` | `hal/adc_ll.h` | Puts the SAR peripheral into FSM power mode (auto on/off per conversion) |
| `esp_adc_cal_characterize()` | `esp_adc_cal.h` | Reads eFuse calibration coefficients and populates a characteristics struct |

**Per-read (called every time `readBatteryVoltageRaw()` runs):**

| Function | Source | What it does |
|----------|--------|--------------|
| `adc_ll_onetime_set_channel()` | `hal/adc_ll.h` | Selects ADC2, channel 0 (GPIO5) |
| `adc_ll_onetime_set_atten()` | `hal/adc_ll.h` | Configures the 12 dB attenuation |
| `adc_ll_onetime_sample_enable()` | `hal/adc_ll.h` | Arms the one-shot sample trigger |
| `adc_ll_onetime_start()` | `hal/adc_ll.h` | Starts the one-shot conversion |
| `adc_ll_adc2_read()` | `hal/adc_ll.h` | Reads the completed raw 12-bit sample |
| `esp_adc_cal_raw_to_voltage()` | `esp_adc_cal.h` | Converts a raw ADC count to calibrated millivolts |

### Why adc_ll_* for the raw read, but esp_adc_cal for conversion?

The ESP-IDF ADC driver (`libdriver.a`) unconditionally rejects ADC2 on
ESP32-C3 at link time.  The raw ADC read therefore bypasses the driver
entirely, using `adc_ll_*` low-level HAL inline functions instead.
Critically, `esp_adc_cal_characterize()` and `esp_adc_cal_raw_to_voltage()`
are pure eFuse-read and math functions -- they do not touch the ADC driver at
all.  This lets the firmware keep the `adc_ll_*` read path while still
getting factory-calibrated millivolt conversion.

### Calibration methods (priority order, highest to lowest)

1. **TP+Fit** -- Two-Point calibration with fitting-curve coefficients burned
   into eFuse.  Most accurate.  Expected on production ESP32-C3 chips.
2. **Two-Point** -- Two known voltage points read from eFuse.  Good accuracy.
3. **eFuse Vref** -- Reference voltage value stored in eFuse.
4. **default** -- No eFuse calibration data available.  Falls back to a
   hardcoded 3300 mV reference.  Equivalent to the pre-calibration linear
   formula.  Typical on dev boards or chips that were not factory-calibrated.

### How to check which method is active

- **Serial log on boot:**
  ```
  Power: ADC cal method=TP+Fit
  ```
- **Web UI:** the System table has an "ADC Cal" row showing the active method.

---

## USB VBUS Detection

GPIO6 is connected to USB VBUS (5 V) through a 10 kOhm / 10 kOhm voltage
divider.  When USB is connected the divider output is approximately 2.5 V,
which the GPIO reads as HIGH.  When USB is disconnected a software pull-down
holds GPIO6 LOW.

**GPIO6 has no ADC channel on ESP32-C3.** The actual USB input voltage cannot
be measured in firmware.  The firmware reports only the connection state
(connected or disconnected), not a voltage value.

If analog USB voltage measurement is needed in the future, an external I2C ADC
chip (for example, an ADS1115) would be required.  The project already has I2C
available.

---

## Voltage Protection

### Low Battery (default threshold: 3.4 V)

When the filtered battery voltage drops below 3.4 V the device transitions to
`STATE_LOW_BATTERY`.  This state reduces features and activates the low-battery
LED indicator.  The threshold is configurable via the web UI.

### Critical Battery (default threshold: 3.2 V)

When voltage drops below 3.2 V the device transitions to
`STATE_CRITICAL_BATTERY` and enters deep sleep to prevent LiPo over-discharge
damage.  The threshold is configurable via the web UI.

### Boot Protection

On cold boot (not a wake from sleep), if battery voltage reads below 3.2 V and
USB is not connected, the device blinks the status LED three times as a warning
and immediately enters deep sleep.  This prevents attempted operation on a
dangerously depleted cell.

### Overvoltage Warning (> 4.3 V)

If the filtered battery voltage exceeds 4.3 V -- above the 4.2 V
fully-charged LiPo maximum -- a warning is logged to serial:

```
Power: WARNING overvoltage X.XXV (max expected 4.2V)
```

This warning fires once when voltage first exceeds the threshold and does not
repeat until voltage drops back below 4.3 V.  Possible causes: charger
malfunction, ADC sensor error, or a battery pack with a higher nominal voltage
than expected.

---

## Noise Filtering

Raw ADC readings are smoothed through a 10-sample moving-average filter before
any threshold comparison or UI display.  This prevents transient ADC noise
spikes from falsely triggering low-battery or overvoltage events.  The filter
fills progressively on boot: the first reading uses only one sample, and
accuracy improves as the buffer fills toward ten samples.

---

## voltageCalibrationOffset

The `voltageCalibrationOffset` configuration value (set via the web UI,
default 0.0 V) is an additive trim applied after the ADC-to-voltage
conversion, whether that conversion is calibrated or not.  Its purpose is
fine-tuning: if the measured voltage is consistently off by a small amount
even after factory calibration, this offset can correct it.

**Note for units upgraded from pre-calibration firmware:** Before the ADC
calibration feature was added, some units may have had this offset set to a
non-zero value to compensate for the old uncalibrated Vref error.  After
upgrading to firmware that uses factory calibration that compensation is no
longer needed and the offset will over-correct.  If the battery voltage
reading seems wrong after a firmware update, zero out `voltageCalibrationOffset`
in the web UI and re-verify against a bench multimeter.
