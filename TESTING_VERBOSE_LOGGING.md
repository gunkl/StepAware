# Testing Guide: Verbose Logging Fixes

## Overview

This guide provides step-by-step testing procedures to verify the verbose logging fixes.

## Test Setup

### Prerequisites

1. ESP32 with StepAware firmware flashed
2. At least 1 distance sensor configured (e.g., ultrasonic)
3. Access to web API (WiFi connected)
4. Serial monitor or web log viewer

### API Endpoints Used

```
GET  /api/debug/logs/current    - View current log
GET  /api/debug/logs/info       - Check filesystem usage
POST /api/debug/config          - Change log level
```

---

## Test 1: Log Level Change (Bug Fix Verification)

**Purpose**: Verify that changing log level from VERBOSE to INFO stops sensor logging.

### Steps

1. **Set log level to VERBOSE**
   ```bash
   curl -X POST http://<ESP32_IP>/api/debug/config \
     -H "Content-Type: application/json" \
     -d '{"level":"VERBOSE"}'
   ```

2. **Verify verbose sensor logs appear**
   - Open serial monitor or view logs via API
   - Should see sensor readings with `[VERBOSE] [SENSOR]`
   - Example:
     ```
     [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY [INITIAL]
     ```

3. **Change log level to INFO**
   ```bash
   curl -X POST http://<ESP32_IP>/api/debug/config \
     -H "Content-Type: application/json" \
     -d '{"level":"INFO"}'
   ```

4. **Verify sensor logs STOP immediately**
   - Sensor verbose logs should disappear
   - Only INFO/WARN/ERROR logs remain
   - Example output should NOT contain `[VERBOSE] [SENSOR]`

5. **Change back to VERBOSE**
   ```bash
   curl -X POST http://<ESP32_IP>/api/debug/config \
     -H "Content-Type: application/json" \
     -d '{"level":"VERBOSE"}'
   ```

6. **Verify sensor logs RESUME**
   - `[VERBOSE] [SENSOR]` logs should reappear
   - System picks up logging from current state

### Expected Results

| Action | Expected Log Output |
|--------|-------------------|
| Set VERBOSE | Sensor readings appear: `[VERBOSE] [SENSOR] Slot 0: ...` |
| Set INFO | Sensor readings STOP immediately |
| Set VERBOSE again | Sensor readings RESUME immediately |

### Pass Criteria

- ✅ Sensor verbose logs appear when level = VERBOSE
- ✅ Sensor verbose logs disappear when level = INFO
- ✅ Level change takes effect immediately (no delay)
- ✅ Logs resume correctly when switching back to VERBOSE

---

## Test 2: Change Detection (Smart Logging)

**Purpose**: Verify that only significant changes are logged, not every reading.

### Steps

1. **Set log level to VERBOSE**
   ```bash
   curl -X POST http://<ESP32_IP>/api/debug/config \
     -H "Content-Type: application/json" \
     -d '{"level":"VERBOSE"}'
   ```

2. **Keep sensor static (no motion)**
   - Don't move any objects near sensor
   - Wait 10-15 seconds

3. **Verify periodic summaries**
   - Should see summary logs every 5 seconds or 20 readings
   - Example:
     ```
     [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
     ```

4. **Move object near sensor**
   - Move hand or object within sensor range
   - Create distance change >50mm

5. **Verify change log appears**
   - Should see `[CHANGED: ...]` annotation
   - Example:
     ```
     [VERBOSE] [SENSOR] Slot 0: dist=1200 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3520 mm, motion DETECTED, dir STATIONARY->APPROACHING]
     ```

6. **Keep object still at new position**
   - Hold object steady
   - Wait 10-15 seconds

7. **Verify new summaries at new distance**
   - Should see summaries with new stable reading
   - Example:
     ```
     [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=1200 mm, motion=YES
     ```

8. **Move object away**
   - Remove object from sensor range

9. **Verify receding/cleared logs**
   - Should see direction change and motion cleared
   - Example:
     ```
     [VERBOSE] [SENSOR] Slot 0: dist=3500 mm, motion=NO, dir=STATIONARY [CHANGED: dist changed from 1200 mm, motion CLEARED, dir APPROACHING->STATIONARY]
     ```

### Expected Results

