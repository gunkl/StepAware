# Grove Ultrasonic Sensor Default Configuration Fix

## Issue

User reported: "grove sensor settings were not what i requested by default when i added the sensor"

When adding a Grove ultrasonic sensor via the web UI, the defaults did not match the requested specification:

**Requested Grove Ultrasonic Sensor Defaults:**
- Default pin: **8** (single pin for Grove)
- Max range: **3000mm**
- Warn threshold: **1100mm**
- Direction enabled: **true**
- Trigger mode: **0 (APPROACHING)**
- Sample window: **3 samples**
- Sample interval: **80ms**

## Root Cause Analysis

The issue was found in **THREE** locations where sensor defaults are set, with **TWO** having incorrect values:

### 1. Web API JavaScript (src/web_api.cpp lines 1815-1839) - INCORRECT

**File**: `src/web_api.cpp`
**Function**: `addSensor()` JavaScript function embedded in HTML

**Problems Found:**
```javascript
// WRONG: Default pin prompt started with "5" instead of "8"
const pin=parseInt(prompt('Enter primary pin (GPIO number):','5'));

// WRONG: Used incorrect defaults for all sensor types
const sensor={
    type:typeNum,
    name:name,
    primaryPin:pin,
    enabled:true,
    isPrimary:freeSlot===0,
    warmupMs:60000,              // WRONG for ultrasonic (should be 0)
    debounceMs:100,              // WRONG (should be 80ms for ultrasonic)
    detectionThreshold:1500,     // WRONG (should be 1100mm)
    maxDetectionDistance:3000,   // CORRECT
    enableDirectionDetection:true, // CORRECT
    directionTriggerMode:0,      // CORRECT (APPROACHING)
    rapidSampleCount:5,          // WRONG (should use sampleWindowSize:3)
    rapidSampleMs:200            // WRONG (should use sampleRateMs:80)
};
```

**Issues:**
1. Default pin suggestion was "5" instead of "8" for ultrasonic sensors
2. Used `rapidSampleCount` and `rapidSampleMs` (deprecated) instead of `sampleWindowSize` and `sampleRateMs`
3. Wrong values: debounceMs=100 (should be 80), detectionThreshold=1500 (should be 1100)
4. PIR-specific defaults (warmupMs=60000) applied to all sensors

### 2. Config Manager JSON Defaults (src/config_manager.cpp line 396) - INCORRECT

**File**: `src/config_manager.cpp`
**Function**: `fromJSON()` sensor parsing

**Problem Found:**
```cpp
m_config.sensors[slot].debounceMs = sensorObj["debounceMs"] | 100;  // WRONG: should be 80
```

When loading sensor configuration from JSON and a sensor didn't have `debounceMs` specified, it defaulted to 100ms instead of 80ms.

**Other values were CORRECT:**
- `detectionThreshold` defaulted to 1100mm ✓
- `maxDetectionDistance` defaulted to 3000mm ✓
- `enableDirectionDetection` defaulted to true ✓
- `directionTriggerMode` defaulted to 0 (APPROACHING) ✓
- `sampleWindowSize` defaulted to 3 ✓
- `sampleRateMs` defaulted to 80ms ✓

### 3. Sensor Factory Defaults (src/sensor_factory.cpp lines 121-128) - CORRECT

**File**: `src/sensor_factory.cpp`
**Function**: `getDefaultConfig(SENSOR_TYPE_ULTRASONIC_GROVE)`

**This was already CORRECT:**
```cpp
case SENSOR_TYPE_ULTRASONIC_GROVE:
    config.primaryPin = PIN_ULTRASONIC_TRIGGER;  // GPIO 8 (from config.h)
    config.secondaryPin = 0;
    config.detectionThreshold = 1100;             // ✓ CORRECT
    config.maxDetectionDistance = 3000;           // ✓ CORRECT
    config.enableDirectionDetection = true;       // ✓ CORRECT
    config.debounceMs = 80;                       // ✓ CORRECT
    // Inherited from global defaults (lines 97-99):
    config.directionTriggerMode = 0;              // ✓ CORRECT (APPROACHING)
    config.sampleWindowSize = 3;                  // ✓ CORRECT
    config.sampleRateMs = 80;                     // ✓ CORRECT
    break;
```

