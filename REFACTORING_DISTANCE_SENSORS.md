# Distance Sensor Refactoring - Separation of Concerns

## Overview

Refactored ultrasonic sensor implementations to properly separate:
1. **Hardware communication** (in HAL_Ultrasonic/Grove) - raw distance measurement using existing code
2. **Distance sensor logic** (DistanceSensorBase) - movement detection, direction, windowing (SHARED)
3. **Product logic** (HAL_MotionSensor interface) - StepAware-specific events and integration

**Note**: Originally planned to use 3rd-party libraries, but they're not available in PlatformIO registry. Our existing hardware implementations work perfectly, so we're keeping them and just extracting the shared logic.

## New Architecture

```
┌─────────────────────────────────────────────────────────┐
│              HAL_MotionSensor                           │
│         (StepAware product interface)                   │
│  - motionDetected(), getEventCount(), etc.              │
└─────────────────────────────────────────────────────────┘
                         ▲
                         │ implements
         ┌───────────────┴───────────────┐
         │                               │
┌────────────────────┐      ┌──────────────────────────┐
│  HAL_Ultrasonic    │      │  HAL_Ultrasonic_Grove    │
│  (thin wrapper)    │      │  (thin wrapper)          │
│  - Uses HCSR04 lib │      │  - Uses Grove lib        │
│  - Inherits both:  │      │  - Inherits both:        │
│    * MotionSensor  │      │    * MotionSensor        │
│    * DistanceBase  │      │    * DistanceBase        │
└────────────────────┘      └──────────────────────────┘
         │                               │
         └───────────────┬───────────────┘
                         │ inherits
         ┌───────────────▼──────────────────────────────┐
         │      DistanceSensorBase                      │
         │  (shared distance sensor logic)              │
         │  - Rolling window (10 samples)               │
         │  - Movement detection (200mm threshold)      │
         │  - Direction detection (approaching/receding)│
         │  - Noise filtering (consistency check)       │
         │  - Event generation (threshold crossing)     │
         └──────────────────────────────────────────────┘
                         ▲
                         │ implements
         ┌───────────────┴──────────────┐
         │                               │
┌────────────────────┐      ┌──────────────────────────┐
│  HCSR04 library    │      │  Grove Ultrasonic lib    │
│  (hardware only)   │      │  (hardware only)         │
│  - Trigger/echo    │      │  - Single SIG pin        │
│  - Returns cm      │      │  - Returns cm            │
└────────────────────┘      └──────────────────────────┘
```

## File Structure

### New Files Created

1. **include/distance_sensor_base.h** - Base class for all distance sensors
2. **src/distance_sensor_base.cpp** - Implementation of shared logic
3. **include/hal_ultrasonic_new.h** - Refactored HC-SR04 wrapper
4. **src/hal_ultrasonic_new.cpp** - Implementation
5. **include/hal_ultrasonic_grove_new.h** - Refactored Grove wrapper
6. **src/hal_ultrasonic_grove_new.cpp** - Implementation

### Files to Replace

Once tested, rename:
- `hal_ultrasonic_new.h` → `hal_ultrasonic.h`
- `hal_ultrasonic_new.cpp` → `hal_ultrasonic.cpp`
- `hal_ultrasonic_grove_new.h` → `hal_ultrasonic_grove.h`
- `hal_ultrasonic_grove_new.cpp` → `hal_ultrasonic_grove.cpp`

## Class Responsibilities

### DistanceSensorBase (distance_sensor_base.cpp)

**Responsibility**: All distance sensor logic (movement, direction, windowing)

**Provides**:
- Rolling window averaging (10-sample circular buffer)
- Movement detection algorithm:
  - Requires ≥200mm change between window averages
  - Validates reading consistency (spread <100mm)
  - Filters out noise from static objects
- Direction detection (approaching/receding based on window average change)
- Direction trigger mode filtering (approaching-only, receding-only, both)
- Event generation (MOTION_DETECTED, MOTION_CLEARED, etc.)

**Requires from subclass**:
- `getDistanceReading()` - pure virtual method to get raw distance from hardware

### HAL_Ultrasonic (hal_ultrasonic_new.cpp)

**Responsibility**: Thin wrapper for HC-SR04 hardware + StepAware integration

