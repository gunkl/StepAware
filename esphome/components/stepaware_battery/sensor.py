import esphome.codegen as cg
from esphome.components import esp32, sensor
from esphome.components.esp32 import include_builtin_idf_component
from esphome.components.esp32.const import VARIANT_ESP32C3
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

DEPENDENCIES = ["esp32"]

CONF_SAMPLES = "samples"

stepaware_battery_ns = cg.esphome_ns.namespace("stepaware_battery")
StepAwareBatterySensor = stepaware_battery_ns.class_(
    "StepAwareBatterySensor", sensor.Sensor, cg.PollingComponent
)

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        StepAwareBatterySensor,
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            # Number of raw conversions averaged into one published value.
            cv.Optional(CONF_SAMPLES, default=8): cv.int_range(min=1, max=64),
        }
    )
    .extend(cv.polling_component_schema("60s")),
    esp32.only_on_variant(
        supported=[VARIANT_ESP32C3],
        msg_prefix="stepaware_battery (ADC2 channel 0 = GPIO5 register access)",
    ),
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_samples(config[CONF_SAMPLES]))
    # Only the eFuse curve-fitting calibration (adc_cali_*) comes from esp_adc. The conversion
    # itself bypasses the esp_adc oneshot driver, so no CONFIG_ADC_ONESHOT_FORCE_USE_ADC2_ON_C3.
    include_builtin_idf_component("esp_adc")
