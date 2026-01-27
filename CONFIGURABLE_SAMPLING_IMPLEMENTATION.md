# Configurable Sensor Sampling for Faster Pedestrian Detection

**Date**: 2026-01-26
**Status**: IN PROGRESS
**Priority**: HIGH

## Problem Statement

The current hardcoded sensor sampling is too slow for real-world pedestrian detection:

### Current Configuration (Hardcoded)
- **Sample rate**: 60ms minimum (fixed)
- **Window size**: 10 samples (fixed)
- **Window fill time**: 600ms
- **Detection latency**: ~600-800ms from initial approach

### Real-World Requirements
- **Human walking speed**: ~1.25 m/s (4 ft/s)
- **Desired alert distance**: 1-2 seconds ahead = 1250-2500mm
- **Reaction time needed**: 0.5-1.0 seconds
- **Maximum acceptable latency**: 200-300ms

### The Math
At current settings:
- Window fills in 600ms
- Human walks 750mm in that time
- If detection threshold is 1500mm, alert fires at 750mm (too late!)
- User needs warning when person is still 1500-2000mm away

## Solution: Configurable Sampling

Make both sample rate and window size configurable per sensor.

### Recommended Settings for Pedestrian Detection

| Use Case | Window Size | Sample Rate | Fill Time | Distance Traveled |
|----------|-------------|-------------|-----------|-------------------|
| **Fast Alert** (recommended) | 5 | 60ms | 300ms | 375mm |
| **Balanced** | 7 | 60ms | 420ms | 525mm |
| **Smooth** (current) | 10 | 60ms | 600ms | 750mm |
| **Very Smooth** | 15 | 60ms | 900ms | 1125mm |

For your use case: **Window size = 5, Sample rate = 60ms**
- Alert triggers in 300ms
- Person has traveled only 375mm
- Can set threshold to 1500mm and still give 1+ second warning

## Implementation Status

### ✅ Completed

1. **distance_sensor_base.h** - Made window size configurable
   - Added `m_sampleWindowSize` member (default 10)
   - Added `MAX_SAMPLE_WINDOW_SIZE = 20` and `MIN_SAMPLE_WINDOW_SIZE = 3`
   - Added `setSampleWindowSize(uint8_t size)` method
   - Updated constructor to accept `windowSize` parameter

2. **distance_sensor_base.cpp** - Implemented dynamic windowing
   - Constructor accepts window size, clamps to 3-20 range
   - `setSampleWindowSize()` resets window when changed
   - Updated all window operations to use `m_sampleWindowSize` instead of constant
   - `addSampleToWindow()` uses dynamic size
   - `isMovementDetected()` uses dynamic size

3. **sensor_types.h** - Added config fields
   - Added `sampleWindowSize` to `SensorConfig`
   - Added `sampleRateMs` to `SensorConfig`

4. **config_manager.h** - Updated persistent config
   - Replaced `rapidSampleCount/rapidSampleMs` with `sampleWindowSize/sampleRateMs`

### 🚧 Remaining Work

1. **config_manager.cpp** - Update serialization
   - [ ] Update `fromJSON()` to read new fields
   - [ ] Update `toJSON()` to write new fields
   - [ ] Update `loadDefaults()` to set reasonable defaults
   - [ ] Handle migration from old `rapidSample*` fields

2. **sensor_factory.cpp** - Pass config to sensors
   - [ ] Read `sampleWindowSize` from config
   - [ ] Pass to ultrasonic sensor constructors
   - [ ] Read `sampleRateMs` and configure measurement interval

3. **hal_ultrasonic.cpp** - Accept window size
   - [ ] Update constructor to pass `windowSize` to `DistanceSensorBase`
   - [ ] Make measurement interval configurable

4. **hal_ultrasonic_grove.cpp** - Accept window size
   - [ ] Update constructor to pass `windowSize` to `DistanceSensorBase`
   - [ ] Make measurement interval configurable

5. **web_api.cpp** - Apply config changes
   - [ ] Already applies threshold changes
   - [ ] Add window size configuration
   - [ ] Add sample rate configuration

6. **Web UI** - Add configuration controls
   - [ ] Add "Sample Window Size" slider (3-20)
   - [ ] Add "Sample Rate" input (60-500ms)
   - [ ] Show estimated response time calculation
   - [ ] Warning for values that may cause issues

## Recommended Default Values

```cpp
// For pedestrian detection (fast response)
#define DEFAULT_SAMPLE_WINDOW_SIZE  5    // Quick response
#define DEFAULT_SAMPLE_RATE_MS      60   // HC-SR04 minimum

// For general motion (balanced)
#define DEFAULT_SAMPLE_WINDOW_SIZE  7    // Balanced
#define DEFAULT_SAMPLE_RATE_MS      60   // HC-SR04 minimum

// For smooth/stable (low noise)
#define DEFAULT_SAMPLE_WINDOW_SIZE  10   // Current behavior
#define DEFAULT_SAMPLE_RATE_MS      60   // HC-SR04 minimum
```