| Sensor State | Expected Log |
|-------------|--------------|
| Initial reading | `[INITIAL]` annotation |
| Static (no change) | Summary every 5s or 20 readings |
| Distance change >50mm | `[CHANGED: dist changed from ...]` |
| Motion detected | `[CHANGED: motion DETECTED]` |
| Motion cleared | `[CHANGED: motion CLEARED]` |
| Direction change | `[CHANGED: dir A->B]` |
| Minor noise (<50mm) | No log (filtered) |

### Pass Criteria

- ✅ First reading logged with `[INITIAL]`
- ✅ Periodic summaries appear when static
- ✅ Changes logged with `[CHANGED: ...]` annotation
- ✅ Minor fluctuations (<50mm) NOT logged
- ✅ Log volume reduced (not logging every 80ms)

---

## Test 3: Filesystem Protection

**Purpose**: Verify that verbose logging no longer fills filesystem.

### Steps

1. **Check initial filesystem usage**
   ```bash
   curl http://<ESP32_IP>/api/debug/logs/info
   ```
   - Note the `filesystemUsage` percentage

2. **Enable VERBOSE logging**
   ```bash
   curl -X POST http://<ESP32_IP>/api/debug/config \
     -H "Content-Type: application/json" \
     -d '{"level":"VERBOSE"}'
   ```

3. **Run for 10-30 minutes with sensor activity**
   - Move objects periodically
   - Simulate real-world usage
   - OR leave static for steady-state test

4. **Check filesystem usage again**
   ```bash
   curl http://<ESP32_IP>/api/debug/logs/info
   ```

5. **Calculate log growth rate**
   - Compare before/after filesystem usage
   - Estimate time to reach 30% limit

### Expected Results (30 minutes, 1 sensor)

| Scenario | Old Behavior | New Behavior |
|----------|-------------|--------------|
| Static sensor | 30% in 15-20 min | ~1-2% growth in 30 min |
| Active sensor | 30% in 10-15 min | ~2-5% growth in 30 min |

### Pass Criteria

- ✅ Filesystem growth is LINEAR, not exponential
- ✅ 30-minute test uses <5% additional filesystem
- ✅ Projected time to 30% is HOURS, not minutes
- ✅ Log file size is REASONABLE (not megabytes)

### Filesystem Monitoring

**Before fix** (expected old behavior):
```json
{
  "filesystemUsage": 28,
  "currentLogSize": 245000,
  "totalLogsSize": 480000,
  "bootCycle": 5
}
```
*Rapid growth, hitting 30% in 15-20 minutes*

---

**After fix** (expected new behavior):
```json
{
  "filesystemUsage": 5,
  "currentLogSize": 15000,
  "totalLogsSize": 42000,
  "bootCycle": 5
}
```
*Slow growth, sustainable for hours/days*

---

## Test 4: Multiple Change Scenarios

**Purpose**: Test various change detection scenarios.

### Test 4.1: Approaching Object

1. Set VERBOSE logging
2. Start with sensor clear (no object)
3. Move object slowly toward sensor
4. Expected logs:
   ```
   [VERBOSE] [SENSOR] Slot 0: dist=4000 mm, motion=NO, dir=STATIONARY [INITIAL]
   [VERBOSE] [SENSOR] Slot 0: dist=3200 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 4000 mm, motion DETECTED, dir STATIONARY->APPROACHING]
   [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3200 mm]
   [VERBOSE] [SENSOR] Slot 0: dist=1000 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 2100 mm]
   ```

### Test 4.2: Receding Object

1. Start with object close to sensor
2. Move object away from sensor
3. Expected logs:
   ```
   [VERBOSE] [SENSOR] Slot 0: dist=500 mm, motion=YES, dir=APPROACHING [current state]
   [VERBOSE] [SENSOR] Slot 0: dist=1200 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 500 mm, dir APPROACHING->RECEDING]
   [VERBOSE] [SENSOR] Slot 0: dist=2500 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 1200 mm]
   [VERBOSE] [SENSOR] Slot 0: dist=3800 mm, motion=NO, dir=STATIONARY [CHANGED: dist changed from 2500 mm, motion CLEARED, dir RECEDING->STATIONARY]
   ```

### Test 4.3: Noise Filtering

1. Keep sensor static
2. Allow minor noise fluctuations (±5-45mm)
3. Expected logs:
   ```
   [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY [INITIAL]
   [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
   [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
   ```
   (Minor fluctuations NOT logged individually)

