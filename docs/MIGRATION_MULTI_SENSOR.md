# Migration Guide: Single Sensor to Multi-Sensor

This guide helps you migrate existing StepAware code from single-sensor to multi-sensor architecture.

## Overview

The multi-sensor system is **backward compatible** - existing single-sensor code continues to work. This guide shows you how to migrate when you're ready.

## Quick Migration (Minimal Changes)

### Before (Single Sensor)
```cpp
#include "sensor_factory.h"

HAL_MotionSensor* motionSensor = nullptr;

void setup() {
    // Create single sensor
    motionSensor = SensorFactory::createFromType(SENSOR_TYPE_PIR, MOCK_HARDWARE);
    motionSensor->begin();

    // Use in state machine
    stateMachine = new StateMachine(motionSensor, &hazardLED, &statusLED, &modeButton);
}

void loop() {
    motionSensor->update();
    stateMachine->update();
}
```

### After (Multi-Sensor, Single Sensor Slot)
```cpp
#include "sensor_manager.h"

SensorManager sensorMgr;

void setup() {
    sensorMgr.begin();

    // Add single sensor to slot 0
    SensorConfig config;
    config.type = SENSOR_TYPE_PIR;
    config.primaryPin = 5;
    sensorMgr.addSensor(0, config, "PIR Sensor", true, MOCK_HARDWARE);

    // Use primary sensor in state machine
    HAL_MotionSensor* primarySensor = sensorMgr.getPrimarySensor();
    stateMachine = new StateMachine(primarySensor, &hazardLED, &statusLED, &modeButton);
}

void loop() {
    sensorMgr.update();  // Updates all sensors
    stateMachine->update();
}
```

**Benefits of Migration:**
- ✅ Same functionality as before
- ✅ Ready to add more sensors when needed
- ✅ Centralized sensor management
- ✅ Better error handling

## Step-by-Step Migration

### Step 1: Include Header
Replace:
```cpp
#include "sensor_factory.h"
```

With:
```cpp
#include "sensor_manager.h"
```

### Step 2: Change Global Variables
Replace:
```cpp
HAL_MotionSensor* motionSensor = nullptr;
```

With:
```cpp
SensorManager sensorMgr;
```

### Step 3: Update Sensor Creation
Replace:
```cpp
motionSensor = SensorFactory::createFromType(SENSOR_TYPE_PIR, MOCK_HARDWARE);
if (!motionSensor) {
    Serial.println("Failed to create sensor");
    while(1);
}
motionSensor->begin();
```

With:
```cpp
if (!sensorMgr.begin()) {
    Serial.println("Failed to initialize sensor manager");
    while(1);
}

SensorConfig config;
config.type = SENSOR_TYPE_PIR;
config.primaryPin = PIN_MOTION_SENSOR;
config.warmupMs = PIR_WARMUP_TIME_MS;

if (!sensorMgr.addSensor(0, config, "Motion Sensor", true, MOCK_HARDWARE)) {
    Serial.println("Failed to add sensor");
    while(1);
}
```

### Step 4: Update References
Replace all `motionSensor->` calls with the appropriate pattern:

**For motion detection:**
```cpp
// Before
if (motionSensor->isMotionDetected()) {
    // Handle motion
}

// After (Option 1: Direct sensor access)
HAL_MotionSensor* sensor = sensorMgr.getSensor(0);
if (sensor && sensor->isMotionDetected()) {
    // Handle motion
}

// After (Option 2: Use fusion)
if (sensorMgr.isMotionDetected()) {
    // Handle motion
}
```

**For sensor status:**
```cpp
// Before
if (motionSensor->isReady()) {
    // Sensor ready
}

// After
if (sensorMgr.allSensorsReady()) {
    // All sensors ready
}

// Or for specific sensor
HAL_MotionSensor* sensor = sensorMgr.getSensor(0);
if (sensor && sensor->isReady()) {
    // Specific sensor ready
}
```

**For StateMachine integration:**
```cpp
// Before
stateMachine = new StateMachine(motionSensor, &hazardLED, &statusLED, &modeButton);

// After
HAL_MotionSensor* primarySensor = sensorMgr.getPrimarySensor();
stateMachine = new StateMachine(primarySensor, &hazardLED, &statusLED, &modeButton);
```