## Sensor Hardware Constraints

### HC-SR04 Ultrasonic
- **Minimum cycle time**: 60ms
- Reason: Ultrasonic pulse travel time + echo processing
- Cannot reliably go faster without false readings
- **Do NOT set sampleRateMs < 60**

### Grove Ultrasonic v2.0
- **Minimum cycle time**: 60ms
- Same constraint as HC-SR04 (similar hardware)
- **Do NOT set sampleRateMs < 60**

### PIR Sensors (Future)
- No sample rate constraint
- Can sample as fast as needed (10-50ms typical)

## Configuration Strategy

### Fast Pedestrian Alert Profile
```json
{
  "sensorType": "ULTRASONIC",
  "detectionThreshold": 1500,  // 1.5 meters
  "sampleWindowSize": 5,       // Fast response
  "sampleRateMs": 60,          // HC-SR04 minimum
  "enableDirectionDetection": true,
  "directionTriggerMode": 0    // Approaching only
}
```

**Performance:**
- Response time: ~300ms
- Person travels: 375mm during window fill
- Alert at: 1500mm (person still 1125mm away = 0.9s warning)

### Balanced Profile
```json
{
  "sensorType": "ULTRASONIC",
  "detectionThreshold": 1000,  // 1 meter
  "sampleWindowSize": 7,       // Balanced
  "sampleRateMs": 60,
  "enableDirectionDetection": true,
  "directionTriggerMode": 0
}
```

**Performance:**
- Response time: ~420ms
- Alert at: 1000mm (person still 575mm away = 0.46s warning)

### Current Default (Too Slow)
```json
{
  "sensorType": "ULTRASONIC",
  "detectionThreshold": 500,   // 0.5 meters
  "sampleWindowSize": 10,      // Slow response
  "sampleRateMs": 60,
  "enableDirectionDetection": false
}
```

**Performance:**
- Response time: ~600ms
- Alert at: 500mm (person already TOO CLOSE = < 0.1s warning)

## Testing Plan

1. **Compile Test**: Ensure all changes compile
2. **Window Size Test**: Set window to 5, verify faster response
3. **Sample Rate Test**: Verify 60ms minimum is respected
4. **Distance Test**: Set threshold to 1500mm, verify early warning
5. **Walking Test**: Have person walk toward sensor at normal pace
   - Should trigger at 1500mm
   - Should have ~1 second to react
6. **Validation**: Check serial diagnostic output shows correct window size

## Migration Strategy

When loading old configs with `rapidSampleCount/rapidSampleMs`:
1. If both are 0 or missing: Use new defaults (windowSize=5, rate=60)
2. If present: Ignore old values, use new defaults
3. Log migration message
4. Save config with new fields

## User Documentation

Add to web UI and docs:

**Sample Window Size**:
- Range: 3-20 samples
- Smaller = faster response, more noise
- Larger = slower response, smoother
- Recommended for pedestrians: 5-7
- Default: 5

**Sample Rate**:
- Range: 60-500ms
- Faster = quicker updates (but can't go below sensor minimum)
- HC-SR04/Grove limit: 60ms minimum
- Recommended: 60ms (sensor maximum speed)
- Default: 60ms

**Response Time Calculator**:
```
Response Time = Window Size × Sample Rate
Window Size 5 × 60ms = 300ms response
```

## Files to Modify

### Core Logic (Done)
- [x] include/distance_sensor_base.h
- [x] src/distance_sensor_base.cpp
- [x] include/sensor_types.h
- [x] include/config_manager.h

### Configuration (TODO)
- [ ] src/config_manager.cpp (JSON serialization)
- [ ] src/sensor_factory.cpp (pass config to sensors)

### Sensor Implementation (TODO)
- [ ] src/hal_ultrasonic.cpp (constructor update)
- [ ] src/hal_ultrasonic_grove.cpp (constructor update)
- [ ] include/hal_ultrasonic.h (add windowSize param)
- [ ] include/hal_ultrasonic_grove.h (add windowSize param)

### Web Interface (TODO)
- [ ] data/index.html (add UI controls)
- [ ] data/app.js (handle new fields)

### Documentation (TODO)
- [ ] README.md (add configuration guide)
- [ ] Update sensor configuration examples

## Next Steps

1. **Finish config_manager.cpp updates** (JSON serialization)
2. **Update sensor constructors** to accept window size
3. **Test compilation** and fix any errors
4. **Flash and test** with window size = 5
5. **Verify faster response** in pedestrian detection
6. **Update web UI** to expose configuration
7. **Document** recommended settings

---

**Current Status**: Core implementation complete, need to wire up configuration system
**Estimated Time to Complete**: 2-3 hours
**Testing Required**: Yes - real-world pedestrian walking tests
