# Multi-Sensor Support (Issue #4 Phase 2)

## Overview

StepAware now supports using **multiple sensors simultaneously** for enhanced motion detection capabilities. Up to 4 sensors can be configured to work independently or in combination.

## Features

### Sensor Slots
- **4 independent sensor slots** (0-3)
- Each slot can hold any supported sensor type (PIR, IR, Ultrasonic)
- Individual enable/disable control
- Named sensors for easy identification
- Primary sensor designation

### Sensor Types Supported
- **PIR (Passive Infrared)**: Motion detection, wide angle
- **IR (Infrared)**: Beam-break detection, narrow beam
- **Ultrasonic**: Distance measurement, direction detection

### Fusion Modes

The SensorManager supports multiple fusion modes for combining sensor data:

#### 1. ANY Mode (Default)
Motion is detected if **any** enabled sensor triggers.

**Use Case**: Maximum coverage - trigger warning if any sensor detects motion
```cpp
sensorManager.setFusionMode(FUSION_MODE_ANY);
```

#### 2. ALL Mode
Motion is detected only if **all** enabled sensors trigger.

**Use Case**: Reduce false positives - require confirmation from multiple sensors
```cpp
sensorManager.setFusionMode(FUSION_MODE_ALL);
```

#### 3. TRIGGER + MEASURE Mode
Primary sensor triggers detection, secondary sensor(s) provide measurement data.

**Use Case**: Low-power PIR trigger → High-precision ultrasonic measurement
```cpp
sensorManager.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
```

#### 4. INDEPENDENT Mode
Sensors operate independently, primary sensor determines motion state.

**Use Case**: Multiple coverage zones with separate processing
```cpp
sensorManager.setFusionMode(FUSION_MODE_INDEPENDENT);
```

## Common Use Cases

### 1. Multi-Directional Coverage
**Scenario**: Hallway with traffic from both directions

```cpp
SensorManager sensorMgr;
sensorMgr.begin();

// PIR facing left
SensorConfig leftPIR;
leftPIR.type = SENSOR_TYPE_PIR;
leftPIR.primaryPin = 5;
sensorMgr.addSensor(0, leftPIR, "Left PIR", true);

// PIR facing right
SensorConfig rightPIR;
rightPIR.type = SENSOR_TYPE_PIR;
rightPIR.primaryPin = 6;
sensorMgr.addSensor(1, rightPIR, "Right PIR", false);

// ANY mode: trigger if either side detects motion
sensorMgr.setFusionMode(FUSION_MODE_ANY);
```

### 2. Trigger + Measurement Combo
**Scenario**: Low-power PIR trigger with precise ultrasonic distance

```cpp
SensorManager sensorMgr;
sensorMgr.begin();

// PIR as trigger (low power, always on)
SensorConfig pirConfig;
pirConfig.type = SENSOR_TYPE_PIR;
pirConfig.primaryPin = 5;
sensorMgr.addSensor(0, pirConfig, "PIR Trigger", true);

// Ultrasonic for distance (higher power, only when triggered)
SensorConfig usConfig;
usConfig.type = SENSOR_TYPE_ULTRASONIC;
usConfig.primaryPin = 12;  // Trigger pin
usConfig.secondaryPin = 14; // Echo pin
usConfig.detectionThreshold = 300;  // 30cm threshold
usConfig.enableDirectionDetection = true;
sensorMgr.addSensor(1, usConfig, "Distance Sensor", false);

sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
```

### 3. Redundant Detection
**Scenario**: Critical application requiring multiple sensor confirmation

```cpp
SensorManager sensorMgr;
sensorMgr.begin();

// Primary PIR
SensorConfig pir1;
pir1.type = SENSOR_TYPE_PIR;
pir1.primaryPin = 5;
sensorMgr.addSensor(0, pir1, "PIR Primary", true);

// Secondary PIR for confirmation
SensorConfig pir2;
pir2.type = SENSOR_TYPE_PIR;
pir2.primaryPin = 6;
sensorMgr.addSensor(1, pir2, "PIR Secondary", false);

// ALL mode: both sensors must agree
sensorMgr.setFusionMode(FUSION_MODE_ALL);
```

### 4. Direction-Aware Detection
**Scenario**: Detect approaching vs. departing motion

