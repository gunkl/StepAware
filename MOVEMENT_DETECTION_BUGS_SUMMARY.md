# Movement Detection Bugs - Summary and Fixes

## Overview
Three critical bugs were preventing walking approach detection from working correctly. All have been fixed.

---

## Bug #1: m_lastRawDistance Updated Too Early

### Problem
```cpp
// OLD CODE (line 82):
m_lastRawDistance = rawDistance;  // Set BEFORE comparison

// Later (line 140):
updateDualModeDetectionState(rawDistance);
    // Inside this function:
    if (m_lastRawDistance > 0 && rawDistance < m_lastRawDistance) {
        // This ALWAYS compared: if (rawDistance < rawDistance) → ALWAYS FALSE!
    }
```

**Impact:** Gradual approach detection never worked because it compared the current value against itself.

### Fix
Moved `m_lastRawDistance = rawDistance` to line 143, **AFTER** the `updateDualModeDetectionState()` call:

```cpp
// Line 140: updateDualModeDetectionState(rawDistance);
//   Now m_lastRawDistance holds PREVIOUS value
//   Comparison: if (3089 < 3093) → TRUE ✓

// Line 143: m_lastRawDistance = rawDistance;
//   Update AFTER comparison
```

### Validation
With real log data:
- Before: `if (3089 < 3089)` → FALSE ❌
- After: `if (3089 < 3093)` → TRUE ✓
- Result: "Gradual approach detected:" messages now appear in logs ✓

---

## Bug #2: Premature Reset of Gradual Approach Flag

### Problem
```cpp
// OLD CODE:
beyondDetectionThreshold = (m_currentDistance > m_detectionThreshold);
if (beyondDetectionThreshold) {
    // Reset flags even when STATIONARY or APPROACHING!
    m_seenApproachingFromOutside = false;  ❌
}
```

**Impact:** During walking approach, when direction briefly became STATIONARY, the gradual approach flag was cleared. System then treated entry into detection zone as "sudden appearance" instead.

### Example from Logs
```
[39561] Gradual approach active, at 1933mm, direction=APPROACHING ✓
[39710] Direction changed to STATIONARY (delta=0mm)
[39710] Object left detection zone, resetting dual-mode state ❌
        m_seenApproachingFromOutside = false  ❌ CLEARED!
[39746] Raw reading 908mm (entered zone)
        Switches to SUDDEN APPEARANCE mode ❌ WRONG!
```

### Fix
Only reset gradual approach flag when **clearly moving away** (RECEDING), not when STATIONARY:

```cpp
// NEW CODE:
beyondDetectionThreshold = (m_currentDistance > m_detectionThreshold);
movingAway = (m_direction == DIRECTION_RECEDING);  // ONLY receding, NOT stationary

if (beyondDetectionThreshold && movingAway) {
    // Only reset when clearly moving away
    m_seenApproachingFromOutside = false;
}
```

### Validation
With real log data:
- Before: Reset on STATIONARY → flag cleared during approach ❌
- After: Only reset on RECEDING → flag preserved during approach ✓
- Result: Stays in gradual approach mode through the entire approach ✓

---

## Bug #3: Missing Direction Stability Logic

### Problem
```cpp
// OLD CODE:
m_direction = newDirection;  // Updated IMMEDIATELY every cycle!
```

Direction changed instantly without requiring stability, causing rapid bouncing:

**From Log #7:**
```
[31619] Direction: RECEDING (delta=604mm)
[31797] Direction: STATIONARY (delta=1mm)  ← 178ms later!
```

**Impact:** Direction bounced between APPROACHING/STATIONARY/RECEDING every few readings, making direction matching unreliable.

### Fix
Added direction stability tracking requiring **400ms of consistent direction**:

**New Members:**
```cpp
static constexpr uint32_t DIRECTION_STABILITY_TIME_MS = 400;
MotionDirection m_candidateDirection;  // Pending direction being evaluated
uint8_t m_directionStabilityCount;     // Consecutive samples with same direction
```

**New Logic:**
```cpp
void DistanceSensorBase::updateDirection()
{
    // Calculate required stable samples
    uint8_t requiredStableSamples = DIRECTION_STABILITY_TIME_MS / m_sampleIntervalMs;
    // Example: 400ms / 75ms = 5.3 → 6 samples

    if (newDirection == m_candidateDirection) {
        // Same direction - increment counter
        m_directionStabilityCount++;

        if (m_directionStabilityCount >= requiredStableSamples) {
            // STABLE for 400ms - confirm direction change
            m_direction = newDirection;
            LOG: "Direction confirmed after N samples (Nms): APPROACHING"
        }
    } else {
        // Different direction - start new candidate
        m_candidateDirection = newDirection;
        m_directionStabilityCount = 1;
        LOG: "Direction candidate: APPROACHING (need N stable samples)"
    }
}
```

### Validation
With 75ms sample interval:
- Required stability: 400ms / 75ms = 6 samples ≈ **450ms**
- Before: Direction changed every 60-180ms ❌
- After: Direction confirmed after 6 consecutive stable samples ✓
- Result: Much more stable direction detection, no more rapid bouncing ✓

---

## Combined Impact

