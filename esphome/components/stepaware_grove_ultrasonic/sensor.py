from esphome import pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_INPUT,
    CONF_PIN,
    CONF_TIMEOUT,
    ICON_ARROW_EXPAND_VERTICAL,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
)

CONF_PULSE_TIME = "pulse_time"

# Speed of sound 343 m/s, round trip: echo time (us) = distance (m) / (0.000343 / 2).
_M_PER_US_ROUND_TRIP = 0.000343 / 2
# Grove v2.0 rated range is 3.5 m; cap the echo wait so one read never exceeds ~30 ms.
_MAX_TIMEOUT_M = 5.0

stepaware_grove_ultrasonic_ns = cg.esphome_ns.namespace("stepaware_grove_ultrasonic")
GroveUltrasonicSensor = stepaware_grove_ultrasonic_ns.class_(
    "GroveUltrasonicSensor", sensor.Sensor, cg.PollingComponent
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        GroveUltrasonicSensor,
        unit_of_measurement=UNIT_METER,
        icon=ICON_ARROW_EXPAND_VERTICAL,
        accuracy_decimals=2,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            # Shared trigger/echo line. Input by default; the driver enables the output
            # driver only for the trigger pulse.
            cv.Required(CONF_PIN): pins.gpio_pin_schema({CONF_INPUT: True}, internal=True),
            cv.Optional(CONF_TIMEOUT, default="3.5m"): cv.All(
                cv.distance, cv.Range(min=0.02, max=_MAX_TIMEOUT_M)
            ),
            cv.Optional(CONF_PULSE_TIME, default="10us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=cv.TimePeriod(microseconds=100)),
            ),
        }
    )
    .extend(cv.polling_component_schema("100ms"))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_timeout_us(int(config[CONF_TIMEOUT] / _M_PER_US_ROUND_TRIP)))
    cg.add(var.set_pulse_time_us(config[CONF_PULSE_TIME]))