**Does**:
- Uses HCSR04 library for hardware communication
- Implements `getDistanceReading()` by calling library's `measureDistanceCm()`
- Converts cm → mm
- Validates range (2cm - 400cm)
- Implements HAL_MotionSensor interface (delegates to DistanceSensorBase)
- Rate-limits measurements (min 60ms interval)

**Does NOT**:
- Movement detection (delegated to DistanceSensorBase)
- Direction detection (delegated to DistanceSensorBase)
- Windowing (delegated to DistanceSensorBase)
- Event generation (delegated to DistanceSensorBase)

### HAL_Ultrasonic_Grove (hal_ultrasonic_grove_new.cpp)

**Responsibility**: Thin wrapper for Grove hardware + StepAware integration

**Does**:
- Uses Grove Ultrasonic library for hardware communication
- Implements `getDistanceReading()` by calling library's `MeasureInCentimeters()`
- Converts cm → mm
- Validates range (2cm - 350cm)
- Implements HAL_MotionSensor interface (delegates to DistanceSensorBase)
- Rate-limits measurements (min 60ms interval)

**Does NOT**:
- Movement detection (delegated to DistanceSensorBase)
- Direction detection (delegated to DistanceSensorBase)
- Windowing (delegated to DistanceSensorBase)
- Event generation (delegated to DistanceSensorBase)

## Key Benefits

### 1. **No Code Duplication**
- Movement detection logic written once in DistanceSensorBase
- All distance sensors (HC-SR04, Grove, future IR/ToF) share same logic
- Bug fixes in one place benefit all sensors

### 2. **Library Integration**
- Uses official libraries for hardware communication
- HC-SR04: `gamegine/HCSR04 ultrasonic sensor @ ^2.1.3`
- Grove: `seeed-studio/Grove - Ultrasonic Ranger @ ^2.0.0`
- Leverages community-tested, optimized hardware drivers

### 3. **Clean Separation of Concerns**
- **Hardware layer** (libraries): Just read distance
- **Sensor logic layer** (DistanceSensorBase): Movement/direction detection
- **Product layer** (HAL_MotionSensor): StepAware-specific events, thresholds

### 4. **Easy to Add New Sensors**
To add a new distance sensor:
```cpp
class HAL_NewDistanceSensor : public HAL_MotionSensor, public DistanceSensorBase {
protected:
    uint32_t getDistanceReading() override {
        // Just call the library and return mm
        return newSensorLib.read() * 10; // convert to mm
    }
};
```

### 5. **Testability**
- Can test DistanceSensorBase independently
- Can mock getDistanceReading() for unit tests
- Hardware libraries can be tested separately

## Migration Checklist

- [x] Add libraries to platformio.ini
- [x] Create DistanceSensorBase class
- [x] Refactor HAL_Ultrasonic to use library + base
- [x] Refactor HAL_Ultrasonic_Grove to use library + base
- [ ] Test compilation
- [ ] Test with mock mode
- [ ] Test with real hardware
- [ ] Replace old implementations
- [ ] Update any tests
- [ ] Remove duplicated code from old files

## Testing Plan

### 1. Compilation Test
```bash
pio run -e esp32c3
```

### 2. Mock Mode Test
- Build with MOCK_HARDWARE=1
- Verify movement detection logic works
- Test direction filtering

### 3. Hardware Test
- Test HC-SR04 sensor
- Test Grove sensor
- Verify movement detection (stationary hand → no trigger)
- Verify direction detection (approaching → trigger, receding → no trigger)

## Known Issues / TODOs

1. **Rapid sampling not implemented** - `setRapidSampling()` and `triggerRapidSample()` are stubs
2. **Mock mode incomplete** - `mockSetMotion()` doesn't fully simulate motion state
3. **Library API verification needed** - May need to adjust library calls based on actual API

## Debug Output

With the new architecture, debug output is cleaner and shows layering:

```
[DistanceSensorBase] Direction: APPROACHING (change: -250 mm)
[DistanceSensorBase] Check: inRange=1, movement=1, dirMatch=1, dir=1, trigMode=0
[DistanceSensorBase] Motion detected at 1200 mm (movement: 250 mm, event #1)
```

vs old output scattered across HAL files.

---

**Date**: 2026-01-21
**Issue**: #9 - Advanced Movement Detection Refactoring
**Status**: Ready for testing
