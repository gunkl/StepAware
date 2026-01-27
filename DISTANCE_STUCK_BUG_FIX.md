# Critical Fix: Distance Sensor Stuck on Nearest Reading

**Date**: 2026-01-26
**Status**: FIXED
**Severity**: CRITICAL

## Problem Summary

The ultrasonic distance sensor would "stick" on the nearest distance reading and fail to update when the object moved away or disappeared:

### Observed Symptoms
1. Sensor positioned to see >1000mm distance consistently
2. User walks close to sensor
3. Sensor correctly detects approach and shows nearest distance (e.g., 300mm)
4. User walks away
5. **BUG**: Sensor remains stuck at 300mm distance
6. Serial logs show "echo not received" timeouts
7. Distance value never updates - stays frozen at last valid reading
8. Direction shows "APPROACHING" indefinitely
9. Motion detection remains active even though object is gone

## Root Cause

**File**: `src/distance_sensor_base.cpp:28-53`

The `updateDistanceSensor()` function only processed valid (non-zero) distance readings:

```cpp
void DistanceSensorBase::updateDistanceSensor()
{
    uint32_t rawDistance = getDistanceReading();

    // BUGGY CODE - Only process valid readings
    if (rawDistance > 0) {
        addSampleToWindow(rawDistance);
        // ... update averages and direction
    }
    // If rawDistance == 0 (timeout), DO NOTHING!
}
```

### Why This Caused Stuck Readings

The sensor uses a 10-sample rolling window for averaging:
- When object is close: Window fills with values like [300, 310, 295, 305, ...]
- Average: ~302mm
- When object disappears: `getDistanceReading()` returns 0 (timeout - no echo)
- **BUG**: Code skips the entire update when rawDistance == 0
- Old samples stay in the window forever: [300, 310, 295, 305, ...]
- Average stays at 302mm indefinitely
- Direction calculation sees no change → remains "APPROACHING"
- Motion detection continues to trigger

### What Should Happen

When the sensor times out (no echo received):
1. It means no object is present within detection range
2. The rolling window should be updated with max-range values
3. After ~5-10 timeout readings, the average should increase
4. Object should be marked as "no longer detected"
5. Direction should change from APPROACHING → RECEDING → STATIONARY
6. Motion detection should clear

## The Fix

**File**: `src/distance_sensor_base.cpp`

Changed the update logic to handle timeouts properly:

```cpp
void DistanceSensorBase::updateDistanceSensor()
{
    // Get raw distance from subclass implementation
    uint32_t rawDistance = getDistanceReading();

    // Store previous average for direction detection (before updating)
    m_lastWindowAverage = m_windowAverage;

    if (rawDistance > 0) {
        // Valid reading - add to rolling window
        addSampleToWindow(rawDistance);
    } else {
        // Timeout/no echo - treat as "no object detected"
        // This prevents distance from getting stuck at old values
        // We add a max-range reading to indicate object is gone
        addSampleToWindow(m_maxDistance);
    }

    // Calculate new average from window
    m_windowAverage = calculateWindowAverage();
    m_currentDistance = m_windowAverage;

    // Update direction if enabled
    if (m_directionEnabled && m_windowFilled) {
        updateDirection();
    }

    // Check for threshold crossing events
    checkThresholdEvents();
}
```

### Key Changes

1. **Always update the window** - Don't skip updates on timeout
2. **Timeout = max distance** - When rawDistance is 0, insert `m_maxDistance` into window
3. **Move average storage** - Store previous average BEFORE conditional, not inside it
4. **Always recalculate** - Distance, direction, and threshold checks happen every update

### Why This Works

**Example scenario** (threshold = 500mm, max distance = 2000mm):

| Update | Raw Reading | Window Contents | Average | Detection |
|--------|-------------|-----------------|---------|-----------|
| 1-10 | 1200mm | [1200, 1200, ...] | 1200mm | No (above threshold) |
| 11 | 300mm | [1200, 1200, ..., 300] | 1110mm | No |
| 12 | 300mm | [1200, 1200, ..., 300, 300] | 1020mm | No |
| 13-16 | 300mm | [..., 300, 300, 300, 300] | 480mm | **YES** (below threshold, approaching) |
| 17 | 0 (timeout) | [300, 300, ..., **2000**] | 470mm | YES (still has old samples) |
| 18 | 0 (timeout) | [300, 300, ..., **2000**, **2000**] | 640mm | YES |
| 19 | 0 (timeout) | [300, 300, ..., **2000**, **2000**, **2000**] | 810mm | NO (above threshold) |
| 20-25 | 0 (timeout) | [**2000**, **2000**, ..., **2000**] | 2000mm | NO (object cleared) |

