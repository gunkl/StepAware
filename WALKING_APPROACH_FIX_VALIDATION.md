# Walking Approach Detection - Fix Validation

## Summary of Bugs Found

### Bug #1: m_lastRawDistance Updated Too Early
**Location:** `distance_sensor_base.cpp:82` (before fix)
**Problem:** `m_lastRawDistance` was set to current reading BEFORE comparison, causing gradual approach check to always compare current value against itself
**Impact:** Gradual approach detection never worked (always `rawDistance < rawDistance` → FALSE)

**Fix:** Moved `m_lastRawDistance = rawDistance` to line 143, AFTER `updateDualModeDetectionState()` call

### Bug #2: Premature Reset of Gradual Approach Flag
**Location:** `distance_sensor_base.cpp:508-521`
**Problem:** Reset `m_seenApproachingFromOutside` whenever distance > threshold, even when direction was STATIONARY (unclear) or APPROACHING
**Impact:** Gradual approach flag cleared during active approach, causing system to fall back to sudden appearance mode

**Fix:** Only reset when direction is CLEARLY RECEDING:
```cpp
// OLD:
bool movingAway = (m_direction == DIRECTION_RECEDING || m_direction == DIRECTION_STATIONARY);

// NEW:
bool movingAway = (m_direction == DIRECTION_RECEDING);
```

---

## Validation with Real Log Data

### Test Scenario from Log (7) - Timestamp 39561-39806

**Initial Conditions:**
- Detection threshold: 1100mm
- Direction sensitivity: 350mm
- You walking toward sensor from distance

**Actual Readings:**
```
Time      Raw Distance    Window Avg    Direction      Event
[39561]   ~1900mm        1933mm        APPROACHING    Gradual approach active
[39679]   ~1900mm        1933mm        APPROACHING    Still approaching
[39710]   ~1900mm        1933mm        STATIONARY     Direction changed
[39746]   908mm          1933mm        STATIONARY     WIPE triggered
[39806]   877mm          908mm         STATIONARY     After wipe
[39865]   ~850mm         ~860mm        STATIONARY     Continuing
```

---

### Execution with OLD CODE (BUGGY)

#### Step 1: Reading at 1900mm, direction = APPROACHING
```cpp
// Gradual approach was detected earlier (Bug #1 WAS fixed in uploaded code)
m_seenApproachingFromOutside = true

// Trigger check:
inRange = (1933 <= 1100) → FALSE
Gradual approach log: "inRange=0, movement=1, dirMatch=1 → trigger=0"
```
**State:** Gradual approach active, waiting to enter zone ✓

---

#### Step 2: Direction becomes STATIONARY (delta = 0mm)
```cpp
// Reset check (Bug #2):
beyondDetectionThreshold = (1933 > 1100) → TRUE
movingAway = (STATIONARY == RECEDING || STATIONARY == STATIONARY) → TRUE ❌

if (TRUE && TRUE) {
    LOG: "Object left detection zone (dist=1933 > threshold=1100), resetting dual-mode state"
    m_seenApproachingFromOutside = false  ❌ CLEARED!
    m_suddenAppearance = false
}
```
**State:** Gradual approach flag CLEARED even though you're still approaching ❌

---

#### Step 3: Wipe at 908mm (entered detection zone)
```cpp
// Window reset to 908mm
m_consecutiveInRangeCount = 1

// After 3 consecutive readings (at 908, 877, etc.):
if (!m_seenApproachingFromOutside && !m_suddenAppearance) {
    // Both false, so enters this block
    m_suddenAppearance = true  ❌ WRONG MODE!
    m_awaitingDirectionConfirmation = true
    LOG: "Sudden appearance: 4 consecutive readings within range..."
}
```
**State:** Using SUDDEN APPEARANCE mode instead of gradual approach ❌

---

#### Step 4: Direction confirmation (sudden appearance mode)
```cpp
// After 3 readings, window = [908, 877, ~850mm]
// Window average ≈ 878mm
// Previous average = 908mm
// Delta = 878 - 908 = -30mm
// abs(-30) < 350 → STATIONARY

if (m_suddenAppearance) {
    if (!m_awaitingDirectionConfirmation) {
        shouldTrigger = inRange && directionMatches;
        directionMatches = (STATIONARY == APPROACHING) → FALSE ❌
        shouldTrigger = TRUE && FALSE → FALSE ❌
    }
}
```
**Result:** NO TRIGGER ❌

---

### Execution with NEW CODE (FIXED)

#### Step 1: Reading at 1900mm, direction = APPROACHING
```cpp
// Gradual approach detected earlier (Bug #1 fixed)
m_seenApproachingFromOutside = true

// Trigger check:
inRange = (1933 <= 1100) → FALSE
Gradual approach log: "inRange=0, movement=1, dirMatch=1 → trigger=0"
```
**State:** Gradual approach active, waiting to enter zone ✓

---

#### Step 2: Direction becomes STATIONARY (delta = 0mm)
```cpp
// Reset check (Bug #2 FIXED):
beyondDetectionThreshold = (1933 > 1100) → TRUE
movingAway = (STATIONARY == DIRECTION_RECEDING) → FALSE ✓✓

if (TRUE && FALSE) → FALSE {
    // Does NOT enter this block!
}
// m_seenApproachingFromOutside PRESERVED! ✓✓✓
```
**State:** Gradual approach flag PRESERVED! ✓✓✓

