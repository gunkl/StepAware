import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_STATE, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_STEPAWARE_ID, StepAware

DEPENDENCIES = ["stepaware"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_STEPAWARE_ID): cv.use_id(StepAware),
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(
            icon="mdi:state-machine", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_STEPAWARE_ID])
    if conf := config.get(CONF_STATE):
        cg.add(parent.setStateTextSensor(await text_sensor.new_text_sensor(conf)))
