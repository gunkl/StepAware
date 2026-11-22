import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_MOVING

from . import CONF_STEPAWARE_ID, StepAware

DEPENDENCIES = ["stepaware"]

CONF_APPROACHING = "approaching"
CONF_LEAVING = "leaving"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_STEPAWARE_ID): cv.use_id(StepAware),
        cv.Optional(CONF_APPROACHING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOVING, icon="mdi:arrow-down-bold"
        ),
        cv.Optional(CONF_LEAVING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOVING, icon="mdi:arrow-up-bold"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_STEPAWARE_ID])
    if conf := config.get(CONF_APPROACHING):
        cg.add(parent.setApproachingSensor(await binary_sensor.new_binary_sensor(conf)))
    if conf := config.get(CONF_LEAVING):
        cg.add(parent.setLeavingSensor(await binary_sensor.new_binary_sensor(conf)))