### Step 5: Update Loop
Replace:
```cpp
void loop() {
    motionSensor->update();
    // ... rest of loop
}
```

With:
```cpp
void loop() {
    sensorMgr.update();  // Updates all sensors automatically
    // ... rest of loop
}
```

### Step 6: Update Status Printing
Replace:
```cpp
Serial.printf("Sensor: %s\n", motionSensor->getCapabilities().sensorTypeName);
Serial.printf("Ready: %s\n", motionSensor->isReady() ? "YES" : "NO");
Serial.printf("Motion: %s\n", motionSensor->isMotionDetected() ? "YES" : "NO");
```

With:
```cpp
sensorMgr.printStatus();  // Prints all sensor info
```

## Adding Additional Sensors

Once migrated, adding sensors is easy:

### Add Second PIR (Different Direction)
```cpp
void setup() {
    // ... existing sensor in slot 0 ...

    // Add second PIR
    SensorConfig config2;
    config2.type = SENSOR_TYPE_PIR;
    config2.primaryPin = PIN_MOTION_SENSOR_2;
    sensorMgr.addSensor(1, config2, "Rear PIR", false, MOCK_HARDWARE);

    // Set fusion mode
    sensorMgr.setFusionMode(FUSION_MODE_ANY);  // Trigger if either detects
}
```

### Add Ultrasonic for Distance
```cpp
void setup() {
    // ... existing PIR in slot 0 ...

    // Add ultrasonic
    SensorConfig usConfig;
    usConfig.type = SENSOR_TYPE_ULTRASONIC;
    usConfig.primaryPin = PIN_TRIGGER;
    usConfig.secondaryPin = PIN_ECHO;
    usConfig.detectionThreshold = 300;  // 30cm
    usConfig.enableDirectionDetection = true;

    sensorMgr.addSensor(1, usConfig, "Distance Sensor", false, MOCK_HARDWARE);

    // PIR triggers, ultrasonic measures
    sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
}
```

## Common Patterns

### Pattern 1: Get Combined Status
```cpp
void loop() {
    sensorMgr.update();

    CombinedSensorStatus status = sensorMgr.getStatus();

    Serial.printf("Active sensors: %u\n", status.activeSensorCount);
    Serial.printf("Detecting: %u\n", status.detectingSensorCount);

    if (status.anyMotionDetected) {
        Serial.println("Motion detected!");

        if (status.nearestDistance > 0) {
            Serial.printf("Distance: %u mm\n", status.nearestDistance);
        }
    }
}
```

### Pattern 2: Access Individual Sensors
```cpp
void loop() {
    sensorMgr.update();

    // Access specific sensors
    HAL_MotionSensor* pir = sensorMgr.getSensor(0);
    HAL_MotionSensor* ultrasonic = sensorMgr.getSensor(1);

    if (pir && pir->isMotionDetected()) {
        Serial.println("PIR triggered");

        if (ultrasonic) {
            uint32_t dist = ultrasonic->getDistance();
            Serial.printf("Distance: %u mm\n", dist);
        }
    }
}
```

### Pattern 3: Power Management
```cpp
void loop() {
    sensorMgr.update();

    HAL_MotionSensor* trigger = sensorMgr.getSensor(0);  // Low-power PIR
    HAL_MotionSensor* measure = sensorMgr.getSensor(1);  // High-power ultrasonic

    if (trigger && trigger->isMotionDetected()) {
        // Enable measurement sensor
        sensorMgr.setSensorEnabled(1, true);
        measure->update();

        // Use measurement
        uint32_t dist = measure->getDistance();
        // ... handle distance ...

    } else {
        // Disable measurement sensor to save power
        sensorMgr.setSensorEnabled(1, false);
    }
}
```

## Troubleshooting

### Issue: "Failed to add sensor"
**Cause**: Sensor creation failed or invalid slot

**Solution**:
```cpp
if (!sensorMgr.addSensor(0, config, "Sensor", true)) {
    Serial.println(sensorMgr.getLastError());  // Print error
}
```

### Issue: No motion detected after migration
**Cause**: Fusion mode mismatch or primary sensor not set

**Solution**:
```cpp
// Verify configuration
if (!sensorMgr.validateConfiguration()) {
    Serial.println(sensorMgr.getLastError());
}

// Print status to debug
sensorMgr.printStatus();

// Ensure correct fusion mode
sensorMgr.setFusionMode(FUSION_MODE_ANY);  // Start with ANY mode
```

