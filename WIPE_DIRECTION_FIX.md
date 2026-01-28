# Wipe Direction Candidate Fix

## Problem

When a wipe occurs (raw reading enters detection zone while window average is outside), the system was resetting the direction candidate to `STATIONARY` or `UNKNOWN`. This caused the direction confirmation process to start from the wrong state.

### Example from Logs

```
>>> WIPING: raw=256 is IN range, window=1407 is OUT of range
Direction candidate: STATIONARY (need 6 stable samples = 450 ms)
```

**Problem**: At the moment of wipe, raw=256mm and window=1407mm clearly indicates **APPROACHING**, but the system reset to STATIONARY.

## Root Cause

In `resetWindowWithDistance()`, the direction candidate was blindly reset:
```cpp
m_candidateDirection = DIRECTION_UNKNOWN;
m_directionStabilityCount = 0;
```

This ignored the fact that we have valuable information from comparing the new raw reading with the previous window average.

## Solution

### Changes Made

1. **Updated function signature** to accept previous average:
   ```cpp
   // include/distance_sensor_base.h
   void resetWindowWithDistance(uint32_t distance_mm, uint16_t previousAverage = 0);
   ```

2. **Added intelligent direction initialization** in `resetWindowWithDistance()`:
   ```cpp
   if (previousAverage > 0) {
       int32_t delta = (int32_t)distance_mm - (int32_t)previousAverage;

       if (abs(delta) >= (int32_t)m_directionSensitivity) {
           if (delta < 0) {
               // New distance < previous → APPROACHING
               m_candidateDirection = DIRECTION_APPROACHING;
               m_directionStabilityCount = 1;
           } else {
               // New distance > previous → RECEDING
               m_candidateDirection = DIRECTION_RECEDING;
               m_directionStabilityCount = 1;
           }
       } else {
           // Within sensitivity threshold → STATIONARY
           m_candidateDirection = DIRECTION_STATIONARY;
           m_directionStabilityCount = 1;
       }
   }
   ```

3. **Capture previous average before wiping**:
   ```cpp
   // src/distance_sensor_base.cpp:132
   uint16_t previousAverage = m_windowAverage;
   resetWindowWithDistance(rawDistance, previousAverage);
   ```

## How It Works

When a wipe occurs:

1. **Before wipe**: System has window average (e.g., 1407mm)
2. **Raw reading enters zone**: e.g., 256mm
3. **Wipe triggered**: Captures previousAverage = 1407mm
4. **Compare**: 256 < 1407, delta = -1151mm
5. **Check delta**: abs(-1151) >= 350 (direction sensitivity)
6. **Set candidate**: delta < 0 → `DIRECTION_APPROACHING`
7. **Initialize count**: `m_directionStabilityCount = 1`

Now the direction confirmation process starts with the **correct candidate** rather than STATIONARY/UNKNOWN.

## Expected Behavior After Fix

### Log Output - Walking Approach with Wipe

```
>>> WIPING: raw=256 is IN range, window=1407 is OUT of range
  Direction candidate set to APPROACHING (new=256 < prev=1407)
Direction candidate: APPROACHING (need 6 stable samples = 450 ms)
```

After 5 more samples showing APPROACHING:
```
Direction confirmed after 6 samples (450 ms): APPROACHING
Gradual approach: inRange=1, movement=1, dirMatch=1 → trigger=1
```

## Direction Stability Confirmation (6 Samples)

**Question**: Are the "6 stable samples" based on raw values or averages?

**Answer**: **Window averages**, not raw values. Here's how it works:

1. **Each sample cycle**:
   - 3 raw readings are collected into the window
   - Window average is calculated from these 3 values
   - Direction is computed by comparing current avg vs last avg

2. **Stability tracking**:
   - Requires 6 consecutive **window averages** showing same direction
   - Each window average already filters noise (averaging 3 raw values)
   - Formula: `requiredSamples = 400ms / 75ms = 6 samples`

3. **Why this is correct**:
   - Using averaged values filters sensor noise
   - Prevents direction bouncing from single noisy readings
   - 6 window averages = 6 * 75ms = 450ms of stability
   - Each window average represents 3 raw readings

**Example**:
```
Sample 1: Raw=[308, 310, 312] → Avg=310 → Compare with last → APPROACHING
Sample 2: Raw=[290, 288, 292] → Avg=290 → Compare with last → APPROACHING (count=2)
Sample 3: Raw=[270, 268, 272] → Avg=270 → Compare with last → APPROACHING (count=3)
...
Sample 6: Raw=[210, 208, 212] → Avg=210 → Compare with last → APPROACHING (count=6)
→ Direction CONFIRMED: APPROACHING ✓
```

## Impact on Walking Approach Detection

This fix addresses one of the key issues preventing triggers after wipe:

### Before Fix:
1. Walking from 3000mm → sensor detects gradual approach ✓
2. Raw reading 256mm triggers wipe
3. Direction candidate reset to STATIONARY ❌
4. Next 6 samples needed to confirm APPROACHING
5. By then, object may have stopped or trigger window missed ❌

### After Fix:
1. Walking from 3000mm → sensor detects gradual approach ✓
2. Raw reading 256mm triggers wipe
3. Direction candidate set to APPROACHING ✓
4. Stability count starts at 1 (needs 5 more)
5. Faster confirmation, higher chance of trigger ✓

## Testing

When testing, look for these log patterns:

### Successful Wipe with Correct Direction:
```
>>> WIPING: raw=XXX is IN range, window=YYY is OUT of range
  Direction candidate set to APPROACHING (new=XXX < prev=YYY)
Direction candidate: APPROACHING (need 6 stable samples = 450 ms)
... (5 more samples with APPROACHING)
Direction confirmed after 6 samples (450 ms): APPROACHING
Gradual approach: inRange=1, movement=1, dirMatch=1 → trigger=1
```

### Edge Case - Small Delta:
If the delta is small (< 350mm sensitivity):
```
>>> WIPING: raw=900 is IN range, window=1100 is OUT of range
  Direction candidate set to STATIONARY (delta=-200 < sensitivity=350)
```
This is correct behavior - the change is too small to confidently declare a direction.

## Related Issues

This fix works in conjunction with:
- **Bug #1 Fix**: `m_lastRawDistance` timing (enables gradual approach detection)
- **Bug #2 Fix**: Premature flag reset (preserves gradual approach flag)
- **Bug #3 Fix**: Direction stability (prevents rapid bouncing)

Together, these fixes should enable reliable walking approach detection.

## Files Modified

1. `include/distance_sensor_base.h`
   - Line 301: Added `previousAverage` parameter to `resetWindowWithDistance()`

2. `src/distance_sensor_base.cpp`
   - Line 132: Capture previous average before wipe
   - Line 135: Pass previous average to `resetWindowWithDistance()`
   - Lines 219-273: Added intelligent direction candidate initialization logic

---

**Status**: Implemented and ready for testing
**Expected Impact**: Significant improvement in walking approach detection after wipe
**Next**: Build, upload, and test on hardware
