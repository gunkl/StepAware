# `/diagnose-sleep` — ESP32 Sleep/Power Management Diagnostics

Specialized diagnostic for ESP32 light/deep sleep issues in StepAware. Uses `/diagnose-device` framework but adds sleep-specific pattern recognition, hypothesis classification, and fix recommendations.

**Extends**: `/diagnose-device` general framework

---

## When to Use This Skill

✅ **Use `/diagnose-sleep` when:**
- Device crashes during sleep entry
- Device fails to wake from sleep
- Unexpected reboots related to power management
- Sleep durations don't match configuration
- "Mystery gaps" in device timeline
- RTC memory not persisting across sleeps
- Watchdog timeouts during sleep operations

❌ **Use `/diagnose-device` instead when:**
- Issue not related to sleep/power management
- Need general device diagnostics
- Multiple unrelated symptoms

---

## Step 1: Confirm Device IP

Same as `/diagnose-device`:
- Read `secrets.env` for `DEVICE_IP`
- Confirm with user or use argument-provided IP
- Validate connectivity

---

## Step 1.5: Validate User Environment (CRITICAL)

**Before analyzing logs or forming hypotheses, validate key assumptions with the user using AskUserQuestion.**

This prevents wasted investigation based on incorrect assumptions.

**Questions to Ask:**
1. Power source during test? (USB vs battery)
2. Which sensors were triggered? (Near, far, both, unknown)
3. What was observed when unresponsive? (LEDs, network, etc.)
4. How was device recovered? (Button, power cycle, automatic)
5. Battery voltage after recovery? (Healthy, low, critical, unknown)

Use AskUserQuestion tool with these verification questions BEFORE proceeding to log analysis.

---

## Step 2: Launch Sleep-Specialized Coordinator

Launch coordinator with sleep-specific analysis focus:

```python
coordinator = Task(
    subagent_type="general-purpose",
    description="Diagnose sleep issues",
    prompt=f"""
    Coordinate comprehensive SLEEP diagnostic for {DEVICE_IP}.

    Issue Description: {ISSUE_DESCRIPTION}

    PHASE 1: Data Collection (4 Parallel Agents)
    Standard collection PLUS sleep-specific:
    - Sleep cycle pattern matrix (Ready → Entered → Woke)
    - Wake source analysis (timer, GPIO, button)
    - Sleep duration statistics
    - RTC memory persistence tracking
    - Power state transitions

    PHASE 2: Sleep-Specific Pattern Analysis
    - Identify sleep entry failures
    - Detect wake failures
    - Analyze duration dependencies
    - Check RTC corruption patterns
    - Validate timer calculations

    PHASE 3: Classify Sleep Issues
    - **Type A: Pre-Sleep Crash**
      - Pattern: "Sleep check: READY" but no enterLightSleep() logs
      - Root: Watchdog timeout in handlePowerState()
      - Fix: Add esp_task_wdt_reset() before enterLightSleep() call

    - **Type B: During-Sleep-Entry Crash**
      - Pattern: enterLightSleep() called but no "WOKE UP" log
      - Root: Crash during GPIO setup, Serial.end(), or esp_light_sleep_start()
      - Fix: Add watchdog feeds at each sleep entry sub-step

    - **Type C: Sleep-Wake Failure**
      - Pattern: Never wakes, next boot is fresh
      - Root: Crash during wake handler, RTC corruption
      - Fix: Investigate wake GPIO handling, RTC validation

    - **Type D: Duration-Dependent**
      - Pattern: Short sleeps work, long sleeps fail
      - Root: Timer overflow (uint32_t with large ms values)
      - Fix: Add bounds checking, validate calculations

    PHASE 4: Generate Sleep-Specific Fix Strategy
    - Prioritize sleep-critical issues
    - Provide file/line numbers for power_manager.cpp
    - Include sleep timing validations
    - Add watchdog placements

    PHASE 5: Create Verification Plan
    - Short sleep test (15-30 min)
    - Medium sleep test (1-2 hours)
    - Long sleep test (8+ hours overnight)
    - Success criteria specific to sleep

    PROJECT CONTEXT:
    - Key file: src/power_manager.cpp
    - Functions: handlePowerState(), enterLightSleep(), enterDeepSleep()
    - Config file: include/config.h (sleep timing constants)

    Begin execution now.
    """,
    run_in_background=True
)
```

---

## Step 3: Sleep-Specific Success Criteria

When coordinator completes, validate these sleep-specific criteria:

