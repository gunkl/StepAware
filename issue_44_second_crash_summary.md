# Issue #44 - Second Crash Analysis & Additional Fixes

## Executive Summary

After deploying build 0226 with the original three-track fixes, a **second crash occurred** with a **different signature**. The crash moved deeper into the ESP-IDF layer, revealing that the watchdog timeout was still occurring but now **inside** `esp_light_sleep_start()` itself.

**Build History**:
- Build 0223: Original firmware with `duration_ms=0` bug
- Build 0226: Fixed duration validation → Crash moved to ESP-IDF layer
- Build 0227: **NEW** - Disabled watchdog during sleep entry + persistent rotation logging

---

## Second Crash Analysis (Build 0226)

### Crash Signature Comparison

| Aspect | First Crash (0223) | Second Crash (0226) |
|--------|-------------------|---------------------|
| **Firmware** | 0.5.10 (build 0223) | 0.5.11 (build 0226) ✅ FIXED |
| **Crash Line** | power_manager.cpp:928 | power_manager.cpp:941 |
| **Location** | Before ESP-IDF call | **Inside esp_light_sleep_start()** |
| **Duration Validation** | ❌ Not present | ✅ Present and passed |
| **Watchdog Feeds** | ✅ One feed | ✅ Two feeds (lines 785, 939) |
| **ESP-IDF Location** | N/A | sleep_modes.c:894 |

### Core Dump Analysis

**Stack Trace (Thread 5 - loopTask)**:
```
#0  esp_light_sleep_start() at sleep_modes.c:894  ← WATCHDOG TIMEOUT HERE
#1  PowerManager::enterLightSleep(reason="idle timeout 180000ms") at power_manager.cpp:941
#2  PowerManager::handlePowerState() at power_manager.cpp:580
#3  PowerManager::update() at power_manager.cpp:327
#4  loop() at main.cpp:1413
```

**Key Observation**: The crash occurred **inside** the ESP-IDF function `esp_light_sleep_start()` at line 894 of sleep_modes.c. This indicates the sleep configuration is taking >5 seconds, triggering the task watchdog.

### Root Cause

The ESP-IDF `esp_light_sleep_start()` function performs extensive operations:
- Timer wakeup configuration
- GPIO wakeup configuration (GPIO0, GPIO1, GPIO4)
- Peripheral state saving
- Clock reconfiguration
- UART teardown

Under certain conditions (likely GPIO configuration with multiple wake sources), these operations exceed the 5-second watchdog timeout.

---

## New Fixes - Build 0227

### Track D: Watchdog Protection During Sleep Entry 🆕

**Problem**: ESP-IDF sleep function takes >5 seconds, but we cannot feed watchdog from inside ESP-IDF code.

**Solution**: Remove task from watchdog before sleep entry, re-add after wake.

**File**: `src/power_manager.cpp`

**Changes**:

1. **Before Sleep** (line ~941):
```cpp
// CRITICAL FIX (Issue #44 - Second crash): Disable watchdog for sleep entry
// ESP-IDF esp_light_sleep_start() can take >5s during GPIO/timer configuration
// Remove this task from watchdog before sleep, re-add after wake
esp_task_wdt_delete(NULL);  // NULL = current task (loopTask)
DEBUG_LOG_SYSTEM("Light sleep: Removed task from watchdog for ESP-IDF sleep entry");
```

2. **After Wake** (line ~961):
```cpp
// Re-enable watchdog after wake
esp_task_wdt_add(NULL);  // NULL = current task (loopTask)
DEBUG_LOG_SYSTEM("Light sleep: Re-added task to watchdog after wake");
```

**Rationale**:
- Cannot extend watchdog timeout dynamically in this ESP-IDF version
- Removing task from watchdog allows slow sleep entry without timeout
- Watchdog protection restored immediately after wake
- Other tasks remain protected during sleep entry

---

### Track C: Persistent Rotation Diagnostics Enhancement 🆕

**Problem**: Log rotation diagnostics print to Serial (USB), which isn't monitored during overnight tests. When rotation fails during crashes, we lose forensic evidence.

**Solution**: Write rotation diagnostics to persistent file BEFORE attempting rotation.

**File**: `src/debug_logger.cpp`

**New Function** (line ~568):
```cpp
void DebugLogger::logRotationAttempt() {
#if !MOCK_HARDWARE
    // Write rotation diagnostics to persistent file
    File debugFile = LittleFS.open("/logs/rotation_debug.txt", "w");
    if (!debugFile) {
        Serial.println("[DebugLogger] ERROR: Cannot create rotation_debug.txt");
        return;
    }

    // Log timestamp
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[Boot #%lu]", m_bootCycle);
    debugFile.printf("%s ROTATION ATTEMPT\n", timestamp);

    // Log filesystem state
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    size_t available = total - used;
    float percentUsed = (used * 100.0f) / total;
    debugFile.printf("  FS: %u/%u bytes (%.1f%% used, %u free)\n",
                    used, total, percentUsed, available);

    // Log file existence
    debugFile.printf("  Files: boot_2=%d boot_1=%d current=%d\n",
                    LittleFS.exists("/logs/boot_2.log"),
                    LittleFS.exists("/logs/boot_1.log"),
                    LittleFS.exists(CURRENT_LOG));

    debugFile.close();
    Serial.println("[DebugLogger] Rotation debug logged to /logs/rotation_debug.txt");
#endif
}
```

