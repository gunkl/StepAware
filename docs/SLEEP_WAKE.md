# Sleep & Wake System — ESP32-C3 StepAware

StepAware's ESP32-C3 supports two sleep modes managed by `PowerManager`: light
sleep (mode 1), in which the CPU clock slows but RAM is retained and execution
resumes at the point of suspension; and deep sleep (mode 2), in which the CPU
shuts down entirely and the system reboots on wake.  This document captures
every design decision that governs how the firmware enters, transitions between,
and recovers from those states.  It is the single source of truth for sleep /
wake behaviour; any code change that touches sleep entry, GPIO wakeup
configuration, or wake-source routing must be reflected here before merging.

---

## Pin Assignments

| Pin   | Function                                                                 |
|-------|--------------------------------------------------------------------------|
| GPIO0 | Mode button (active-low, internal pull-up) — wake source in all sleep modes |
| GPIO1 | Near-zone PIR sensor (active-HIGH) — registered as a motion wake pin    |
| GPIO4 | Far-zone PIR sensor (active-HIGH) — registered as a motion wake pin     |
| GPIO5 | Battery ADC (reserved, not used for wakeup)                             |
| GPIO6 | VBUS detect                                                             |
| GPIO20| PIR power rail (shared); held HIGH via `gpio_hold_en` during deep sleep |

---

## Design Decisions

### 1. Wake-source pins are not hardcoded

Wake-source pins are registered at boot through
`PowerManager::setMotionWakePins()`.  That method is called with the list of
active sensor GPIO numbers that `ConfigManager` exposes after loading the stored
configuration.  If a sensor is added, removed, or remapped in the configuration
the wake-source list updates on the next boot with no code change.

**Rule:** Any code that configures sleep or inspects wake sources must read from
the `m_motionWakePins[]` array.  Do **not** reference `PIN_PIR_NEAR`,
`PIN_PIR_FAR`, or any other raw GPIO constant for this purpose.

### 2. USB-JTAG-Serial must be re-initialised after light sleep

The ESP32-C3 routes `Serial` through its USB-JTAG-Serial peripheral.  The
peripheral clock is gated during light sleep.  If the first operation after
`esp_light_sleep_start()` returns is a serial write (or any code that implicitly
writes to serial, such as a `Serial.print` inside a log macro), the peripheral
has not yet been re-clocked and the write stalls until the hardware watchdog
fires — producing what looks like a full boot rather than a resume.

**Fix applied:**

1. `Serial.flush()` is called immediately **before** entering light sleep to
   drain any buffered output.
2. `Serial.begin(SERIAL_BAUD_RATE)` is called as the **very first** statement
   after `esp_light_sleep_start()` returns, before any other serial activity.

### 3. Light-to-deep-sleep transition uses a timer, not a polling loop

`enterLightSleep()` blocks on the `esp_light_sleep_start()` call.  A polling
loop such as `handlePowerState()` inside the `STATE_LIGHT_SLEEP` case can never
execute while the CPU is sleeping.  The transition to deep sleep is therefore
achieved by passing `lightSleepToDeepSleepMs` as the
`duration_ms` argument to the light-sleep call.  When that timer expires the
hardware wakes the CPU; the post-wake code reads `esp_sleep_get_wakeup_cause()`
and, if the cause is `ESP_SLEEP_WAKEUP_TIMER`, calls `enterDeepSleep()` to
complete the transition.

### 4. Crash-recovery fallback

If the USB-JTAG-Serial re-initialisation (Decision 2) does not fully prevent a
watchdog reset, the system reboots.  `detectAndRouteWakeSource()` runs during
that boot and performs the following check:

* The RTC-backed `lastState` value is `STATE_LIGHT_SLEEP`.
* `esp_sleep_get_wakeup_cause()` returns `ESP_SLEEP_WAKEUP_UNDEFINED` (the
  signature of a crash reset rather than a clean wake).

When both conditions are true the code scans every pin in `m_motionWakePins[]`.
Any pin that reads HIGH indicates that a PIR sensor fired while the system was
sleeping or crashing; the wake source is routed to `STATE_MOTION_ALERT` so the
warning plays despite the unclean wake.

### 5. Motion alert injection after reboot

Both deep sleep and the crash-recovery path described in Decision 4 result in a
full CPU reboot.  PIR sensors require approximately 60 seconds of warm-up time
before their outputs are reliable, and the state machine's edge detector does
not fire during that window.  The alert would therefore be silently dropped if
the normal sensor-ready gate were the only path to `STATE_MOTION_ALERT`.

To close that gap, `main.cpp` `setup()` checks `g_power.getState()` after
`PowerManager::begin()` completes.  If the state is already `STATE_MOTION_ALERT`
and the operating mode is `MOTION_DETECT`, `triggerWarning()` is called
directly, bypassing the `m_sensorReady` guard entirely.

### 6. Button wake is always GPIO0 LOW

The mode button is wired active-low with a hardware pull-up.  Its wakeup
configuration (pin number and polarity) is set independently of the
motion-wake-pin list.  It is added to both light-sleep and deep-sleep GPIO
wakeup sources in every call to `enterLightSleep()` and `enterDeepSleep()`,
regardless of what sensors are active.

### 7. Deep-sleep GPIO wakeup uses a bitmask

`esp_deep_sleep_enable_gpio_wakeup()` accepts a single bitmask of pins and a
single polarity value.  All registered PIR motion pins share HIGH polarity and
are OR'd into one bitmask that is passed in a single call.  The mode button
(GPIO0, LOW polarity) requires a separate call because it has the opposite
polarity.

