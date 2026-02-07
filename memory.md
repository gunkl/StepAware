# StepAware — Tooling & Environment Notes

This file is the source of truth for build tooling, executable locations,
and environment quirks discovered during development.  Claude reads this
at the start of any session to avoid re-discovering known facts.

---

## Docker

All builds and tests run inside the project's Docker container:

```bash
docker-compose run --rm stepaware-dev <command>
```

The container image is defined in `docker-compose.yml`.  PlatformIO,
all toolchains, and package caches are pre-installed inside it.

---

## PlatformIO

| Command | What it does |
|---|---|
| `pio run -e esp32c3` | Build firmware for ESP32-C3 |
| `pio test -e native` | Run unit tests on host (no hardware needed) |
| `pio run -e esp32c3 --upload-port <port>` | Flash firmware |

Package cache (inside container): `/root/.platformio/packages/`

Key paths inside the container:
- Framework headers: `/root/.platformio/packages/framework-arduinoespressif32/tools/sdk/<chip>/include/`
- ESP32-C3 SDK: `.../sdk/esp32c3/`
- Toolchains: `toolchain-riscv32-esp` (RISC-V cross-compiler), `toolchain-esp32ulp` (legacy FSM ULP assembler — ESP32 original only, not C3)

---

## Device Secrets

Device-specific values (IP address, etc.) are stored in `secrets.env` in the
project root.  That file is gitignored — only `secrets.env.example` is in the
repo.

When a task needs the device IP (skills like `/coredump`, ad-hoc curl calls,
log downloads, or any other interaction with the live device), **read
`secrets.env` first** and extract `DEVICE_IP`.  Do not hardcode or assume an
IP address.

If `secrets.env` does not exist, tell the user to copy `secrets.env.example`
to `secrets.env` and fill in their values before proceeding.

---

## Python

Use `python3`, not `python`.  The container does not symlink a bare
`python` command.

---

## ESP32-C3 ULP RISC-V

**The ESP32-C3 hardware DOES have a RISC-V ULP coprocessor** (see TRM §29).
However, `framework-arduinoespressif32` (v3.0.0 / espressif32@6.5.0) does
**not** bundle ULP support for C3:

- `esp_ulp_riscv.h` — missing for C3 (only present under `sdk/esp32s2/` and `sdk/esp32s3/`)
- `libulp.a` — missing for C3
- `ulp_riscv.h` linker script — missing for C3