### Test 4.4: Rapid Changes

1. Move object back and forth rapidly
2. Each >50mm change should be logged
3. Expected logs show all significant transitions:
   ```
   [VERBOSE] [SENSOR] Slot 0: dist=1000 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3000 mm...]
   [VERBOSE] [SENSOR] Slot 0: dist=2500 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 1000 mm, dir APPROACHING->RECEDING]
   [VERBOSE] [SENSOR] Slot 0: dist=800 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 2500 mm, dir RECEDING->APPROACHING]
   ```

### Pass Criteria

- ✅ All >50mm distance changes logged
- ✅ All motion state changes logged (NO->YES, YES->NO)
- ✅ All direction changes logged (APPROACHING<->RECEDING)
- ✅ Minor fluctuations (<50mm) filtered out
- ✅ Change annotations clear and accurate

---

## Test 5: Multi-Sensor Configuration

**Purpose**: Verify change detection works with multiple sensors.

### Steps

1. Configure 2+ sensors
2. Enable VERBOSE logging
3. Trigger motion on sensor 0 only
4. Verify only sensor 0 logs changes
5. Trigger motion on sensor 1
6. Verify both sensors log independently

### Expected Results

```
[VERBOSE] [SENSOR] Slot 0: dist=3500 mm, motion=NO, dir=STATIONARY [INITIAL]
[VERBOSE] [SENSOR] Slot 1: dist=2800 mm, motion=NO, dir=STATIONARY [INITIAL]
[VERBOSE] [SENSOR] Slot 0: dist=1200 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3500 mm, motion DETECTED, dir STATIONARY->APPROACHING]
[VERBOSE] [SENSOR] Slot 1: No change (20 readings over 5000 ms) - dist=2800 mm, motion=NO
[VERBOSE] [SENSOR] Slot 1: dist=800 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 2800 mm, motion DETECTED, dir STATIONARY->APPROACHING]
[VERBOSE] [SENSOR] Slot 0: No change (18 readings over 4500 ms) - dist=1200 mm, motion=YES
```

### Pass Criteria

- ✅ Each sensor tracked independently
- ✅ Sensor 0 changes don't affect Sensor 1 logs
- ✅ Summaries shown for inactive sensors
- ✅ All sensors respect 50mm threshold

---

## Test 6: Log Download and Analysis

**Purpose**: Verify logs are readable and parsable.

### Steps

1. Run system for 15-30 minutes with VERBOSE logging
2. Download current log:
   ```bash
   curl http://<ESP32_IP>/api/debug/logs/current > current_log.txt
   ```

3. Analyze log file:
   - Check file size (should be reasonable, not megabytes)
   - Count `[VERBOSE] [SENSOR]` entries
   - Verify `[CHANGED: ...]` annotations present
   - Verify `No change (...)` summaries present

### Expected Log File Characteristics

| Metric | Expected Value |
|--------|---------------|
| File size (30 min, 1 sensor) | 50-200 KB |
| `[VERBOSE] [SENSOR]` entries/min | 12-60 (not 720+) |
| `[CHANGED: ...]` annotations | Present on all transitions |
| `No change (...)` summaries | Every 5 seconds when static |

### Sample Analysis Commands

```bash
# Count total sensor log lines
grep "\[SENSOR\]" current_log.txt | wc -l

# Count change events
grep "\[CHANGED:" current_log.txt | wc -l

# Count summaries
grep "No change" current_log.txt | wc -l

# Show all changes
grep "\[CHANGED:" current_log.txt
```

### Pass Criteria

- ✅ Log file size is REASONABLE (<1MB for 30 min)
- ✅ Change events clearly marked
- ✅ Summaries present when static
- ✅ Log is readable and well-formatted

---

## Test 7: Performance Impact

**Purpose**: Verify change detection doesn't impact performance.

### Steps

1. Monitor free heap before/during VERBOSE logging
   ```bash
   # Check heap via serial monitor
   # Or via web API if available
   ```

2. Measure loop iteration time
   - Use serial debug to log loop times
   - Compare VERBOSE on vs off

3. Check sensor reading frequency
   - Verify sensors still read at 80ms rate
   - Confirm no delays introduced

### Expected Results

| Metric | Before | After | Impact |
|--------|--------|-------|--------|
| Free heap | ~200KB | ~200KB | Negligible |
| Loop time | ~10-20ms | ~10-20ms | Negligible |
| Sensor rate | 80ms | 80ms | None |