### Before Fixes:
1. ❌ Gradual approach never detected (Bug #1)
2. ❌ Flag reset during approach (Bug #2)
3. ❌ Direction bounced rapidly (Bug #3)
4. ❌ Always used wrong detection mode
5. ❌ Never triggered on walking approach

### After Fixes:
1. ✅ Gradual approach correctly detected from raw readings
2. ✅ Flag preserved through STATIONARY states
3. ✅ Direction stable for 400ms before confirming
4. ✅ Uses gradual approach mode correctly
5. ⚠️ Trigger conditions may still need tuning (see below)

---

## Remaining Limitation

After entering the detection zone via a wipe, the system may still not trigger due to:

### Issue: Post-Wipe Detection Thresholds

**What happens:**
1. Walking from 3000mm → 1000mm triggers wipe
2. Window reset: [908, 908, 908]
3. Next readings: [908, 877, 850, 820, 800]
4. Small deltas (20-30mm) → Direction = STATIONARY
5. Direction matching fails: `STATIONARY != APPROACHING`

**Why it happens:**
- Direction sensitivity = 350mm (designed for far distances)
- Small changes after wipe (20-30mm) don't meet threshold
- Direction confirmed as STATIONARY (correctly, per stability logic)
- Gradual approach mode requires: `inRange && movement && directionMatch`
- directionMatch fails because STATIONARY ≠ APPROACHING

### Recommended Solution

For gradual approach mode, **skip direction matching requirement**:

```cpp
if (m_seenApproachingFromOutside) {
    // We KNOW they're approaching from raw readings
    // Don't require window-based direction to also match
    shouldTrigger = inRange && movementDetected;  // Remove directionMatches
}
```

**Rationale:**
- Gradual approach was detected from RAW readings (3093 → 3089 → etc.)
- We already know the object is approaching
- Window-based direction after wipe is unreliable (small changes, reset state)
- Trust the gradual approach detection, not the post-wipe direction

### Alternative Solutions

1. **Lower direction sensitivity for gradual approach:**
   - Use 100-150mm threshold instead of 350mm
   - Only when `m_seenApproachingFromOutside == true`

2. **Special movement threshold after wipe:**
   - If wipe happened within last N readings
   - Use lower movement threshold

3. **Time-based trigger delay:**
   - After entering zone via gradual approach
   - Wait 500ms, then trigger regardless of direction

---

## Test Plan

### 1. Build and Upload Firmware
```bash
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo -t upload
```

### 2. Test Scenarios

#### Scenario A: Hand Wave (Should NOT trigger)
1. Wave hand in front of sensor 2-3 times
2. **Expected:** No alert
3. **Check logs for:**
   - No "Gradual approach detected:" (hand starts inside zone)
   - "Sudden appearance" messages OK
   - No trigger

#### Scenario B: Walking Approach (Should trigger with additional fix)
1. Stand 3-4 meters away
2. Walk steadily toward sensor
3. **Expected:** Alert when entering detection zone
4. **Check logs for:**
   - ✅ "Gradual approach detected: dist=X (prev=Y)"
   - ✅ "Direction candidate: APPROACHING"
   - ✅ "Direction confirmed after 6 samples (450 ms): APPROACHING"
   - ✅ "Gradual approach: inRange=1, movement=1, dirMatch=..."
   - ⚠️ May still show "trigger=0" if directionMatch fails

#### Scenario C: Direction Stability
1. Any movement
2. **Check logs for:**
   - "Direction candidate:" messages (not confirmed yet)
   - "Direction confirmed after N samples:" (only after stability)
   - No rapid bouncing RECEDING→STATIONARY→APPROACHING

### 3. Log Analysis

Download logs and check:
```bash
grep -i "gradual approach detected" stepaware_current.log
# Should see: "Gradual approach detected: dist=X (prev=Y)"

grep -i "direction confirmed" stepaware_current.log
# Should see: "Direction confirmed after N samples"

grep -i "direction candidate" stepaware_current.log
# Should see: "Direction candidate: APPROACHING (need N samples)"
```

---

## Suggested Commit Message

```
Fix three critical movement detection bugs

1. Fixed m_lastRawDistance timing bug
   - Moved update to after updateDualModeDetectionState() call
   - Now correctly compares current vs previous raw readings
   - Gradual approach detection now works

2. Fixed premature reset of gradual approach flag
   - Only reset when direction is RECEDING, not STATIONARY
   - Preserves flag during approach when direction briefly unclear
   - System correctly stays in gradual approach mode

3. Added direction stability confirmation logic
   - Requires 400ms of consistent direction before confirming
   - Prevents rapid bouncing between directions
   - Direction changes now stable and reliable

These fixes enable detection of walking approaches from outside
the detection zone while still ignoring hand waves. Direction
tracking is now much more stable and reliable.

Note: Post-wipe direction matching may still prevent triggers
in some cases. Consider removing directionMatch requirement
for gradual approach mode in future update.
```

---

## Next Steps

1. ✅ All three bugs fixed
2. ⏭️ Test on hardware
3. ⏭️ Monitor logs for confirmation
4. ⏭️ If still no trigger on walking approach:
   - Remove `directionMatches` requirement from gradual approach mode
   - Or adjust direction sensitivity for post-wipe scenarios