**Called From** `rotateLogs()` (line ~605):
```cpp
void DebugLogger::rotateLogs() {
#if !MOCK_HARDWARE
    Serial.println("[DebugLogger] === LOG ROTATION START ===");

    // NEW (Issue #44 - Second crash): Log rotation attempt to persistent file
    logRotationAttempt();

    // ... rest of rotation logic
```

**New Web API Endpoint** (`src/web_api.cpp` line ~358):
```cpp
// NEW (Issue #44 - Second crash): Rotation diagnostics endpoint
m_server->on("/api/debug/logs/rotation_debug", HTTP_GET, [this](AsyncWebServerRequest* req) {
#if !MOCK_HARDWARE
    req->send(LittleFS, "/logs/rotation_debug.txt", "text/plain");
#else
    this->sendError(req, 501, "Not available in mock mode");
#endif
});
```

**Rationale**:
- Diagnostics written to LittleFS BEFORE rotation attempts
- File survives reboot even if rotation fails
- Provides forensic evidence of WHY rotation failed during crashes
- Accessible via web API: `GET /api/debug/logs/rotation_debug`

---

## Build Verification

**Status**: ✅ SUCCESS

**Build 0227 Stats**:
```
RAM:   [===       ]  25.1% (used 82260 bytes from 327680 bytes)
Flash: [========  ]  79.5% (used 1249730 bytes from 1572864 bytes)
```

**Changes from Build 0226**:
- Flash: +1200 bytes (79.4% → 79.5%) - Rotation diagnostics function
- RAM: No change

---

## Testing Plan

### Test 1: Watchdog Timeout Elimination
1. Flash build 0227 to device
2. Wait for idle timeout (3 minutes)
3. Monitor logs for "Removed task from watchdog" message
4. Verify device enters sleep successfully
5. Verify "Re-added task to watchdog" message on wake
6. Run overnight test (8+ hours)

**Expected Result**: No watchdog timeouts during sleep entry

### Test 2: Persistent Rotation Diagnostics
1. Flash build 0227 to device
2. Trigger device reboot (any method)
3. Retrieve rotation diagnostics: `curl http://10.123.0.98/api/debug/logs/rotation_debug`
4. Verify file contains:
   - Boot cycle number
   - Filesystem health (total/used/free)
   - File existence flags (boot_2/boot_1/current)
5. Force crash and check if diagnostics survived

**Expected Result**: rotation_debug.txt persists across reboots with actionable data

### Test 3: Overnight Stability
1. Flash build 0227
2. Run overnight test on battery (8+ hours)
3. Morning check:
   - Device responsive
   - No unexpected reboots
   - Logs properly rotated
   - rotation_debug.txt shows successful rotation

**Success Criteria**:
- ✅ Zero watchdog timeouts
- ✅ Zero crashes
- ✅ Logs preserved and rotated correctly
- ✅ rotation_debug.txt created and readable

---

## Deployment Notes

**Version**: 0.5.12 (recommended)
**Build**: 0227
**Breaking Changes**: None
**Config Changes**: None required

**API Additions**:
- `GET /api/debug/logs/rotation_debug` - Download rotation diagnostics

**Rollback Plan**:
If issues occur, revert to build 0226:
```bash
git checkout [commit-hash-0226]
docker-compose run --rm stepaware-dev pio run -e esp32c3 -t upload
```

**Monitoring After Deployment**:
Focus on these log patterns:
- `Removed task from watchdog for ESP-IDF sleep entry` - Confirm watchdog protection disabled
- `Re-added task to watchdog after wake` - Confirm protection restored
- `Rotation debug logged to /logs/rotation_debug.txt` - Confirm diagnostics written
- Any new watchdog timeouts or panics

---

## Related Issues

- Issue #44 - Original crash: enterLightSleep(duration_ms=0)
- Issue #38 - GPIO1 XTAL_32K_N pad glitch workaround
- Issue #41 - updateStats() m_stateEnterTime clobbering fix
- Issue #42 - Battery voltage calibration offset
- Issue #43 - PIR tracking stability improvements

---

## Files Modified

| File | Lines Changed | Purpose |
|------|--------------|---------|
| `src/power_manager.cpp` | +8, ~3 | Disable/enable watchdog around sleep entry |
| `src/debug_logger.cpp` | +34 (new function) | Persistent rotation diagnostics |
| `src/debug_logger.cpp` | +3 | Call logRotationAttempt() in rotateLogs() |
| `include/debug_logger.h` | +7 | Add logRotationAttempt() declaration |
| `src/web_api.cpp` | +9 | Add rotation_debug endpoint |

**Total**: 5 files, ~61 lines added/modified

---

## Summary

The second crash revealed a deeper issue: ESP-IDF's sleep entry function takes >5 seconds under certain conditions. Build 0227 addresses this by temporarily removing the task from watchdog protection during sleep entry, and adds persistent rotation diagnostics to preserve forensic evidence during crashes.

**Next Steps**:
1. Flash build 0227
2. Run overnight stability test
3. Monitor for any new issues
4. Close issue if overnight test passes

---

**Prepared by**: Claude Sonnet 4.5
**Date**: 2026-02-09
**Build**: 0227 (ESP32-C3)
**Status**: Ready for deployment testing
