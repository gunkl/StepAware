# StepAware → ESPHome conversion plan (draft for approval)

Based on `main` @ `c3d4b25` (v0.6.3) in the `stepaware-dev` container, and the ESPHome
2026.6.5 sources installed there. Nothing has been built or flashed yet.

## 0. Preserving the Arduino firmware

- Branch **`legacy-arduino`** points at `c3d4b25` (v0.6.3) and is pushed to
  `origin/legacy-arduino`, at your request. You could also tag it: `git tag v0.6.3 c3d4b25`.
- The ESPHome work goes in a new `esphome/` folder on the local branch `feature/esphome`,
  alongside the existing `src/`. Nothing gets deleted until you decide the ESPHome build has
  replaced the old one.

## 1. Deep sleep: yes, it still works

Deep sleep is the same chip state whichever framework puts the chip there, so sleep current
doesn't change. Four things do change:

| Topic | Current firmware | ESPHome | Impact |
|---|---|---|---|
| Wake sources | GPIO1 + GPIO4 (PIRs, HIGH), GPIO0 (button, LOW), optional timer | Built-in `deep_sleep` on the C3 accepts **one** `wakeup_pin` (checked in `deep_sleep/__init__.py`) | A small hook in our custom component arms the other two pins before `esp_deep_sleep_start()`. It's the same IDF call `power_manager.cpp` uses today, so it's proven on this hardware. |
| PIR power (GPIO20) | `gpio_hold_en` + deep-sleep hold keeps the AM312s powered | Not handled by ESPHome | Same hook re-applies the hold, and releases it on boot. Ported directly. |
| Light-sleep stage | 3 min idle → light sleep (WiFi off) → 1 min → deep sleep | No light-sleep component | **Decided: dropped.** Idle → deep sleep directly. |
| WiFi on each wake | (check against current behaviour) | Default is WiFi + HA API connect on every boot, roughly 1–3 s of radio at about 80–120 mA | **Decided:** `wifi: enable_on_boot: false`, and the boot logic turns WiFi on for every wake **except** a PIR (motion) GPIO wake. See the wake policy below. |

**Wake policy (decided)**

| Wake cause | WiFi |
|---|---|
| PIR near/far (GPIO wake with a PIR pin high) | **Off**: warn, then sleep again |
| Button (GPIO wake with GPIO0 low) | On |
| Timer (periodic check-in; interval is an HA `number`) | On: report battery, read "stay awake", sleep |
| Power-on / reset / OTA reboot (`UNDEFINED`) | On |
| Crash / watchdog / brownout reset | On, so it stays reachable for debugging |
| USB power present (VBUS high) at any wake | On, and no deep sleep while on USB |

If a PIR and the button are both active at wake, the button wins and WiFi comes on.

What you give up in Home Assistant:

- The device shows **unavailable** while asleep.
- Changes made in HA only reach it at the next check-in.
- OTA only works while it's awake. A "stay awake" `input_boolean` in HA, read on check-in, can
  trigger `deep_sleep.prevent` so you can do OTA or debugging.

**ULP:** the ESP32-C3 has **no ULP coprocessor**, so the commented-out `ulp/` path in
`platformio.ini` could never have been enabled on this chip. Nothing is lost there.

## 2. Architecture (recommendation: hybrid on ESP-IDF)

**Framework: ESP-IDF.** It's ESPHome's default and best-supported option on the C3. Arduino
under ESPHome would let us keep the Adafruit LED-backpack library, but it adds size and
build-time quirks.

| Part | Implementation |
|---|---|
| PIR near/far, button | Native `binary_sensor: gpio` (button gets `on_click` / `on_multi_click` for mode and long-press) |
| Hazard LED (GPIO3, PWM) | Native `output: ledc` + `light: monochromatic` |
| Status LED (GPIO2) | Native `status_led` or a `gpio` output |
| Battery (GPIO5, ADC2) | Native `adc` sensor (ESPHome allows ADC2 channel 0 on C3) + `template` percent sensor. *Risk:* ADC2 readings can fail while WiFi is on, the same as today. |
| VBUS detect (GPIO6) | Native `binary_sensor: gpio` |
| HC-SR04 | Native `ultrasonic` sensor |
| Grove ultrasonic (1 pin) | **Custom**: port `hal_ultrasonic_grove` |
| 8×8 LED matrix (HT16K33) | **Custom**: there's no HT16K33 matrix component in core. Write a small driver on ESPHome's `i2c` (the protocol is trivial) and port the animation templates. |
| State machine, direction detector, mode logic | **Custom `stepaware` external component**. Keep `state_machine` / `direction_detector` as plain C++ with the existing mock seams, so the native PlatformIO tests keep running. |
| Sleep policy | Custom component decides when to sleep, arms the multi-pin wakeup and GPIO20 hold, then calls `deep_sleep.enter` |
| Config storage | ESPHome `globals` / entity `restore_value` (NVS) instead of the LittleFS JSON |
| Web UI, NTP, custom WiFi manager, custom OTA, watchdog manager | **Dropped**, replaced by HA, `time: homeassistant`, ESPHome `wifi`, `ota: esphome`, and ESPHome's own task watchdog |

