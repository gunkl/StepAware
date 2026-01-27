# Fix: Distance Sensor Returning 0 mm in Mock Mode

**Date**: 2026-01-21
**Status**: FIXED

## Problem

Distance sensors (both HC-SR04 and Grove Ultrasonic) were returning 0 mm in mock mode, which caused:
- Confusing status displays
- Invalid distance readings
- Potential false motion detection (if 0 was in valid range)

## Root Cause

The mock distance member variable `m_mockDistance` was initialized to 0 in both constructors:

**Affected Files**:
- `src/hal_ultrasonic.cpp:16` - `m_mockDistance(0)`
- `src/hal_ultrasonic_grove.cpp:15` - `m_mockDistance(0)`

In mock mode, the sensor returns `m_mockDistance` directly, so it would always report 0 mm until explicitly set via `mockSetDistance()`.

## Fix Applied

Changed the default mock distance initialization from 0 to 1000mm (1 meter):

```cpp
// Before:
m_mockDistance(0)

// After:
m_mockDistance(1000)  // Default mock distance: 1000mm (1m) - above threshold, won't trigger
```

### Rationale for 1000mm Default

- **Above detection threshold** (500mm) - Won't trigger false motion detection
- **Within valid sensor range** (20mm - 2000mm) - Represents a valid reading
- **Realistic distance** - Simulates a person standing ~1 meter away
- **Easy to verify** - Clear, non-zero value for testing
- **Safe default** - Won't cause unexpected behavior

## Testing

After building and flashing:

1. **Check status display**: Should show `Distance: 1000 mm` instead of `0 mm`
2. **Test mock commands**:
   - Type `s` to view system status - distance should be 1000 mm
   - Type `d` to set mock distance to 250 mm
   - Type `s` again - distance should now be 250 mm (triggers motion)
3. **Verify motion detection**:
   - With distance at 1000 mm (default): No motion (above 500mm threshold)
   - After typing `d` (sets to 250 mm): Motion detected (below 500mm threshold)

## Related Changes

This fix is part of a broader cleanup that also addressed:
- Debug serial flooding causing device bricking (see DEBUG_SERIAL_BRICKING_FIX.md)
- LOG_LEVEL changed from DEBUG to INFO
- Removed excessive DEBUG_PRINTF calls from sensor read loops

## Verification Commands

```
# View current distance
s

# Set mock distance to 250mm (should trigger motion)
d

# Check status again
s

# Look for:
# - Distance: 250 mm (or whatever you set)
# - Motion Events: should increment when distance < 500mm
```

## Files Modified

1. `src/hal_ultrasonic.cpp:16` - Changed m_mockDistance initialization
2. `src/hal_ultrasonic_grove.cpp:15` - Changed m_mockDistance initialization

## Commit Message Suggestion

```
Fix distance sensors returning 0 mm in mock mode

Changed default mock distance from 0 to 1000mm (1m) to provide
a realistic, safe default value that:
- Won't trigger false motion detection (above 500mm threshold)
- Represents a valid sensor reading within range
- Makes it clear the sensor is working in mock mode

Previously, mock sensors initialized to 0 mm which was confusing
and could cause unexpected behavior.
```

---

**Status**: Fixed - Ready for testing