### 8. ULP fallback

When mode 2 deep sleep is entered, the code first attempts to load and start a
ULP RISC-V program (`ulp_pir_monitor.c`) that would monitor GPIO1 autonomously
during deep sleep.  If the ULP peripheral is unavailable — which is the case in
the current build, because the `framework-arduinoespressif32` package does not
ship the required headers and library for ESP32-C3 (see `memory.md`, "ESP32-C3
ULP RISC-V") — or if loading or starting the program fails at runtime, the code
falls back transparently to standard GPIO deep-sleep wakeup covering **all**
registered motion pins, not only GPIO1.  The ULP source is retained in
`ulp/ulp_pir_monitor.c` and is ready to activate when the framework gap is
closed.

---

## Flow Diagrams

### Light sleep cycle (mode 1)

```
Idle for 3 minutes
        │
        ▼
enterLightSleep(0)                  ← duration 0 = wake only on GPIO
  ├── Serial.flush()                ← drain output buffer
  ├── configure motion pins         ← all pins in m_motionWakePins[], HIGH polarity
  ├── configure button pin          ← GPIO0, LOW polarity
  └── esp_light_sleep_start()       ← CPU blocks here until a wake event
              │
              ▼  [wake event]
  ├── Serial.begin(SERIAL_BAUD_RATE)  ← MUST execute before any serial output
  ├── restore CPU frequency
  ├── disable GPIO wakeup sources
  └── detect wake cause
            ├── motion pin HIGH  →  STATE_MOTION_ALERT  →  alert plays via edge detection
            └── button pressed   →  STATE_ACTIVE         →  resume normal operation
        │
        ▼
  return to main loop               ← normal resume; no reboot
```

### Light-to-deep-sleep transition (mode 2, lightSleepToDeepSleepMs > 0)

```
Idle for 3 minutes
        │
        ▼
enterLightSleep(lightSleepToDeepSleepMs)   ← timer added alongside GPIO wakeup
  ├── Serial.flush()
  ├── configure motion pins + button
  └── esp_light_sleep_start()               ← blocks

  ──── if GPIO wake fires first ────────────────────────────────
        │
        ▼
        same post-wake path as mode 1 above (serial re-init, detect cause, resume)

  ──── if timer fires first ────────────────────────────────────
        │
        ▼
  post-wake: esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER
        │
        ▼
  enterDeepSleep(0, "light sleep timeout")
    ├── configure motion pins for deep-sleep GPIO wakeup (bitmask, HIGH polarity)
    ├── configure button for deep-sleep GPIO wakeup (GPIO0, LOW polarity)
    ├── gpio_hold_en(GPIO20)              ← hold PIR power rail HIGH
    └── esp_deep_sleep_start()            ← never returns

              │
              ▼  [GPIO wake event]
        full CPU reboot
              │
              ▼
        restoreStateFromRTC()
              │
              ▼
        alert injection in setup()        ← triggerWarning() called directly
```

### Crash recovery (light sleep USB-JTAG-Serial crash)

```
enterLightSleep() → esp_light_sleep_start()
        │
        ▼  [wake event]
  Serial.begin() → peripheral not yet ready / write stalls
        │
        ▼
  watchdog reset  →  full reboot
        │
        ▼
  restoreStateFromRTC()
    lastState == STATE_LIGHT_SLEEP          ← persisted in RTC memory before sleep
        │
        ▼
  esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED   ← crash signature
        │
        ▼
  scan m_motionWakePins[]:
    any pin HIGH?
      ├── YES  →  route to STATE_MOTION_ALERT  →  alert injection in setup()
      └── NO   →  route to STATE_ACTIVE        ← button wake or spurious reset
```

---

## Testing Checklist

Run each scenario on hardware after any change to sleep entry, GPIO wakeup
configuration, or wake-source routing.

1. **Mode 1 — near PIR wake from light sleep**
   - Let the device idle until it enters light sleep (serial log confirms).
   - Trigger the near-zone PIR (GPIO1).
   - Confirm: serial output resumes cleanly (no reboot log), alert sound plays.

2. **Mode 1 — far PIR wake from light sleep**
   - Same as (1) but trigger the far-zone PIR (GPIO4).
   - Confirm: serial output resumes cleanly, alert sound plays.

3. **Mode 2 direct — near/far PIR wake from deep sleep**
   - Set `lightSleepToDeepSleepMs = 0` so the device goes straight to deep sleep after idle.
   - Trigger either PIR sensor.
   - Confirm: device reboots (boot log visible), alert plays on boot via injection path.

4. **Mode 2 staged — light sleep timer fires, then next wake boots with alert**
   - Set `lightSleepToDeepSleepMs` to a short value (e.g. 5 s) for faster iteration.
   - Let the device idle; confirm serial log shows light-sleep entry, then timer-triggered transition to deep sleep.
   - Trigger a PIR sensor while the device is in deep sleep.
   - Confirm: device reboots, alert plays on boot via injection path.

5. **Button wake from light sleep and deep sleep — no alert**
   - Let the device enter light sleep (mode 1). Press the mode button.
   - Confirm: serial resumes, state returns to ACTIVE, no alert plays.
   - Let the device enter deep sleep (mode 2, either direct or staged). Press the mode button.
   - Confirm: device reboots, state is ACTIVE, no alert plays.

---

## Regression Guard

> Any changes to sleep entry, GPIO wakeup configuration, or wake-source
> routing **must** update this document before the changeset is merged.
> Failure to keep this document current will silently break the crash-recovery
> and alert-injection paths on the next hardware revision.