The trade-off: the I/O drivers get rewritten (they're simple), but the proven decision logic
is reused mostly as-is.

## 3. Home Assistant entities (proposed)

| Entity | Type | Source |
|---|---|---|
| Operating mode | `select`: Off / Continuous / Motion | `defaultMode`, `rememberLastMode` |
| Power saving | `select`: Off / Deep sleep | `powerSavingMode` (light-sleep option removed) |
| Motion warning duration | `number` (s) | `motionWarningDuration` |
| Idle → deep sleep | `number` (s) | `deepSleepAfterMs` |
| Check-in interval | `number` (min) | new (timer wake) |
| LED brightness full/medium/dim | `number` 0–255, or a `light` | `ledBrightness*` |
| Sensor min/max distance | `number` (cm) | `sensorMin/MaxDistance` |
| Direction detection | `switch` | `sensorDirectionEnabled` |
| Stay awake (OTA/debug) | HA `input_boolean` read by the device | new |
| Motion near / far | `binary_sensor` | PIRs |
| Approaching / leaving | `binary_sensor` or `event` | direction detector |
| Distance | `sensor` (cm) | ultrasonic |
| Battery voltage / % | `sensor` | ADC |
| USB power | `binary_sensor` | VBUS |
| State, wake reason | `text_sensor` | state machine, `esp_sleep_get_wakeup_cause` |
| Restart / Safe mode | `button` | native |

**Stay on the device only (YAML, not in HA):**

- Battery thresholds
- Button debounce and long-press times
- Blink rates
- Rapid-sample count and interval
- Sensor slot / fusion / display slot layout
- Log level

**Dropped:** WiFi SSID/password (moved to `secrets.yaml`), NTP server, timezone, file logging.

## 4. OTA migration findings (the blocker)

1. **The existing endpoint will accept an ESPHome image.**
   - `ota_manager.cpp` only checks that the size is greater than 0 and at most the next app
     partition (`0x180000` = 1,572,864 bytes).
   - The Arduino `Update` library also checks the `0xE9` image magic byte.
   - There's no signature or version check.
   - Upload ESPHome's **OTA app image** (`firmware.ota.bin`), **not** `firmware.factory.bin`.
2. **Partition tables differ.** An app OTA never rewrites the table, so the device keeps
   StepAware's layout:
   - **StepAware:** nvs 0x9000, otadata 0xE000, app0/app1 1.5 MB each, coredump, LittleFS.
   - **ESPHome default:** otadata first, 1.75 MB apps, nvs at the end.
   - **Plan:** set `esp32: partitions:` to a copy of the current `partitions.csv`. That gives a
     correct 1.5 MB size check at compile time and a matching table for any later USB flash.
   - After that, ESPHome's native OTA writes to app0/app1 using the same otadata.
   - The coredump and LittleFS partitions just sit unused.
3. **Bootloader stays the Arduino one.** It almost certainly has app rollback disabled, so there
   is **no automatic rollback** to StepAware. ESPHome's `safe_mode` (boot-loop counter → WiFi +
   OTA only) helps only if the image gets as far as WiFi.
4. **The first image must include:**
   - `safe_mode:`
   - `wifi:` with a fallback `ap:`
   - `ota: - platform: esphome` with a password
   - `api:` encryption
   - `logger`

   All credentials come from a gitignored `esphome/secrets.yaml`, with placeholders committed
   as `secrets.yaml.example`. The first image should also **not** enable deep sleep, so it
   stays reachable.
5. **Settings are lost.** StepAware's LittleFS config isn't read by ESPHome, so re-enter
   settings in HA after the flash.
6. **Rescue path (no spare board, so the first hop happens on the real device):**
   - Before the OTA, back up the whole 4 MB flash over USB from Windows:
     `esptool --chip esp32c3 read_flash 0 0x400000 stepaware-v0.6.3-full.bin`.
     Writing that file back with `write_flash 0` restores v0.6.3 exactly, settings included.
   - Keep `firmware.factory.bin` from the ESPHome build and esptool on Windows ready.
   - Do the OTA with the device on the bench and USB in reach.
