from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, esp32, sensor
from esphome.components.esp32 import include_builtin_idf_component
from esphome.components.esp32.const import VARIANT_ESP32C3
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

DEPENDENCIES = ["esp32"]

CONF_SAMPLES = "samples"
CONF_DIVIDER_RATIO = "divider_ratio"
CONF_FULL_CHARGE_CALIBRATION = "full_charge_calibration"
CONF_USB_SENSOR = "usb_sensor"
CONF_FULL_VOLTAGE = "full_voltage"
CONF_STABLE_TIME = "stable_time"
CONF_MAX_SLOPE = "max_slope_per_hour"
CONF_MAX_SPREAD = "max_spread"
CONF_FACTOR = "factor"

# The calibrator keeps at most this many readings in its flat window (battery_calibration.h).
MAX_WINDOW_SAMPLES = 64

stepaware_battery_ns = cg.esphome_ns.namespace("stepaware_battery")
StepAwareBatterySensor = stepaware_battery_ns.class_(
    "StepAwareBatterySensor", sensor.Sensor, cg.PollingComponent
)
ResetCalibrationAction = stepaware_battery_ns.class_(
    "ResetCalibrationAction", automation.Action
)

CALIBRATION_SCHEMA = cv.Schema(
    {
        # On USB = charging. Calibration only runs while this is on.
        cv.Required(CONF_USB_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        # The charger's constant-voltage setpoint, in true volts (4.20 V for a standard LiPo).
        cv.Optional(CONF_FULL_VOLTAGE, default="4.20V"): cv.All(
            cv.voltage, cv.float_range(min=3.6, max=4.5)
        ),
        cv.Optional(CONF_STABLE_TIME, default="30min"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(minutes=5), max=cv.TimePeriod(hours=4)),
        ),
        cv.Optional(CONF_MAX_SLOPE, default="0.03V"): cv.All(
            cv.voltage, cv.float_range(min=0.001, max=0.5)
        ),
        cv.Optional(CONF_MAX_SPREAD, default="0.04V"): cv.All(
            cv.voltage, cv.float_range(min=0.005, max=0.5)
        ),
        # The learned gain factor (unknown until the first full charge).
        cv.Optional(CONF_FACTOR): sensor.sensor_schema(
            accuracy_decimals=4,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:tune-vertical",
        ),
    }
)


def _validate_window(config):
    """The flat window has to fit the calibrator's fixed sample buffer."""
    if CONF_FULL_CHARGE_CALIBRATION not in config:
        return config
    interval = config[CONF_UPDATE_INTERVAL]
    stable = config[CONF_FULL_CHARGE_CALIBRATION][CONF_STABLE_TIME]
    if interval.total_milliseconds == 0:
        raise cv.Invalid(f"{CONF_FULL_CHARGE_CALIBRATION} needs a polling update_interval")
    samples = stable.total_milliseconds // interval.total_milliseconds + 1
    if samples > MAX_WINDOW_SAMPLES:
        raise cv.Invalid(
            f"stable_time / update_interval gives {samples} readings per window; at most "
            f"{MAX_WINDOW_SAMPLES} fit. Use a longer update_interval or a shorter stable_time."
        )
    if samples < 6:
        raise cv.Invalid(
            f"stable_time / update_interval gives only {samples} readings per window; "
            "need at least 6."
        )
    return config


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
            # Battery volts = pin volts x divider_ratio, applied before calibration. Leave at 1
            # and use a multiply filter instead only when not using full_charge_calibration.
            cv.Optional(CONF_DIVIDER_RATIO, default=1.0): cv.float_range(min=0.5, max=20.0),
            cv.Optional(CONF_FULL_CHARGE_CALIBRATION): CALIBRATION_SCHEMA,
        }
    )
    .extend(cv.polling_component_schema("60s")),
    _validate_window,
    esp32.only_on_variant(
        supported=[VARIANT_ESP32C3],
        msg_prefix="stepaware_battery (ADC2 channel 0 = GPIO5 register access)",
    ),
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_samples(config[CONF_SAMPLES]))
    cg.add(var.set_divider_ratio(config[CONF_DIVIDER_RATIO]))
    if cal := config.get(CONF_FULL_CHARGE_CALIBRATION):
        cg.add_define("USE_STEPAWARE_BATTERY_CALIBRATION")
        usb = await cg.get_variable(cal[CONF_USB_SENSOR])
        cg.add(var.set_usb_sensor(usb))
        cg.add(
            var.set_calibration(
                cal[CONF_FULL_VOLTAGE],
                cal[CONF_STABLE_TIME].total_milliseconds,
                cal[CONF_MAX_SLOPE],
                cal[CONF_MAX_SPREAD],
            )
        )
        if factor_conf := cal.get(CONF_FACTOR):
            factor = await sensor.new_sensor(factor_conf)
            cg.add(var.set_factor_sensor(factor))
    # Only the eFuse curve-fitting calibration (adc_cali_*) comes from esp_adc. The conversion
    # itself bypasses the esp_adc oneshot driver, so no CONFIG_ADC_ONESHOT_FORCE_USE_ADC2_ON_C3.
    include_builtin_idf_component("esp_adc")


@automation.register_action(
    "stepaware_battery.reset_calibration",
    ResetCalibrationAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(StepAwareBatterySensor)}),
    synchronous=True,
)
async def reset_calibration_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)
