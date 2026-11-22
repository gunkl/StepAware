import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_INITIAL_OPTION, CONF_MODE, CONF_RESTORE_VALUE

from . import CONF_STEPAWARE_ID, MODE_OPTIONS, StepAware, stepaware_ns

DEPENDENCIES = ["stepaware"]

StepAwareModeSelect = stepaware_ns.class_("StepAwareModeSelect", select.Select)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_STEPAWARE_ID): cv.use_id(StepAware),
        cv.Optional(CONF_MODE): select.select_schema(
            StepAwareModeSelect, icon="mdi:alert-octagram"
        ).extend(
            {
                cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
                # Falls back to the hub's default_mode when omitted.
                cv.Optional(CONF_INITIAL_OPTION): cv.one_of(*MODE_OPTIONS, space=" "),
            }
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_STEPAWARE_ID])
    if mode_config := config.get(CONF_MODE):
        s = await select.new_select(mode_config, options=MODE_OPTIONS)
        await cg.register_parented(s, config[CONF_STEPAWARE_ID])
        cg.add(s.setRestoreValue(mode_config[CONF_RESTORE_VALUE]))
        if CONF_INITIAL_OPTION in mode_config:
            cg.add(parent.setDefaultMode(MODE_OPTIONS.index(mode_config[CONF_INITIAL_OPTION])))
        cg.add(parent.setModeSelect(s))