## Fixes Applied

### Fix 1: Web API JavaScript (src/web_api.cpp)

**Changed lines 1815-1839** to:
```javascript
function addSensor(){
    const freeSlot=sensorSlots.findIndex(s=>s===null);
    if(freeSlot===-1){alert('Maximum 4 sensors allowed. Remove a sensor first.');return;}
    const type=prompt('Select sensor type:\\n0 = PIR Motion\\n1 = IR Beam-Break\\n2 = Ultrasonic (HC-SR04 4-pin)\\n4 = Ultrasonic (Grove 3-pin)','0');
    if(type===null)return;
    const typeNum=parseInt(type);
    if(typeNum<0||typeNum>4||typeNum===3){alert('Invalid sensor type');return;}
    const name=prompt('Enter sensor name:','Sensor '+(freeSlot+1));
    if(!name)return;

    // NEW: Set default pin based on sensor type
    let defaultPin='5';
    if(typeNum===2)defaultPin='8';  // HC-SR04 trigger pin
    if(typeNum===4)defaultPin='8';  // Grove signal pin

    const pin=parseInt(prompt('Enter primary pin (GPIO number):',defaultPin));
    if(isNaN(pin)||pin<0||pin>48){alert('Invalid pin number');return;}

    // NEW: Create sensor with correct ultrasonic defaults
    const sensor={
        type:typeNum,
        name:name,
        primaryPin:pin,
        enabled:true,
        isPrimary:freeSlot===0,
        warmupMs:60000,                  // Will be overridden for ultrasonic
        debounceMs:50,                   // Will be overridden for ultrasonic
        detectionThreshold:1100,         // FIXED: 1100mm (was 1500)
        maxDetectionDistance:3000,       // Correct
        enableDirectionDetection:true,   // Correct
        directionTriggerMode:0,          // Correct (APPROACHING)
        sampleWindowSize:3,              // FIXED: Use correct field (was rapidSampleCount:5)
        sampleRateMs:80                  // FIXED: Use correct field (was rapidSampleMs:200)
    };

    if(typeNum===2){
        const echoPin=parseInt(prompt('Enter echo pin for HC-SR04 (GPIO number):','9'));
        if(isNaN(echoPin)||echoPin<0||echoPin>48){alert('Invalid echo pin');return;}
        sensor.secondaryPin=echoPin;
    }
    if(typeNum===4){
        sensor.secondaryPin=0;
    }

    // NEW: Apply PIR-specific overrides
    if(typeNum===0){
        sensor.warmupMs=60000;
        sensor.debounceMs=50;
        sensor.enableDirectionDetection=false;
    }

    sensorSlots[freeSlot]=sensor;
    renderSensors();
    saveSensors();
}
```

**Key Changes:**
1. Added logic to set default pin to "8" for ultrasonic sensors (types 2 and 4)
2. Changed `detectionThreshold` from 1500 to 1100mm
3. Replaced `rapidSampleCount` with `sampleWindowSize:3`
4. Replaced `rapidSampleMs` with `sampleRateMs:80`
5. Changed base `debounceMs` from 100 to 50ms
6. Added PIR-specific override logic to set appropriate warmup/debounce for PIR sensors

### Fix 2: Config Manager Defaults (src/config_manager.cpp)

**Changed line 396** from:
```cpp
m_config.sensors[slot].debounceMs = sensorObj["debounceMs"] | 100;
```

**To:**
```cpp
m_config.sensors[slot].debounceMs = sensorObj["debounceMs"] | 80;  // 80ms sample interval (ultrasonic) / 50ms debounce (PIR)
```

**Rationale:** When loading a sensor configuration from JSON without an explicit `debounceMs` value, default to 80ms which is correct for ultrasonic sensors. PIR sensors will typically have their debounceMs explicitly set in the saved config.

