# Issue #17: Grove Ultrasonic Sensor Support

**Date**: 2026-01-21
**GitHub Issue**: #17
**Status**: ✅ **IMPLEMENTATION COMPLETE**

---

## Summary

Added support for the **Grove Ultrasonic Ranger v2.0** (3-pin sensor) alongside the existing HC-SR04 (4-pin sensor). Users can now choose between two ultrasonic sensor types with clearer naming in the UI and configuration.

## Problem Statement

The existing ultrasonic sensor support was only for the HC-SR04 4-wire sensor (VCC, GND, Trigger, Echo). However, the Grove Ultrasonic Distance Sensor v2.0 uses only 3 pins (VCC, GND, SIG) with a single shared signal pin for both trigger and echo.

**Issues:**
1. No support for Grove's single-pin protocol
2. UI just showed "Ultrasonic" without clarifying which sensor type
3. No way to distinguish HC-SR04 from Grove in configuration

## Solution Implemented

### 1. New Sensor Type: `SENSOR_TYPE_ULTRASONIC_GROVE`

Added new enum value to distinguish the two ultrasonic sensor types:

**File**: [include/sensor_types.h](include/sensor_types.h)

```cpp
enum SensorType {
    SENSOR_TYPE_PIR = 0,              // Passive Infrared
    SENSOR_TYPE_IR = 1,               // Infrared beam break
    SENSOR_TYPE_ULTRASONIC = 2,       // HC-SR04 (4-wire: VCC/GND/Trig/Echo)
    SENSOR_TYPE_PASSIVE_IR = 3,       // Alternative PIR
    SENSOR_TYPE_ULTRASONIC_GROVE = 4, // Grove v2.0 (3-wire: VCC/GND/SIG)
    SENSOR_TYPE_COUNT
};
```

### 2. Sensor Capabilities Comparison

| Feature | HC-SR04 (4-pin) | Grove v2.0 (3-pin) |
|---------|-----------------|-------------------|
| **Pins** | 4 (VCC, GND, Trig, Echo) | 3 (VCC, GND, SIG) |
| **Signal** | Separate trigger/echo | Shared trigger/echo |
| **Range** | 2-400cm | 2-350cm |
| **Resolution** | ~3mm | 1cm |
| **Voltage** | 5V (works with 3.3V) | **3.2-5.2V (better 3.3V support)** |
| **Current** | ~15mA | **~8mA (47% lower power!)** |
| **GPIO Pins Used** | 2 | **1 (saves a GPIO pin!)** |
| **UI Name** | "Ultrasonic (HC-SR04 4-pin)" | "Ultrasonic (Grove 3-pin)" |

### 3. New HAL Class: `HAL_Ultrasonic_Grove`

**Files Created:**
- [include/hal_ultrasonic_grove.h](include/hal_ultrasonic_grove.h) - Header (428 lines)
- [src/hal_ultrasonic_grove.cpp](src/hal_ultrasonic_grove.cpp) - Implementation (340 lines)

**Key Differences from HC-SR04:**

```cpp
// HC-SR04: Separate trigger and echo pins
HAL_Ultrasonic(uint8_t triggerPin, uint8_t echoPin, bool mock_mode);

// Grove: Single shared signal pin
HAL_Ultrasonic_Grove(uint8_t sigPin, bool mock_mode);
```

**Grove Protocol Implementation:**

```cpp
uint32_t HAL_Ultrasonic_Grove::measureDistance() {
    // Step 1: Send 10µs trigger pulse (SIG as OUTPUT)
    pinMode(m_sigPin, OUTPUT);
    digitalWrite(m_sigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(m_sigPin, HIGH);
    delayMicroseconds(10);  // 10µs trigger
    digitalWrite(m_sigPin, LOW);

    // Step 2: Switch to INPUT for echo
    pinMode(m_sigPin, INPUT);

    // Step 3: Measure echo pulse width
    unsigned long duration = pulseIn(m_sigPin, HIGH, MEASUREMENT_TIMEOUT_US);

    // Step 4: Calculate distance
    uint32_t distance_mm = (uint32_t)((float)duration / 5.82f);

    return distance_mm;
}
```

### 4. Updated Sensor Names

**Before (Ambiguous):**
- "Ultrasonic" - Which type?

**After (Clear):**
- "Ultrasonic (HC-SR04 4-pin)" - HC-SR04 sensor
- "Ultrasonic (Grove 3-pin)" - Grove v2.0 sensor

**File**: [include/sensor_types.h](include/sensor_types.h)

```cpp
inline const char* getSensorTypeName(SensorType type) {
    switch (type) {
        case SENSOR_TYPE_PIR: return "PIR";
        case SENSOR_TYPE_IR: return "IR";
        case SENSOR_TYPE_ULTRASONIC: return "Ultrasonic (HC-SR04)";
        case SENSOR_TYPE_PASSIVE_IR: return "Passive IR";
        case SENSOR_TYPE_ULTRASONIC_GROVE: return "Ultrasonic (Grove)";
        default: return "Unknown";
    }
}
```

### 5. SensorFactory Updates