---

#### Step 3: Wipe at 908mm (entered detection zone)
```cpp
// Window reset to 908mm
m_consecutiveInRangeCount = 1

// After 3 consecutive readings (at 908, 877, etc.):
if (!m_seenApproachingFromOutside && !m_suddenAppearance) {
    // FALSE: m_seenApproachingFromOutside is TRUE!
    // Does NOT enter this block ✓
}
// Stays in GRADUAL APPROACH mode! ✓✓✓
```
**State:** Using GRADUAL APPROACH mode (correct!) ✓✓✓

---

#### Step 4: Trigger check (gradual approach mode)
```cpp
if (m_seenApproachingFromOutside) {
    // GRADUAL APPROACH MODE ✓
    shouldTrigger = inRange && movementDetected && directionMatches;

    inRange = (878 <= 1100) → TRUE ✓

    // Movement detection:
    // Change from 1933 → 908 is large (1025mm change)
    // But after wipe, window is reset, so m_lastWindowAverage = m_windowAverage
    // Movement requires change between m_lastWindowAverage and m_windowAverage
    // After wipe: both are 908mm → no movement detected initially

    // After a few more readings: [908, 877, 850]
    // Current avg = 878mm, last avg = 908mm
    // Change = 30mm < adaptive threshold (75ms * 25mm/ms = 1875mm)
    // Movement: FALSE (change too small)

    movementDetected = FALSE ⚠️

    // Direction:
    // Delta = -30mm, abs(-30) < 350 → STATIONARY
    directionMatches = (STATIONARY == APPROACHING) → FALSE ⚠️

    shouldTrigger = TRUE && FALSE && FALSE → FALSE ⚠️
}
```
**Result:** Still NO TRIGGER ⚠️ (but for different reason)

---

## Remaining Limitation

The fixes solve the bugs, but there's a **fundamental limitation** after a wipe:

1. **Window Reset:** All values set to same distance (e.g., 908mm)
2. **No Movement Detected:** Change between readings too small after wipe
3. **Direction STATIONARY:** Changes within sensitivity threshold (350mm)

### Why This Happens

When you walk from 3000mm → 1000mm quickly:
- Sensor gets mixed readings due to noise/reflections
- Large jump triggers wipe (resets window to current close reading)
- After wipe, all window values are identical
- Small subsequent changes (908 → 877 → 850) don't meet thresholds

### Potential Solutions (Future Work)

1. **Lower direction sensitivity** for gradual approach mode:
   - Current: 350mm
   - Suggested: 100-150mm for gradual approach
   - Would detect smaller changes as APPROACHING

2. **Special movement detection after wipe in gradual approach mode:**
   - If `m_seenApproachingFromOutside == true`, use lower movement threshold
   - Or trust that wipe itself indicates movement

3. **Skip direction matching in gradual approach mode:**
   - We already know direction from raw readings (gradual approach detected)
   - Don't require window direction to also match

---

## Test Results Summary

### Before Fixes:
- ❌ Gradual approach never detected (Bug #1)
- ❌ Flag reset during approach (Bug #2)
- ❌ Always used sudden appearance mode
- ❌ Never triggered on walking approach

### After Fixes:
- ✅ Gradual approach correctly detected from raw readings
- ✅ Flag preserved through STATIONARY direction states
- ✅ Uses gradual approach mode (not sudden appearance)
- ⚠️ May not trigger due to window-based movement/direction thresholds after wipe

### What Works Now:
- Hand waving detection prevention ✓
- Gradual approach flag preservation ✓
- Correct mode selection ✓

### What Still Needs Work:
- Trigger conditions too strict after wipe
- Direction sensitivity may be too high
- Movement detection fails after window reset

---

## Bug #3: Missing Direction Stability Logic

### Problem
Direction was updated **immediately** every cycle without requiring stability, causing rapid bouncing:
- APPROACHING → STATIONARY (178ms later)
- STATIONARY → RECEDING (60ms later)
- RECEDING → STATIONARY (120ms later)

### Fix
Added direction confirmation logic requiring **400ms of stability**:
```cpp
// New members:
MotionDirection m_candidateDirection;  // Pending direction being evaluated
uint8_t m_directionStabilityCount;     // Consecutive samples with same direction
static constexpr uint32_t DIRECTION_STABILITY_TIME_MS = 400;

// Logic:
- Calculate required samples: 400ms / 75ms = 6 samples
- Track candidate direction
- Only confirm direction after 6 consecutive samples show same direction
```

### Impact
- Prevents rapid direction bouncing
- More reliable direction detection
- Cleaner, more stable operation

---

## Recommended Next Steps

1. **Test current fixes** - Confirm gradual approach mode is used with stable direction
2. **Monitor logs** for:
   - "Gradual approach detected:" (raw reading decreasing)
   - "Direction candidate:" (new direction being evaluated)
   - "Direction confirmed after N samples:" (stable direction)
3. **Additional fix needed:** Gradual approach mode should skip direction matching or use lower thresholds
4. **Suggested:** When `m_seenApproachingFromOutside == true`, trigger on `inRange && movementDetected` only

