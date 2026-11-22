"""StepAware boot-time button-hold resets (port of main.cpp handleBootButtonHold()).

Hold the mode button while the device boots (power-on / reset / software reboot; not a
deep-sleep wake):
  - released before wifi_reset_hold    -> cancelled, normal boot
  - released after wifi_reset_hold     -> WiFi reset: runtime-saved WiFi credentials
                                          (captive portal / improv) are erased, reboot
  - released after factory_reset_hold  -> factory reset: all ESPHome preferences (NVS) are
                                          erased, reboot
  - still held after max_wait          -> cancelled (stuck button), normal boot

Runs before the GPIO binary sensors are set up, so the core stepaware component never sees
the boot-time hold (no mode change, no long-press reboot).
"""

import logging

import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_OUTPUT

CODEOWNERS = ["@gunkl"]
DEPENDENCIES = ["esp32"]

_LOGGER = logging.getLogger(__name__)

CONF_BUTTON_PIN = "button_pin"
CONF_WIFI_RESET_HOLD = "wifi_reset_hold"
CONF_FACTORY_RESET_HOLD = "factory_reset_hold"
CONF_MAX_WAIT = "max_wait"

# ESP32-C3 strapping pins (esphome/components/esp32/gpio_esp32_c3.py). GPIO0 is not one.
_C3_STRAPPING_PINS = {2, 8, 9}

stepaware_boot_reset_ns = cg.esphome_ns.namespace("stepaware_boot_reset")
StepAwareBootReset = stepaware_boot_reset_ns.class_("StepAwareBootReset", cg.Component)


def _gpio_number(value):
    if isinstance(value, str) and value.upper().startswith("GPIO"):
        value = value[4:]
    value = cv.int_range(min=0, max=21)(value)
    if value in _C3_STRAPPING_PINS:
        _LOGGER.warning(
            "stepaware_boot_reset: GPIO%d is an ESP32-C3 strapping pin; holding it low at "
            "reset can change the boot mode",
            value,
        )
    return value


def _validate_times(config):
    wifi = config[CONF_WIFI_RESET_HOLD].total_milliseconds
    factory = config[CONF_FACTORY_RESET_HOLD].total_milliseconds
    max_wait = config[CONF_MAX_WAIT].total_milliseconds
    if not wifi < factory < max_wait:
        raise cv.Invalid(
            f"need {CONF_WIFI_RESET_HOLD} < {CONF_FACTORY_RESET_HOLD} < {CONF_MAX_WAIT}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StepAwareBootReset),
            # Active-low button with internal pull-up (GPIO0 on StepAware).
            cv.Optional(CONF_BUTTON_PIN, default=0): _gpio_number,
            # Feedback LED; normally the hazard LED's ledc output.
            cv.Optional(CONF_OUTPUT): cv.use_id(output.FloatOutput),
            # BUTTON_WIFI_RESET_MS / BUTTON_FACTORY_RESET_MS in include/config.h
            cv.Optional(CONF_WIFI_RESET_HOLD, default="15s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=2)),
            ),
            cv.Optional(CONF_FACTORY_RESET_HOLD, default="30s"): cv.positive_time_period_milliseconds,
            # Upper bound on how long setup() may block; a longer hold is treated as a stuck
            # button and cancels.
            cv.Optional(CONF_MAX_WAIT, default="40s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(seconds=120)),
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_times,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.setButtonPin(config[CONF_BUTTON_PIN]))
    cg.add(
        var.setHoldTimes(
            config[CONF_WIFI_RESET_HOLD],
            config[CONF_FACTORY_RESET_HOLD],
            config[CONF_MAX_WAIT],
        )
    )

    if CONF_OUTPUT in config:
        full_id, out = await cg.get_variable_with_full_id(config[CONF_OUTPUT])
        cg.add(var.setOutput(out))
        # This component sets up before the outputs (it must run before the button's
        # binary_sensor). If the output is itself a Component (ledc is), it is brought up
        # early through Component::call() so the LED can show feedback.
        if full_id.type.inherits_from(cg.Component):
            cg.add(var.setOutputComponent(out))