```cpp
SensorManager sensorMgr;
sensorMgr.begin();

// Ultrasonic with direction detection
SensorConfig usConfig;
usConfig.type = SENSOR_TYPE_ULTRASONIC;
usConfig.primaryPin = 12;
usConfig.secondaryPin = 14;
usConfig.detectionThreshold = 500;  // 50cm
usConfig.enableDirectionDetection = true;
sensorMgr.addSensor(0, usConfig, "US Directional", true);

// In main loop
sensorMgr.update();
if (sensorMgr.isMotionDetected()) {
    MotionDirection dir = sensorMgr.getPrimaryDirection();
    if (dir == DIRECTION_APPROACHING) {
        // Person walking toward sensor - activate warning
    } else if (dir == DIRECTION_RECEDING) {
        // Person walking away - disable or shorten warning
    }
}
```

## API Reference

### SensorManager Class

#### Initialization
```cpp
SensorManager sensorMgr;
bool success = sensorMgr.begin();
```

#### Adding Sensors
```cpp
bool addSensor(uint8_t slotIndex,           // 0-3
               const SensorConfig& config,   // Sensor configuration
               const char* name,             // User-defined name
               bool isPrimary,               // Primary sensor flag
               bool mockMode);               // Mock mode for testing
```

#### Removing Sensors
```cpp
bool removeSensor(uint8_t slotIndex);
```

#### Enable/Disable Sensors
```cpp
bool setSensorEnabled(uint8_t slotIndex, bool enabled);
```

#### Fusion Mode
```cpp
void setFusionMode(SensorFusionMode mode);
SensorFusionMode getFusionMode() const;
```

#### Motion Detection
```cpp
bool isMotionDetected();  // Based on fusion mode
```

#### Status Information
```cpp
CombinedSensorStatus getStatus();
uint8_t getActiveSensorCount() const;
bool allSensorsReady();
uint32_t getNearestDistance();
MotionDirection getPrimaryDirection();
```

#### Utility Functions
```cpp
void update();                    // Call in main loop
void resetEventCounts();
void printStatus();              // Print to Serial
bool validateConfiguration();
```

### SensorConfig Structure

```cpp
struct SensorConfig {
    SensorType type;                 // SENSOR_TYPE_PIR, IR, ULTRASONIC
    uint8_t primaryPin;              // Motion/trigger pin
    uint8_t secondaryPin;            // Echo pin (ultrasonic only)
    uint32_t detectionThreshold;     // Distance threshold (mm)
    uint32_t debounceMs;             // Debounce time
    uint32_t warmupMs;               // Warmup override
    bool enableDirectionDetection;   // Enable direction (if supported)
    bool invertLogic;                // Invert detection logic
};
```

### CombinedSensorStatus Structure

```cpp
struct CombinedSensorStatus {
    bool anyMotionDetected;          // At least one detecting
    bool allMotionDetected;          // All sensors detecting
    uint8_t activeSensorCount;       // Enabled sensors
    uint8_t detectingSensorCount;    // Currently detecting
    uint32_t nearestDistance;        // Closest distance (mm)
    MotionDirection primaryDirection;// Direction from primary
    uint32_t combinedEventCount;     // Total events
};
```

## Integration Example

### Complete Multi-Sensor Setup

```cpp
#include "sensor_manager.h"

SensorManager sensorMgr;

void setup() {
    Serial.begin(115200);

    // Initialize sensor manager
    if (!sensorMgr.begin()) {
        Serial.println("Failed to initialize sensor manager");
        while(1);
    }

    // Add PIR sensor (primary)
    SensorConfig pirCfg;
    pirCfg.type = SENSOR_TYPE_PIR;
    pirCfg.primaryPin = 5;
    pirCfg.warmupMs = 60000;  // 60 second warmup

    if (!sensorMgr.addSensor(0, pirCfg, "Front PIR", true)) {
        Serial.println("Failed to add PIR sensor");
    }

    // Add ultrasonic sensor (secondary)
    SensorConfig usCfg;
    usCfg.type = SENSOR_TYPE_ULTRASONIC;
    usCfg.primaryPin = 12;
    usCfg.secondaryPin = 14;
    usCfg.detectionThreshold = 300;  // 30cm
    usCfg.enableDirectionDetection = true;

    if (!sensorMgr.addSensor(1, usCfg, "Distance Sensor", false)) {
        Serial.println("Failed to add ultrasonic sensor");
    }

    // Set fusion mode
    sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);

    // Validate configuration
    if (!sensorMgr.validateConfiguration()) {
        Serial.print("Invalid configuration: ");
        Serial.println(sensorMgr.getLastError());
    }

    // Print initial status
    sensorMgr.printStatus();
}

void loop() {
    // Update all sensors
    sensorMgr.update();

    // Check for motion
    if (sensorMgr.isMotionDetected()) {
        CombinedSensorStatus status = sensorMgr.getStatus();

        Serial.println("Motion detected!");
        Serial.printf("  Sensors detecting: %u / %u\n",
                     status.detectingSensorCount,
                     status.activeSensorCount);

        if (status.nearestDistance > 0) {
            Serial.printf("  Distance: %u mm\n", status.nearestDistance);
        }

        if (status.primaryDirection == DIRECTION_APPROACHING) {
            Serial.println("  Direction: Approaching");
        } else if (status.primaryDirection == DIRECTION_RECEDING) {
            Serial.println("  Direction: Receding");
        }
    }

    delay(100);
}
```

