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
| GPIO1 | Near-zone PIR sensor (active-HIGH) — registered as a motion wake pin; also UART0 RXD in default IO_MUX (see Decision 9) |
| GPIO4 | Far-zone PIR sensor (active-HIGH) — registered as a motion wake pin     |
| GPIO5 | Battery ADC (reserved, not used for wakeup)                             |
| GPIO6 | VBUS detect                                                             |
| GPIO20| PIR power rail (shared); held HIGH via `gpio_hold_en` during light and deep sleep (see Decision 14) |

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

**Fix applied (three-part sequence):**

1. `Serial.flush()` drains any buffered output while the peripheral is still
   open.
2. `Serial.end()` cleanly shuts down the USB-JTAG-Serial controller **before**
   `esp_light_sleep_start()`.  This step is critical: without it the controller
   FIFO retains stale state across the sleep boundary and the Windows host
   driver never sees a disconnect event, so it never re-syncs.  `flush()` alone
   was not sufficient — on-device logs confirmed the wake code path executed
   correctly but the host-side serial port remained dead until USB was
   physically disconnected and reconnected.
3. `Serial.begin(SERIAL_BAUD_RATE)` is called as the **very first** statement
   after `esp_light_sleep_start()` returns, before any other serial activity.
   Because the peripheral was shut down cleanly, `begin()` performs a true
   cold-start that the host can recover from.

### 3. Light-to-deep-sleep transition uses a timer, not a polling loop

`enterLightSleep()` blocks on the `esp_light_sleep_start()` call.  A polling
loop such as `handlePowerState()` inside the `STATE_LIGHT_SLEEP` case can never
execute while the CPU is sleeping.  The transition to deep sleep is therefore
achieved by passing `lightSleepToDeepSleepMs` as the
`duration_ms` argument to the light-sleep call.  When that timer expires the
hardware wakes the CPU; the post-wake code reads `esp_sleep_get_wakeup_cause()`
and, if the cause is `ESP_SLEEP_WAKEUP_TIMER`, calls `enterDeepSleep()` to
complete the transition.

**Timer compensation:** the actual value passed to
`esp_sleep_enable_timer_wakeup()` is `duration_ms` minus the real-time setup
overhead (`millis() - m_stateEnterTime`).  `millis()` on ESP32 is compensated
for time spent inside `esp_light_sleep_start()`, but the GPIO-configuration
code that runs *before* the call executes in wall-clock time and must be
subtracted explicitly.

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

