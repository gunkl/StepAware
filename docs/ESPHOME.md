# StepAware on ESPHome

User guide for the ESPHome build of StepAware (`esphome/` on branch `feature/esphome`).
Why the project moved to ESPHome, and the migration log, are in
[ESPHOME_PLAN.md](ESPHOME_PLAN.md). This page covers what the ESPHome firmware does and how
to run it.

Versions: ESPHome 2026.6.5, ESP-IDF 5.5.4, ESP32-C3 (4 MB flash).

Status markers used below:

- **(verified)**: seen working on the real device.
- **(not verified on hardware)**: compiles and validates, but nobody has tested it on the
  device yet.

## Contents

1. [Hardware and pin map](#hardware-and-pin-map)
2. [Components](#components)
3. [Home Assistant entities](#home-assistant-entities)
4. [Deep sleep and Stay Awake](#deep-sleep-and-stay-awake)
5. [Battery calibration](#battery-calibration)
6. [Dormant mode (switched off)](#dormant-mode-switched-off)
7. [Building, flashing, OTA](#building-flashing-ota)
8. [ESPHome Device Builder (Home Assistant)](#esphome-device-builder-home-assistant)
9. [USB rescue](#usb-rescue)
10. [Boot-time button resets](#boot-time-button-resets)
11. [Optional packages](#optional-packages)
12. [Crash diagnostics](#crash-diagnostics)
13. [Old firmware feature map](#old-firmware-feature-map)

## Hardware and pin map

| GPIO | Function | Notes |
|---|---|---|
| 0 | Mode button | Active low, internal pull-up. Deep-sleep wake source (LOW); the only wake source while dormant. Not a strapping pin on the C3. |
| 1 | PIR near (AM312) | Deep-sleep wake source (HIGH) |
| 2 | Status LED | ESPHome `status_led`. Strapping pin (`ignore_strapping_warning`). |
| 3 | Hazard LED | `ledc` PWM output `hazard_led_pwm` |
| 4 | PIR far (AM312) | Deep-sleep wake source (HIGH); used for direction detection |
| 5 | Battery voltage | ADC2 channel 0, 1:2 divider (`battery_divider_ratio: 2.0`) |
| 6 | USB VBUS detect | HIGH = USB power present |
| 7 / 10 | I2C SDA / SCL | HT16K33 8x8 matrix at 0x70, 100 kHz |
| 20 | PIR power rail | Held on through deep sleep, so the PIRs don't need to warm up again. Driven LOW and not held while dormant (PIRs off). |
| 8 / 9 | Optional ultrasonic | Trigger / echo (HC-SR04), or GPIO8 alone (Grove). Both are strapping pins. See [packages](#optional-packages). |

ESP32-C3 strapping pins are GPIO2, GPIO8 and GPIO9 (`_ESP32C3_STRAPPING_PINS` in ESPHome's
`gpio_esp32_c3.py`). GPIO9 low at reset selects download mode. GPIO0 is a plain RTC GPIO on
the C3, so holding the mode button during reset is safe.

The flash layout is a copy of the old firmware's `partitions.csv` (`esphome/partitions.csv`):

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 20 KB |
| otadata | 0xE000 | 8 KB |
| app0 / app1 | 0x10000 / 0x190000 | 1.5 MB each |
| coredump | 0x310000 | 64 KB |
| spiffs | 0x320000 | 896 KB (unused by ESPHome) |

## Components

Local external components live in `esphome/components/`.

| Component | What it does |
|---|---|
| `stepaware` | Core logic: operating modes (Off / Continuous / Motion), the motion warning state machine, the button, hazard LED patterns and direction detection. The decision logic is plain C++ in `components/stepaware/core/`, unit-tested natively (`pio test -e native_esphome`). It can optionally use a distance sensor (`distance:`). |
| `stepaware_matrix` | HT16K33 8x8 matrix driver and the animations (motion alert, battery low, boot status, error, WiFi connected/disconnected). Has actions `stepaware_matrix.start_animation`, `.stop_animation`, `.clear`, `.standby`, `.set_brightness` and `.set_snake_progress`. `standby` (HT16K33 oscillator off) runs before every deep sleep. |
| `stepaware_battery` | Reads the battery on GPIO5 (ADC2) with low-level register access, so it works while WiFi is on. ESPHome's `adc` times out on ADC2 when WiFi is active. Applies the divider ratio and the learned [full-charge calibration](#battery-calibration). Has action `stepaware_battery.reset_calibration`. |
| `stepaware_power` | Deep-sleep policy. It classifies the wake cause, decides whether WiFi starts on this boot, decides when it is safe to sleep, and arms all three GPIO wake sources and the GPIO20 PIR rail hold. ESPHome's `deep_sleep` can't do mixed-level multi-pin wake on the C3, so this component does it; the actual shutdown still goes through `deep_sleep`. It also owns [dormant mode](#dormant-mode-switched-off): the flag in flash, the boot re-entry and the critical-battery shutdown. |
| `stepaware_grove_ultrasonic` | Sensor platform for the single-pin Grove Ultrasonic Ranger v2.0. Reports metres. |
| `stepaware_boot_reset` | Hold the button at boot for a WiFi reset or a factory reset. See [Boot-time button resets](#boot-time-button-resets). |

Two other parts of the ESPHome port were in progress when this page was written: battery
states and nightly PIR recalibration (`stepaware_power` / `stepaware_battery`), and matrix
UI work (`stepaware` / `stepaware_matrix`). Check those components' `__init__.py` for any
entities added since.

Native ESPHome pieces used: `wifi` with a fallback AP, `captive_portal`, `api` (encrypted),
`ota: esphome`, `safe_mode` (10 failed boots puts it in safe mode, with `wifi.enable` in
`on_safe_mode`), `logger`, `i2c`, `ledc`, `status_led`, `deep_sleep`, `gpio` binary sensors,
and a `homeassistant` binary sensor for Stay Awake.

## Home Assistant entities

These are the names used in `esphome/stepaware.yaml`. Entities marked *(optional)* exist only
if they are configured in the YAML.

### Controls

| Entity | Type | Range / options | Default | Source |
|---|---|---|---|---|
| Operating Mode | select | Off / Continuous / Motion | `default_mode: Motion`, restored after a reboot | `stepaware` select |
| Motion Warning Duration | number (s) | 1–300 | 5 | `stepaware` number `warning_duration` |
| Hazard LED Brightness | number (%) | 0–100 | 100 (`led_brightness`) | `stepaware` number `led_brightness` |
| Direction Detection | switch | ON = only approaching (far → near) motion warns | restore, default ON | `stepaware` switch |
| Matrix Brightness | number | 0–15 | 15 | `stepaware_matrix` number |
| Power Saving Mode | select | Off / Deep sleep | **Off** (`default_mode`) | `stepaware_power` |
| Idle Timeout | number (s) | 10–3600 | 180 | `stepaware_power`: idle time before sleep in WiFi sessions |
| Motion Wake Idle | number (s) | 5–600 | 15 | `stepaware_power`: idle time before sleep after a motion wake (WiFi off) |
| Check-in Interval | number (min) | 0–1440 (0 = no timer wake) | 60 | `stepaware_power`: timer wake while asleep |
| Restart / Restart (Safe Mode) | button | | | native |
| Clear LED Matrix | button | | | template, runs `stepaware_matrix.clear` |
| Reset Battery Calibration | button (config) | | | template, runs `stepaware_battery.reset_calibration`: factor back to 1.0 until the next full charge. See [Battery calibration](#battery-calibration). |
| Recalibrate PIRs | button | | | `stepaware_power` `pir_recalibration`: power-cycles the PIR rail now (2 s off + 60 s warm-up) |
| Shut Down | button | | | `stepaware_power` `dormant`: power-off icon for 1.5 s, then [dormant](#dormant-mode-switched-off) until the mode button is pressed. Refused during an OTA. |

### Sensors

| Entity | Type | Notes |
|---|---|---|
| Motion Near / Motion Far | binary_sensor (motion) | PIRs, GPIO1 / GPIO4 |
| Mode Button | binary_sensor | GPIO0, 50 ms debounce |
| USB Power | binary_sensor (plug) | GPIO6 |
| Approaching / Leaving | binary_sensor (moving) | Direction detector |
| State | text_sensor (diagnostic) | `Off`, `Continuous`, `Motion (warming up)`, `Motion (watching)`, `Motion (warning)`, `Rebooting`, `Shutting down` |
| Battery Voltage | sensor (V) | 8-sample read × divider (`divider_ratio: 2.0`) × [calibration factor](#battery-calibration), 5-point moving average, every 60 s **(verified: 3.80 V, before calibration existed)** |
| Battery Calibration | sensor (diagnostic) | The learned gain factor. Unknown until the first full charge on USB. Kept in flash. |
| Battery | sensor (%) | Linear from 3.0 V (0 %) to 4.2 V (100 %) of the calibrated voltage |
| Battery Low | binary_sensor (battery) | Below 3.4 V (clears above 3.5 V) after 3 plausible readings; false on USB. Triggers the matrix battery-low animation. |
| Battery Critical | binary_sensor (battery) | Below 3.2 V for 3 consecutive plausible readings; false on USB. On battery this puts the device into [dormant](#dormant-mode-switched-off) (button-only wake, persisted), even when Power Saving Mode is Off (`critical_shutdown: true`, as in v0.6.x). |
| Battery State | text_sensor | `Normal`, `Low`, `Critical`, `USB`, `Unknown` |
| PIR Recalibrating | binary_sensor | ON during the 62 s rail power-cycle; PIR inputs read as no-motion meanwhile |
| WiFi Signal | sensor (dBm) | |
| ESPHome Version | text_sensor | |
| Wake Reason | text_sensor (diagnostic) | `Power on`, `Software reset`, `Brownout reset`, `Crash (panic)`, `Watchdog reset`, `Crash reset`, `Motion (PIR)`, `Button`, `Check-in timer`, `Spurious GPIO wake`, `Other sleep wake`. Suffix ` (from dormant)` when this boot woke the device from dormant (e.g. `Button (from dormant)`, or `Power on (from dormant)` when the button was held at power-on); otherwise ` (after critical battery)` / ` (USB)` as before. |
| Last Shutdown | text_sensor (diagnostic) | Why the device last went dormant: `Button hold`, `Home Assistant`, `Critical battery`, `Other`, or `None` (never). Kept in flash; together with Wake Reason it tells HA that it came back from dormant and why it went. |
| Deep Sleep Count | sensor (diagnostic) | Kept in RTC memory; resets on power loss |
| Sleep Blocked | binary_sensor (diagnostic) | *(optional)* ON while something prevents sleep |
| Sleep Block Reason | text_sensor (diagnostic) | *(optional)* `Power saving off`, `On USB power`, `OTA in progress`, `Stay awake`, `Busy`, `Boot grace period`, `Home Assistant check-in`, `Waiting for Home Assistant`, `Recent activity`, `Wake pin active`, `None` |

Packages add more entities:

- `diagnostics.yaml`: Reset Reason, Device Info, Heap Free, Heap Max Block, Heap Min Free,
  Loop Time, Uptime.
- The ultrasonic packages: Distance (cm, throttled to 1/s).

### Device-side options (YAML only)

Options on `stepaware:`:

- `pir_warmup` (60 s)
- `long_press` (1 s)
- `reboot_on_long_press` (true): releasing between `long_press` and `dormant_hold` reboots
- `dormant_hold` (5 s, max 30 s, `0s` disables): hold at least this long, then release, to
  go dormant; must be longer than `long_press`
- `on_dormant`: automation run when the dormant gesture is released (`stepaware.yaml` calls
  `id(power_ctl).requestDormant(step_power::DORMANT_REASON_GESTURE)`)
- `blink_warning`, `blink_fast` and `blink_slow` (500 ms, 250 ms, 1 s)
- `hazard_led_with_matrix` (false: with a matrix present, motion warnings show only on the
  matrix)
- `distance`, `distance_min` and `distance_max` (30 / 200, in the distance sensor's unit, cm)
- `direction_detection`: `enabled`, `confirmation_window` (5 s),
  `simultaneous_threshold` (150 ms default, 100 ms in `stepaware.yaml`),
  `pattern_timeout` (10 s)
- `battery_low` (binary_sensor id) and `battery_level` (sensor id, %): drive the battery-low
  notice and the post-warning battery bitmap
- `sensor_status_pixels` (true): light two pixels per PIR while it is high, as v0.6.x did when idle

Matrix display behaviour (ported from v0.6.x):

- Mode change: a 2 s mode icon (X = Off, filled circle = Continuous, eye = Motion). Continuous
  starts its arrow loop after the icon.
- Holding the button (from 1 s): the refresh symbol with a countdown bar filling the top row;
  at 5 s it turns into the power-off symbol. Release on the refresh symbol = reboot (the
  symbol stays 2 s), release on the power-off symbol = dormant (it stays until the device
  sleeps, 1.5 s). Without a matrix the hazard LED shows the same: fast blink = reboot,
  solid = dormant.
- After a motion warning in Motion mode: the WiFi status icon for 2 s, then the battery level for
  2 s, then dark.
- First WiFi connect per boot: the WiFi emblem for 2 s.
- Idle: dark apart from the PIR status pixels.
- `stepaware_matrix.start_animation` types: `MOTION_ALERT`, `BATTERY_LOW`, `BOOT_STATUS`,
  `ERROR`, `WIFI_CONNECTED`, `WIFI_DISCONNECTED`, `HEART`, `SPINNER`, `WAVE`, `ALERT`.

Options on `stepaware_power:`:

- `pir_pins`, `button_pin`, `usb_power_pin` and `pir_power_pin`
- `boot_grace` (60 s)
- `checkin_min_awake` (10 s)
- `wifi_timeout` (30 s)
- `busy_condition` and `last_activity` lambdas
- `activity_sensors`
- `on_motion_wake` and `on_sleep` automations (`stepaware.yaml`: `on_sleep` calls
  `id(stepaware_core).prepareForSleep()`: blank, HT16K33 standby, hazard LED off)
- `dormant:` *(optional block; dormant itself is always active)*: `shutdown_button` (Shut
  Down), `last_shutdown` (Last Shutdown), `feedback_time` (1.5 s power-off icon before
  sleeping), `wake_hold` (`0s` = a single press wakes it; e.g. `1s` = the waking press must be
  held 1 s or it goes straight back to dormant, against bumps in a bag), `on_shutdown`
  (`stepaware.yaml`: show the power-off icon) and `on_dormant_wake` (`stepaware.yaml`: the
  waking press is not counted as a click)
- `battery:`: `voltage_sensor`, `low_threshold` (3.4 V in `stepaware.yaml`),
  `critical_threshold` (3.2 V), `hysteresis` (0.1 V), `min_valid_voltage` / `max_valid_voltage`
  (2.5 / 5.0 V; readings outside are ignored), `min_samples` (3), `critical_samples` (3),
  `critical_shutdown` (true)
- `pir_recalibration:`: `time_id` (`ha_time`), `window_start` / `window_end` (02–04 local),
  `quiescence` (1 h without motion), `cooldown` (2 h), `power_off_time` (2 s), `warmup` (60 s),
  `on_recalibration_start` / `on_recalibration_end`. Runs at most once per night and only while
  awake, so in Deep sleep mode it happens on a check-in that lands in the window.

### Button

Holds act when you **release** the button, so you can see which action you'll get first.

| Action | Feedback | Effect |
|---|---|---|
| Short press (under 1 s) | mode icon | Cycles the mode Off → Continuous → Motion → Off, immediately on release. |
| Hold 1 s to under 5 s, release | refresh symbol + countdown bar, LED fast blink | Reboots (refresh symbol for 2 s). |
| Hold 5 s or longer, release | power-off symbol, LED solid | [Dormant](#dormant-mode-switched-off): "switched off" until the next press. |
| Press while dormant | | Wakes it (WiFi on, as after a normal button wake). This press is not counted as a click. |
| Hold at power-on / reboot | | Boot-time resets (see below). Also wakes a dormant device. |

## Deep sleep and Stay Awake

Power Saving Mode is **Off** by default, so the device never sleeps until you set it to
**Deep sleep** in HA. The device then shows as unavailable while it sleeps.
**(Deep sleep: not verified on hardware.)**

**Wake policy** (`stepaware_power`):

| Wake cause | WiFi | Session |
|---|---|---|
| PIR (GPIO1/GPIO4 HIGH) | Off | Warning, then sleep again after *Motion Wake Idle* |
| Button (GPIO0 LOW; wins over a PIR) | On | Interactive: sleeps after *Idle Timeout* |
| Check-in timer | On | Reports, reads Stay Awake, sleeps once HA has had `checkin_min_awake` (10 s), or after `wifi_timeout` (30 s) if HA never connects |
| Power-on / reset / OTA reboot / crash / brownout | On | Interactive, plus a 60 s boot grace period |
| USB power present | On | Never sleeps while on USB |

Sleep is blocked while any of these is true:

- Power Saving Mode is Off
- USB power is present
- an OTA is running
- Stay Awake is on
- a warning is active
- the boot grace period hasn't ended
- HA is still checking in
- the idle time hasn't elapsed
- a wake pin is still active

*Sleep Block Reason* shows which one applies.

### Stay Awake

1. In HA, create a Toggle helper named **stepaware_stay_awake** (Settings > Devices &
   services > Helpers). This gives the entity `input_boolean.stepaware_stay_awake`. The device
   reads it through a `homeassistant` binary sensor.
2. Turn it on. The device only sees it while WiFi and the API are connected: on the next
   button wake, check-in, boot or USB plug-in. Motion wakes keep WiFi off and don't read it.
3. When *Sleep Block Reason* shows `Stay awake`, the device stays up for OTA or debugging.
   Turn the helper off when you're done. That starts a fresh idle period.

If the helper is missing or unavailable, the normal sleep policy applies.

### Recovering from unexpected sleep

Try these in order:

1. **Press the button.** The device wakes with WiFi on and stays up for *Idle Timeout*
   (180 s). In that time, set Power Saving Mode to Off or turn on Stay Awake. This also
   wakes it from [dormant](#dormant-mode-switched-off), which USB, check-ins and safe mode
   never do.
2. **Plug in USB.** While VBUS is present the device never sleeps and WiFi comes on. This
   works whenever it next wakes; press the button to wake it at once.
3. **Wait for the check-in.** It runs every *Check-in Interval* (60 min) if the interval
   isn't 0.
4. If it boot-loops or never reaches WiFi: `safe_mode` starts after 10 failed boots and comes
   up with WiFi and OTA only. Failing that, use the [USB rescue](#usb-rescue).

## Battery calibration

**Why.** GPIO5 sees the battery through a resistor divider and the ADC. The divider's tolerance
and the ADC's gain error make the reading off by a fixed percentage: 1 % resistors alone allow
about ±2 %, roughly ±80 mV at 4.2 V. The 3.4 V / 3.2 V thresholds compare against that reading,
so without correction the cell could reach a lower true voltage than intended before the
critical shutdown.

**How.** On USB, the charger's constant-voltage phase holds the cell at a fixed voltage,
nominally 4.20 V. `stepaware_battery` watches for a flat reading on USB:
- **What counts as flat:** over the last `stable_time` (30 min), a least-squares slope of at
  most 0.03 V/h and at most 40 mV between the lowest and highest reading.
- **The correction:** factor = 4.20 V ÷ that reading. It's applied inside the sensor, so
  *Battery Voltage*, the low / critical thresholds and *Battery* (%) all use corrected volts.
- **Which plateau:** within one charge, the highest flat plateau wins. Before the
  constant-voltage phase the reading is still rising, and after the charger stops the cell
  relaxes below it.
- **Repeated charges:** each charge that finds a plateau blends into the stored factor.
  Charge 2 counts ½, charge 3 ⅓, and from charge 4 on each counts ¼.
- **Plausibility:** a factor outside 0.90–1.10 is rejected and logged as a warning. That much
  error means a wrong `divider_ratio` or a wiring fault, not tolerance.
- **Storage:** the factor is in flash (NVS key `stepaware_battery_cal`). It survives reboots,
  deep sleep and OTA. A factory reset erases it.

**Limits.**
- Only the gain error is corrected. An offset would need a second known voltage. After eFuse
  calibration the ADC's offset is small next to the divider error.
- The charger's setpoint has its own tolerance, typically ±0.5–1 %. If the charger has a short
  constant-voltage phase, the window may catch the relaxed voltage after it stops instead,
  which reads up to about 1 % low.
- Until the first full charge, the factor is 1.0 and everything behaves as before.

**Options** (`sensor: platform: stepaware_battery`):

```yaml
divider_ratio: 2.0            # battery V = pin V x this (replaces the old multiply filter)
full_charge_calibration:
  usb_sensor: usb_power       # binary_sensor; calibration only runs while on
  full_voltage: 4.20V         # charger CV setpoint in true volts (4.35V for a LiHV cell)
  stable_time: 30min          # flat window; stable_time / update_interval must fit 64 readings
  max_slope_per_hour: 0.03V   # optional
  max_spread: 0.04V           # optional
  factor:
    name: "Battery Calibration"
```

Action: `stepaware_battery.reset_calibration: battery_voltage`. The *Reset Battery Calibration*
button runs it. Logs at INFO when a plateau is learned, and at WARN when one is rejected.

**Not verified on hardware yet:** a real charge cycle (plateau detection, then the factor
being stored and applied).

## Dormant mode (switched off)

Dormant is StepAware's "off switch": deep sleep with the **mode button as the only wake
source**, the PIRs unpowered and the matrix in standby, so the battery lasts and charges
faster. **(Not verified on hardware.)**

**Entering it**

- Hold the button **5 s or longer and release** (the power-off symbol shows from 5 s).
- Press **Shut Down** in Home Assistant.
- Automatically on a **confirmed critical battery** (Battery Critical, 3 readings below 3.2 V,
  on battery only; `critical_shutdown: true`).

The power-off icon shows for 1.5 s, then the device sleeps. If the button is still held it
waits for the release (the press would wake it at once).

Dormant overrides Power Saving Mode Off, USB power, Stay Awake and every idle / busy rule. The
one exception: a gesture or Shut Down **during an OTA is refused** (the icon goes away again),
and an OTA that starts during the 1.5 s cancels it.

**While dormant**

| Item | State |
|---|---|
| Wake sources | GPIO0 LOW (button) only. No PIR, no check-in timer. USB (GPIO6) can't wake the C3 from deep sleep at all. |
| PIR rail (GPIO20) | Driven LOW, not held (the pad floats once the chip sleeps): PIRs unpowered. PIR inputs get pull-downs instead of pull-ups so they can't back-feed the sensors. |
| Matrix | HT16K33 standby (display off, oscillator off). This now happens before **every** deep sleep, not only dormant. |
| Hazard LED | Off |
| Flag | `{active, reason}` in flash (NVS), so it survives total power loss |

**Waking it.** Press the button. It boots with WiFi on, like a normal button wake (Idle
Timeout applies if Power Saving Mode is Deep sleep), Wake Reason reads `Button (from
dormant)`, and Last Shutdown says why it went. The waking press does not change the mode.
With `wake_hold: 1s` the press must be held 1 s, otherwise it goes straight back to sleep.

**Every boot checks the flag first** (`stepaware_power` setup, before WiFi, I2C or any
entity):

| Boot | Button | Result |
|---|---|---|
| Deep-sleep wake by the button | | Wakes (flag cleared) |
| Any other deep-sleep wake | | Back to dormant |
| Power-on, brownout, crash, software reset | not held | Back to dormant (within a few ms; WiFi never starts) |
| Power-on, brownout, crash, software reset | held | Wakes (flag cleared), then the boot-time resets run as usual |

USB never changes the result. So:

- **Plugging in USB while dormant** leaves it asleep and charging.
- **After a flat battery**, plugging in powers the chip up; it sees the flag and goes straight
  back to dormant while it charges. Press the button when you want it on.
- **Boot-time resets from dormant:** hold the button while you power it up (battery and USB
  connected / reconnected). Holding at power-on clears the dormant flag first, then the 15 s /
  30 s WiFi / factory reset works as before. A factory reset also erases the flag.
- Re-entry boots don't count as failed boots for `safe_mode` (its boot counter is cleared on
  the re-entry path), so repeated USB plug-ins or a brownout loop can't land it in safe mode.

**Expected current while dormant** *(estimates; measure with a multimeter in series with
the battery)*:

| Part | Dormant | Notes |
|---|---|---|
| ESP32-C3 deep sleep, GPIO wake only | ~5 µA | datasheet figure for deep sleep with RTC timer; no RTC peripherals are kept on |
| PIRs (2 × AM312) | 0 | rail off (the normal deep sleep keeps them on: ~10–15 µA each) |
| HT16K33 standby | ~10 µA max (datasheet) | a blank but running HT16K33 draws far more; that is what standby fixes |
| GPIO0 pull-up | 0 | only while the button is pressed |
| Battery divider on GPIO5 | V<sub>bat</sub> / (R1 + R2) | **hardware**: always connected; e.g. 2 × 100 kΩ = ~20 µA at 4 V |
| Board power LED, LDO quiescent, charger IC | board-dependent | **hardware**: can be the largest drain (a power LED is often 1–5 mA) |

So the firmware's share should be in the tens of µA; whatever the meter shows above that is
board-level drain (divider, LDO, LEDs, charger) and needs a hardware change to remove.

**Recovery.** If the device "won't turn on": press the mode button once. If it still seems
dead, it may be in dormant with a flat battery: charge it, then press the button. To clear
dormant without the button working, power-cycle with the button held, or USB-flash
`firmware.factory.bin` (erases NVS, including the flag).

## Building, flashing, OTA

Everything runs in the `stepaware-dev` container. From the Windows host (Git Bash) use:

```bash
MSYS_NO_PATHCONV=1 docker exec -it -u dev -w /workspace/StepAware/esphome stepaware-dev bash
```

Then, inside the container:

```bash
esphome config stepaware.yaml                              # validate
esphome compile stepaware.yaml                             # build
esphome upload stepaware.yaml --device 10.123.0.98         # native OTA (verified)
esphome logs stepaware.yaml --device 10.123.0.98           # API log stream
```

- Secrets come from `esphome/secrets.yaml` (gitignored). Copy `secrets.yaml.example` and fill
  it in.
- Build output goes to `esphome/.esphome/build/stepaware/build/`. (ESPHome 2026.6.x built
  through PlatformIO into `.pioenvs/stepaware/`. Since 2026.11 it calls ESP-IDF directly, and
  the toolchain lives in `~/.cache/esphome/idf/` instead of `~/.platformio`.)
  - `firmware.ota.bin`: for OTA
  - `firmware.factory.bin`: for a full USB flash at 0x0, bootloader and partition table
    included
  - `firmware.elf`: for crash decoding
- The app slot is 1.5 MB. `esphome compile` prints the flash usage.
- For OTA with deep sleep on: turn on Stay Awake, wake the device (button), and confirm
  *Sleep Block Reason* = `Stay awake` before you upload.
- The `test_*.yaml` files in `esphome/` are build-only checks for each component (names
  `stepaware-test-*`). **Never flash them to the device.**

## ESPHome Device Builder (Home Assistant)

The dev container does all development and testing. Home Assistant's ESPHome Device Builder
builds the same config straight from GitHub, so the device shows in its dashboard with
Logs, Install and Validate.

**How the config is split:**

| File | Role |
|---|---|
| `esphome/stepaware_base.yaml` | The whole device config. It has no paths relative to the top-level YAML, so it works as a local `!include` and as a remote package. |
| `esphome/stepaware.yaml` | Container entry point. Includes the base, adds the local `components/` folder and `partitions.csv`. |
| `esphome/ha/stepaware.yaml` | Device Builder entry point. Pulls the base and `components/` from `gunkl/StepAware@feature/esphome`. |

Both entry files list the same external components; keep those lists the same. ESPHome
resolves `partitions:` and local `external_components` paths relative to the top-level YAML,
never inside a remote package, which is why the entry files hold them.

**One-time setup in Home Assistant:**

1. Copy `esphome/ha/stepaware.yaml` to `/config/esphome/stepaware.yaml`.
2. Copy `esphome/partitions.csv` to `/config/esphome/stepaware-partitions.csv`. The table
   never changes, because OTA never rewrites the table on the device.
3. Add the keys from `secrets.yaml.example` to HA's `/config/esphome/secrets.yaml`. Use the
   same `api_encryption_key` and `ota_password` as the device. The `!secret` tags in the
   GitHub package resolve from HA's file.

**Day to day:** test in the container, commit, push. The wrapper uses `refresh: 0s`, so the
Device Builder fetches the branch on every Validate or Install. Nothing that isn't pushed
reaches HA. The Device Builder builds with its own ESPHome version, which may differ from
the container's.

Checked on 2026-11-22 against a local git copy of the branch: the wrapper validates with the
same config as `stepaware.yaml` (apart from the two paths), its secrets come from the HA-side
file, and a new commit is picked up on the next build.

## USB rescue

USB serial is the C3's built-in USB-Serial-JTAG. The container has no USB access, so run
esptool on Windows (`pip install esptool`):

```bash
# copy the image out of the container
docker cp stepaware-dev:/workspace/StepAware/esphome/.esphome/build/stepaware/build/firmware.factory.bin .

esptool --chip esp32c3 --port COM5 write-flash 0x0 firmware.factory.bin
```

- `write-flash` is esptool v5 syntax; v4 uses `write_flash`.
- If esptool can't connect, enter download mode: hold the board's BOOT button (GPIO9 on C3
  boards, **not** the StepAware mode button on GPIO0) and tap reset or replug USB. This
  depends on the board.
- To return to the old firmware, write back the full-flash backup with
  `write-flash 0x0 stepaware-flash-backup-2026-11-22.bin`. See the migration log in
  ESPHOME_PLAN.md.
- `firmware.factory.bin` runs from 0x0 to the end of app0, and its nvs range (0x9000–0xDFFF)
  is padded with 0xFF. Writing it at 0x0 therefore **erases NVS**:
  - WiFi settings saved at runtime
  - entity values
  - Power Saving Mode, which comes back as Off

  The device boots with its YAML defaults. To update only the app over USB and keep NVS,
  write `firmware.ota.bin` at the running slot instead: `write-flash 0x10000 firmware.ota.bin`
  for app0. Check which slot is active in the boot log first.

## Boot-time button resets

`stepaware_boot_reset` is a port of `handleBootButtonHold()`. **(Not verified on hardware.)**

```yaml
external_components:
  - source: {type: local, path: components}
    components: [stepaware_boot_reset]     # add to the existing list

stepaware_boot_reset:
  button_pin: GPIO0
  output: hazard_led_pwm
  wifi_reset_hold: 15s       # BUTTON_WIFI_RESET_MS
  factory_reset_hold: 30s    # BUTTON_FACTORY_RESET_MS
  max_wait: 40s              # cap on how long boot may block
```

**How to use it.** Hold the button while the device boots: power-on, reset, or a software
reboot. The reboot gesture now acts on release, so there are three ways:

- Press **Restart** in Home Assistant, then hold the button before it comes back up.
- Hold the button 1–5 s and release (refresh symbol), then **press and keep holding** at
  once: the reboot follows 2 s after the release, with the button held.
- Power it up (battery / USB) with the button held. This also works from dormant: holding at
  power-on clears the dormant flag first.

| Hold time (from boot) | Hazard LED | Release does |
|---|---|---|
| under 15 s | slow pulse (2 s cycle) | Cancels; normal boot |
| 15–30 s | fast blink (250 ms) | **WiFi reset**: 3 short blinks, then reboot |
| 30–40 s | solid on | **Factory reset**: on for 2 s, then reboot |
| over 40 s | off | Treated as a stuck button and cancelled; normal boot |

**WiFi reset.** Erases the WiFi credentials that ESPHome saved at runtime:

- `WiFiComponent::save_wifi_sta()`, used by the captive portal and improv, stores the SSID
  and password in NVS (namespace `esphome`).
- The key is `App.get_config_version_hash()` (88491487 when the YAML has no compiled network).
- Fast-connect data is stored at key + 1.
- The component erases those keys with `nvs_erase_key()`.

After the reboot WiFi uses the credentials compiled from `secrets.yaml`. If it can't connect,
it falls back to the `StepAware Fallback` AP and the captive portal. All other settings are
kept.

**Factory reset.** Calls `global_preferences->reset()`, the same as ESPHome's `factory_reset`
component. That erases the whole NVS partition:

- runtime WiFi credentials
- operating mode
- numbers and switches
- power-saving mode
- the safe-mode boot counter

Every value then comes back up at its YAML default. Power Saving Mode returns to Off, so the
device won't sleep after a factory reset.

**Details:**

- The check runs in `setup()` at priority HARDWARE + 10. That is after `stepaware_power`, and
  before the GPIO binary sensors and ledc outputs. It reads the button straight from the GPIO,
  so the core never sees the boot-time hold: no mode change, and no long-press reboot loop.
- It brings the `ledc` output up early (through `Component::call()`) to drive the LED.
- It feeds the watchdog while it waits.
- It doesn't run on **deep-sleep wakes**, so a button wake still reaches the core as a normal
  press (except a wake from dormant, whose press is swallowed). On a sleeping device: wake
  it, then use one of the ways above.
- With the dormant flag set, `stepaware_power` (which runs first) only lets a reset boot
  through when the button is held, and then clears the flag, so this check always sees a
  normal, non-dormant boot.
- Both resets reboot with `esp_restart()`. They don't use `App.safe_reboot()`, because most
  components haven't been set up yet at that point.

## Optional packages

The files in `esphome/packages/` can each be added with `packages:` in the main YAML.
`esphome/test_misc.yaml` and `test_misc_grove.yaml` compile them.

```yaml
packages:
  time: !include packages/time.yaml
  diagnostics: !include packages/diagnostics.yaml
  ultrasonic: !include packages/ultrasonic_hcsr04.yaml    # or packages/ultrasonic_grove.yaml
```

| Package | What it adds |
|---|---|
| `time.yaml` | `time:` with `homeassistant` (id `ha_time`) and an `sntp` fallback (id `sntp_time`, pool.ntp.org). The timezone comes from substitution `timezone`, default `America/Los_Angeles`. The old firmware used a fixed UTC-8 with no DST. Nightly PIR recalibration needs local time; without a valid clock it never runs. Time syncs only during WiFi sessions; the system clock keeps running, and drifting, through deep sleep. |
| `diagnostics.yaml` | `debug:` with Reset Reason, Device Info, Heap Free, Heap Max Block, Heap Min Free and Loop Time; an `uptime` sensor; and the ESP-IDF core dump to flash (see [Crash diagnostics](#crash-diagnostics)). |
| `ultrasonic_hcsr04.yaml` | HC-SR04 on GPIO8 (trigger) and GPIO9 (echo) using ESPHome `ultrasonic`. The echo needs level shifting from 5 V. |
| `ultrasonic_grove.yaml` | Grove ranger on GPIO8 using `stepaware_grove_ultrasonic`. It adds its own `external_components` entry. |

**Ultrasonic packages:**

- Both define the internal, unthrottled sensor `distance_cm` **in cm**, for the core, plus a
  throttled HA copy named *Distance*. Use only one of the two packages.
- To make the distance trigger warnings, wire it into the core:

  ```yaml
  stepaware:
    distance: distance_cm
    distance_min: 30    # cm
    distance_max: 200   # cm
  ```

- The core compares the sensor's state directly with `distance_min` / `distance_max` and
  treats NaN as "no target". The packages therefore turn timeouts into NaN rather than
  filtering them out. `filter_out: nan` or `clamp` would hold the last in-range reading and
  leave the warning on.
- GPIO8 and GPIO9 are strapping pins (`ignore_strapping_warning: true`). Make sure the sensor
  never holds GPIO9 low during reset.

None of the packages has been verified on hardware.

## Crash diagnostics

`packages/diagnostics.yaml` adds this to `esp32: framework:`:

```yaml
advanced:
  include_builtin_idf_components: [espcoredump]
sdkconfig_options:
  CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH: y
  CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF: y
  CONFIG_ESP_COREDUMP_CHECKSUM_CRC32: y
```

- This is the Kconfig version of the old `-D CONFIG_ESP_COREDUMP_*` flags in `platformio.ini`.
- ESPHome leaves IDF's `espcoredump` component out of the build by default. Without
  `include_builtin_idf_components`, the sdkconfig options are silently dropped.
- The cached `sdkconfig.<name>` in the build directory keeps old Kconfig values. After adding
  this package to an existing build, run `esphome clean stepaware.yaml` before you compile.
  Then confirm that `.esphome/build/stepaware/sdkconfig.stepaware` contains
  `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`. The core dump adds about 12.5 KB of flash.
- Package dicts merge key by key, so these options are added to the main file's `esp32:`
  block.
- On a panic, IDF writes an ELF core dump to the `coredump` partition (0x310000, 64 KB) before
  it reboots. *Wake Reason* / *Reset Reason* then show a panic or watchdog reset.

**Reading a core dump** (USB, from Windows):

```bash
esptool --chip esp32c3 --port COM5 read-flash 0x310000 0x10000 coredump.bin
docker cp coredump.bin stepaware-dev:/tmp/coredump.bin
```

Then decode it in the container, against the ELF **of the build that crashed**:

```bash
~/.cache/esphome/idf/penvs/5.5.6/bin/esp-coredump --chip esp32c3 info_corefile \
  --gdb ~/.cache/esphome/idf/tools/riscv32-esp-elf-gdb/17.1_20260402/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb \
  --core /tmp/coredump.bin --core-format raw \
  /workspace/StepAware/esphome/.esphome/build/stepaware/build/firmware.elf
```

- The version folders in these paths (`5.5.6`, `esp-14.2.0_…`) change with each ESPHome
  release. To find the current ones, run
  `find ~/.cache/esphome/idf -name esp-coredump -o -name 'riscv32-esp-elf-addr2line'`.
- **gdb is a separate install.** ESPHome's direct ESP-IDF build doesn't download
  `riscv32-esp-elf-gdb`, and `esp-coredump` needs it. It's installed into the `~/.cache`
  volume, so it survives container rebuilds. To reinstall it, for example after an ESPHome
  release moves to a new ESP-IDF version:

  ```bash
  I=~/.cache/esphome/idf; V=5.5.6   # the ESP-IDF version under $I/frameworks
  IDF_TOOLS_PATH=$I $I/penvs/$V/bin/python $I/frameworks/$V/tools/idf_tools.py install riscv32-esp-elf-gdb
  ```

- Checked on 2026-11-22: gdb 17.1 loads the ESPHome `firmware.elf`, and StepAware's own code
  resolves to file and line. Decoding a real core dump hasn't been tried yet.

Keep a copy of `firmware.elf` for every image you flash. The next compile overwrites it.

**The old scripts:**

| Script | Still useful? |
|---|---|
| `tools/extract_coredump.py` | Yes. It reads the same offset and size (0x310000 / 0x10000). Its Python path is hard-coded for another machine, so the `esptool read-flash` line above is simpler. |
| `analyze_coredump.py` | Same `esp_coredump info_corefile --core-format raw` call as above, but with hard-coded Windows paths and the old ELF path `.pio/build/esp32c3/firmware.elf`. Use the command above instead. |
| `decode_coredump.py` | Still works as a fallback (raw scan of the dump plus addr2line). Change `ELF` to the ESPHome `firmware.elf` (`.esphome/build/stepaware/build/firmware.elf`) and `ADDR2LINE` to `~/.cache/esphome/idf/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-addr2line`. That's the current version folder; see the `find` command above. |

The old `/api/coredump` HTTP download is gone with the web API. Over USB is the only way to
read a dump. **(Core dump capture: not verified on hardware.)**

## Old firmware feature map

| Old firmware (`src/`) | ESPHome build |
|---|---|
| State machine, modes, warning timer (`state_machine`) | `stepaware` core (reused logic in `core/`) |
| Direction detector | `stepaware` `direction_detection` + Approaching / Leaving / Direction Detection entities |
| PIR HAL, warm-up | `gpio` binary sensors + `pir_warmup` |
| Button HAL (click, long press) | `gpio` binary sensor + `stepaware` button logic. **Changed:** the long press acts on release (1–5 s = reboot), and a 5 s hold enters dormant. |
| (new) Off switch | [Dormant mode](#dormant-mode-switched-off): 5 s hold / Shut Down / critical battery, persisted in flash, button-only wake |
| Boot-hold WiFi / factory reset | `stepaware_boot_reset` |
| Hazard LED HAL patterns | `ledc` + `stepaware` LED patterns |
| 8x8 matrix HAL + animations | `stepaware_matrix`; mode icons, reboot icon, post-warning sequence, status pixels, WiFi emblem and battery-low notice in the `stepaware` core |
| Custom animations (LittleFS `data/animations`) | Built in: `HEART`, `SPINNER`, `WAVE`, `ALERT`. Loading new files at runtime is **dropped** (no filesystem). |
| Battery low / critical states, critical shutdown | `stepaware_power` `battery:` (thresholds as v0.6.x actually used: 3.4 / 3.2 V; adds hysteresis and sample validation). The old "low battery blocks sleep" rule is **dropped**: it drained the battery faster. |
| Boot snake-progress milestones | **Dropped**: ESPHome's boot has no equivalent stages |
| Ultrasonic HAL (HC-SR04 / Grove) | `packages/ultrasonic_hcsr04.yaml` / `ultrasonic_grove.yaml` |
| Sensor manager / factory, multi-sensor slots, fusion | **Dropped**: fixed wiring in YAML. Extra sensors are YAML entries. |
| Power manager: deep sleep, multi-pin wake, PIR rail hold | `stepaware_power` |
| Light-sleep stage | **Dropped** (decision in ESPHOME_PLAN.md): idle goes straight to deep sleep |
| Battery monitoring (ADC2 low-level) | `stepaware_battery` + template % sensor |
| PIR recalibration scheduler | `stepaware_power` `pir_recalibration:` + `packages/time.yaml`; Recalibrate PIRs button and PIR Recalibrating sensor |
| Config manager (LittleFS JSON) | Entity `restore_value` in NVS + YAML options. Settings from the old firmware were not migrated. |
| Web UI (`data/`) and REST API (`web_api`) | **Dropped**: Home Assistant is the UI, and the native API replaces REST. ESPHome `web_server` could be added, but it costs flash and RAM. |
| WiFi manager (AP setup, reconnect) | ESPHome `wifi` + fallback AP + `captive_portal` |
| NTP manager | `packages/time.yaml` (`homeassistant` + `sntp`) |
| Watchdog manager (module health checks) | **Dropped**: the ESP-IDF task watchdog, ESPHome's loop watchdog feeding and `safe_mode` cover hangs and boot loops. Health shows through the diagnostic entities. |
| Crash handler (LittleFS crash log) | Core dump to flash (`packages/diagnostics.yaml`) + Reset Reason / Wake Reason |
| Debug / file logging (LittleFS) | **Dropped**: `logger` over USB and the API (`esphome logs`). HA history keeps entity state. |
| Serial config console | **Dropped**: settings are HA entities or YAML |
| Custom OTA (`/api/ota/upload`) | `ota: esphome` (verified) + `safe_mode`; rollback works with the ESPHome bootloader |
| Status LED patterns (GPIO2) | ESPHome `status_led`: blinks on error or warning only, no mode patterns |
| Light sensor (photoresistor) | **Dropped**: it was disabled in hardware (it conflicts with PIR far on GPIO4). If wired to a free ADC1 pin, ESPHome `adc` would work. |
| Partition table, coredump partition | Same table (`esphome/partitions.csv`); the spiffs partition is unused |