## Power Consumption Considerations

### Low Power Strategy: PIR Trigger + Ultrasonic Measure

This combination provides excellent power efficiency:

1. **PIR sensor** (~65µA): Always on, low power
2. **Ultrasonic sensor** (~15mA): Only activated when PIR triggers

**Implementation**:
```cpp
void loop() {
    sensorMgr.update();

    HAL_MotionSensor* pir = sensorMgr.getSensor(0);  // Primary PIR
    HAL_MotionSensor* ultrasonic = sensorMgr.getSensor(1);

    if (pir && pir->isMotionDetected()) {
        // PIR triggered - enable ultrasonic for measurement
        sensorMgr.setSensorEnabled(1, true);
        ultrasonic->update();

        uint32_t distance = ultrasonic->getDistance();
        if (distance < 300) {  // Within 30cm
            // Activate warning
        }
    } else {
        // No motion - disable ultrasonic to save power
        sensorMgr.setSensorEnabled(1, false);
    }
}
```

## Configuration Persistence

Multi-sensor configurations can be saved to ConfigManager for persistence across reboots. This functionality will be added in a future update.

**Planned structure**:
```json
{
  "sensors": [
    {
      "slot": 0,
      "enabled": true,
      "isPrimary": true,
      "name": "Front PIR",
      "type": 0,
      "primaryPin": 5,
      "config": { /* sensor-specific config */ }
    },
    {
      "slot": 1,
      "enabled": true,
      "isPrimary": false,
      "name": "Distance Sensor",
      "type": 2,
      "primaryPin": 12,
      "secondaryPin": 14,
      "config": { /* sensor-specific config */ }
    }
  ],
  "fusionMode": 2
}
```

## Debugging and Monitoring

### Print Status
```cpp
sensorMgr.printStatus();
```

**Output**:
```
========== Sensor Manager Status ==========
Active Sensors: 2 / 4
Fusion Mode: TRIGGER_MEASURE

Slot 0: Front PIR [PRIMARY]
  Type: PIR Motion Sensor
  Enabled: YES
  Ready: YES
  Motion: NO
  Events: 15

Slot 1: Distance Sensor
  Type: Ultrasonic Distance Sensor
  Enabled: YES
  Ready: YES
  Motion: NO
  Distance: 1250 mm
  Direction: Stationary
  Events: 8

Combined Status:
  Any Motion: NO
  All Motion: NO
  Detecting: 0 / 2
  Total Events: 23
===========================================
```

### Error Handling
```cpp
if (!sensorMgr.addSensor(0, config, "Test")) {
    Serial.println(sensorMgr.getLastError());
}
```

## Limitations

- Maximum **4 sensors** simultaneously
- Only **one primary sensor** allowed
- TRIGGER_MEASURE mode requires at least 2 sensors
- Direction detection requires sensor with that capability
- Distance measurement requires sensor with that capability

## Future Enhancements

- [ ] Configuration persistence via ConfigManager
- [ ] Web UI for multi-sensor configuration
- [ ] Serial commands for runtime sensor management
- [ ] Event-based sensor triggering
- [ ] Sensor health monitoring
- [ ] Dynamic power management per sensor

## See Also

- [Sensor Types Documentation](sensor_types.h)
- [Sensor Factory Documentation](sensor_factory.h)
- [Hardware Abstraction Layer](../README.md#hal)
- [Issue #4 - Hardware Universality](https://github.com/yourusername/stepaware/issues/4)
