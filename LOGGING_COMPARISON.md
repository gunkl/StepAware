# Verbose Logging: Before vs After Comparison

## Visual Comparison

### BEFORE (Old Behavior)

**Filesystem fills rapidly with repetitive logs:**

```
[0000004567] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000004647] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000004727] [VERBOSE] [SENSOR] Slot 0: dist=3518 mm, motion=NO, dir=STATIONARY
[0000004807] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000004887] [VERBOSE] [SENSOR] Slot 0: dist=3517 mm, motion=NO, dir=STATIONARY
[0000004967] [VERBOSE] [SENSOR] Slot 0: dist=3519 mm, motion=NO, dir=STATIONARY
[0000005047] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000005127] [VERBOSE] [SENSOR] Slot 0: dist=3522 mm, motion=NO, dir=STATIONARY
[0000005207] [VERBOSE] [SENSOR] Slot 0: dist=3518 mm, motion=NO, dir=STATIONARY
[0000005287] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000005367] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000005447] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
... (continues indefinitely at ~12 logs/second)
... (filesystem fills in minutes)
```

**Problems:**
- 12+ logs per second per sensor
- 720+ log entries per minute
- Minor noise fluctuations (±5mm) logged as separate events
- Filesystem fills rapidly (30% in 10-20 minutes)
- Impossible to find important events in the noise
- Changing log level to INFO had NO EFFECT (kept logging)

---

### AFTER (New Behavior)

**Intelligent change detection with periodic summaries:**

```
[0000004567] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY [INITIAL]
[0000009567] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
[0000014567] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
[0000015234] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3520 mm, motion DETECTED, dir STATIONARY->APPROACHING]
[0000016100] [VERBOSE] [SENSOR] Slot 0: dist=2050 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 2100 mm]
[0000021100] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=2050 mm, motion=YES
[0000024500] [VERBOSE] [SENSOR] Slot 0: dist=2045 mm, motion=NO, dir=STATIONARY [CHANGED: motion CLEARED, dir APPROACHING->STATIONARY]
[0000029500] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=2045 mm, motion=NO
```

**Benefits:**
- ~0.2-1 logs per second (only on changes)
- ~12-60 log entries per minute
- Noise filtered (only >50mm changes logged)
- Filesystem stays healthy (30% in hours/days)
- Important events clearly marked with `[CHANGED: ...]`
- Log level changes work correctly

---

## Log Level Change Bug

### BEFORE: Changing Log Level Had NO EFFECT

```
User: POST /api/debug/config {"level":"INFO"}
API Response: {"success":true,"message":"Debug config updated"}

[Still logs VERBOSE sensor data!]
[0000005447] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000005527] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000005607] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
...continues logging despite level change...
```

**Problem**: `logSensorReading()` hardcoded LEVEL_VERBOSE, ignoring `m_level`

---

### AFTER: Log Level Changes Work Immediately

```
User: POST /api/debug/config {"level":"INFO"}
API Response: {"success":true,"message":"Debug config updated"}

[Sensor verbose logs STOP immediately]
[Only INFO+ logs shown]
[0000005500] [INFO] [STATE] State: IDLE -> ACTIVE (reason: motion detected)
[0000008500] [INFO] [STATE] State: ACTIVE -> IDLE (reason: timeout)

User: POST /api/debug/config {"level":"VERBOSE"}
API Response: {"success":true,"message":"Debug config updated"}

[Sensor verbose logs RESUME immediately]
[0000009000] [VERBOSE] [SENSOR] Slot 0: No change (15 readings over 4500 ms) - dist=3520 mm, motion=NO
[0000009234] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3520 mm, motion DETECTED, dir STATIONARY->APPROACHING]
```

**Fixed**: Both `logSensorReading()` and `logSensorReadingIfChanged()` check `m_level` before logging

---

## Detailed Event Logging

### Motion Detection Event

**Before (hidden in noise):**
```
[0000014900] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000014980] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000015060] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING
[0000015140] [VERBOSE] [SENSOR] Slot 0: dist=2095 mm, motion=YES, dir=APPROACHING
[0000015220] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING
[0000015300] [VERBOSE] [SENSOR] Slot 0: dist=2102 mm, motion=YES, dir=APPROACHING
```
*Hard to spot the transition!*

---

**After (clearly marked):**
```
[0000014567] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
[0000015060] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3520 mm, motion DETECTED, dir STATIONARY->APPROACHING]
[0000020060] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=2100 mm, motion=YES
```
*Instantly clear what happened!*

---

## Filesystem Usage Over Time

### Scenario: 1 sensor, 80ms update rate, VERBOSE logging

| Time      | Before (Old)      | After (New)       | Difference |
|-----------|-------------------|-------------------|------------|
| 1 minute  | ~720 log entries  | ~12-60 entries    | 12-60x fewer |
| 10 minutes| ~7,200 entries    | ~120-600 entries  | 12-60x fewer |
| 1 hour    | ~43,200 entries   | ~720-3,600 entries| 12-60x fewer |
| **Filesystem** | **30% in 15-20 min** | **30% in 4-20 hours** | **12-60x longer** |

### Multiple Sensors

| Sensors | Before (Old)      | After (New)       |
|---------|-------------------|-------------------|
| 1       | 720 logs/min      | 12-60 logs/min    |
| 2       | 1,440 logs/min    | 24-120 logs/min   |
| 4       | 2,880 logs/min    | 48-240 logs/min   |
| **4 sensors** | **Fills in 7-10 min** | **Fills in 2-10 hours** |

---

## Change Detection Examples

### Example 1: Object Approaching