### Pass Criteria

- ✅ Free heap unchanged
- ✅ Loop time unchanged
- ✅ Sensor reading rate unchanged
- ✅ No performance degradation

---

## Regression Tests

**Purpose**: Ensure old functionality still works.

### Test R1: DEBUG Level Logging

1. Set log level to DEBUG
2. Verify motion detection events still logged
3. Expected:
   ```
   [DEBUG] [SENSOR] Slot 0 (Front): MOTION DETECTED (dist=1200 mm)
   [DEBUG] [SENSOR] Slot 0 (Front): Motion cleared
   ```

### Test R2: State Machine Logging

1. Verify state transitions still logged
2. Expected:
   ```
   [INFO] [STATE] State: IDLE -> ACTIVE (reason: motion detected)
   [INFO] [STATE] State: ACTIVE -> IDLE (reason: timeout)
   ```

### Test R3: Config Logging

1. Change config via API
2. Verify config changes logged
3. Expected:
   ```
   [DEBUG] [CONFIG] Configuration updated
   ```

### Pass Criteria

- ✅ All non-sensor logging still works
- ✅ Log levels respected for all categories
- ✅ No regression in other logging functionality

---

## Troubleshooting

### Issue: Logs not appearing

**Check:**
1. Log level is VERBOSE: `GET /api/debug/logs/info`
2. Sensor category enabled (categoryMask includes CAT_SENSOR = 0x04)
3. Sensor is initialized and reading

### Issue: Too many logs still

**Check:**
1. Using `logSensorReadingIfChanged()` not `logSensorReading()`
2. Threshold values (may need adjustment)
3. Multiple sensors (each logs independently)

### Issue: Changes not detected

**Check:**
1. Distance change >50mm (threshold)
2. Motion state actually changing
3. Sensor reading correctly (check raw values)

---

## Summary Checklist

### Required Tests

- [ ] Test 1: Log Level Change (CRITICAL - bug fix)
- [ ] Test 2: Change Detection (CRITICAL - main feature)
- [ ] Test 3: Filesystem Protection (CRITICAL - main issue)
- [ ] Test 4: Multiple Change Scenarios
- [ ] Test 5: Multi-Sensor (if applicable)
- [ ] Test 6: Log Download
- [ ] Test 7: Performance Impact
- [ ] Regression Tests (R1-R3)

### Success Criteria

All tests must pass:
- ✅ Log level changes work immediately
- ✅ Only significant changes logged
- ✅ Filesystem usage sustainable (hours/days, not minutes)
- ✅ Change annotations clear and accurate
- ✅ No performance impact
- ✅ No regressions in other logging

### Acceptance

System is **READY FOR DEPLOYMENT** when:
- All tests pass
- Filesystem can run for 24+ hours at VERBOSE without hitting 30%
- Log level changes are instant and effective
- Logs are informative and actionable

---

## Test Report Template

```
# Verbose Logging Fix Test Report

Date: __________
Firmware Version: __________
Hardware: __________

## Test 1: Log Level Change
- [ ] PASS / [ ] FAIL
Notes: _________________________________________________

## Test 2: Change Detection
- [ ] PASS / [ ] FAIL
Notes: _________________________________________________

## Test 3: Filesystem Protection
- [ ] PASS / [ ] FAIL
30-min usage: _____% (should be <5%)
Notes: _________________________________________________

## Test 4: Change Scenarios
- [ ] PASS / [ ] FAIL
Notes: _________________________________________________

## Test 5: Multi-Sensor
- [ ] PASS / [ ] FAIL / [ ] N/A
Notes: _________________________________________________

## Test 6: Log Download
- [ ] PASS / [ ] FAIL
Log size (30 min): _____ KB (should be <200KB)
Notes: _________________________________________________

## Test 7: Performance
- [ ] PASS / [ ] FAIL
Notes: _________________________________________________

## Regression Tests
- [ ] PASS / [ ] FAIL
Notes: _________________________________________________

## Overall Result
- [ ] ALL TESTS PASSED - READY FOR DEPLOYMENT
- [ ] TESTS FAILED - ISSUES FOUND

Issues/Observations:
_________________________________________________________
_________________________________________________________

Tester Signature: __________________
```

---

**Last Updated**: 2026-01-27
**StepAware Project**: Debug Logging System Testing