7. **Still to check:** whether the container can reach the device by IP (`ping` / `curl`).

## 5. Step-by-step migration

1. ✅ `legacy-arduino` is pushed. `feature/esphome` exists locally.
2. Minimal `esphome/stepaware.yaml`:
   - C3 board on ESP-IDF, custom partitions
   - WiFi + fallback AP, API, OTA, safe_mode, logger
   - PIRs, button, hazard LED, battery ADC
   - No sleep

   Check it with `esphome config` and `esphome compile`, and confirm the image is under 1.5 MB.
3. **Before flashing:** you back up the full flash over USB (section 4.6), and we confirm the
   container can reach the device's IP.
4. **Real device:** upload `firmware.ota.bin` through StepAware's OTA. Confirm the device boots,
   joins WiFi and shows up in HA. Then do one ESPHome→ESPHome OTA to prove the new path.
5. Add the `stepaware` external component, ported in this order:
   - state machine and modes
   - direction detector
   - HT16K33 matrix and animations
   - Grove ultrasonic

   Port the native tests alongside each piece. Each step is one compile followed by one OTA.
6. Add deep sleep last:
   - multi-pin wake and GPIO20 hold hook
   - WiFi only on button/timer wakes
   - the "stay awake" switch

   You measure sleep current and wake behaviour on the bench.
7. Battery soak test against v0.6.3's numbers, then decide whether to retire `src/`.

## 6. Testing

- `esphome config` and `esphome compile` on every change, in the container.
- The existing `pio test -e native` suites stay for the reused logic.
- On-device checks (yours):
  - each wake source
  - GPIO20 hold (PIRs still powered during sleep)
  - wake-to-LED latency
  - sleep current
  - an OTA in each direction

## Migration log (2026-11-22)

- **Step 1:** uploaded stage-1 `firmware.ota.bin` through StepAware 0.6.4's `/api/ota/upload`.
  - It landed in `app1` byte for byte, and otadata selected ota_1.
  - The 8×8 matrix kept showing the last OTA progress frame (62/64), because ESPHome doesn't
    drive the HT16K33 yet.
- **Step 2:** the device booted ESPHome but failed WiFi with `4-Way Handshake Timeout` (wrong
  password in `secrets.yaml`). It was fixed over USB from Windows.
- **Step 3:** took a full flash backup over USB: `esphome-build/stepaware-flash-backup-2026-11-22.bin`
  (StepAware 0.6.4 in app0, LittleFS settings included).
- **Finding:** the old firmware was Arduino 2.x on **ESP-IDF v4.4.7**, so its bootloader was too
  old for ESPHome's OTA rollback.
- **Step 4:** flashed `firmware.factory.bin` at 0x0 over USB.
  - This replaced the bootloader, so rollback works now.
  - ESPHome now boots from app0. StepAware's app is overwritten; the backup and the
    `legacy-arduino` branch still hold it.
- **Result:**
  - Device `stepaware` at **10.124.0.98** (IoT VLAN; DNS name `stepaware`).
  - The API works from the container.
  - Native ESPHome OTA from the container succeeded, and the device came back.
- **Open issue: battery reads 0.0 V.** ADC2 (GPIO5) needs
  `CONFIG_ADC_ONESHOT_FORCE_USE_ADC2_ON_C3`, and even then reads fail with `ESP_ERR_TIMEOUT`
  (263) while WiFi is active. That's the C3's ADC2/WiFi arbiter. StepAware 0.6.x read it
  through low-level `adc_ll_*` calls in `power_manager.cpp`. Port that approach, or move the
  divider to an ADC1 pin (GPIO0–4) in a hardware revision.

### Stage 2 (2026-11-22): all components integrated

- **Components** in `esphome/components/`:
  - `stepaware`: core logic in `core/`, plain C++ with native tests (`pio test -e native_esphome`, 46 cases)
  - `stepaware_matrix`
  - `stepaware_battery`
  - `stepaware_power`
  - `stepaware_grove_ultrasonic`: built but not wired; this unit has only two PIRs
- **Image:** 988,750 bytes (62.9% of the slot). Pushed with native OTA. All entities report.
- **Battery** reads 3.80 V with WiFi on.
- **Native tests:** `pio test -e native` passes 215/215 (169 old + 46 new).
- **Power Saving Mode defaults to Off.** Deep sleep stays untested until it's switched on from HA.
- **Still to verify on the device:**
  - matrix orientation and the stale frame (the HT16K33 NACKed at 0x70 before; it needs a full
    power cycle, including the LiPo)
  - button cycling and long-press reboot
  - every deep-sleep wake path
  - battery reading against a multimeter