```
[0000004567] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY [INITIAL]
[0000015234] [VERBOSE] [SENSOR] Slot 0: dist=2100 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 3520 mm, motion DETECTED, dir STATIONARY->APPROACHING]
[0000016100] [VERBOSE] [SENSOR] Slot 0: dist=1500 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 2100 mm]
[0000017000] [VERBOSE] [SENSOR] Slot 0: dist=800 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 1500 mm]
[0000018000] [VERBOSE] [SENSOR] Slot 0: dist=600 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 800 mm]
[0000019000] [VERBOSE] [SENSOR] Slot 0: dist=550 mm, motion=YES, dir=APPROACHING [CHANGED: dist changed from 600 mm]
[0000024000] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=550 mm, motion=YES
```

**Clear narrative**: Object approached from 3520mm to 550mm, then stopped.

---

### Example 2: Object Receding

```
[0000024000] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=550 mm, motion=YES
[0000025000] [VERBOSE] [SENSOR] Slot 0: dist=700 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 550 mm, dir APPROACHING->RECEDING]
[0000026000] [VERBOSE] [SENSOR] Slot 0: dist=1200 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 700 mm]
[0000027000] [VERBOSE] [SENSOR] Slot 0: dist=2500 mm, motion=YES, dir=RECEDING [CHANGED: dist changed from 1200 mm]
[0000028000] [VERBOSE] [SENSOR] Slot 0: dist=3400 mm, motion=NO, dir=STATIONARY [CHANGED: dist changed from 2500 mm, motion CLEARED, dir RECEDING->STATIONARY]
[0000033000] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3400 mm, motion=NO
```

**Clear narrative**: Object receded from 550mm to 3400mm, then motion cleared.

---

### Example 3: Noise Filtering

**Before (noise creates false events):**
```
[0000005000] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY
[0000005080] [VERBOSE] [SENSOR] Slot 0: dist=3515 mm, motion=NO, dir=STATIONARY
[0000005160] [VERBOSE] [SENSOR] Slot 0: dist=3518 mm, motion=NO, dir=STATIONARY
[0000005240] [VERBOSE] [SENSOR] Slot 0: dist=3522 mm, motion=NO, dir=STATIONARY
[0000005320] [VERBOSE] [SENSOR] Slot 0: dist=3517 mm, motion=NO, dir=STATIONARY
```
*Every minor fluctuation logged*

---

**After (noise ignored):**
```
[0000004567] [VERBOSE] [SENSOR] Slot 0: dist=3520 mm, motion=NO, dir=STATIONARY [INITIAL]
[0000009567] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
[0000014567] [VERBOSE] [SENSOR] Slot 0: No change (20 readings over 5000 ms) - dist=3520 mm, motion=NO
```
*Noise filtered, only summaries shown*

**Note**: Fluctuations ±5-49mm are ignored (below 50mm threshold)

---

## Configuration Thresholds

### Adjustable Parameters

In `include/debug_logger.h`:

```cpp
// Distance change threshold (millimeters)
static constexpr uint32_t DISTANCE_CHANGE_THRESHOLD_MM = 50;

// Summary interval (number of unchanged readings)
static constexpr uint32_t UNCHANGED_SUMMARY_INTERVAL = 20;

// Summary interval (time in milliseconds)
static constexpr uint32_t UNCHANGED_TIME_SUMMARY_MS = 5000;
```

### Effect of Threshold Changes

| Threshold | Value | Effect |
|-----------|-------|--------|
| `DISTANCE_CHANGE_THRESHOLD_MM` | 20mm | More sensitive, logs small movements |
| `DISTANCE_CHANGE_THRESHOLD_MM` | 100mm | Less sensitive, only large movements |
| `UNCHANGED_SUMMARY_INTERVAL` | 10 | More frequent summaries |
| `UNCHANGED_SUMMARY_INTERVAL` | 50 | Less frequent summaries |
| `UNCHANGED_TIME_SUMMARY_MS` | 2000 | Summary every 2 seconds |
| `UNCHANGED_TIME_SUMMARY_MS` | 10000 | Summary every 10 seconds |

**Recommendation**: Keep defaults (50mm, 20 readings, 5000ms) for balanced logging.

---

## Summary Statistics

### Log Volume Reduction

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Logs/second (static) | 12-15 | 0.2 | **60-75x fewer** |
| Logs/minute (static) | 720-900 | 12 | **60-75x fewer** |
| Logs/second (moving) | 12-15 | 0.5-2 | **6-30x fewer** |
| Logs/minute (moving) | 720-900 | 30-120 | **6-30x fewer** |
| Time to 30% filesystem | 15-20 min | 4-20 hours | **12-60x longer** |

### Bug Fixes

| Issue | Status | Fix |
|-------|--------|-----|
| Log level change ignored | FIXED | Check `m_level` before logging |
| Filesystem fills rapidly | FIXED | Smart change detection |
| Noise in logs | FIXED | 50mm threshold filters minor fluctuations |
| Hard to find events | FIXED | `[CHANGED: ...]` annotations |

---

## Recommendation

**Use the new smart logging** by default:

```cpp
// In sensor_manager.cpp (already updated)
g_debugLogger.logSensorReadingIfChanged(i, dist, motion, dir);
```

**Only use the old method** for manual debugging:

```cpp
// For temporary ultra-verbose debugging
g_debugLogger.logSensorReading(i, dist, motion, dir);  // Every reading
```

---

**Conclusion**: The new logging system is **safe**, **efficient**, and **informative** for long-term debugging.

---

**Last Updated**: 2026-01-27
**StepAware Project**: Debug Logging System Enhancement
