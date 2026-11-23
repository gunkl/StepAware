"""StepAware deep-sleep power policy (ESPHome external component).

Ports the wake-cause routing and sleep policy of the Arduino firmware's PowerManager.
Relies on ESPHome's deep_sleep component for the actual shutdown; arms the C3's mixed-level
GPIO wake sources (PIRs HIGH, button LOW) itself, which deep_sleep cannot express.

Dormant mode ("switched off", button-only wake, flag persisted in flash) is always built in:
the critical-battery shutdown uses it, and every boot re-enters it unless the button woke
the device. The optional ``dormant:`` block adds the "Shut Down" button, the "Last Shutdown"
sensor, the feedback delay, hold-to-wake and the on_shutdown / on_dormant_wake hooks.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, button, number, select, sensor, switch
from esphome.components import text_sensor
from esphome.components import time as time_
from esphome.components.deep_sleep import DeepSleepComponent
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INITIAL_VALUE,
    CONF_TIME_ID,
    DEVICE_CLASS_BATTERY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)
from esphome.core import CORE
import esphome.final_validate as fv

CODEOWNERS = ["@gunkl"]
DEPENDENCIES = ["esp32", "wifi", "deep_sleep"]


def AUTO_LOAD(config):
    # button only with pir_recalibration (its "Recalibrate PIRs" entity): loading it
    # unconditionally would reserve the id "button" in configs that use it for a sensor.
    base = ["binary_sensor", "number", "select", "sensor", "switch", "text_sensor"]
    if not config or "pir_recalibration" in config or "dormant" in config:
        base.append("button")
    return base


CONF_DEEP_SLEEP_ID = "deep_sleep_id"
CONF_PIR_PINS = "pir_pins"
CONF_BUTTON_PIN = "button_pin"
CONF_USB_POWER_PIN = "usb_power_pin"
CONF_PIR_POWER_PIN = "pir_power_pin"
CONF_STAY_AWAKE = "stay_awake"
CONF_BUTTON_SENSOR = "button_sensor"
CONF_ACTIVITY_SENSORS = "activity_sensors"
CONF_BUSY_CONDITION = "busy_condition"
CONF_LAST_ACTIVITY = "last_activity"
CONF_BOOT_GRACE = "boot_grace"
CONF_CHECKIN_MIN_AWAKE = "checkin_min_awake"
CONF_WIFI_TIMEOUT = "wifi_timeout"
CONF_ON_MOTION_WAKE = "on_motion_wake"
CONF_ON_SLEEP = "on_sleep"
CONF_POWER_SAVING_MODE = "power_saving_mode"
CONF_IDLE_TIMEOUT = "idle_timeout"
CONF_MOTION_WAKE_IDLE = "motion_wake_idle"
CONF_MOTION_WAKE_WIFI = "motion_wake_wifi"
CONF_CHECKIN_INTERVAL = "checkin_interval"
CONF_WAKE_REASON = "wake_reason"
CONF_DEEP_SLEEP_COUNT = "deep_sleep_count"
CONF_SLEEP_BLOCKED = "sleep_blocked"
CONF_SLEEP_BLOCK_REASON = "sleep_block_reason"
CONF_DEFAULT_MODE = "default_mode"

# battery:
CONF_BATTERY = "battery"
CONF_VOLTAGE_SENSOR = "voltage_sensor"
CONF_LOW_THRESHOLD = "low_threshold"
CONF_CRITICAL_THRESHOLD = "critical_threshold"
CONF_HYSTERESIS = "hysteresis"
CONF_MIN_VALID_VOLTAGE = "min_valid_voltage"
CONF_MAX_VALID_VOLTAGE = "max_valid_voltage"
CONF_MIN_SAMPLES = "min_samples"
CONF_CRITICAL_SAMPLES = "critical_samples"
CONF_CRITICAL_SHUTDOWN = "critical_shutdown"
CONF_BATTERY_LOW = "battery_low"
CONF_BATTERY_CRITICAL = "battery_critical"
CONF_BATTERY_STATE = "battery_state"

# pir_recalibration:
CONF_PIR_RECALIBRATION = "pir_recalibration"
CONF_WINDOW_START = "window_start"
CONF_WINDOW_END = "window_end"
CONF_QUIESCENCE = "quiescence"
CONF_COOLDOWN = "cooldown"
CONF_POWER_OFF_TIME = "power_off_time"
CONF_WARMUP = "warmup"
CONF_BUTTON = "button"
CONF_RECALIBRATING = "recalibrating"
CONF_ON_RECALIBRATION_START = "on_recalibration_start"
CONF_ON_RECALIBRATION_END = "on_recalibration_end"

# dormant:
CONF_DORMANT = "dormant"
CONF_SHUTDOWN_BUTTON = "shutdown_button"
CONF_LAST_SHUTDOWN = "last_shutdown"
CONF_FEEDBACK_TIME = "feedback_time"
CONF_WAKE_HOLD = "wake_hold"
CONF_ON_SHUTDOWN = "on_shutdown"
CONF_ON_DORMANT_WAKE = "on_dormant_wake"

MODE_OPTIONS = ["Off", "Deep sleep"]
# ESP32-C3 deep-sleep GPIO wake is limited to the RTC pads GPIO0..GPIO5.
C3_WAKE_PINS = range(0, 6)

stepaware_power_ns = cg.esphome_ns.namespace("stepaware_power")
StepAwarePower = stepaware_power_ns.class_("StepAwarePower", cg.Component)
PowerModeSelect = stepaware_power_ns.class_(
    "PowerModeSelect", select.Select, cg.Parented.template(StepAwarePower)
)
PowerNumber = stepaware_power_ns.class_(
    "PowerNumber", number.Number, cg.Parented.template(StepAwarePower)
)
RecalButton = stepaware_power_ns.class_(
    "RecalButton", button.Button, cg.Parented.template(StepAwarePower)
)
PowerSwitch = stepaware_power_ns.class_(
    "PowerSwitch", switch.Switch, cg.Parented.template(StepAwarePower)
)
ShutdownButton = stepaware_power_ns.class_(
    "ShutdownButton", button.Button, cg.Parented.template(StepAwarePower)
)


def _wake_pin(value):
    value = cv.int_(value)
    if value not in C3_WAKE_PINS:
        raise cv.Invalid("ESP32-C3 deep-sleep GPIO wake only supports GPIO0-GPIO5")
    return value


def _gpio_number(value):
    if isinstance(value, str) and value.upper().startswith("GPIO"):
        value = value[4:]
    return cv.int_range(min=0, max=21)(value)


def _wake_pin_number(value):
    return _wake_pin(_gpio_number(value))


def _number_schema(unit, icon, initial, min_v, max_v):
    return cv.All(
        number.number_schema(
            PowerNumber,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=icon,
            unit_of_measurement=unit,
        ).extend(
            {
                cv.Optional(CONF_INITIAL_VALUE, default=initial): cv.float_range(
                    min=min_v, max=max_v
                ),
            }
        ),
    )


NUMBER_RANGES = {
    # key: (min, max, step)
    CONF_IDLE_TIMEOUT: (10, 3600, 1),
    CONF_MOTION_WAKE_IDLE: (5, 600, 1),
    CONF_CHECKIN_INTERVAL: (0, 1440, 1),
}

def _validate_battery(config):
    low = config[CONF_LOW_THRESHOLD]
    crit = config[CONF_CRITICAL_THRESHOLD]
    if not config[CONF_MIN_VALID_VOLTAGE] < crit < low < config[CONF_MAX_VALID_VOLTAGE]:
        raise cv.Invalid(
            "battery thresholds must satisfy "
            "min_valid_voltage < critical_threshold < low_threshold < max_valid_voltage"
        )
    return config


# Defaults: BATTERY_VOLTAGE_LOW / BATTERY_VOLTAGE_CRITICAL in include/config.h.
BATTERY_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_VOLTAGE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_LOW_THRESHOLD, default=3.3): cv.voltage,
            cv.Optional(CONF_CRITICAL_THRESHOLD, default=3.0): cv.voltage,
            cv.Optional(CONF_HYSTERESIS, default=0.1): cv.All(
                cv.voltage, cv.float_range(min=0.0, max=0.5)
            ),
            cv.Optional(CONF_MIN_VALID_VOLTAGE, default=2.5): cv.voltage,
            cv.Optional(CONF_MAX_VALID_VOLTAGE, default=5.0): cv.voltage,
            cv.Optional(CONF_MIN_SAMPLES, default=3): cv.int_range(min=1, max=20),
            # Never act on a single sample.
            cv.Optional(CONF_CRITICAL_SAMPLES, default=3): cv.int_range(min=2, max=20),
            cv.Optional(CONF_CRITICAL_SHUTDOWN, default=True): cv.boolean,
            cv.Optional(
                CONF_BATTERY_LOW, default={"name": "Battery Low"}
            ): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY, icon="mdi:battery-alert"
            ),
            cv.Optional(
                CONF_BATTERY_CRITICAL, default={"name": "Battery Critical"}
            ): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY, icon="mdi:battery-alert-variant-outline"
            ),
            cv.Optional(CONF_BATTERY_STATE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:battery-heart-variant"
            ),
        }
    ),
    _validate_battery,
)


def _validate_recal(config):
    if config[CONF_WINDOW_START] == config[CONF_WINDOW_END]:
        raise cv.Invalid("window_start and window_end must differ")
    return config


RECAL_SCHEMA = cv.All(
    cv.Schema(
        {
            # Without a time source only the button works (no nightly schedule).
            cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            cv.Optional(CONF_WINDOW_START, default=2): cv.int_range(min=0, max=23),
            cv.Optional(CONF_WINDOW_END, default=4): cv.int_range(min=0, max=23),
            cv.Optional(CONF_QUIESCENCE, default="1h"): cv.positive_time_period_seconds,
            cv.Optional(CONF_COOLDOWN, default="2h"): cv.positive_time_period_seconds,
            # PIR_RECAL_POWER_OFF_MS / PIR_WARMUP_TIME_MS in include/config.h.
            cv.Optional(CONF_POWER_OFF_TIME, default="2s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=30)
                ),
            ),
            cv.Optional(CONF_WARMUP, default="60s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(minutes=5)),
            ),
            cv.Optional(
                CONF_BUTTON, default={"name": "Recalibrate PIRs"}
            ): button.button_schema(
                RecalButton, entity_category=ENTITY_CATEGORY_CONFIG, icon="mdi:motion-sensor"
            ),
            cv.Optional(
                CONF_RECALIBRATING, default={"name": "PIR Recalibrating"}
            ): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:motion-sensor-off"
            ),
            cv.Optional(CONF_ON_RECALIBRATION_START): automation.validate_automation({}),
            cv.Optional(CONF_ON_RECALIBRATION_END): automation.validate_automation({}),
        }
    ),
    _validate_recal,
)

DORMANT_SCHEMA = cv.Schema(
    {
        cv.Optional(
            CONF_SHUTDOWN_BUTTON, default={"name": "Shut Down"}
        ): button.button_schema(ShutdownButton, icon="mdi:power"),
        cv.Optional(
            CONF_LAST_SHUTDOWN, default={"name": "Last Shutdown"}
        ): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:power-sleep"
        ),
        # Power-off icon time before sleeping (Shut Down button and the hold gesture).
        cv.Optional(CONF_FEEDBACK_TIME, default="1500ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=5)),
        ),
        # 0s = a single press wakes it; e.g. 1s = the press must be held 1 s, otherwise it
        # goes straight back to dormant (bag-bump protection).
        cv.Optional(CONF_WAKE_HOLD, default="0s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=5)),
        ),
        cv.Optional(CONF_ON_SHUTDOWN): automation.validate_automation({}),
        cv.Optional(CONF_ON_DORMANT_WAKE): automation.validate_automation({}),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StepAwarePower),
        cv.GenerateID(CONF_DEEP_SLEEP_ID): cv.use_id(DeepSleepComponent),
        cv.Optional(CONF_PIR_PINS, default=[1, 4]): cv.All(
            cv.ensure_list(_wake_pin_number), cv.Length(min=1, max=5)
        ),
        cv.Optional(CONF_BUTTON_PIN, default=0): _wake_pin_number,
        cv.Optional(CONF_USB_POWER_PIN, default=6): _gpio_number,
        cv.Optional(CONF_PIR_POWER_PIN, default=20): _gpio_number,
        cv.Optional(CONF_STAY_AWAKE): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_BUTTON_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_ACTIVITY_SENSORS, default=[]): cv.ensure_list(
            cv.use_id(binary_sensor.BinarySensor)
        ),
        cv.Optional(CONF_BUSY_CONDITION): cv.returning_lambda,
        cv.Optional(CONF_LAST_ACTIVITY): cv.returning_lambda,
        cv.Optional(
            CONF_BOOT_GRACE, default="60s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_CHECKIN_MIN_AWAKE, default="10s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_WIFI_TIMEOUT, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DEFAULT_MODE, default="Off"): cv.one_of(*MODE_OPTIONS),
        cv.Optional(CONF_ON_MOTION_WAKE): automation.validate_automation({}),
        cv.Optional(CONF_ON_SLEEP): automation.validate_automation({}),
        cv.Optional(
            CONF_POWER_SAVING_MODE, default={"name": "Power Saving Mode"}
        ): select.select_schema(
            PowerModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:power-sleep",
        ),
        cv.Optional(
            CONF_IDLE_TIMEOUT, default={"name": "Idle Timeout"}
        ): _number_schema("s", "mdi:timer-sand", 180, 10, 3600),
        cv.Optional(
            CONF_MOTION_WAKE_IDLE, default={"name": "Motion Wake Idle"}
        ): _number_schema("s", "mdi:motion-sensor", 15, 5, 600),
        cv.Optional(
            CONF_CHECKIN_INTERVAL, default={"name": "Check-in Interval"}
        ): _number_schema("min", "mdi:timer-sync-outline", 60, 0, 1440),
        # Off (default) = PIR wakes in Deep sleep mode on battery keep the radio off.
        cv.Optional(
            CONF_MOTION_WAKE_WIFI, default={"name": "WiFi on Motion Wake"}
        ): switch.switch_schema(
            PowerSwitch,
            block_inverted=True,
            default_restore_mode="RESTORE_DEFAULT_OFF",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:wifi-arrow-up-down",
        ),
        cv.Optional(
            CONF_WAKE_REASON, default={"name": "Wake Reason"}
        ): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:alarm"
        ),
        cv.Optional(
            CONF_DEEP_SLEEP_COUNT, default={"name": "Deep Sleep Count"}
        ): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:sleep",
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_SLEEP_BLOCKED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:sleep-off"
        ),
        cv.Optional(CONF_SLEEP_BLOCK_REASON): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:sleep-off"
        ),
        cv.Optional(CONF_BATTERY): BATTERY_SCHEMA,
        cv.Optional(CONF_PIR_RECALIBRATION): RECAL_SCHEMA,
        cv.Optional(CONF_DORMANT): DORMANT_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


def _contains_key(obj, key):
    if isinstance(obj, dict):
        return key in obj or any(_contains_key(v, key) for v in obj.values())
    if isinstance(obj, list):
        return any(_contains_key(v, key) for v in obj)
    return False


def _final_validate(config):
    full = fv.full_config.get()

    # deep_sleep must not arm anything or schedule sleeps on its own: its deep_sleep_() runs
    # after our on_powerdown() and would override / add wake sources.
    ds_path = full.get_path_for_id(config[CONF_DEEP_SLEEP_ID])[:-1]
    ds_conf = full.get_config_for_path(ds_path)
    for key in ("sleep_duration", "run_duration", "wakeup_pin", "wakeup_pin_mode"):
        if key in ds_conf:
            raise cv.Invalid(
                f"stepaware_power owns the sleep policy: remove '{key}' from deep_sleep "
                "(wake sources and timer are armed by stepaware_power)"
            )

    # In safe mode only components registered before the safe-mode check run, so this
    # component is absent. With enable_on_boot: false WiFi would then stay off and the device
    # would be unreachable; require the standard recovery hook.
    wifi_conf = full.get("wifi", {})
    if "safe_mode" in full and not wifi_conf.get("enable_on_boot", True):
        if not _contains_key(full["safe_mode"].get("on_safe_mode", []), "wifi.enable"):
            raise cv.Invalid(
                "wifi enable_on_boot is false: add 'safe_mode: on_safe_mode: - wifi.enable:' "
                "so safe mode is reachable (stepaware_power does not run in safe mode), or "
                "leave enable_on_boot true (stepaware_power turns WiFi off on motion wakes)"
            )

    pins = list(config[CONF_PIR_PINS]) + [config[CONF_BUTTON_PIN]]
    if len(set(pins)) != len(pins):
        raise cv.Invalid("pir_pins and button_pin must be distinct")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    # Register after the safe-mode check: in safe mode this component must not run at all.
    await cg.past_safe_mode()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    ds = await cg.get_variable(config[CONF_DEEP_SLEEP_ID])
    cg.add(var.setDeepSleep(ds))
    for pin in config[CONF_PIR_PINS]:
        cg.add(var.addPirPin(pin))
    cg.add(var.setButtonPin(config[CONF_BUTTON_PIN]))
    cg.add(var.setUsbPowerPin(config[CONF_USB_POWER_PIN]))
    cg.add(var.setPirPowerPin(config[CONF_PIR_POWER_PIN]))
    cg.add(var.setBootGraceMs(config[CONF_BOOT_GRACE]))
    cg.add(var.setCheckinMinAwakeMs(config[CONF_CHECKIN_MIN_AWAKE]))
    cg.add(var.setWifiTimeoutMs(config[CONF_WIFI_TIMEOUT]))
    cg.add(var.setDefaultMode(MODE_OPTIONS.index(config[CONF_DEFAULT_MODE])))

    if CONF_STAY_AWAKE in config:
        cg.add(var.setStayAwakeSensor(await cg.get_variable(config[CONF_STAY_AWAKE])))
    if CONF_BUTTON_SENSOR in config:
        cg.add(var.setButtonSensor(await cg.get_variable(config[CONF_BUTTON_SENSOR])))
    for sid in config[CONF_ACTIVITY_SENSORS]:
        cg.add(var.addActivitySensor(await cg.get_variable(sid)))

    if CONF_BUSY_CONDITION in config:
        lam = await cg.process_lambda(config[CONF_BUSY_CONDITION], [], return_type=cg.bool_)
        cg.add(var.setBusyCondition(lam))
    if CONF_LAST_ACTIVITY in config:
        lam = await cg.process_lambda(config[CONF_LAST_ACTIVITY], [], return_type=cg.uint32)
        cg.add(var.setLastActivity(lam))

    for conf in config.get(CONF_ON_MOTION_WAKE, []):
        await automation.build_automation(var.getMotionWakeTrigger(), [], conf)
    for conf in config.get(CONF_ON_SLEEP, []):
        await automation.build_automation(var.getSleepTrigger(), [], conf)

    sel = cg.new_Pvariable(config[CONF_POWER_SAVING_MODE][CONF_ID])
    await select.register_select(sel, config[CONF_POWER_SAVING_MODE], options=MODE_OPTIONS)
    await cg.register_parented(sel, var)
    cg.add(var.setModeSelect(sel))

    for key, setter in (
        (CONF_IDLE_TIMEOUT, "setIdleTimeoutNumber"),
        (CONF_MOTION_WAKE_IDLE, "setMotionWakeIdleNumber"),
        (CONF_CHECKIN_INTERVAL, "setCheckinIntervalNumber"),
    ):
        conf = config[key]
        min_v, max_v, step = NUMBER_RANGES[key]
        num = cg.new_Pvariable(conf[CONF_ID])
        await number.register_number(num, conf, min_value=min_v, max_value=max_v, step=step)
        await cg.register_parented(num, var)
        cg.add(num.setInitialValue(conf[CONF_INITIAL_VALUE]))
        cg.add(getattr(var, setter)(num))

    sw = await switch.new_switch(config[CONF_MOTION_WAKE_WIFI])
    await cg.register_parented(sw, var)
    cg.add(var.setMotionWakeWifiSwitch(sw))

    ts = await text_sensor.new_text_sensor(config[CONF_WAKE_REASON])
    cg.add(var.setWakeReasonSensor(ts))
    sn = await sensor.new_sensor(config[CONF_DEEP_SLEEP_COUNT])
    cg.add(var.setDeepSleepCountSensor(sn))
    if CONF_SLEEP_BLOCKED in config:
        bs = await binary_sensor.new_binary_sensor(config[CONF_SLEEP_BLOCKED])
        cg.add(var.setSleepBlockedSensor(bs))
    if CONF_SLEEP_BLOCK_REASON in config:
        ts2 = await text_sensor.new_text_sensor(config[CONF_SLEEP_BLOCK_REASON])
        cg.add(var.setSleepBlockReasonSensor(ts2))

    if "safe_mode" in CORE.config:
        # Dormant re-entry boots clear safe_mode's boot-attempt counter (see
        # StepAwarePower::reenterDormantNow()).
        cg.add_define("STEPAWARE_POWER_HAS_SAFE_MODE")

    if "ota" in CORE.config:
        from esphome.components import ota

        ota.request_ota_state_listeners()

    if CONF_BATTERY in config:
        bconf = config[CONF_BATTERY]
        cg.add(var.setBatterySensor(await cg.get_variable(bconf[CONF_VOLTAGE_SENSOR])))
        cg.add(
            var.setBatteryConfig(
                bconf[CONF_LOW_THRESHOLD],
                bconf[CONF_CRITICAL_THRESHOLD],
                bconf[CONF_HYSTERESIS],
                bconf[CONF_MIN_VALID_VOLTAGE],
                bconf[CONF_MAX_VALID_VOLTAGE],
                bconf[CONF_MIN_SAMPLES],
                bconf[CONF_CRITICAL_SAMPLES],
            )
        )
        cg.add(var.setCriticalShutdown(bconf[CONF_CRITICAL_SHUTDOWN]))
        bs = await binary_sensor.new_binary_sensor(bconf[CONF_BATTERY_LOW])
        cg.add(var.setBatteryLowSensor(bs))
        bs = await binary_sensor.new_binary_sensor(bconf[CONF_BATTERY_CRITICAL])
        cg.add(var.setBatteryCriticalSensor(bs))
        if CONF_BATTERY_STATE in bconf:
            ts3 = await text_sensor.new_text_sensor(bconf[CONF_BATTERY_STATE])
            cg.add(var.setBatteryStateSensor(ts3))

    if CONF_PIR_RECALIBRATION in config:
        rconf = config[CONF_PIR_RECALIBRATION]
        cg.add(var.setRecalEnabled(True))
        if CONF_TIME_ID in rconf:
            cg.add(var.setTime(await cg.get_variable(rconf[CONF_TIME_ID])))
        cg.add(
            var.setRecalSchedule(
                rconf[CONF_WINDOW_START],
                rconf[CONF_WINDOW_END],
                rconf[CONF_QUIESCENCE].total_seconds,
                rconf[CONF_COOLDOWN].total_seconds,
            )
        )
        cg.add(var.setRecalTiming(rconf[CONF_POWER_OFF_TIME], rconf[CONF_WARMUP]))
        btn = await button.new_button(rconf[CONF_BUTTON])
        await cg.register_parented(btn, var)
        cg.add(var.setRecalButton(btn))
        bs = await binary_sensor.new_binary_sensor(rconf[CONF_RECALIBRATING])
        cg.add(var.setRecalibratingSensor(bs))
        for conf in rconf.get(CONF_ON_RECALIBRATION_START, []):
            await automation.build_automation(var.getRecalStartTrigger(), [], conf)
        for conf in rconf.get(CONF_ON_RECALIBRATION_END, []):
            await automation.build_automation(var.getRecalEndTrigger(), [], conf)

    if CONF_DORMANT in config:
        dconf = config[CONF_DORMANT]
        cg.add(var.setDormantFeedbackMs(dconf[CONF_FEEDBACK_TIME]))
        cg.add(var.setWakeHoldMs(dconf[CONF_WAKE_HOLD]))
        btn = await button.new_button(dconf[CONF_SHUTDOWN_BUTTON])
        await cg.register_parented(btn, var)
        cg.add(var.setShutdownButton(btn))
        ts4 = await text_sensor.new_text_sensor(dconf[CONF_LAST_SHUTDOWN])
        cg.add(var.setLastShutdownSensor(ts4))
        for conf in dconf.get(CONF_ON_SHUTDOWN, []):
            await automation.build_automation(var.getShutdownTrigger(), [], conf)
        for conf in dconf.get(CONF_ON_DORMANT_WAKE, []):
            await automation.build_automation(var.getDormantWakeTrigger(), [], conf)