**Current workaround (Issue #27):**
- ULP code in `src/power_manager.cpp` is gated behind `#ifdef HAS_ULP_RISCV`
- Mode 2 (Deep Sleep) falls back to standard GPIO deep-sleep wakeup automatically
- The ULP source (`ulp/ulp_pir_monitor.c`) is kept in the repo, ready to activate

**To enable ULP when the framework adds C3 support**, uncomment the three
lines in `platformio.ini` marked `HAS_ULP_RISCV`.  Do not assume this is a
hardware limitation — it is a framework packaging gap.

---

## ESP32-C3 GPIO Pin Restrictions

**Reserved/Restricted Pins (DO NOT use for general purpose):**
- **GPIO5**: Used for battery voltage divider circuit
- **GPIO6**: Not a general-purpose I/O pin on ESP32-C3

**Deep Sleep Wakeup Constraint:**
- Only GPIO 0-5 can wake from deep sleep on ESP32-C3
- Sensors/buttons for wakeup must be on GPIO 0-5

**Current Pin Assignments:**
- GPIO1: Near PIR sensor
- GPIO2: (formerly Status LED - now configurable)
- GPIO3: (formerly Hazard LED - now configurable)
- GPIO4: Far PIR sensor
- GPIO5: Battery voltage divider (RESERVED)

**`pinMode` constants are bitmasks, not 0/1/2/3.**
`INPUT=0x01, INPUT_PULLUP=0x05, INPUT_PULLDOWN=0x09`.
Never pass a raw enum value to `pinMode()`.  See "GPIO / pinMode Best Practices"
in `CLAUDE.md` for the full explanation and the switch-case pattern to use.
(Issue #37)

---

## Voltage Monitoring

All ADC calibration, voltage divider, USB detection, and voltage-
protection details live in `docs/VOLTAGE_MONITORING.md`.  Read it
before touching anything in PowerManager's ADC or battery path.

---

## Web UI Buffer Constraints

**Critical ESP32 Heap Limitation:** Max contiguous heap allocation is ~64KB.

The inline HTML dashboard (`buildDashboardHTML()` in `src/web_api.cpp`) is split
into TWO parts to stay within this limit:

**Current Buffer Sizes (lines 2542, 2993):**
- `g_htmlPart1` (HTML/CSS): Reserved 44KB (45056 bytes)
- `g_htmlPart2` (JavaScript): Reserved 44KB (45056 bytes), typically ~41KB used

**IMPORTANT: When adding UI features:**
1. Check serial log during boot for current sizes:
   ```
   buildDashboardHTML() complete: part1=XXXXX part2=XXXXX total=XXXXX bytes
   ```
2. If either part approaches its reservation (44KB):
   - **Increase reservation** at lines 2542 or 2993 (max ~48KB per part is safe)
   - **OR minify** JavaScript (remove whitespace, shorten variable names)
   - **OR split into 3 parts** (last resort)

3. Each part MUST stay under 64KB individually
4. Total size can exceed 64KB (parts are allocated separately)

**Symptoms of buffer overflow:**
- Device crashes during web UI load
- Boot loops when accessing dashboard
- Heap fragmentation warnings in serial log

**Known safe ranges:**
- **As of 2026-01:** Part 1: ~35-40KB typical, 44KB reserved; Part 2: ~41KB, 44KB reserved (3KB headroom)
- **As of 2026-02-02:** Part 2 increased to 48KB (49152 bytes) after adding:
  - Single LED display type support with type-aware UI (~1.5KB JavaScript)
  - Enhanced editDisplay() with GPIO pin editing (~1KB JavaScript)
  - 4 new global LED settings (brightnessMedium, 3 blink rates) (~1KB HTML + JavaScript)
  - Total additions: ~3.5KB to Part 2
  - New Part 2 headroom: ~3-4KB at 48KB reservation

---

## Sleep / Wake System

Any changes to sleep entry, wake sources, or GPIO wakeup MUST be
vetted against `docs/SLEEP_WAKE.md` before merging.  The ESP32-C3
USB-JTAG-Serial peripheral requires explicit re-init after light
sleep — see that doc for the full picture.

Wake-source pins are NOT hardcoded.  They are populated at boot from
the sensor configuration via `PowerManager::setMotionWakePins()`.

---

## Device Diagnostics

### General Device Diagnostics: `/diagnose-device`

**Issue**: Any device malfunction (crashes, reboots, connectivity, sensor failures, etc.)

**Skill**: `/diagnose-device [IP_ADDRESS] ["issue description"]`

**Architecture**: Agent-based execution
- Coordinator Agent (background): Manages workflow, synthesizes results
- 4 Parallel Data Collectors: Logs, status, core dump, filesystem
- Analysis Agents: Pattern recognition, timeline reconstruction
- Verification Agent: Tests fixes, validates success criteria

**What it does:**
- Parallel data collection (minimize wait time)
- Comprehensive pattern analysis across all data sources
- Multi-issue detection and classification
- Prioritized fix recommendations with file/line numbers
- Parallel vs sequential fix strategy
- Automated verification test plan

**When to use:**
- Device behaving unexpectedly
- Need systematic root cause analysis
- Multiple symptoms (may be related issues)
- Want comprehensive diagnostics without manual iteration
- First time investigating a device issue

**Skill file**: `.claude/skills/diagnose-device.md`

---

### Sleep/Power Management Diagnostics: `/diagnose-sleep`

**Issue**: Device crashes during sleep entry, fails to wake, or has unexplained reboots related to power management

**Skill**: `/diagnose-sleep [IP_ADDRESS]`

**Extends**: `/diagnose-device` with sleep-specific analysis

**Sleep-Specific Analysis:**
- Sleep cycle pattern matrix (Ready → Entered → Woke)
- Wake source analysis (timer, GPIO, button)
- Duration-dependent failure detection
- RTC memory persistence tracking
- Power state transition timing

**Common Sleep Issues & Fixes:**

1. **Pre-Sleep Crash (Type A)**
   - **Pattern**: "Sleep check: READY" but no enterLightSleep() logs
   - **Root**: Watchdog timeout in handlePowerState() before sleep call
   - **Fix**: Add `esp_task_wdt_reset()` in handlePowerState() before enterLightSleep() call
   - **File**: `src/power_manager.cpp` - handlePowerState()

2. **During-Sleep-Entry Crash (Type B)**
   - **Pattern**: enterLightSleep() called but device doesn't wake
   - **Root**: Crash during Serial.end(), GPIO setup, or esp_light_sleep_start()
   - **Fix**: Add watchdog feeds before Serial.end() and GPIO operations
   - **File**: `src/power_manager.cpp` - enterLightSleep()

3. **Duration-Dependent Failure (Type D)**
   - **Pattern**: Short sleeps work, long sleeps fail
   - **Root**: Timer overflow with large millisecond values
   - **Fix**: Add bounds checking, cap duration to 1 hour max
   - **File**: `src/power_manager.cpp` - enterLightSleep() timer setup

4. **RTC Corruption (Type C)**
   - **Pattern**: Device always does "Fresh boot" instead of RTC restore
   - **Root**: RTC magic not persisting or wake handler crash
   - **Fix**: Check deep sleep wake handling, validate RTC magic
   - **File**: `src/power_manager.cpp` - restoreStateFromRTC()

**Key Files:**
- `src/power_manager.cpp` - Sleep entry, handlePowerState(), enterLightSleep()
- `src/crash_handler.cpp` - Boot reason detection, reset tracking
- `include/config.h` - Sleep timing constants

**Related Skills:**
- `/device-logs` - Download individual log files
- `/coredump` - Download and analyze crash dumps
- `/diagnose-device` - General diagnostic framework (parent skill)

**Skill file**: `.claude/skills/diagnose-sleep.md`

---

## Sleep Diagnostics - User Assumption Validation

**CRITICAL**: When diagnosing sleep issues, ALWAYS validate assumptions with user before forming hypothesis.

**Why This Matters:**
- Incorrect assumptions waste investigation time (e.g., assuming USB connected when battery-only)
- User knows the actual test environment better than logs can reveal
- Asking prevents debugging the wrong problem

**Validation Questions** (use AskUserQuestion before log analysis):
1. **Power source** - USB vs battery only
2. **Sensors triggered** - Near, far, both, or unknown
3. **Observations** - What was seen before recovery (LEDs, network, etc.)
4. **Recovery method** - Button press, power cycle, or automatic
5. **Battery voltage** - Healthy, low, critical, or unknown

**Procedure**: See `.claude/skills/diagnose-sleep.md` Step 1.5 for full validation workflow

**Related Documentation**: `docs/TROUBLESHOOTING.md` - Diagnostic checklist and known limitations