```
**Sleep Diagnostics Success Criteria:**
- [ ] Identified sleep entry point (Ready/Entered/Woke pattern)
- [ ] Determined crash location (before/during/after sleep)
- [ ] Analyzed wake sources and timing
- [ ] Checked timer overflow risks
- [ ] Validated RTC memory persistence
- [ ] Tested short/medium/long sleep durations
- [ ] Confirmed detailed logs present
- [ ] No unexplained boot gaps remain
```

---

## Step 4: Sleep Issue Classification Reference

### Type A: Pre-Sleep Crash

**Pattern:**
```
[timestamp] Sleep check: READY
[timestamp] <silence - log ends>
[next boot] Fresh boot, initializing RTC memory
```

**Evidence:**
- "Sleep check: READY" present in logs
- NO "feeding watchdog before sleep preparation" logs
- NO "entering light sleep" messages
- Next boot is fresh (not RTC restored)

**Root Cause:**
Watchdog timeout in `handlePowerState()` between sleep readiness check and actual `enterLightSleep()` call.

**Fix:**
```cpp
// In src/power_manager.cpp - handlePowerState() function
// After logging "Sleep check: READY", add:
esp_task_wdt_reset();
DEBUG_LOG_SYSTEM("About to call enterLightSleep()");

// Right before enterLightSleep() call:
esp_task_wdt_reset();
DEBUG_LOG_SYSTEM("Free heap before sleep: %d bytes", esp_get_free_heap_size());
enterLightSleep(duration, reason);
```

**Test Plan:**
1. Flash fixed firmware
2. Wait for "Sleep check: READY" message
3. Verify "About to call enterLightSleep()" appears
4. Confirm device successfully enters sleep
5. Check device wakes properly

---

### Type B: During-Sleep-Entry Crash

**Pattern:**
```
[timestamp] Light sleep: feeding watchdog before sleep preparation
[timestamp] Light sleep: flushing Serial buffer
[timestamp] <silence - no WOKE UP message>
[next boot] Fresh boot OR core dump present
```

**Evidence:**
- enterLightSleep() was called (detailed logs present)
- Never reaches "WOKE UP successfully" message
- Core dump may show crash during Serial.end() or GPIO ops

**Root Cause:**
Crash during sleep entry operations:
- Serial.end() taking too long
- GPIO configuration failure
- esp_light_sleep_start() exception

**Fix:**
```cpp
// In src/power_manager.cpp - enterLightSleep() function
// Already implemented in Issue #1 fix:
// - Watchdog feeds before Serial.end()
// - Watchdog feeds after GPIO setup
// - Watchdog feeds before esp_light_sleep_start()

// If still failing, add more granular feeds
```

**Test Plan:**
1. Monitor for "feeding watchdog" messages
2. Check if "WOKE UP successfully" appears
3. Verify sleep duration matches expected
4. Confirm no core dumps generated

---

### Type C: Sleep-Wake Failure

**Pattern:**
```
[timestamp] WOKE UP successfully
[timestamp] Wake snapshot: cause=X GPIO1=X GPIO4=X
[timestamp] <crash during wake handling>
[next boot] Fresh boot, initializing RTC memory
```

**Evidence:**
- Device wakes from sleep (wake snapshot present)
- Crashes during wake handler processing
- RTC memory not preserved OR fresh boot after wake

**Root Cause:**
- Exception in wake source detection
- GPIO read failure during wake
- RTC corruption preventing state restoration

**Fix:**
```cpp
// In src/power_manager.cpp - wakeUp() or detectAndRouteWakeSource()
// Add validation and error handling:

esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
    DEBUG_LOG_SYSTEM("WARNING: Undefined wake cause, treating as crash recovery");
    // Handle gracefully
}

// Add try-catch around GPIO reads
// Validate RTC magic before using RTC data
```

**Test Plan:**
1. Verify wake source correctly identified
2. Check RTC state persists across wake
3. Confirm no crashes after "WOKE UP" message
4. Validate wake count increments properly

---

### Type D: Duration-Dependent Failure

**Pattern:**
```
Short sleeps (15-30 min): SUCCESS ✓
Medium sleeps (1-2 hours): SUCCESS ✓
Long sleeps (8+ hours): FAILURE ✗
```

**Evidence:**
- Pattern shows success rate decreases with duration
- May see timer overflow errors in logs
- Long sleeps never complete OR device reboots

**Root Cause:**
- Timer overflow: `uint32_t` can't hold large millisecond values
- Light sleep max duration exceeded
- Memory leak accumulating over time

