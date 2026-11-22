import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_RESTORE_VALUE,
    DEVICE_CLASS_DURATION,
    ENTITY_CATEGORY_CONFIG,
    UNIT_PERCENT,
    UNIT_SECOND,
)

from . import CONF_LED_BRIGHTNESS, CONF_STEPAWARE_ID, StepAware, stepaware_ns

DEPENDENCIES = ["stepaware"]

CONF_WARNING_DURATION = "warning_duration"

StepAwareNumber = stepaware_ns.class_("StepAwareNumber", number.Number)
NumberKind = stepaware_ns.enum("NumberKind", is_class=True)

_RESTORE = {cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_STEPAWARE_ID): cv.use_id(StepAware),
        cv.Optional(CONF_WARNING_DURATION): number.number_schema(
            StepAwareNumber,
            icon="mdi:timer-outline",
            entity_category=ENTITY_CATEGORY_CONFIG,
            device_class=DEVICE_CLASS_DURATION,
            unit_of_measurement=UNIT_SECOND,
        ).extend(_RESTORE),
        cv.Optional(CONF_LED_BRIGHTNESS): number.number_schema(
            StepAwareNumber,
            icon="mdi:brightness-6",
            entity_category=ENTITY_CATEGORY_CONFIG,
            unit_of_measurement=UNIT_PERCENT,
        ).extend(_RESTORE),
    }
)

# key -> (kind, min, max, step)
_NUMBERS = {
    # motionWarningDuration (ConfigManager default 5 s)
    CONF_WARNING_DURATION: (NumberKind.WARNING_DURATION, 1, 300, 1),
    # ledBrightnessFull as a percentage of the 8-bit PWM range
    CONF_LED_BRIGHTNESS: (NumberKind.LED_BRIGHTNESS, 0, 100, 1),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_STEPAWARE_ID])
    for key, (kind, min_value, max_value, step) in _NUMBERS.items():
        if conf := config.get(key):
            n = await number.new_number(
                conf, min_value=min_value, max_value=max_value, step=step
            )
            await cg.register_parented(n, config[CONF_STEPAWARE_ID])
            cg.add(n.setKind(kind))
            cg.add(n.setRestoreValue(conf[CONF_RESTORE_VALUE]))
            cg.add(parent.addNumber(n))
