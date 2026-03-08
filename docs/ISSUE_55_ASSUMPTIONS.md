# Issue #55 — Boot Sequence Experience (Reimplementation of #30)

## Problem Statement
On power cycle, the device only displays the "eye" icon (MOTION_DETECT mode indicator). The boot sequence features specified in Issue #30 (snake progress, WiFi emblem, post-alert info, OTA progress) are not present in the current codebase despite Issue #30 being closed as implemented in build 0268.

## Assumptions Table

| ID | Assumption | Status | Evidence |
|----|-----------|--------|----------|
| A1 | BOOT_OK checkmark (ANIM_BOOT_STATUS) is the only boot visual currently | UNPROVEN | Code shows `startAnimation(ANIM_BOOT_STATUS, 3000)` at boot — need device video to confirm it's visible before mode indicator overwrites it |
| A2 | `setSnakeProgress()` as direct draw (not animation) won't conflict with `update()` loop | UNPROVEN | Need to verify `m_currentPattern` stays `ANIM_NONE` after direct `drawFrame()` call, meaning `updateAnimation()` is a no-op |
| A3 | `Wire.begin()` called early (before LittleFS) won't conflict with later I2C use | UNPROVEN | Wire library should handle re-init with same pins gracefully; needs device test |
| A4 | Mode indicator (eye) drawn by `enterMode()` will correctly overwrite snake progress at end of boot | UNPROVEN | `enterMode()` calls `drawBitmap()` which calls `drawFrame()` — should fully overwrite; verify no ghost pixels |
| A5 | `stopWarning()` is called from both `EVENT_TIMER_EXPIRED` and `exitMode()` — post-warn sequence must only trigger from timer expiry path | UNPROVEN | Both call sites confirmed in code; guard with `m_currentMode == MOTION_DETECT` check to prevent post-warn on mode switch |
| A6 | OTA progress callback invoked per-chunk won't overwhelm I2C bus | UNPROVEN | I2C at 100kHz, `drawFrame()` is ~1ms per transfer; OTA chunks are multi-KB so callback rate is low (~10-50/sec). Should be fine but verify no upload slowdown |

## Current Boot Sequence (what exists)
1. Serial.begin() + 1000ms delay
2. UART0 teardown
3. LittleFS init
4. g_debugLogger.begin()
5. CrashHandler::begin()
6. configManager.begin() + validate
7. Sensor manager + sensors loaded
8. PowerManager callbacks
9. LEDs, button init
10. **LED Matrix init** → `startAnimation(ANIM_BOOT_STATUS, 3000)` (static checkmark)
11. `StateMachine::begin(defaultMode)` → `enterMode()` → draws mode indicator (eye for MOTION_DETECT, overwrites checkmark)
12. WiFi, NTP, Web API started

## Target Boot Sequence (what Issue #30 requires)
1. Serial.begin() + UART0 teardown
2. **Stage 1: Early matrix probe** → snake pixel 1 (M0)
3. LittleFS init → snake 8 pixels (M1)
4. g_debugLogger.begin() → snake 13 pixels (M2)
5. CrashHandler → snake 18 pixels (M3)
6. configManager + **Stage 2: matrix reconfigure** → snake 24 pixels (M4)
7. Sensors loaded → snake 33 pixels (M5)
8. Direction detector → snake 40 pixels (M6)
9. LEDs + button → snake 48 pixels (M7)
10. State machine begin → snake 56 pixels (M8), then mode indicator drawn
11. WiFi manager → snake 60 pixels (M9)
12. Boot complete → snake 64 pixels (M10), transitions to mode indicator

## Features to Implement

### 1. Snake Boot Progress Animation
- 8x8 LED matrix fills in snake pattern (L→R on even rows, R→L on odd rows)
- 10+ milestones mapped to 0–64 pixel progression
- VERBOSE-level logging at each milestone for video-to-serial correlation

### 2. Early Matrix Initialization
- Stage 1: Probe with compile-time defaults before LittleFS/config
- Stage 2: Reconfigure from user config after configManager.begin()

### 3. WiFi First-Connect Emblem
- Show WIFI_CONNECTED icon for 2s on first WiFi connection per boot
- Static flag resets on each power cycle
- Don't interrupt active animations (motion alert, etc.)

### 4. Post-Motion-Alert Info Display
- After motion alert expires: WiFi status (2s) → battery level (2s) → idle
- PostWarningPhase sub-state machine in StateMachine
- Only triggers in MOTION_DETECT mode (not during mode switch)
- New battery bitmaps for healthy levels (FULL, 75%, 50%)

### 5. OTA Progress Display
- Snake-fill pattern during firmware upload (0–64 pixels = 0–100%)
- ProgressCallback in OTAManager, wired in web_api.cpp
- Full snake + checkmark on completion

## Files to Modify

| File | Changes |
|------|---------|
| `include/hal_ledmatrix_8x8.h` | Add `setSnakeProgress()`, `showBatteryBitmap()`, `showWifiConnected()`, `showWifiDisconnected()` |
| `src/hal_ledmatrix_8x8.cpp` | Snake lookup table, 3 new battery bitmaps, implement new methods |
| `src/main.cpp` | Early matrix init (Stage 1/2), boot milestones, WiFi emblem, WiFi disconnect callback |
| `include/state_machine.h` | `PostWarningPhase` enum, new members, `setWiFiConnected()` |
| `src/state_machine.cpp` | Post-warning sequence, `_updatePostWarningPhase()`, `hasDisplayActivity()` update |
| `include/ota_manager.h` | `ProgressCallback` typedef, `setProgressCallback()` |
| `src/ota_manager.cpp` | Callback invocation in `handleUploadChunk()` |
| `src/web_api.cpp` | Wire OTA progress to LED matrix |
| `include/config.h` | Timing constants for post-warn and WiFi emblem |

## Debug Logging Plan

All new instrumentation uses DEBUG or VERBOSE level per project rules:

| Location | Level | Example |
|----------|-------|---------|
| Boot milestones | VERBOSE | `Boot M3: CrashHandler init, snake=18/64` |
| Snake progress draw (at 0,16,32,48,64) | DEBUG | `Snake progress: 32/64 pixels drawn` |
| WiFi emblem decision | DEBUG | `WiFi emblem: shown=N, animating=Y` |
| Post-warn phase transitions | DEBUG | `Post-warn: WIFI→BATTERY, battery=78%` |
| Post-warn start/end | DEBUG | `Post-warn: starting (mode=MOTION_DETECT)` |
| OTA progress (every 10%) | DEBUG | `OTA progress: 40% → 26/64 pixels` |
| showBatteryBitmap bracket | DEBUG | `Battery bitmap: 78% → BATTERY_75` |

## Verification Checklist
- [ ] Build compiles clean (`pio run -e esp32c3`)
- [ ] Boot: snake fill visible across milestones, ends with mode indicator
- [ ] WiFi: emblem shows once on first connection per boot
- [ ] Post-alert: WiFi (2s) → battery (2s) → dark after motion alert
- [ ] OTA: snake tracks upload progress 0–100%
- [ ] Mode cycling during post-warn doesn't glitch display
- [ ] Debug logs: download `/api/debug/logs/current` at DEBUG level, verify all instrumentation
- [ ] Update assumptions table: mark each A1–A6 as PROVEN/DISPROVEN

---

**Created**: 2026-03-07
**Related Issues**: #30 (original), #55 (this reimplementation)
**Plan file**: `.claude/plans/distributed-prancing-mitten.md`