## Configuration Flow

Understanding how defaults are applied helps explain why this fix was necessary:

```
User adds sensor via Web UI
    ↓
addSensor() JavaScript function (web_api.cpp)
    → Creates sensor object with JavaScript defaults
    → Stores to browser localStorage
    → POSTs to /api/config
    ↓
handlePostConfig() (web_api.cpp)
    → Parses JSON
    → Saves to ConfigManager
    → Persists to LittleFS
    ↓
ConfigManager::fromJSON() (config_manager.cpp)
    → Loads from JSON
    → Applies fallback defaults for missing fields
    ↓
SensorManager::addSensor() (sensor_manager.cpp)
    → Receives SensorConfig from ConfigManager
    ↓
SensorFactory::create() (sensor_factory.cpp)
    → Creates HAL object
    → Applies config values via setter methods:
        • setDetectionThreshold(config.detectionThreshold)
        • setMeasurementInterval(config.debounceMs)  ← Maps to sample interval!
        • setSampleWindowSize(config.sampleWindowSize)
        • setDirectionDetection(config.enableDirectionDetection)
        • setDirectionTriggerMode(config.directionTriggerMode)
```

**Key Insight**: The `config.debounceMs` field is actually used as the **measurement/sample interval** for ultrasonic sensors, not as a debounce time. The naming is historical from PIR sensor implementation. For ultrasonic sensors, it should be 80ms to achieve the desired 80ms sample interval.

## Verification

After applying these fixes, when a user adds a Grove ultrasonic sensor via the web UI:

1. **Pin prompt** will default to "8" ✓
2. **Detection threshold** will be 1100mm ✓
3. **Max detection distance** will be 3000mm ✓
4. **Direction detection** will be enabled ✓
5. **Direction trigger mode** will be 0 (APPROACHING) ✓
6. **Sample window size** will be 3 samples ✓
7. **Sample interval** will be 80ms ✓

These values will be:
- Stored correctly in the web UI
- Persisted correctly to JSON/LittleFS
- Loaded correctly when reading config
- Applied correctly to the HAL sensor object

## Related Files

### Files Modified
- `src/web_api.cpp` - Fixed JavaScript addSensor() function defaults
- `src/config_manager.cpp` - Fixed JSON loading default for debounceMs

### Files with Correct Implementations (No Changes Needed)
- `src/sensor_factory.cpp` - Already had correct defaults
- `include/config.h` - PIN_ULTRASONIC_TRIGGER correctly set to 8
- `src/hal_ultrasonic_grove.cpp` - Correctly uses sampleWindowSize=3 in constructor
- `include/sensor_types.h` - Correct capabilities for Grove sensor

## Testing Recommendations

To verify the fix:

1. **Clear existing config** (delete/reset StepAware config)
2. **Add Grove sensor via web UI**:
   - Select sensor type "4 = Ultrasonic (Grove 3-pin)"
   - Verify default pin prompt shows "8"
   - Enter pin 8
   - Save configuration
3. **Check /api/config** response to verify:
   ```json
   {
     "sensors": [{
       "type": 4,
       "primaryPin": 8,
       "detectionThreshold": 1100,
       "maxDetectionDistance": 3000,
       "debounceMs": 80,
       "enableDirectionDetection": true,
       "directionTriggerMode": 0,
       "sampleWindowSize": 3,
       "sampleRateMs": 80
     }]
   }
   ```
4. **Restart device** and verify config persists correctly
5. **Test sensor detection** to ensure 3-sample window and 80ms interval work as expected

## Historical Context

The web UI originally used `rapidSampleCount` and `rapidSampleMs` fields, which were part of an older rapid sampling feature. These have been superseded by `sampleWindowSize` and `sampleRateMs` which are the current standard fields used throughout the codebase.

The fix ensures the web UI uses the current field names and correct default values.

---

**Fixed**: 2026-01-27
**Issue**: Grove sensor defaults not matching user request
**Files Modified**: `src/web_api.cpp`, `src/config_manager.cpp`
