"""Brightness number (0-15) for the StepAware LED matrix."""

import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_RESTORE_VALUE, ENTITY_CATEGORY_CONFIG

from .. import CONF_STEPAWARE_MATRIX_ID, StepAwareMatrix, stepaware_matrix_ns

DEPENDENCIES = ["stepaware_matrix"]

BrightnessNumber = stepaware_matrix_ns.class_(
    "BrightnessNumber", number.Number, cg.Component, cg.Parented.template(StepAwareMatrix)
)

CONFIG_SCHEMA = (
    number.number_schema(
        BrightnessNumber, icon="mdi:brightness-6", entity_category=ENTITY_CATEGORY_CONFIG
    )
    .extend(
        {
            cv.GenerateID(CONF_STEPAWARE_MATRIX_ID): cv.use_id(StepAwareMatrix),
            # Persist the level and re-apply it on boot (overrides the YAML brightness).
            cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await number.new_number(config, min_value=0, max_value=15, step=1)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_STEPAWARE_MATRIX_ID])
    cg.add(var.setRestoreValue(config[CONF_RESTORE_VALUE]))
