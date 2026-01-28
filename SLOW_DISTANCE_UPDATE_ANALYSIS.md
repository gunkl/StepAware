# Slow Distance Update Analysis

**Date**: 2026-01-27
**Issue**: Distance measurements take 3-4 seconds to update when hand placed in front of sensor
**Impact**: Defeats purpose of fast pedestrian detection, breaks direction/velocity detection

## Problem Root Cause

The delay is caused by **rolling window averaging** interacting with **movement detection thresholds**.

### Current Architecture

#### 1. Rolling Window Averaging
[distance_sensor_base.cpp:94-125](distance_sensor_base.cpp#L94-L125)

- **Purpose**: Smooth out sensor noise for stable readings
- **Implementation**: Circular buffer of last N samples, uses average
- **Window size**: 5 samples (hardcoded in HAL_Ultrasonic constructor)
- **Sample rate**: 60ms (HC-SR04 minimum)

#### 2. Movement Detection Logic
[distance_sensor_base.cpp:127-157](distance_sensor_base.cpp#L127-L157)

```cpp
bool DistanceSensorBase::isMovementDetected() const
{
    // Need filled window to detect movement
    if (!m_windowFilled || m_lastWindowAverage == 0) {
        return false;
    }

    // Calculate change between current and previous window averages
    int32_t change = abs((int32_t)m_windowAverage - (int32_t)m_lastWindowAverage);

    // Movement threshold: 200mm
    bool significantChange = (change >= MOVEMENT_THRESHOLD_MM);  // 200mm
    bool consistentReading = (windowRange < 100);

    return (significantChange && consistentReading) || (change >= 300);
}
```

### Why It's Slow

#### Problem 1: Window Smoothing Delays Change Detection

With a 5-sample window, each new sample only changes the average by **1/5th** of the difference.

**Example - Hand from 3500mm to 500mm**:
```
Sample    Window Contents             Average    Change from Previous
------    -------------------         -------    --------------------
Start     [3500, 3500, 3500, 3500, 3500]  3500       0
1 (60ms)  [3500, 3500, 3500, 3500, 500]   3100       400 ✓ Triggers (>200mm)
2 (120ms) [3500, 3500, 3500, 500, 500]    2800       300 ✓
3 (180ms) [3500, 3500, 500, 500, 500]     2500       300 ✓
4 (240ms) [3500, 500, 500, 500, 500]      2100       400 ✓
5 (300ms) [500, 500, 500, 500, 500]       500        1600 ✓
```
**Detection time**: 60ms (first sample with hand) ✓ **FAST**

**Example - Gradual Approach from 3500mm to 3000mm**:
```
Sample    Real Distance    Window Contents                  Average    Change
------    -------------    -------------------              -------    ------
Start     3500            [3500, 3500, 3500, 3500, 3500]   3500       0
1         3400            [3500, 3500, 3500, 3500, 3400]   3480       20 ✗
2         3300            [3500, 3500, 3500, 3400, 3300]   3400       80 ✗
3         3200            [3500, 3500, 3400, 3300, 3200]   3380       20 ✗
4         3100            [3500, 3400, 3300, 3200, 3100]   3300       80 ✗
5         3000            [3400, 3300, 3200, 3100, 3000]   3200       100 ✗
6         3000            [3300, 3200, 3100, 3000, 3000]   3120       80 ✗
7         3000            [3200, 3100, 3000, 3000, 3000]   3060       60 ✗
8         3000            [3100, 3000, 3000, 3000, 3000]   3020       40 ✗
9         3000            [3000, 3000, 3000, 3000, 3000]   3000       20 ✗
```
**Detection time**: NEVER triggers (each change <200mm) ✗ **FAILURE**

#### Problem 2: 200mm Movement Threshold Too High

The `MOVEMENT_THRESHOLD_MM = 200` (20cm) is tuned to filter noise, but it's too high for:
- Slow-moving objects
- Gradual approaches
- Hand placed directly in front of sensor (if it arrives gradually in the window)

#### Problem 3: Window Must Fill First

Before ANY detection can occur:
- **Window fill time**: 5 samples × 60ms = **300ms minimum**
- During this time, `m_windowFilled = false` → no movement detection

## Measured Behavior

**User report**: "it takes 3-4 seconds to get an accurate reading"

### Why 3-4 Seconds?

**Scenario**: Sensor reading 3500mm (nothing close), user places hand at 200mm:

1. **First 300ms**: Window filling with new reading (200mm mixing with 3500mm)
   - Averages: 3100 → 2800 → 2500 → 2100 → 200mm
   - Each step: change of 300-400mm ✓ **Should trigger at 60-120ms**

**If it's taking 3-4 seconds, something else is wrong...**

### Hypothesis: Sensor Already Had Recent Data

If the sensor had been reading a **different stable distance** (not max range), then:

**Example**: Sensor reading 1500mm (static object like wall), user approaches to 500mm gradually:
```
Real approach: 1500 → 1400 → 1300 → 1200 → 1100 → 1000 → 900 → 800 → 700 → 600 → 500mm

Window    Average    Change    Triggers?
-------   -------    ------    ---------
[1500...]  1500      0         ✗ (no change)
[1400...]  1480      20        ✗ (< 200mm)
[1300...]  1440      40        ✗
[1200...]  1380      60        ✗
[1100...]  1300      80        ✗
[1000...]  1200      100       ✗
[900...]   1100      100       ✗
[800...]   1000      100       ✗
[700...]   900       100       ✗
[600...]   800       100       ✗
[500...]   700       100       ✗ NEVER TRIGGERS!
```

**Time for 10 steps**: 10 × 60ms = **600ms** and still not triggered.

For a 200mm threshold to be met with 100mm/step changes, you need **2-3 full window replacements**:
- 2 windows: 10 samples = 600ms
- 3 windows: 15 samples = 900ms
- 5 windows: 25 samples = **1500ms (1.5 seconds)**

If the approach is even more gradual, you could hit 3-4 seconds.

## Current Distance Reading vs Motion Detection

### Important Distinction

**TWO separate values**:

1. **`m_currentDistance`** (getCurrentDistance())
   - Updated EVERY sample (60ms)
   - Uses rolling window average
   - This DOES update within 60-300ms

2. **`m_objectDetected`** (isMotionDetected())
   - Requires 200mm change in window average
   - May take many seconds if movement is gradual
   - This is what triggers motion events

### User Expectation

User wants **current distance reading** to update quickly (for display/debugging).
But the **motion detection** has strict requirements to avoid false triggers.

**Conflict**: These two goals require different sensitivities.

## Solutions

### Option 1: Reduce Window Size (Fastest Response, More Noise)

**Change**: Window size 5 → 3

**Impact**:
- Response time: 300ms → 180ms (for window fill)
- Average smoothing: Less noise filtering
- Movement detection: 2 windows = 6 samples = 360ms (vs current 600ms)

**Configuration**:
```cpp
// In hal_ultrasonic.cpp constructor:
DistanceSensorBase(minDistance, maxDistance, 3)  // Changed from 5
```

**Pros**:
- ✅ 40% faster response
- ✅ Less memory usage
- ✅ Simpler tuning

**Cons**:
- ❌ More noise in readings
- ❌ Still requires 200mm threshold
- ❌ Doesn't fix gradual approach problem

### Option 2: Reduce Movement Threshold (Better Detection, More False Positives)

**Change**: `MOVEMENT_THRESHOLD_MM` 200mm → 100mm

**Impact**:
- Detects smaller movements
- More likely to trigger on sensor noise
- Might trigger on stationary objects if sensor is noisy

**Configuration**:
```cpp
// In distance_sensor_base.h:
static constexpr uint32_t MOVEMENT_THRESHOLD_MM = 100;  // Was 200
```

**Pros**:
- ✅ Detects gradual approaches
- ✅ Faster detection of small movements
- ✅ No latency increase

**Cons**:
- ❌ May trigger on noise
- ❌ May trigger on static objects
- ❌ Requires testing to tune

### Option 3: Dual-Mode Detection (Best of Both Worlds)

**Change**: Use **raw reading** for display, **filtered reading** for motion detection

**Implementation**:
```cpp
// Add to DistanceSensorBase:
uint32_t m_rawDistance;  // Latest single reading

uint32_t getRawDistance() const { return m_rawDistance; }  // Immediate
uint32_t getCurrentDistance() const { return m_windowAverage; }  // Smooth

// In updateDistanceSensor():
m_rawDistance = getDistanceReading();  // Store raw
addSampleToWindow(m_rawDistance);      // Then filter
```

**Pros**:
- ✅ **Instant** distance updates (60ms) for display/debugging
- ✅ **Filtered** distance for motion detection (prevents false triggers)
- ✅ Best of both worlds
- ✅ No compromise on noise immunity

**Cons**:
- ❌ Requires code changes to HAL interface
- ❌ Adds one uint32_t to memory footprint
- ❌ Users must choose which value to use (raw vs filtered)

### Option 4: Adaptive Threshold (Smart Detection)

**Change**: Adjust threshold based on window consistency

**Implementation**:
```cpp
bool DistanceSensorBase::isMovementDetected() const
{
    // ... existing code ...

    // Adaptive threshold: lower threshold if readings are very consistent
    uint32_t threshold = MOVEMENT_THRESHOLD_MM;
    if (windowRange < 50) {  // Very stable readings
        threshold = 100;  // Allow smaller changes to trigger
    }

    bool significantChange = (change >= threshold);
    // ...
}
```

**Pros**:
- ✅ Fast detection when sensor is stable
- ✅ Noise immunity when sensor is noisy
- ✅ Self-tuning

**Cons**:
- ❌ More complex logic
- ❌ Harder to reason about behavior
- ❌ May have edge cases

### Option 5: Exponential Moving Average (Different Smoothing Algorithm)

**Change**: Replace rolling window with EMA (exponential moving average)

**Formula**: `filtered = alpha * raw + (1 - alpha) * filtered`

**Implementation**:
```cpp
// In updateDistanceSensor():
if (m_filteredDistance == 0) {
    m_filteredDistance = rawDistance;  // Initialize
} else {
    // Alpha = 0.3 means 30% new value, 70% old value
    m_filteredDistance = (0.3 * rawDistance) + (0.7 * m_filteredDistance);
}
```

**Pros**:
- ✅ No window fill delay (responds immediately)
- ✅ Less memory (no array needed)
- ✅ Configurable responsiveness (tune alpha)
- ✅ Still filters noise

**Cons**:
- ❌ Different smoothing characteristics
- ❌ Requires retuning movement thresholds
- ❌ No "window range" metric for consistency checking

## Recommendations

### Immediate Fix (Low Risk): Option 1 + Option 2

**Combine**:
1. Reduce window size: 5 → 3
2. Reduce threshold: 200mm → 100mm

**Expected result**:
- Window fill: 300ms → 180ms
- Movement detection: More sensitive, catches gradual approaches
- Total latency: ~200-400ms for most cases

**Risk**: Slightly more noise, may need tuning.

### Best Fix (Medium Risk): Option 3

**Add raw distance reading** separate from filtered distance.

- **For display/debugging**: Use `getRawDistance()` → updates every 60ms
- **For motion detection**: Use `getCurrentDistance()` → filtered, stable

**Expected result**:
- UI shows instant distance updates
- Motion detection remains stable and noise-immune
- Best user experience

**Risk**: Requires code changes, testing on real hardware.

### Future Enhancement: Option 4

**Adaptive threshold** based on reading consistency.

- When sensor readings are very stable (low noise), use 100mm threshold
- When sensor readings are noisy, use 200mm threshold
- Automatically adjusts to conditions

**Expected result**:
- Optimal performance in all scenarios
- Self-tuning system

**Risk**: Complex logic, needs extensive testing.

## Testing Plan

### Test 1: Static to Close (Large Movement)
1. Sensor reading 3500mm (nothing in front)
2. Place hand at 200mm quickly
3. **Measure**: Time until distance updates to ~200mm
4. **Expected**: < 500ms

### Test 2: Gradual Approach (Small Movements)
1. Sensor reading 2000mm
2. Slowly approach to 1000mm over 2 seconds
3. **Measure**: Time until motion detected
4. **Expected**: < 1000ms

### Test 3: Static Object (No False Positives)
1. Sensor reading 1500mm (wall/object)
2. Leave static for 10 seconds
3. **Measure**: Count motion events
4. **Expected**: 0 events

### Test 4: Walking Speed (Real Use Case)
1. Person at 3000mm
2. Walk toward sensor at 1.25 m/s (normal walking)
3. **Measure**: Detection latency
4. **Expected**: < 500ms

## Implementation Priority

**Phase 1 (Immediate)**:
1. Reduce window size to 3
2. Reduce threshold to 100mm or 150mm
3. Test on hardware
4. Tune if needed

**Phase 2 (Best Fix)**:
1. Add `getRawDistance()` method
2. Update web UI to show both raw and filtered
3. Keep motion detection using filtered
4. Test on hardware

**Phase 3 (Enhancement)**:
1. Implement adaptive threshold
2. Add metrics to track false positive rate
3. Tune algorithm parameters
4. Deploy and monitor

---

**Status**: Analysis complete
**Recommendation**: Start with Phase 1 (window size + threshold), then implement Phase 2 (raw distance)
**Expected improvement**: 3-4 seconds → 200-400ms for most scenarios
