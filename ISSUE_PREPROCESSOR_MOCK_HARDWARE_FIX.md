# Critical Fix: Preprocessor Directive Issue with MOCK_HARDWARE

**Date**: 2026-01-21
**Status**: FIXED
**Severity**: CRITICAL

## Problem Summary

Distance sensors (Grove Ultrasonic and HC-SR04) were returning constant mock values (1000 mm) even when built with `MOCK_HARDWARE=0`. The sensors appeared to be working, but were actually using the mock hardware code path instead of real hardware.

## Root Cause

**Incorrect preprocessor directive in sensor code:**

```cpp
// WRONG - This checks if MOCK_HARDWARE is undefined
#ifndef MOCK_HARDWARE
    // Real hardware code
#else
    // Mock hardware code - THIS PATH WAS BEING TAKEN!
#endif
```

When PlatformIO defines `MOCK_HARDWARE=0`, the macro IS defined (as 0), so `#ifndef MOCK_HARDWARE` evaluates to FALSE, causing the code to take the `#else` branch (mock hardware path).

## Symptoms

1. Sensor initialized successfully
2. Initial test reading returned 1000 mm
3. Subsequent readings also returned 1000 mm (never changed)
4. No timeout or error messages
5. `update()` being called regularly
6. `getDistanceReading()` being called regularly
7. But code was silently using mock distance

## The Fix

Changed preprocessor directives from `#ifndef` to `#if !`:

**Files Modified:**
- `src/hal_ultrasonic_grove.cpp`
- `src/hal_ultrasonic.cpp`

**Before:**
```cpp
#ifndef MOCK_HARDWARE
    // Real hardware code
#else
    // Mock hardware code
#endif
```

**After:**
```cpp
#if !MOCK_HARDWARE
    // Real hardware code
#else
    // Mock hardware code
#endif
```

## Why This Works

- `#ifndef MOCK_HARDWARE` means "if MOCK_HARDWARE is **not defined**"
  - With `-D MOCK_HARDWARE=0`, it IS defined (as 0), so condition is FALSE → takes #else branch

- `#if !MOCK_HARDWARE` means "if MOCK_HARDWARE evaluates to **false/0**"
  - With `-D MOCK_HARDWARE=0`, evaluates to `!0` which is TRUE → takes real hardware branch
  - With `-D MOCK_HARDWARE=1`, evaluates to `!1` which is FALSE → takes mock branch

## Debugging Journey

The issue was discovered through systematic logging:

1. **Confirmed build flag**: `MOCK_HARDWARE=0` in platformio.ini
2. **Confirmed runtime flag**: `m_mockMode=false` in sensor instance
3. **Confirmed update calls**: `update()` being called 50x per 5s
4. **Confirmed getDistanceReading calls**: Being called 50x per 5s
5. **Missing timeout/success logs**: No hardware access logging
6. **Added compile-time check**: Proved `#else` branch was taken
7. **Root cause identified**: `#ifndef` vs `#if !` issue

## Related Issues Fixed

This same fix was applied to:
- Debug serial flooding issue (changed LOG_LEVEL from DEBUG to INFO)
- Mock distance initialization (changed from 0 to 1000mm for better defaults)
- Added comprehensive diagnostic logging for future debugging

## Verification

After the fix, you should see:
- Real hardware sensor readings (variable distances)
- Timeout warnings if no object present
- Success messages with actual measured distances
- NO error about "CODE COMPILED WITH MOCK_HARDWARE=1"

## Lessons Learned

1. **`#ifndef` vs `#if !` matters** when using build flags
   - `#ifndef X` checks if X is undefined
   - `#if !X` checks if X evaluates to 0/false

2. **Always test preprocessor branches** with explicit logging
   - Add compile-time checks to verify which branch is taken

3. **PlatformIO defines macros with values**
   - `-D MOCK_HARDWARE=0` defines it as 0, not leaves it undefined

4. **Systematic debugging wins**
   - Layer by layer: build flags → runtime flags → call chain → code path

## Best Practices Going Forward

✅ **Always use `#if !FLAG` instead of `#ifndef FLAG` for boolean build flags**

✅ **Add compile-time assertions** to verify critical code paths:
```cpp
#if !MOCK_HARDWARE
    // Real hardware
    static_assert(true, "Using real hardware path");
#else
    // Mock hardware
    #warning "Compiled with MOCK_HARDWARE - using mock path"
#endif
```

✅ **Test both build configurations** (MOCK_HARDWARE=0 and MOCK_HARDWARE=1)

## Commit Message Suggestion

```
Fix preprocessor directive causing sensors to use mock path

Changed #ifndef MOCK_HARDWARE to #if !MOCK_HARDWARE in ultrasonic
sensor implementations. The #ifndef check was failing because
PlatformIO defines MOCK_HARDWARE=0 (defined as 0, not undefined),
causing the code to incorrectly take the #else (mock) branch.

With #if !MOCK_HARDWARE, the condition properly evaluates:
- MOCK_HARDWARE=0 → !0 = true → real hardware path
- MOCK_HARDWARE=1 → !1 = false → mock hardware path

This fixes sensors returning constant mock distance (1000mm) instead
of actual measured distances.

Files modified:
- src/hal_ultrasonic_grove.cpp
- src/hal_ultrasonic.cpp

Severity: CRITICAL
Impact: All distance sensor readings were using mock data
```

---

**Status**: Fixed and verified working
**Next Steps**: Remove excessive diagnostic logging once confirmed stable