**Result**: After ~5-10 timeout readings, the old close-range samples are pushed out of the window and replaced with max-range values. The average increases, crosses back above the threshold, and motion detection clears.

## Testing

### Test Case 1: Object Approaches and Leaves
1. Place sensor with clear path to distant wall (>1m)
2. Enable diagnostic mode: `v`
3. **Expected**: Distance shows >1000mm, Motion: NO
4. Walk close to sensor (<500mm)
5. **Expected**: Distance decreases, Motion: YES, Direction: APPROACHING
6. Walk away from sensor
7. **Expected**: Distance increases over ~2-3 seconds, Motion: NO, Direction: RECEDING
8. Stand still far away
9. **Expected**: Distance stabilizes at far value, Direction: STATIONARY

### Test Case 2: Object Suddenly Removed
1. Hold hand close to sensor (300mm)
2. **Expected**: Motion: YES, Distance: ~300mm
3. Quickly remove hand completely
4. **Expected**:
   - Sensor shows "echo not received" warnings
   - Distance increases: 300 → 500 → 800 → 1200 → 2000mm over ~2-3 seconds
   - Motion clears after distance exceeds threshold
   - Direction changes: APPROACHING → RECEDING → STATIONARY

### Test Case 3: Intermittent Detection
1. Wave hand in and out of sensor range quickly
2. **Expected**:
   - Distance fluctuates based on hand position
   - When hand is out of range, timeouts cause distance to increase
   - Motion detection triggers on approach, clears on retreat
   - No "stuck" values

## Verification via Serial Diagnostic

Enable diagnostic mode and watch the output:

```
[S0] Dist: 300 mm [NEAR] (thresh:500) Motion:YES Dir:APPR >>> TRIGGER
[S0] Dist: 310 mm [NEAR] (thresh:500) Motion:YES Dir:APPR >>> TRIGGER
[S0] Dist: 305 mm [NEAR] (thresh:500) Motion:YES Dir:APPR >>> TRIGGER
(object removed)
[HAL_Ultrasonic] Timeout - no echo received
[S0] Dist: 450 mm [NEAR] (thresh:500) Motion:YES Dir:RECD >>> TRIGGER
[HAL_Ultrasonic] Timeout - no echo received
[S0] Dist: 650 mm [FAR ] (thresh:500) Motion:NO  Dir:RECD     (idle)
[HAL_Ultrasonic] Timeout - no echo received
[S0] Dist: 950 mm [FAR ] (thresh:500) Motion:NO  Dir:RECD     (idle)
[HAL_Ultrasonic] Timeout - no echo received
[S0] Dist:1400 mm [FAR ] (thresh:500) Motion:NO  Dir:STAT     (idle)
```

Notice how distance increases even though "echo not received" warnings appear.

## Impact on System Behavior

### Before Fix
- Motion warnings would stay active indefinitely
- LED matrix arrow animation would never stop
- Warning duration timeout would eventually stop display, but sensor still reported motion
- Rapid re-triggering when warning expired
- Direction stuck at APPROACHING

### After Fix
- Motion detection clears naturally when object leaves
- Warning displays for configured duration, then stops
- No re-triggering unless new motion actually occurs
- Direction updates correctly: APPROACHING → RECEDING → STATIONARY
- System returns to idle state

## Related Configuration

**Sample window size**: 10 readings
- Located in `include/distance_sensor_base.h:259`
- Determines how many timeout readings needed to clear old values
- 10 samples at ~60ms intervals = ~600ms to fully refresh

**Max detection distance**: 2000mm (HC-SR04), 4000mm (Grove)
- Defined in sensor implementations
- Timeout readings insert this value into window

**Detection threshold**: 500mm (configurable)
- Object below threshold = motion detected
- Object above threshold = no motion

## Files Modified

1. `src/distance_sensor_base.cpp` - Fixed `updateDistanceSensor()` method

## Commit Message Suggestion

```
Fix critical bug: distance sensor stuck on nearest reading

The distance sensor would "stick" at the nearest detected distance
and fail to update when the object moved away or disappeared.

Root cause: updateDistanceSensor() only processed valid (non-zero)
readings. When sensor timed out (no echo), it skipped the entire
update cycle, leaving old samples in the rolling window forever.

Fix: Always update the rolling window, even on timeout. When
rawDistance is 0 (no echo), insert max-range value into window.
After 5-10 timeout readings, old close-range samples are flushed
out and average distance increases naturally.

This ensures:
- Motion detection clears when object leaves
- Direction updates correctly (APPROACHING → RECEDING → STATIONARY)
- No stuck distance values
- System returns to idle state properly

Severity: CRITICAL
Impact: Motion detection, warning triggers, direction detection
```

---

**Status**: Fixed - Ready for testing
**Priority**: HIGH - Test immediately before other changes
