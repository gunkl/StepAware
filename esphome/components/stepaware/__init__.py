"""StepAware core decision logic (modes, button, hazard LED, direction detection).

The decision logic itself is plain C++ in ./core (no ESPHome/Arduino dependency) so it
can be unit-tested natively (test/test_esphome_core). This package is the ESPHome glue.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, output, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import loader

CODEOWNERS = ["@gunkl"]
AUTO_LOAD = ["binary_sensor", "output"]

CONF_STEPAWARE_ID = "stepaware_id"
CONF_ENABLED = "enabled"
CONF_MOTION_NEAR = "motion_near"
CONF_MOTION_FAR = "motion_far"
CONF_BUTTON = "button"
CONF_DISTANCE = "distance"
CONF_DISTANCE_MIN = "distance_min"
CONF_DISTANCE_MAX = "distance_max"
CONF_HAZARD_LED = "hazard_led"
CONF_MATRIX = "matrix"
CONF_DEFAULT_MODE = "default_mode"
CONF_MOTION_WARNING_DURATION = "motion_warning_duration"
CONF_PIR_WARMUP = "pir_warmup"
CONF_LONG_PRESS = "long_press"
CONF_REBOOT_ON_LONG_PRESS = "reboot_on_long_press"
CONF_DORMANT_HOLD = "dormant_hold"
CONF_ON_DORMANT = "on_dormant"
CONF_LED_BRIGHTNESS = "led_brightness"
CONF_BLINK_WARNING = "blink_warning"
CONF_BLINK_FAST = "blink_fast"
CONF_BLINK_SLOW = "blink_slow"
CONF_HAZARD_LED_WITH_MATRIX = "hazard_led_with_matrix"
CONF_DIRECTION_DETECTION = "direction_detection"
CONF_CONFIRMATION_WINDOW = "confirmation_window"
CONF_SIMULTANEOUS_THRESHOLD = "simultaneous_threshold"
CONF_PATTERN_TIMEOUT = "pattern_timeout"
CONF_BATTERY_LOW = "battery_low"
CONF_BATTERY_LEVEL = "battery_level"
CONF_SENSOR_STATUS_PIXELS = "sensor_status_pixels"

stepaware_ns = cg.esphome_ns.namespace("stepaware")
StepAware = stepaware_ns.class_("StepAware", cg.Component)

# Declared by name only so this component builds without stepaware_matrix loaded.
StepAwareMatrix = cg.esphome_ns.namespace("stepaware_matrix").class_("StepAwareMatrix")

# Index order must match step_logic::StepStateMachine::OperatingMode.
MODE_OPTIONS = ["Off", "Continuous", "Motion"]

def _ship_core_sources():
    """Make the build copy ./core/*.h/.cpp as well.

    External components only ship files directly in the package directory. The loader
    can descend into non-package subdirectories (the mechanism ESPHome uses for
    esphome/core/); enable it on this component's cached manifest. Must run after the
    manifest is cached (i.e. not at import time) and before the source tree is copied.
    """
    manifest = loader.get_component("stepaware")
    if manifest is None or not hasattr(manifest, "recursive_sources"):
        raise cv.Invalid("stepaware: this ESPHome version cannot ship core/ sources")
    manifest.recursive_sources = True


def _validate_distance(config):
    if config[CONF_DISTANCE_MIN] >= config[CONF_DISTANCE_MAX]:
        raise cv.Invalid(f"{CONF_DISTANCE_MIN} must be below {CONF_DISTANCE_MAX}")
    return config


def _validate_dormant_hold(config):
    hold = config[CONF_DORMANT_HOLD].total_milliseconds
    if hold and hold <= config[CONF_LONG_PRESS].total_milliseconds:
        raise cv.Invalid(f"{CONF_DORMANT_HOLD} must be longer than {CONF_LONG_PRESS} (or 0s)")
    return config


DIRECTION_SCHEMA = cv.Schema(
    {
        # Initial state of the approaching-only filter; the direction_detection
        # switch (if configured) overrides it with its restored state.
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        cv.Optional(
            CONF_CONFIRMATION_WINDOW, default="5s"
        ): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=1), max=cv.TimePeriod(seconds=30)),
        ),
        cv.Optional(
            CONF_SIMULTANEOUS_THRESHOLD, default="150ms"
        ): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=2)),
        ),
        cv.Optional(CONF_PATTERN_TIMEOUT, default="10s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=5), max=cv.TimePeriod(seconds=60)),
        ),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StepAware),
            cv.Required(CONF_MOTION_NEAR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_MOTION_FAR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_BUTTON): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_HAZARD_LED): cv.use_id(output.FloatOutput),
            cv.Optional(CONF_DISTANCE): cv.use_id(sensor.Sensor),
            # Same unit as the distance sensor (cm for StepAware's ultrasonic).
            # Defaults: SENSOR_MIN/MAX_DISTANCE_CM in include/config.h.
            cv.Optional(CONF_DISTANCE_MIN, default=30.0): cv.float_,
            cv.Optional(CONF_DISTANCE_MAX, default=200.0): cv.float_,
            cv.Optional(CONF_MATRIX): cv.use_id(StepAwareMatrix),
            # Matrix UI inputs (all optional). battery_low: rising edge plays the
            # battery-low notice; battery_level (percent): post-warning battery bitmap.
            cv.Optional(CONF_BATTERY_LOW): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
            # Near/far PIR pixels in the matrix's right column while it is idle.
            cv.Optional(CONF_SENSOR_STATUS_PIXELS, default=True): cv.boolean,
            # Used when there is no mode select, or it has no stored value.
            cv.Optional(CONF_DEFAULT_MODE, default="Motion"): cv.one_of(
                *MODE_OPTIONS, space=" "
            ),
            # Initial values; the HA numbers override them once restored/changed.
            cv.Optional(CONF_MOTION_WARNING_DURATION, default="5s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=1), max=cv.TimePeriod(seconds=300)),
            ),
            cv.Optional(CONF_LED_BRIGHTNESS, default=1.0): cv.percentage,
            cv.Optional(CONF_PIR_WARMUP, default="60s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_LONG_PRESS, default="1s"): cv.positive_time_period_milliseconds,
            # Holds act on release: long_press .. dormant_hold = reboot (if enabled),
            # >= dormant_hold = dormant (on_dormant fires; 0s disables the gesture).
            cv.Optional(CONF_REBOOT_ON_LONG_PRESS, default=True): cv.boolean,
            cv.Optional(CONF_DORMANT_HOLD, default="5s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(seconds=30)),
            ),
            cv.Optional(CONF_ON_DORMANT): automation.validate_automation({}),
            cv.Optional(CONF_BLINK_WARNING, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BLINK_FAST, default="250ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BLINK_SLOW, default="1s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_HAZARD_LED_WITH_MATRIX, default=False): cv.boolean,
            cv.Optional(CONF_DIRECTION_DETECTION, default={}): DIRECTION_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_distance,
    _validate_dormant_hold,
)


async def to_code(config):
    _ship_core_sources()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.setMotionNear(await cg.get_variable(config[CONF_MOTION_NEAR])))
    cg.add(var.setMotionFar(await cg.get_variable(config[CONF_MOTION_FAR])))
    cg.add(var.setButton(await cg.get_variable(config[CONF_BUTTON])))
    cg.add(var.setHazardOutput(await cg.get_variable(config[CONF_HAZARD_LED])))

    if CONF_DISTANCE in config:
        cg.add(var.setDistanceSensor(await cg.get_variable(config[CONF_DISTANCE])))
    cg.add(var.setDistanceRange(config[CONF_DISTANCE_MIN], config[CONF_DISTANCE_MAX]))

    if CONF_MATRIX in config:
        cg.add_define("USE_STEPAWARE_MATRIX")
        cg.add(var.setMatrix(await cg.get_variable(config[CONF_MATRIX])))

    if CONF_BATTERY_LOW in config:
        cg.add(var.setBatteryLowSensor(await cg.get_variable(config[CONF_BATTERY_LOW])))
    if CONF_BATTERY_LEVEL in config:
        cg.add(var.setBatteryLevelSensor(await cg.get_variable(config[CONF_BATTERY_LEVEL])))
    cg.add(var.setSensorStatusPixels(config[CONF_SENSOR_STATUS_PIXELS]))

    cg.add(var.setDefaultMode(MODE_OPTIONS.index(config[CONF_DEFAULT_MODE])))
    cg.add(var.setMotionWarningDurationMs(config[CONF_MOTION_WARNING_DURATION]))
    cg.add(var.setLedBrightness(config[CONF_LED_BRIGHTNESS]))
    cg.add(var.setPirWarmupMs(config[CONF_PIR_WARMUP]))
    cg.add(var.setLongPressMs(config[CONF_LONG_PRESS]))
    cg.add(var.setRebootOnLongPress(config[CONF_REBOOT_ON_LONG_PRESS]))
    cg.add(var.setDormantHoldMs(config[CONF_DORMANT_HOLD]))
    for conf in config.get(CONF_ON_DORMANT, []):
        await automation.build_automation(var.getDormantTrigger(), [], conf)
    cg.add(
        var.setBlinkIntervals(
            config[CONF_BLINK_FAST], config[CONF_BLINK_SLOW], config[CONF_BLINK_WARNING]
        )
    )
    cg.add(var.setHazardLedWithMatrix(config[CONF_HAZARD_LED_WITH_MATRIX]))

    dir_cfg = config[CONF_DIRECTION_DETECTION]
    cg.add(var.setDirectionEnabled(dir_cfg[CONF_ENABLED]))
    cg.add(
        var.setDirectionTiming(
            dir_cfg[CONF_CONFIRMATION_WINDOW],
            dir_cfg[CONF_SIMULTANEOUS_THRESHOLD],
            dir_cfg[CONF_PATTERN_TIMEOUT],
        )
    )