**Ordering requirement (Issue #39):** `saveStateToRTC()` must execute *after*
`setState(STATE_LIGHT_SLEEP)`.  If it runs first it persists the previous state
and the `lastState == STATE_LIGHT_SLEEP` check here never matches.  See
Decision 10.

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

### 9. UART0 must be torn down at boot

GPIO1 (the near-zone PIR pin) is mapped to UART0 RXD in the ESP32-C3 default
IO_MUX.  The bootloader initialises UART0.  If it remains installed, residual
peripheral state during the light-sleep clock transition causes GPIO1 to read
HIGH spuriously for 2–3 seconds every time the device enters light sleep —
indistinguishable from a real AM312 detection pulse by level or duration alone.
GPIO4 is unaffected because its IO_MUX function-select was never set to a UART
by the bootloader.

`setup()` calls `uart_driver_delete(UART_NUM_0)` immediately after
`Serial.begin()`.  This is safe: `Serial` on ESP32-C3 with `ARDUINO_USB_MODE=1`
routes through the USB-JTAG-Serial peripheral, which is entirely independent of
UART0.  A self-test `Serial.println` fires right after the delete to confirm
USB-JTAG-Serial is still alive.

**Diagnostic outcomes after flashing:**

| Serial log / behaviour                                   | Conclusion |
|----------------------------------------------------------|------------|
| GPIO1 no longer fires spuriously                         | UART0 was the cause.  Done. |
| GPIO1 still fires; log shows `UART0 teardown: OK`       | UART0 removed but IO_MUX function-select was not reset.  See Decision 11 — `gpio_config()` is now called before each sleep entry to force MCU_SEL back to GPIO mode.  If GPIO1 is still HIGH *before* sleep (visible in `Light sleep GPIO:` log), the sensor itself is driving it — see Issue #40 diagnostic table. |
| Log shows `UART0 teardown: not installed`                | Bootloader did not initialise UART0.  Investigate PCB trace coupling or framework GPIO driver behaviour. |

**SDK gap:** `gpio_set_glitch_filter()` was investigated as an alternative
mitigation but does **not exist** in the esp32c3 SDK headers shipped with
`framework-arduinoespressif32@3.0.0` / `espressif32@6.5.0`.  The only
`glitch_filter` symbols in the package are PCNT-related and only present for
esp32 / esp32s2 / esp32s3.  A future framework upgrade or a manual HAL register
write would be required to enable per-pin digital glitch filtering.

### 10. saveStateToRTC must run after setState

`saveStateToRTC()` writes `m_state` into RTC-backed memory.  It must be called
**after** `setState()` so the value persisted is the *new* state.  The
crash-recovery path (Decision 4) gates on `rtcMemory.lastState ==
STATE_LIGHT_SLEEP`; if `saveStateToRTC()` runs first it writes the previous
state and crash recovery silently fails.

This ordering bug was present in both `enterLightSleep` and `enterDeepSleep`
from the original implementation.  Fixed in Issue #39.  Every future
sleep-entry function must maintain: `setState()` first, `saveStateToRTC()`
second.

### 11. Wake-pin configuration must use gpio_config(), not gpio_set_direction()

On ESP32-C3 there are two layers between a GPIO pad and the GPIO controller:
the **IO_MUX** (selects which peripheral or GPIO function drives/reads the pad)
and the **GPIO controller** (sets direction, pull, interrupt).

| Function | Reconfigures IO_MUX MCU_SEL? | Sets direction/pull? |
|---|---|---|
| `gpio_config()` | **Yes** | Yes |
| `gpio_set_direction()` | No | Yes (direction only) |
| `gpio_pullup_en()` | No | Yes (pull only) |
| Arduino `pinMode()` | **Yes** (calls `gpio_config` internally) | Yes |

GPIO1's IO_MUX defaults to UART0 RXD.  `uart_driver_delete` (Decision 9)
removes the software driver but does **not** reset MCU_SEL.  `HAL_PIR::begin()`
calls `pinMode` at boot, which does reset MCU_SEL.  But `enterLightSleep`
previously used `gpio_set_direction` + `gpio_pullup_en`, which left MCU_SEL
untouched.  If MCU_SEL was ever wrong — e.g. because `gpio_config` did not
fully override UART0's claim in this SDK version, or because a peripheral
driver transiently reclaimed the pin — it stayed wrong through every
subsequent sleep cycle.

**Fix (Issue #40):** `enterLightSleep` now calls `gpio_config()` for every
motion-wake pin and the button pin before arming wakeup.  This guarantees
MCU_SEL is GPIO mode immediately before `esp_light_sleep_start()`.

**Rule:** Any future code that configures a pin for sleep wakeup must use
`gpio_config()`, never `gpio_set_direction()` alone.

### 12. GPIO wake routing must verify pin levels before alerting

`esp_sleep_get_wakeup_cause()` returns `ESP_SLEEP_WAKEUP_GPIO` regardless of
which pin triggered the wake.  The previous routing logic checked only whether
the button was pressed; if it was not, the wake was assumed to be PIR motion and
routed to `STATE_MOTION_ALERT` unconditionally.

A momentary glitch — pad noise, a brief peripheral signal, or a sub-ms
transition during the sleep boundary — can trigger the level-sensitive wakeup
but be gone by the time the post-wake code reads the pin.  The old logic would
fire the alert anyway.

**Fix (Issue #40):** The `ESP_SLEEP_WAKEUP_GPIO` case now scans every pin in
`m_motionWakePins[]`.  Only if at least one reads HIGH is the wake routed to
`STATE_MOTION_ALERT`.  If none is HIGH (and the button is not pressed) the wake
is logged as `"Spurious GPIO wake (no pin HIGH)"` and routed to `STATE_ACTIVE`.
This is the same scan pattern already used in the crash-recovery path (Decision
4).

**Note:** A genuine AM312 pulse lasts 2–3 seconds.  If a real motion event
triggered the wake but the pulse expires before the scan runs (unlikely given
the sub-ms Serial.begin + scan latency), the alert will be missed.  The wake
snapshot log (see below) records pin levels at the exact moment of wake for
post-hoc analysis.

### 13. GPIO1 (XTAL\_32K\_N pad) excluded from light-sleep wakeup sources

**Symptom:** Every entry into `esp_light_sleep_start()` produced an immediate
GPIO wake on GPIO1, regardless of sensor state, IO_MUX value, UART0 driver
state, or pull-up configuration.

**Root-cause isolation (Issue #38, Phase 2):**

| Test | Result | Conclusion |
|---|---|---|
| Halt before `esp_light_sleep_start()` | GPIO1 stays 0 | Glitch is not in firmware setup |
| Full path (sleep entered) | GPIO1 = 1 at wake snapshot | Glitch fires inside the IDF call |
| IO_MUX MCU_SEL at wake | 1 (GPIO) | IO_MUX is not corrupted |
| GPIO4 (plain pad, same sensor model, same power rail) | Never glitches | Pad-specific, not sensor or power-rail |
| GPIO1 removed from wakeup sources | Device sleeps indefinitely | Glitch still occurs but can no longer latch the wake controller |

The glitch is a pad-level artefact of the ESP32-C3 clock-gate transition on the
XTAL\_32K\_N pad.  It is not caused by any software-visible register and cannot be
masked by pull configuration or IO_MUX settings.

**Mitigation:** `enterLightSleep` treats `PIN_PIR_NEAR` (GPIO1) specially:

1. Configured as bare INPUT — pulls disabled — so the glitch pulse cannot be
   reinforced by an on-chip pull.
2. **Not** armed via `gpio_wakeup_enable`.  The sleep controller never sees it.
3. Pull-up is restored via `gpio_config()` immediately after wake, before any
   sensor reads.

**Trade-off:** The near PIR sensor cannot trigger a wake from light sleep.
Wake-from-motion relies on the far PIR (GPIO4) alone.  Both sensors remain
fully readable once the device is in ACTIVE or MOTION\_ALERT.

### 14. GPIO20 (PIR power rail) must be held during light sleep

**Symptom:** GPIO4 (far PIR) was armed for HIGH-level wakeup and
`gpio_wakeup_enable()` returned ESP\_OK, but the device never woke on motion.
Pre-sleep log showed `PIR_PWR=1`; wake snapshot showed `PIR_PWR=0`.

**Root cause:** On ESP32-C3 GPIO output values are **not** retained through
the `esp_light_sleep_start()` clock-gate transition without an explicit
`gpio_hold_en()` call.  GPIO20 reverted to 0, cutting power to both AM312
sensors.  Without power the sensors cannot detect motion and GPIO4 never
transitions to HIGH.

This contradicts the general ESP-IDF documentation that states outputs are
retained during light sleep.  The behaviour is specific to ESP32-C3 and was
confirmed empirically: `PIR_PWR` dropped from 1 to 0 in consecutive
pre-sleep / wake-snapshot pairs with no `gpio_hold_en`.

**Mitigation:** `enterLightSleep()` calls `gpio_hold_en(GPIO20)` after all
per-pin wakeup configuration and before `esp_light_sleep_start()`.
`gpio_hold_dis(GPIO20)` is called in the post-wake cleanup block, after
`gpio_wakeup_disable()` and before the GPIO1 pull-up restore.

**Verified:** Wake snapshot `PIR_PWR=1` confirmed on the first test cycle
after the fix.  GPIO4 HIGH-level wake fires correctly.

---

## Flow Diagrams

### Light sleep cycle (mode 1)

```
Idle for 3 minutes
        │
        ▼
enterLightSleep(0)                  ← duration 0 = wake only on GPIO
  ├── setState(STATE_LIGHT_SLEEP)   ← update in-RAM state first
  ├── saveStateToRTC()              ← persist to RTC (MUST follow setState — see Decision 10)
  ├── gpio_config() motion pins     ← GPIO1 bare INPUT + excluded; GPIO4 armed (see Decisions 11, 13)
  ├── gpio_config() button pin      ← GPIO0, LOW polarity
  ├── log IO_MUX MCU_SEL            ← diagnostic: confirms GPIO1 IO_MUX is GPIO mode
  ├── log GPIO levels + PIR power   ← snapshot before sleep; GPIO1=1 here means sensor is driving HIGH
  ├── Serial.flush() / end()        ← drain + shut down USB-JTAG-Serial cleanly
  └── esp_light_sleep_start()       ← CPU blocks here until a wake event
              │
              ▼  [wake event]
  ├── wake snapshot capture         ← GPIO1/4, PIR_PWR, MCU_SEL, cause stored in statics (Serial not yet up)
  ├── Serial.begin(SERIAL_BAUD_RATE)  ← MUST execute before any serial output
  ├── log wake snapshot             ← pin levels at exact moment of wake
  ├── restore CPU frequency
  ├── disable GPIO wakeup sources
  └── detect wake cause             ← see Decision 12: scans pins, not just button
            ├── motion pin HIGH  →  STATE_MOTION_ALERT  →  alert plays via edge detection
            ├── button pressed   →  STATE_ACTIVE         →  resume normal operation
            └── no pin HIGH      →  STATE_ACTIVE         →  "Spurious GPIO wake" logged
        │
        ▼
  return to main loop               ← normal resume; no reboot
```

### Light sleep — detailed GPIO and register state trace

The flow diagram above shows the code path.  This section walks through
every step with the actual GPIO levels and IO_MUX register state at each
point.  All line numbers refer to `src/power_manager.cpp`.

**Pin map (see also the table at the top of this document):**

| Pin | Role | Active | Wakeup polarity |
|---|---|---|---|
| GPIO0 | Mode button | LOW (pull-up) | LOW-level |
| GPIO1 | PIR Near (AM312); IO_MUX pad = XTAL\_32K\_N | HIGH | **EXCLUDED** — pad glitch (see Decision 13) |
| GPIO4 | PIR Far (AM312); plain GPIO pad | HIGH | HIGH-level (sole motion wake source) |
| GPIO20 | PIR power rail (shared OUTPUT) | HIGH | not a wake source |

GPIO1's IO_MUX MCU_SEL must be **1** for GPIO mode.  Any other value
means a peripheral (e.g. UART0 RXD) is still driving the pad.

---

**Step 1 — Idle timeout fires**

`shouldEnterSleep()` returns true when `millis() - m_lastActivity >=
idleToLightSleepMs` (default 180 000 ms).  The main loop calls
`enterLightSleep(0)`.

Typical GPIO state entering the call: GPIO1=0, GPIO4=0, GPIO0=1
(pulled up, button not pressed), GPIO20=1 (sensor power OUTPUT HIGH).

**Step 2 — State persist (lines 690–691)**

`setState(STATE_LIGHT_SLEEP)` updates in-RAM state; `saveStateToRTC()`
writes it to RTC-backed memory.  Order is critical — see Decision 10.
No GPIO change.

**Step 3 — CPU 160 → 80 MHz (line 705)**

`setCPUFrequency(80)` reconfigures the PLL.  No GPIO registers are
touched.  GPIO states unchanged.

**Step 4 — Arm GPIO wakeup controller (line 708)**

`esp_sleep_enable_gpio_wakeup()` sets a single enable bit in the sleep
controller.  No per-pin configuration yet.

**Step 5 — Configure motion pins via `gpio_config()` (lines 713–734)**

Loops over `m_motionWakePins[]` — GPIO1, then GPIO4 in the default
two-sensor layout.  `gpio_config()` is used instead of
`gpio_set_direction()` because it reconfigures **both** layers (see
Decision 11).

GPIO1 is handled differently from all other motion pins because of the
XTAL\_32K\_N pad glitch (Decision 13):

| Pin | Pull | Armed as wakeup? | Why |
|---|---|---|---|
| GPIO1 (near PIR) | **NONE** (both disabled) | **No** | Pad glitch would cause immediate spurious wake |
| GPIO4 (far PIR) | UP | Yes — HIGH-level | Plain pad, no glitch |

For armed pins, `gpio_wakeup_enable(pin, HIGH_LEVEL)` latches the sleep
controller.

State after this block:

| Pin | Dir | Pull | MCU_SEL | Wakeup |
|---|---|---|---|---|
| GPIO0 | INPUT (unchanged) | UP | unchanged | not yet |
| GPIO1 | INPUT | **NONE** | 1 | **not armed** |
| GPIO4 | INPUT | UP | 1 | HIGH-level |
| GPIO20 | OUTPUT HIGH | — | unchanged | — |

**Step 6 — Configure button via `gpio_config()` (lines 724–732)**

Same as step 5 but for GPIO0 with **LOW-level** wakeup (button is
active-low).  After this step GPIO0's wakeup latch is armed.

**Step 7 — Timer wakeup (lines 740–747) — conditional**

Only armed when `duration_ms > 0` (light→deep staged transition).  With
`duration_ms == 0` (indefinite GPIO-only sleep) this block is skipped.
The value passed to `esp_sleep_enable_timer_wakeup()` is `duration_ms`
minus the real-wall-clock setup overhead — see Decision 3.

**Step 8 — IO_MUX diagnostic read (lines 752–756)**

```cpp
mcuSel = (REG_READ(IO_MUX_GPIO1_REG) >> MCU_SEL_S) & MCU_SEL_V;
```

Confirms step 5 wrote correctly.  Logged as `IO_MUX GPIO1: MCU_SEL=X`.
Expected: **1**.  Any other value means something re-claimed the pad
between step 5 and here.

**Step 9 — Pre-sleep GPIO snapshot (lines 759–776)**

`gpio_get_level()` on every pin.  This is the **last moment all
peripherals are fully awake and trustworthy.**

```
Light sleep GPIO: GPIO1=X GPIO4=X Btn=X PIR_PWR=X
```

If GPIO1 is already 1 here the AM312 is driving it *before* sleep — the
problem is the sensor or its power rail, not IO_MUX or the transition.

**Step 10 — Serial teardown + sleep entry (lines 778–782)**

```cpp
Serial.flush();              // drain TX while USB-JTAG-Serial is alive
Serial.end();                // clean shutdown — see Decision 2
esp_light_sleep_start();     // ← CPU blocks here until a wake event
```

**What the hardware does inside `esp_light_sleep_start()`:**

- Main CPU cores are clock-gated; RAM and peripherals stay powered.
- GPIO controller stays powered, but on ESP32-C3 GPIO outputs are **not**
  automatically retained through the clock-gate transition.  GPIO20
  (PIR power rail) must be held via `gpio_hold_en()` before sleep and
  released via `gpio_hold_dis()` after wake — see Decision 14.
- The GPIO wakeup sleep controller watches all armed pins.  If any pin
  hits its trigger level the CPU is woken.
- If a timer wakeup was armed (step 7) the RTC timer continues counting.

**No log output is possible between here and step 13 — Serial is down.**

**Step 11 — Wake event**

The sleep controller fires.  CPU unblocks out of
`esp_light_sleep_start()`.  The cause register is latched:
`ESP_SLEEP_WAKEUP_GPIO` (7) for a pin trigger,
`ESP_SLEEP_WAKEUP_TIMER` (1) for a timer expiry.

**Step 12 — Wake snapshot capture (lines 786–790)**

Executes in the first microseconds after wake.  Serial is still down so
values are stored in `static` variables and logged later.

```cpp
s_wakeGPIO1  = gpio_get_level(GPIO1);          // immediate read
s_wakeGPIO4  = gpio_get_level(GPIO4);
s_wakePirPwr = gpio_get_level(GPIO20);
s_wakeCause  = esp_sleep_get_wakeup_cause();   // 7 = GPIO, 1 = timer
s_wakeMcuSel = (REG_READ(IO_MUX_GPIO1_REG) >> MCU_SEL_S) & MCU_SEL_V;
```

**Step 13 — Serial reinit (line 795)**

`Serial.begin(SERIAL_BAUD_RATE)` — cold-start the USB-JTAG-Serial
peripheral.  First output becomes possible.

**Step 14 — Wake snapshot log (lines 797–799)**

```
Wake snapshot: cause=X GPIO1=X GPIO4=X PIR_PWR=X MCU_SEL=X
```

**Step 15 — CPU 80 → 160 MHz (line 803)**

**Step 16 — Disarm wakeup sources + restore GPIO1 pull-up**

`gpio_wakeup_disable()` on all motion pins and the button pin.  Without
this, re-entering sleep with a pin still at its trigger level causes an
immediate re-wake.

GPIO1's pull-up (stripped in step 5 to prevent the pad-glitch latch) is
restored here via `gpio_config()`.  Both sensors are in normal
INPUT + pull-up state before the main loop resumes.

**Step 17 — Timer→deep-sleep check (lines 821–827)**

If cause is `ESP_SLEEP_WAKEUP_TIMER` and deep sleep is enabled, calls
`enterDeepSleep()` — never returns (system reboots on next wake).
Otherwise falls through to step 18.

**Step 18 — Wake routing (lines 893–994)**

`detectAndRouteWakeSource()`:

| Cause | Check | Route | Log |
|---|---|---|---|
| GPIO | GPIO0 == 0 | `STATE_ACTIVE` | "Button" |
| GPIO | any motion pin == 1 | `STATE_MOTION_ALERT` | "PIR motion" |
| GPIO | no pin HIGH | `STATE_ACTIVE` | "Spurious GPIO wake (no pin HIGH)" |
| TIMER | — | `STATE_ACTIVE` | "Timer" |
| UNDEFINED | see Decision 4 | crash-recovery scan | see Decision 4 |

Final log: `Power: LIGHT_SLEEP -> <state> (wake: <source>, slept <dur>)`

---

#### Interpreting the four diagnostic signals

Read steps 8, 9, 12, and 18 together to isolate the cause of any
spurious GPIO1 wake:

| Pre-sleep GPIO1 (step 9) | Wake GPIO1 (step 12) | Wake MCU_SEL (step 12) | Conclusion |
|---|---|---|---|
| 0 | 1 | 1 (GPIO) | GPIO1 goes HIGH **during** `esp_light_sleep_start()`.  IO_MUX is correct at both snapshots.  Root cause is pad-level: investigate XTAL\_32K\_N signal coupling or a clock-transition glitch on this specific pad. |
| 0 | 1 | ≠ 1 | IO_MUX becomes corrupted **during** sleep despite `gpio_config()`.  Something in the sleep entry path resets MCU_SEL. |
| 1 | 1 | 1 (GPIO) | GPIO1 is already HIGH **before** sleep.  The AM312 sensor is driving it.  Investigate sensor warmup or power-rail transient during the 160→80 MHz transition window. |
| 1 | 1 | ≠ 1 | Both IO_MUX corruption and sensor-driven HIGH.  `gpio_config()` addresses the IO_MUX half; the sensor issue is separate. |
| * | 0 | * | Wake was not caused by GPIO1.  Check GPIO4 and GPIO0 columns for the actual trigger. |

If the routing result (step 18) is `Spurious GPIO wake (no pin HIGH)`
the glitch cleared before `detectAndRouteWakeSource()` ran.  The alert
was correctly suppressed; the wake snapshot captured the state at the
exact moment the glitch was still present for post-hoc analysis.

#### Note: `enterDeepSleep` GPIO configuration

`enterDeepSleep` (lines 846–862) still configures the button and PIR
fallback pins with `gpio_set_direction` + `gpio_pullup_en` — the pattern
that was replaced with `gpio_config()` in `enterLightSleep`.  This path
is not exercised in the current build: mode 2 uses the ULP coprocessor
route, and the GPIO fallback only fires if ULP fails to load.  If deep
sleep is ever exercised via the fallback it will hit the same IO_MUX gap
described in Decision 11.  Apply the same `gpio_config()` treatment
before enabling that path.

---

### Light-to-deep-sleep transition (mode 2, lightSleepToDeepSleepMs > 0)

```
Idle for 3 minutes
        │
        ▼
enterLightSleep(lightSleepToDeepSleepMs)   ← timer added alongside GPIO wakeup
  ├── setState(STATE_LIGHT_SLEEP)
  ├── saveStateToRTC()                      ← persists STATE_LIGHT_SLEEP to RTC
  ├── configure motion pins + button
  ├── enable timer wakeup                   ← duration minus setup overhead
  ├── Serial.flush() / end()
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
    ├── setState(STATE_DEEP_SLEEP)
    ├── saveStateToRTC()                  ← persists STATE_DEEP_SLEEP to RTC
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
    lastState == STATE_LIGHT_SLEEP          ← persisted after setState() (see Decision 10; was broken before Issue #39)
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

6. **GPIO1 spurious wake regression (Issues #39, #40)**
   - At boot, confirm serial log shows `[Setup] UART0 teardown: OK` and
     `[Setup] USB-JTAG-Serial self-test: OK`.
   - Let the device idle for 3 minutes until light-sleep entry is logged.
   - Do **not** move near the device or trigger any sensor.
   - **Pass:** device stays asleep.  No alert plays.  Serial produces no further
     output until real motion or a button press.
   - **Fail:** serial shows a wake within seconds of sleep entry.  Pull the log
     (`/device-logs`) and record these four lines:
     - `IO_MUX GPIO1: MCU_SEL=` — must be `1` (GPIO).  Any other value means
       IO_MUX is still corrupted despite `gpio_config()`.
     - `Light sleep GPIO: GPIO1=` — if `1`, the AM312 sensor is driving HIGH
       *before* sleep; the problem is the sensor or its power rail, not IO_MUX.
     - `Wake snapshot: … GPIO1= … MCU_SEL=` — pin state at the exact moment of
       wake.  Compare with pre-sleep GPIO1 to determine if the transition itself
       caused the HIGH.
     - `Power: … (wake: …)` — if it says `Spurious GPIO wake (no pin HIGH)` the
       glitch cleared before the scan; the alert was correctly suppressed.
   - See the diagnostic table in Issue #40 for next-step mapping.

7. **Crash-recovery saveStateToRTC ordering (Issue #39)**
   - Let the device enter light sleep.
   - Force a hard reset while it is sleeping (USB power-cycle or reset button).
   - If a PIR pin is HIGH at the moment the device reboots, serial must log
     "PIR motion (crash recovery)" and the alert must play via the injection
     path.  This confirms `lastState == STATE_LIGHT_SLEEP` is now correctly
     persisted by `saveStateToRTC()`.

8. **Light→deep timer fires at the expected time (Issue #39)**
   - Set `lightSleepToDeepSleepMs` to a known value (e.g. 10 s).
   - Let the device idle into light sleep.
   - Confirm: serial shows the timer-wakeup log after approximately 10 s, then
     the transition to deep sleep.  The timer must not fire early (over-
     subtraction) or be skipped.

9. **Spurious GPIO wake is suppressed, not alerted (Issue #40)**
   - Confirm the routing fix is active: after any GPIO wake from light sleep
     where the log shows `Spurious GPIO wake (no pin HIGH)`, verify that no
     alert sound plays and the state returns to ACTIVE (not MOTION_ALERT).
   - This scenario is triggered automatically whenever a momentary glitch wakes
     the device but clears before the post-wake pin scan.  It may also be
     triggered by temporarily shorting a motion pin HIGH for < 1 ms with a
     scope probe while the device is in light sleep.

---

## Sleep Time Counters

Two per-boot-cycle counters track how long the device has spent in each sleep
mode.  Both are exposed via `/api/status` (`power.lightSleepTime` and
`power.deepSleepTime`, in seconds) and displayed on the Status dashboard.

### lightSleepTime

Accumulated by `updateStats()` every loop tick while `m_state ==
STATE_LIGHT_SLEEP`.  Resets to 0 on every reboot (including deep-sleep wake).
Reflects only the current boot cycle.

### deepSleepTime

Persisted across deep-sleep reboots via `rtcMemory.deepSleepAccumulatedSec`.
Flow:

1. `saveStateToRTC()` writes the running `m_stats.deepSleepTime` into RTC
   before each sleep entry.
2. On deep-sleep wake, `begin()` calculates the episode duration from
   `rtc_time_get() - sleepEntryRTC` and adds it to the restored accumulator.
3. On a **fresh** boot (magic mismatch) the counter starts at 0.

Because deep sleep reboots the CPU, `deepSleepTime` cannot be accumulated by
`updateStats()`.  It is set once in `begin()` and thereafter only read.

### RTC magic

The magic constant was bumped from `0xBADC0FFE` to `0xBADC0FF1` when
`deepSleepAccumulatedSec` was added to `rtcMemory`.  Any firmware built before
this change will see a magic mismatch on first boot and reinitialise RTC state
(both counters start at 0).  This is intentional and safe.

### logStateSummary

The 5-minute periodic power-state summary (serial + debug log) includes both
counters in `Xh Ym` format alongside the existing uptime and idle-time lines.

---

## Regression Guard

> Any changes to sleep entry, GPIO wakeup configuration, or wake-source
> routing **must** update this document before the changeset is merged.
> Failure to keep this document current will silently break the crash-recovery
> and alert-injection paths on the next hardware revision.
