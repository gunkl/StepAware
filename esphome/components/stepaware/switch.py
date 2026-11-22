import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_DIRECTION_DETECTION, CONF_STEPAWARE_ID, StepAware, stepaware_ns

DEPENDENCIES = ["stepaware"]

StepAwareDirectionSwitch = stepaware_ns.class_("StepAwareDirectionSwitch", switch.Switch)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_STEPAWARE_ID): cv.use_id(StepAware),
        # ON = only APPROACHING (far -> near) motion triggers warnings.
        cv.Optional(CONF_DIRECTION_DETECTION): switch.switch_schema(
            StepAwareDirectionSwitch,
            icon="mdi:arrow-collapse-right",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="RESTORE_DEFAULT_ON",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_STEPAWARE_ID])
    if conf := config.get(CONF_DIRECTION_DETECTION):
        s = await switch.new_switch(conf)
        await cg.register_parented(s, config[CONF_STEPAWARE_ID])
        cg.add(parent.setDirectionSwitch(s))
