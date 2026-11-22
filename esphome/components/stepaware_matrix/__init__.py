"""StepAware 8x8 LED matrix (HT16K33 I2C backpack) - port of HAL_LEDMatrix_8x8."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_BRIGHTNESS, CONF_DURATION, CONF_ID, CONF_ROTATION, CONF_TYPE

DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_STEPAWARE_MATRIX_ID = "stepaware_matrix_id"
CONF_PIXELS = "pixels"

stepaware_matrix_ns = cg.esphome_ns.namespace("stepaware_matrix")
StepAwareMatrix = stepaware_matrix_ns.class_(
    "StepAwareMatrix", cg.Component, i2c.I2CDevice
)

AnimationType = stepaware_matrix_ns.enum("AnimationType")
ANIMATION_TYPES = {
    "MOTION_ALERT": AnimationType.ANIM_MOTION_ALERT,
    "BATTERY_LOW": AnimationType.ANIM_BATTERY_LOW,
    "BOOT_STATUS": AnimationType.ANIM_BOOT_STATUS,
    "ERROR": AnimationType.ANIM_ERROR,
    "WIFI_CONNECTED": AnimationType.ANIM_WIFI_CONNECTED,
    "WIFI_DISCONNECTED": AnimationType.ANIM_WIFI_DISCONNECTED,
    # Former data/animations/*.txt custom animations, now built in
    "HEART": AnimationType.ANIM_HEART,
    "SPINNER": AnimationType.ANIM_SPINNER,
    "WAVE": AnimationType.ANIM_WAVE,
    "ALERT": AnimationType.ANIM_ALERT,
}

StartAnimationAction = stepaware_matrix_ns.class_("StartAnimationAction", automation.Action)
StopAnimationAction = stepaware_matrix_ns.class_("StopAnimationAction", automation.Action)
ClearAction = stepaware_matrix_ns.class_("ClearAction", automation.Action)
StandbyAction = stepaware_matrix_ns.class_("StandbyAction", automation.Action)
SetBrightnessAction = stepaware_matrix_ns.class_("SetBrightnessAction", automation.Action)
SetSnakeProgressAction = stepaware_matrix_ns.class_(
    "SetSnakeProgressAction", automation.Action
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StepAwareMatrix),
            # MATRIX_BRIGHTNESS_DEFAULT = 15 in include/config.h
            cv.Optional(CONF_BRIGHTNESS, default=15): cv.int_range(min=0, max=15),
            # MATRIX_ROTATION = 0; degrees, same transform as Adafruit_8x8matrix
            cv.Optional(CONF_ROTATION, default=0): cv.one_of(0, 90, 180, 270, int=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x70))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.setInitialBrightness(config[CONF_BRIGHTNESS]))
    cg.add(var.setInitialRotation(config[CONF_ROTATION] // 90))


PARENT_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(StepAwareMatrix)}
)


@automation.register_action(
    "stepaware_matrix.start_animation",
    StartAnimationAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(StepAwareMatrix),
            cv.Required(CONF_TYPE): cv.enum(ANIMATION_TYPES, upper=True, space="_"),
            # 0 = loop until stopped
            cv.Optional(CONF_DURATION, default="0ms"): cv.templatable(
                cv.positive_time_period_milliseconds
            ),
        }
    ),
    synchronous=True,
)
async def start_animation_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    cg.add(var.set_type(ANIMATION_TYPES[config[CONF_TYPE]]))
    duration = await cg.templatable(config[CONF_DURATION], args, cg.uint32)
    cg.add(var.set_duration(duration))
    return var


@automation.register_action(
    "stepaware_matrix.stop_animation",
    StopAnimationAction,
    PARENT_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "stepaware_matrix.clear", ClearAction, PARENT_ACTION_SCHEMA, synchronous=True
)
@automation.register_action(
    "stepaware_matrix.standby", StandbyAction, PARENT_ACTION_SCHEMA, synchronous=True
)
async def simple_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "stepaware_matrix.set_brightness",
    SetBrightnessAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(StepAwareMatrix),
            cv.Required(CONF_BRIGHTNESS): cv.templatable(cv.int_range(min=0, max=15)),
        }
    ),
    synchronous=True,
)
async def set_brightness_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    value = await cg.templatable(config[CONF_BRIGHTNESS], args, cg.uint8)
    cg.add(var.set_brightness(value))
    return var


@automation.register_action(
    "stepaware_matrix.set_snake_progress",
    SetSnakeProgressAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(StepAwareMatrix),
            cv.Required(CONF_PIXELS): cv.templatable(cv.int_range(min=0, max=64)),
        }
    ),
    synchronous=True,
)
async def set_snake_progress_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    value = await cg.templatable(config[CONF_PIXELS], args, cg.uint8)
    cg.add(var.set_pixels(value))
    return var