### Issue: Sensor not updating
**Cause**: Forgot to call `sensorMgr.update()`

**Solution**:
```cpp
void loop() {
    sensorMgr.update();  // MUST call this every loop
    // ... rest of loop
}
```

### Issue: "Invalid configuration" error
**Cause**: TRIGGER_MEASURE mode requires 2+ sensors and primary sensor

**Solution**:
```cpp
// Ensure at least 2 sensors
sensorMgr.addSensor(0, config1, "Sensor 1", true);   // Primary
sensorMgr.addSensor(1, config2, "Sensor 2", false);  // Secondary

// Then set fusion mode
sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
```

## Performance Considerations

### Memory Usage
- Each sensor slot: ~100 bytes
- SensorManager overhead: ~200 bytes
- **Total**: ~600 bytes for 4 sensor slots

### CPU Usage
- `update()` iterates all enabled sensors
- Minimal overhead per sensor (~10µs)
- Fusion logic: ~5µs

### Recommended Approach
1. Start with single sensor in SensorManager
2. Test thoroughly
3. Add additional sensors one at a time
4. Verify each sensor works individually before enabling fusion

## Complete Example

Here's a complete before/after example:

### Before: main.cpp (Single Sensor)
```cpp
#include "sensor_factory.h"

HAL_MotionSensor* motionSensor = nullptr;
StateMachine* stateMachine = nullptr;

void setup() {
    Serial.begin(115200);

    // Create PIR sensor
    motionSensor = SensorFactory::createFromType(SENSOR_TYPE_PIR, false);
    if (!motionSensor || !motionSensor->begin()) {
        Serial.println("Sensor init failed");
        while(1);
    }

    // Create state machine
    stateMachine = new StateMachine(motionSensor, &led, &statusLED, &button);
    stateMachine->begin(StateMachine::MODE_MOTION_DETECT);
}

void loop() {
    motionSensor->update();
    stateMachine->update();
}
```

### After: main.cpp (Multi-Sensor Ready)
```cpp
#include "sensor_manager.h"

SensorManager sensorMgr;
StateMachine* stateMachine = nullptr;

void setup() {
    Serial.begin(115200);

    // Initialize sensor manager
    if (!sensorMgr.begin()) {
        Serial.println("SensorManager init failed");
        while(1);
    }

    // Add PIR sensor to slot 0
    SensorConfig pirCfg;
    pirCfg.type = SENSOR_TYPE_PIR;
    pirCfg.primaryPin = 5;
    pirCfg.warmupMs = 60000;

    if (!sensorMgr.addSensor(0, pirCfg, "PIR", true, false)) {
        Serial.printf("Failed: %s\n", sensorMgr.getLastError());
        while(1);
    }

    // Create state machine with primary sensor
    HAL_MotionSensor* primary = sensorMgr.getPrimarySensor();
    stateMachine = new StateMachine(primary, &led, &statusLED, &button);
    stateMachine->begin(StateMachine::MODE_MOTION_DETECT);

    // Print initial status
    sensorMgr.printStatus();
}

void loop() {
    sensorMgr.update();  // Updates all sensors
    stateMachine->update();
}
```

**Now you can easily add more sensors:**
```cpp
void setup() {
    // ... existing PIR setup ...

    // Add ultrasonic for distance measurement
    SensorConfig usCfg;
    usCfg.type = SENSOR_TYPE_ULTRASONIC;
    usCfg.primaryPin = 12;
    usCfg.secondaryPin = 14;
    usCfg.detectionThreshold = 300;
    sensorMgr.addSensor(1, usCfg, "Distance", false, false);

    // Set trigger+measure mode
    sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
}
```

## Next Steps

1. **Test single sensor** in SensorManager first
2. **Validate** functionality matches original behavior
3. **Add sensors** one at a time when needed
4. **Experiment** with fusion modes
5. **Monitor** performance and power consumption

## See Also

- [Multi-Sensor Documentation](MULTI_SENSOR.md)
- [Sensor Types Reference](../include/sensor_types.h)
- [Issue #4 - Hardware Universality](https://github.com/yourusername/stepaware/issues/4)