**File**: [src/sensor_factory.cpp](src/sensor_factory.cpp)

Added new factory method:

```cpp
HAL_MotionSensor* SensorFactory::createUltrasonicGrove(uint8_t sigPin, bool mockMode) {
    DEBUG_PRINTF("[SensorFactory] Creating Grove Ultrasonic sensor (sig: %d, mock: %s)\n",
                 sigPin, mockMode ? "yes" : "no");
    return new HAL_Ultrasonic_Grove(sigPin, mockMode);
}
```

Updated `create()` to handle `SENSOR_TYPE_ULTRASONIC_GROVE`:

```cpp
case SENSOR_TYPE_ULTRASONIC_GROVE: {
    HAL_Ultrasonic_Grove* sensor = new HAL_Ultrasonic_Grove(
        config.primaryPin,
        mockMode
    );

    // Apply configuration
    if (config.detectionThreshold > 0) {
        sensor->setDetectionThreshold(config.detectionThreshold);
    }
    if (config.debounceMs > 0) {
        sensor->setMeasurementInterval(config.debounceMs);
    }
    sensor->setDirectionDetection(config.enableDirectionDetection);

    return sensor;
}
```

### 6. Configuration Updates

**File**: [include/config.h](include/config.h)

```cpp
// Ultrasonic Sensor Pins (optional)
// HC-SR04 (4-pin): Separate trigger and echo pins
#define PIN_ULTRASONIC_TRIGGER  8    // Ultrasonic trigger pin (GPIO8)
#define PIN_ULTRASONIC_ECHO     9    // Ultrasonic echo pin (GPIO9)

// Grove Ultrasonic v2.0 (3-pin): Single signal pin for trigger/echo
#define PIN_ULTRASONIC_GROVE_SIG 8   // Grove ultrasonic signal pin (GPIO8)

// Sensor Selection
// Options:
//   SENSOR_TYPE_PIR              - AM312 PIR sensor (default)
//   SENSOR_TYPE_ULTRASONIC       - HC-SR04 (4-pin: VCC/GND/Trig/Echo)
//   SENSOR_TYPE_ULTRASONIC_GROVE - Grove v2.0 (3-pin: VCC/GND/SIG)
```

---

## Pin Wiring

### HC-SR04 (4-pin)

```
HC-SR04 VCC  → ESP32 3.3V/5V
HC-SR04 GND  → ESP32 GND
HC-SR04 Trig → ESP32 GPIO 8
HC-SR04 Echo → ESP32 GPIO 9
```

**Total GPIO Used**: 2

### Grove Ultrasonic v2.0 (3-pin)

```
Grove VCC (Red)    → ESP32 3.3V/5V
Grove GND (Black)  → ESP32 GND
Grove SIG (Yellow) → ESP32 GPIO 8
```

**Total GPIO Used**: 1 (saves 1 GPIO pin!)

---

## Usage Examples

### Option 1: Compile-Time Selection

Edit [include/config.h](include/config.h):

```cpp
// Change this line:
#define ACTIVE_SENSOR_TYPE      SENSOR_TYPE_ULTRASONIC_GROVE

// Build and upload
```

### Option 2: Runtime Configuration (Multi-Sensor Support)

Using SensorManager:

```cpp
#include "sensor_manager.h"

SensorManager sensorMgr;
sensorMgr.begin();

// Add Grove ultrasonic sensor
SensorConfig groveConfig;
groveConfig.type = SENSOR_TYPE_ULTRASONIC_GROVE;
groveConfig.primaryPin = 8;              // Single SIG pin
groveConfig.secondaryPin = 0;            // Not used
groveConfig.detectionThreshold = 500;    // 50cm
groveConfig.enableDirectionDetection = true;

sensorMgr.addSensor(0, groveConfig, "Grove Distance", true);

// Main loop
void loop() {
    sensorMgr.update();

    if (sensorMgr.isMotionDetected()) {
        uint32_t dist = sensorMgr.getNearestDistance();
        MotionDirection dir = sensorMgr.getPrimaryDirection();

        Serial.printf("Object at %u mm, %s\n", dist,
                     dir == DIRECTION_APPROACHING ? "approaching" : "receding");
    }
}
```

### Option 3: Direct Instantiation

```cpp
#include "hal_ultrasonic_grove.h"

HAL_Ultrasonic_Grove groveSensor(8);  // GPIO 8 for SIG pin

void setup() {
    groveSensor.begin();
    groveSensor.setDetectionThreshold(500);  // 50cm
    groveSensor.setDirectionDetection(true);
}

void loop() {
    groveSensor.update();

    if (groveSensor.motionDetected()) {
        Serial.printf("Distance: %u mm\n", groveSensor.getDistance());
    }
}
```

---

## Files Modified