### Stage 3 (2026-11-22): porting finished

- **Ported:**
  - matrix UI parity: mode icons, reboot icon, post-warning WiFi/battery sequence, PIR status
    pixels, first-connect WiFi emblem, battery-low notice, the four custom animations built in,
    and a brightness setting that persists
  - battery Low/Critical states, plus a critical-battery shutdown with button-only wake
    (3.4 / 3.2 V, the thresholds v0.6.x really used)
  - nightly PIR recalibration
  - boot-hold WiFi and factory reset (`stepaware_boot_reset`)
  - time from HA with SNTP fallback (`packages/time.yaml`)
  - diagnostics and core dump to flash (`packages/diagnostics.yaml`)
  - optional ultrasonic packages
  - user guide `docs/ESPHOME.md`, with an old-feature → new map
- **Image:** 1,026,622 bytes (65.3%), clean build, pushed by native OTA. 39 entities, no warnings.
- **Native tests:** `native` 280/280, `native_esphome` 71/71, `native_power` 40/40.
- **Verified on the device:**
  - all entities report
  - battery 3.85 V / 70%, and Battery State reads `USB`
  - Recalibrate PIRs: rail off, 62 s, complete, with no false motion while recalibrating
- **Not verified yet:**
  - matrix pixels and orientation
  - button cycling, long press and boot-hold resets
  - every deep-sleep wake path
  - critical-battery shutdown
  - core dump retrieval
- **Note:** the debug Loop Time reported 102–151 ms peaks. These are worth profiling if they
  persist; ESPHome logged no slow-component warnings.

### Dormant mode (2026-11-22)

- **Added** a user-triggered "switched off" state (owner-approved scope), see
  [ESPHOME.md: Dormant mode](ESPHOME.md#dormant-mode-switched-off):
  - Button holds act on **release**: 1 s to under 5 s = reboot (was: reboot at 1 s while still
    held), 5 s or more = dormant. Countdown bar + refresh / power-off icons on the matrix,
    hazard LED fast blink / solid. Short clicks are unchanged. `dormant_hold` is configurable.
  - **Shut Down** button in HA, **Last Shutdown** sensor, Wake Reason suffix `(from dormant)`.
  - Dormant = deep sleep with button-only wake, PIR rail low and not held, PIR pins pulled
    down, HT16K33 standby. The flag `{active, reason}` lives in NVS; every boot checks it
    first and goes straight back to sleep unless the button woke it or is held at power-on.
    USB, Power Saving Mode Off and Stay Awake don't stop it; an OTA refuses / cancels it.
  - The critical-battery shutdown now enters the same persistent dormant state.
  - HT16K33 standby (`stepaware_matrix.standby`) now runs before **every** deep sleep. The old
    pre-sleep path only blanked the display RAM; the oscillator kept running.
  - The waking press doesn't count as a click (`StepButton::suppressNextPress()`).
  - Dormant re-entry boots clear `safe_mode`'s boot counter, so ~10 of them can't trigger
    safe mode.
  - Optional `wake_hold` (default off): require a held press to wake.
- **Native tests:** `native` 312/312, `native_esphome` 89/89, `native_power` 54/54.
- **Image:** 1,031,352 bytes (65.6%; +4,730 bytes over stage 3). Clean build, no warnings.
- **Not verified on hardware yet:** the gesture timing and icons, Shut Down, single-press wake,
  USB plug-in while dormant, flat-battery recovery, and the dormant current.

### Owner verification (2026-11-22)

The owner tested the full build on the device (stage 3 plus dormant mode) and reports that
**everything works**. That covers the matrix display, the button gestures, deep sleep and wake
paths, battery states, PIR recalibration, boot-hold resets and dormant mode (gesture, HA Shut
Down, single-press wake, staying dormant on USB, flat-battery recovery).

Remaining work is housekeeping:
- commit `feature/esphome`
- move to the new PC (`MIGRATE-TO-NEW-PC.md` in the old Windows checkout)
- optionally profile the 100–150 ms Loop Time peaks

## Decisions (2026-11-22)

1. Hybrid design on **ESP-IDF**: approved.
2. Light-sleep stage: **dropped**.
3. WiFi off for motion wakes only; on for every other wake cause; periodic check-in:
   **approved** (see the wake policy in section 1).
4. Spare C3 board: **none**. The first OTA goes to the real device after a full USB flash
   backup.
