# Quick Fix: Window Size = 5 for Fast Pedestrian Detection

**Date**: 2026-01-26
**Status**: READY TO TEST
**Type**: Performance Improvement

## Changes Made

Hardcoded the rolling window size to **5 samples** (from 10) for faster pedestrian detection response.

### Files Modified

1. **src/hal_ultrasonic.cpp** - HC-SR04 constructor
2. **src/hal_ultrasonic_grove.cpp** - Grove Ultrasonic constructor

### Before vs After

| Metric | Before (Window=10) | After (Window=5) | Improvement |
|--------|-------------------|------------------|-------------|
| **Window fill time** | 600ms | 300ms | **2x faster** |
| **Response latency** | ~600-800ms | ~300-400ms | **2x faster** |
| **Distance traveled** (walking) | 750mm | 375mm | **50% less** |
| **Warning distance** (1500mm threshold) | Alert at 750mm | Alert at 1125mm | **50% more notice** |
| **Reaction time** (1500mm threshold) | 0.6s | 1.0s | **67% more time** |

## Performance Calculations

### Human Walking Speed
- Average walking: **1.25 m/s** (4 ft/s)

### Current Configuration
- Sample rate: 60ms (HC-SR04 minimum, unchanged)
- Window size: **5 samples** (changed from 10)
- Window fill: 5 × 60ms = **300ms**
- Distance traveled during fill: 1.25 m/s × 0.3s = **375mm**

### Example Scenario: 1500mm Detection Threshold

**Before (Window=10):**
1. Person at 2000mm, walking toward sensor
2. Window fills in 600ms
3. Person travels 750mm → Now at 1250mm
4. Alert fires when person is at 1250mm (too close!)
5. Reaction time: (1250mm ÷ 1250mm/s) = **1.0s** (marginal)

**After (Window=5):**
1. Person at 2000mm, walking toward sensor
2. Window fills in 300ms
3. Person travels 375mm → Now at 1625mm
4. Person continues... threshold crossed at 1500mm
5. Alert fires when person is at 1500mm
6. Reaction time: (1500mm ÷ 1250mm/s) = **1.2s** (good!)

## Expected Behavior Changes

### Faster Alert Response
- Motion detection triggers **300ms faster**
- Warning displays appear sooner
- More time for user to react

### Slightly More Noise Sensitivity
- Smaller averaging window = less noise filtering
- May see occasional false triggers from vibrations
- **Trade-off**: Speed vs stability (speed is more important for safety)

### Direction Detection
- Still works correctly
- May be slightly less stable in edge cases
- Should be fine for human-scale movements

## Testing Checklist

### Basic Functionality
- [ ] Sensor initializes correctly
- [ ] Distance readings are stable
- [ ] No continuous false triggers

### Performance Test
1. Set detection threshold to **1500mm** via web UI
2. Stand 2 meters from sensor
3. Walk toward sensor at normal pace (~1.25 m/s)
4. **Expected**: Alert triggers when you're ~1.5m away
5. **Expected**: You have ~1.2 seconds to react
6. **Success criteria**: Alert fires while you're still far enough away to stop

### Comparison Test
1. Note the distance at which alert fires
2. Note your reaction time
3. Should be **significantly earlier** than before

### Serial Diagnostic
Enable diagnostic mode (`v` command) and watch:
```
[S0] Dist:2000 mm [FAR ] Motion:NO
[S0] Dist:1850 mm [FAR ] Motion:NO  Dir:APPR
[S0] Dist:1700 mm [FAR ] Motion:NO  Dir:APPR
[S0] Dist:1550 mm [FAR ] Motion:NO  Dir:APPR
[S0] Dist:1500 mm [FAR ] Motion:YES Dir:APPR >>> TRIGGER  ← Should trigger here!
>>> SYSTEM: MOTION DETECTED - WARNING ACTIVE <<<
```

## Rollback Instructions

If window size 5 causes too many false positives, you can:

1. **Quick fix**: Change 5 → 7 in both files (balanced)
2. **Revert**: Change 5 → 10 in both files (original behavior)
3. **Custom**: Change to any value 3-20

```cpp
// In hal_ultrasonic.cpp and hal_ultrasonic_grove.cpp
DistanceSensorBase(
    getDefaultCapabilities(...).minDetectionDistance,
    getDefaultCapabilities(...).maxDetectionDistance,
    7  // Change this value (3-20)
),
```

## Recommended Detection Thresholds

For best results with window size 5:

### Long Range Warning (recommended)
- Threshold: **1500mm** (1.5 meters)
- Gives ~1.2 seconds warning
- Good for high-traffic areas

### Medium Range
- Threshold: **1000mm** (1 meter)
- Gives ~0.8 seconds warning
- Good for moderate spaces

### Short Range (not recommended)
- Threshold: **500mm** (0.5 meters)
- Gives ~0.4 seconds warning
- Too close for effective warning

## Technical Details

### Code Changes

**hal_ultrasonic.cpp:**
```cpp
HAL_Ultrasonic::HAL_Ultrasonic(uint8_t triggerPin, uint8_t echoPin, bool mock_mode)
    : DistanceSensorBase(
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).minDetectionDistance,
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).maxDetectionDistance,
        5  // Window size = 5 for fast pedestrian detection (300ms response)
      ),
      // ...
```

**hal_ultrasonic_grove.cpp:**
```cpp
HAL_Ultrasonic_Grove::HAL_Ultrasonic_Grove(uint8_t sigPin, bool mock_mode)
    : DistanceSensorBase(
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE).minDetectionDistance,
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE).maxDetectionDistance,
        5  // Window size = 5 for fast pedestrian detection (300ms response)
      ),
      // ...
```

### Why This Works

The `DistanceSensorBase` constructor now accepts a third parameter (window size):
- Default was 10 (if not specified)
- Now explicitly set to 5
- All averaging and movement detection automatically adjusts
- No other code changes needed!

## Build and Deploy

```bash
# Build firmware
pio run -e esp32c3

# Upload firmware
pio run -t upload -e esp32c3

# Monitor serial output
pio device monitor
```

After upload:
1. Use diagnostic mode to verify window size is working
2. Test with walking approach
3. Verify faster response time

## Next Steps

If this works well:
1. ✅ Keep window size 5
2. 🔄 Consider making it configurable via web UI
3. 📊 Collect data on false positive rate
4. 🎯 Fine-tune threshold values for optimal warning distance

If too many false positives:
1. Try window size 7 (balanced: 420ms response)
2. Adjust detection threshold higher
3. Enable direction detection (approaching only)

---

**Status**: Ready to build and test
**Expected Result**: 2x faster motion detection response
**Risk**: Slightly higher noise sensitivity (acceptable trade-off)