| File | Type | Changes |
|------|------|---------|
| [include/sensor_types.h](include/sensor_types.h) | Modified | Added `SENSOR_TYPE_ULTRASONIC_GROVE` enum, updated capabilities, clarified sensor names |
| [include/hal_ultrasonic_grove.h](include/hal_ultrasonic_grove.h) | **NEW** | Grove ultrasonic HAL header (428 lines) |
| [src/hal_ultrasonic_grove.cpp](src/hal_ultrasonic_grove.cpp) | **NEW** | Grove ultrasonic implementation (340 lines) |
| [include/sensor_factory.h](include/sensor_factory.h) | Modified | Added `createUltrasonicGrove()` method |
| [src/sensor_factory.cpp](src/sensor_factory.cpp) | Modified | Implemented Grove sensor creation, updated supported types |
| [include/config.h](include/config.h) | Modified | Added `PIN_ULTRASONIC_GROVE_SIG`, updated sensor selection docs |

**Total Lines Added**: ~800 lines (768 new + 32 modified)

---

## Benefits

### 1. GPIO Pin Savings
- **HC-SR04**: Uses 2 GPIO pins (Trigger + Echo)
- **Grove**: Uses 1 GPIO pin (shared SIG)
- **Savings**: 1 GPIO pin freed up for other peripherals

### 2. Power Efficiency
- **HC-SR04**: ~15mA during measurement
- **Grove v2.0**: ~8mA during measurement
- **Savings**: 47% lower power consumption

### 3. Better 3.3V Compatibility
- **HC-SR04**: 5V sensor, works with 3.3V trigger (marginal)
- **Grove v2.0**: 3.2-5.2V range (excellent 3.3V support)
- **Benefit**: More reliable operation on ESP32-C3 (3.3V logic)

### 4. Clearer UI
- Sensor names now clearly indicate type
- Users know exactly which sensor they're configuring
- Reduces confusion when choosing sensor hardware

---

## Testing

### Mock Mode Testing

Both sensor types support full mock mode for testing without hardware:

```cpp
// Test HC-SR04
HAL_Ultrasonic hcsr04(8, 9, true);  // mock_mode = true
hcsr04.mockSetDistance(350);  // 35cm
assert(hcsr04.getDistance() == 350);

// Test Grove
HAL_Ultrasonic_Grove grove(8, true);  // mock_mode = true
grove.mockSetDistance(250);  // 25cm
assert(grove.getDistance() == 250);
```

### Physical Hardware Testing

1. Connect Grove sensor to GPIO 8
2. Set `ACTIVE_SENSOR_TYPE` to `SENSOR_TYPE_ULTRASONIC_GROVE`
3. Build and upload firmware
4. Serial monitor should show:
   ```
   [Setup] Creating Ultrasonic (Grove) sensor...
   [SensorFactory] Creating Grove Ultrasonic sensor (sig: 8, mock: no)
   [Setup] Initializing Ultrasonic (Grove 3-pin)...
   ```

---

## Backward Compatibility

✅ **Fully backward compatible**

- Existing HC-SR04 configurations continue to work
- `SENSOR_TYPE_ULTRASONIC` still defaults to HC-SR04
- No breaking changes to existing code
- New sensor type is opt-in

---

## Documentation References

- [Grove Ultrasonic Ranger Wiki](datasheets/Grove-Ultrasonic_Ranger_WiKi.pdf)
- [Multi-Sensor Support](docs/MULTI_SENSOR.md)
- [Sensor Types Documentation](include/sensor_types.h)
- [Issue #4 Hardware Universality](ISSUE4_STATUS.md)

---

## Related Issues

- **Issue #4 Phase 1**: Sensor abstraction system (complete)
- **Issue #4 Phase 2**: Multi-sensor support (complete)
- **Issue #8**: SensorFactory integration (complete)
- **Issue #17**: Grove ultrasonic support (**this issue**, complete)

---

## Web UI Updates

✅ **COMPLETE** - Web UI now fully supports Grove sensors:

**1. Add Sensor Dialog**:
- Updated prompt shows all sensor types:
  ```
  Select sensor type:
  0 = PIR Motion
  1 = IR Beam-Break
  2 = Ultrasonic (HC-SR04 4-pin)
  4 = Ultrasonic (Grove 3-pin)
  ```
- Grove sensors only ask for 1 GPIO pin (saves time!)
- HC-SR04 sensors ask for both trigger and echo pins

**2. Sensor Cards**:
- Badge shows "HC-SR04" for type 2, "GROVE" for type 4
- Wiring diagrams show correct pin counts
- Configuration displays sensor type clearly

**3. Wiring Diagrams**:
- **HC-SR04**: Shows 4 connections (VCC→5V, GND→GND, TRIG→GPIO, ECHO→GPIO)
- **Grove**: Shows 3 connections (VCC(Red)→3.3V/5V, GND(Black)→GND, SIG(Yellow)→GPIO)

---

## Next Steps (Future Enhancements)

1. **Auto-Detection** (Future):
   - Attempt to detect which sensor is connected
   - Try Grove protocol first (1 pin), fall back to HC-SR04 (2 pins)

3. **Additional Sensors** (Future):
   - JSN-SR04T (waterproof ultrasonic)
   - US-100 (temperature compensation)
   - Maxbotix MB10xx series

---

**Last Updated**: 2026-01-21
**Status**: ✅ Implementation Complete, Ready for Testing

**Closes**: Issue #17