**Fix:**
```cpp
// In src/power_manager.cpp - enterLightSleep() timer setup
uint32_t remainingMs = duration_ms - elapsedMs;

// Add bounds checking:
const uint32_t MAX_LIGHT_SLEEP_MS = 3600000; // 1 hour max
if (remainingMs > MAX_LIGHT_SLEEP_MS) {
    remainingMs = MAX_LIGHT_SLEEP_MS;
    DEBUG_LOG_SYSTEM("Sleep duration capped at 1h to prevent overflow");
}

// Validate timer won't overflow:
uint64_t timerUs = (uint64_t)remainingMs * 1000ULL;
if (timerUs > (UINT64_MAX / 2)) {
    DEBUG_LOG_SYSTEM("ERROR: Timer overflow risk, aborting sleep");
    return;
}

esp_sleep_enable_timer_wakeup(timerUs);
```

**Test Plan:**
1. Test 15-min sleep: Should work
2. Test 30-min sleep: Should work
3. Test 1-hour sleep: Should work (capped)
4. Test 8-hour config: Should cycle 1h sleeps or transition to deep sleep
5. Verify no timer overflow errors in logs

---

## Step 5: Sleep-Specific Verification Plan

### Phase 1: Short Sleep Test (0-2 hours)
```
Duration: 2 hours
Sleep Cycles: 15-30 minute sleeps
Success Criteria:
- [ ] Device reaches "Sleep check: READY"
- [ ] "About to call enterLightSleep()" appears
- [ ] enterLightSleep() successfully called
- [ ] "WOKE UP successfully" after each cycle
- [ ] Sleep durations match expected (±5%)
- [ ] No crashes or unexplained reboots
- [ ] Wake count increments correctly
```

### Phase 2: Medium Sleep Test (2-8 hours)
```
Duration: 6 hours
Sleep Cycles: 1-hour sleeps (if capped) or configured duration
Success Criteria:
- [ ] All short-test criteria continue passing
- [ ] Timer capping works if duration > 1h
- [ ] No timer overflow errors
- [ ] Heap usage stable (no memory leaks)
- [ ] Battery voltage trends normal
- [ ] RTC persistence across all wakes
```

### Phase 3: Long Sleep Test (8-24 hours)
```
Duration: 16 hours (overnight + morning)
Sleep Cycles: Full overnight sleep period
Success Criteria:
- [ ] Device survives overnight (8+ hours)
- [ ] Wake count accumulated correctly
- [ ] Sleep time accumulated correctly
- [ ] No "mystery gaps" in timeline
- [ ] Deep sleep transition works (if configured)
- [ ] Device wakes responsive in morning
- [ ] All logs present and detailed
```

---

## Skill Arguments

**Default:**
```bash
/diagnose-sleep
```
Uses `DEVICE_IP` from secrets.env

**With IP:**
```bash
/diagnose-sleep 10.123.0.98
```

**With test duration:**
```bash
/diagnose-sleep 10.123.0.98 --test-duration=8h
```
Focuses verification on specific sleep duration

---

## Related Skills

- **`/diagnose-device`** - General device diagnostics framework (parent skill)
- **`/device-logs`** - Download device logs for manual analysis
- **`/coredump`** - Download and analyze crash dumps

---

## Key Files Reference

**Primary:**
- `src/power_manager.cpp` - All sleep/wake logic
  - `handlePowerState()` - Sleep readiness and transition
  - `enterLightSleep()` - Light sleep entry (lines 729-935)
  - `enterDeepSleep()` - Deep sleep entry (lines 937-982)
  - `wakeUp()` - Wake handler
  - `detectAndRouteWakeSource()` - Wake source routing

**Configuration:**
- `include/config.h` - Sleep timing constants (lines 127-137)
  - `IDLE_TIMEOUT_MS`
  - `LIGHT_SLEEP_TO_DEEP_SLEEP_MS`

**Diagnostics:**
- `src/crash_handler.cpp` - Boot reason detection
- `src/debug_logger.cpp` - File-based logging

---

## Common Sleep Issues Quick Reference

| Symptom | Issue Type | Quick Fix |
|---------|------------|-----------|
| Crash after "Sleep check: READY" | Type A | Add watchdog feed before enterLightSleep() |
| Crash during sleep entry | Type B | Add more watchdog feeds in enterLightSleep() |
| Device never wakes | Type C | Check wake GPIO handling and RTC |
| Long sleeps fail | Type D | Add timer bounds checking |
| Fresh boot instead of RTC restore | Type C | Validate RTC magic and persistence |
| Timer overflow errors | Type D | Cap sleep duration to 1 hour max |
| Unexplained boot gaps | Multiple | Use this skill for full diagnosis |

---

**Last Updated**: 2026-02-06
**For**: StepAware ESP32 Project
**Framework**: Extends `/diagnose-device` with sleep-specific analysis
