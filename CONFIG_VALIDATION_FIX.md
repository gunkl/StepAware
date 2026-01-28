# Configuration Validation and Correction System

## Issue Summary

**Root Cause**: Configuration fields were not being properly copied from `SensorSlotConfig` to `SensorConfig` during sensor initialization in main.cpp, causing uninitialized memory (garbage values like 165, 42405) to be used for sensor configuration parameters.

**Symptoms**:
- `directionTriggerMode` showing as 165 instead of 0
- `sampleRateMs` showing as 42405 instead of 75
- `directionSensitivity` showing as 42405 instead of 0
- Sensors behaving incorrectly (triggering on wrong directions)

## Fixes Implemented

### 1. Fixed Missing Field Assignments in main.cpp

**File**: `src/main.cpp` (lines 762-775)

**Problem**: When converting from `ConfigManager::SensorSlotConfig` to `SensorConfig`, several critical fields were not being copied:
- `maxDetectionDistance`
- `directionTriggerMode` ← **Primary bug**
- `directionSensitivity`
- `sampleWindowSize`
- `sampleRateMs`

**Solution**: Added all missing field assignments:

```cpp
SensorConfig config;
config.type = static_cast<SensorType>(sensorCfg.type);
config.primaryPin = sensorCfg.primaryPin;
config.secondaryPin = sensorCfg.secondaryPin;
config.detectionThreshold = sensorCfg.detectionThreshold;
config.maxDetectionDistance = sensorCfg.maxDetectionDistance;        // ADDED
config.debounceMs = sensorCfg.debounceMs;
config.warmupMs = sensorCfg.warmupMs;
config.enableDirectionDetection = sensorCfg.enableDirectionDetection;
config.directionTriggerMode = sensorCfg.directionTriggerMode;        // ADDED
config.directionSensitivity = sensorCfg.directionSensitivity;        // ADDED
config.invertLogic = false;
config.sampleWindowSize = sensorCfg.sampleWindowSize;                // ADDED
config.sampleRateMs = sensorCfg.sampleRateMs;                        // ADDED
```

### 2. Added Configuration Validation and Correction System

**Files Modified**:
- `include/config_manager.h` - Added `validateAndCorrect()` method declaration
- `src/config_manager.cpp` - Implemented comprehensive validation
- `src/main.cpp` - Added validation call at boot

**New Method**: `ConfigManager::validateAndCorrect()`

This method performs comprehensive validation and automatic correction of configuration values at boot time.

#### Validation Checks

**Sensor Configuration**:
- `type`: Must be valid sensor type (PIR, Ultrasonic, Grove), otherwise slot disabled
- `detectionThreshold`: 100-5000mm, corrected to 1100mm if invalid
- `maxDetectionDistance`: 200-5000mm, corrected to 3000mm if invalid
- `debounceMs`: 10-1000ms, corrected to 75ms if invalid
- `warmupMs`: 0-120000ms (2 min), corrected to 60000ms if invalid
- `directionTriggerMode`: 0-2 only, corrected to 0 (approaching) if invalid
- `directionSensitivity`: 0 or 10-1000mm, corrected to 0 (auto) if invalid
- `sampleWindowSize`: 3-20, corrected to 3 if invalid
- `sampleRateMs`: 50-1000ms, corrected to 75ms if invalid
- `primaryPin`: 0-21 (ESP32-C3 GPIO range), slot disabled if invalid
- `secondaryPin`: 0-21 or 0 (not used), corrected to 0 if invalid

**Display Configuration**:
- `type`: Must be valid display type, otherwise slot disabled
- `i2cAddress`: 0x00-0x7F, corrected to 0x70 if invalid
- `sdaPin`: 0-21, corrected to 7 if invalid
- `sclPin`: 0-21, corrected to 10 if invalid
- `brightness`: 0-15 for matrix, 0-255 for LED, corrected to max if invalid
- `rotation`: 0-3, corrected to 0 if invalid

**System Configuration**:
- `fusionMode`: 0-2 (ANY, ALL, PRIMARY_ONLY), corrected to 0 if invalid

#### Error Logging

All corrections are logged as **ERROR** level messages with explicit "old → new" format, making them visible in all log configurations and clearly showing what was corrected:

```
[0000001635] [INFO   ] [CONFIG] === Configuration Validation Check ===
[0000001636] [ERROR  ] [CONFIG] Sensor[0]: CORRECTED sampleRateMs: 42405 ms → 75 ms (range 50-1000)
[0000001637] [ERROR  ] [CONFIG] Sensor[0]: CORRECTED directionSensitivity: 42405 mm → 0 mm (auto) (max 1000)
[0000001638] [ERROR  ] [CONFIG] Sensor[0]: CORRECTED directionTriggerMode: 165 → 0 (must be 0=approaching, 1=receding, or 2=both)
[0000001639] [ERROR  ] [CONFIG] Configuration validation: FAILED - corrected 1 sensor errors, 0 display errors
[0000001640] [INFO   ] [CONFIG] Corrected configuration saved to filesystem
```

Example of slot being disabled due to invalid hardware configuration:

```
[0000001635] [ERROR  ] [CONFIG] Sensor[2]: INVALID type 255 (valid: 0=PIR, 3=Ultrasonic, 4=Grove), DISABLING slot
[0000001636] [ERROR  ] [CONFIG] Display[1]: INVALID primaryPin 89 (max 21), DISABLING slot
```

If no errors are found:

```
[0000001635] [INFO   ] [CONFIG] === Configuration Validation Check ===
[0000001636] [INFO   ] [CONFIG] Configuration validation: PASSED (no errors detected)
```

**Message Format**: Each error clearly shows:
- Which configuration parameter was invalid
- The old (corrupted) value
- The new (corrected) value
- The valid range or allowed values
- Clear action taken (CORRECTED or DISABLING slot)

### 3. Boot Sequence Integration

The validation runs automatically at boot, right after configuration is loaded:

```cpp
// Initialize configuration manager (loads from SPIFFS)
if (!configManager.begin()) {
    // Error handling...
}

// Validate and correct configuration for corruption/invalid values
Serial.println("[Setup] Validating configuration...");
if (!configManager.validateAndCorrect()) {
    Serial.println("[Setup] WARNING: Configuration had errors and was corrected");
} else {
    Serial.println("[Setup] Configuration validation: PASSED");
}
```

## Benefits

1. **Automatic Recovery**: System automatically corrects corrupted configuration values instead of crashing or behaving incorrectly

2. **Detailed Error Logging**: All corruption detected and corrected is logged as ERROR, making it easy to diagnose issues

3. **Self-Healing**: Corrected configuration is automatically saved back to filesystem, preventing repeated errors

4. **Boot Verification**: Clear indication at boot whether configuration is healthy or needed correction

5. **Prevents Silent Failures**: Garbage values that would cause incorrect behavior are caught and fixed before sensors are created

## Testing Recommendations

1. **Clean Configuration Test**:
   - Wipe config and save new configuration via web UI
   - Check logs for "Configuration validation: PASSED"
   - Verify sensors work correctly

2. **Corruption Recovery Test**:
   - Manually corrupt config file or use old incompatible config
   - Reboot device
   - Check ERROR logs showing what was corrected
   - Verify corrected values are now working

3. **Direction Mode Test**:
   - Set directionTriggerMode to 0 (approaching only)
   - Verify logs show mode=0 instead of mode=165 or mode=BOTH
   - Test that only approaching motion triggers detection

## Related Files

- `include/config_manager.h` - Method declaration
- `src/config_manager.cpp` - Validation implementation
- `src/main.cpp` - Boot integration and field copying fix
- `src/distance_sensor_base.cpp` - Debug logging for direction mode

## Future Enhancements

Consider adding validation for:
- WiFi configuration (SSID length, password length)
- Network settings (IP addresses, ports)
- Animation settings (frame counts, durations)
- File paths (character restrictions)

---

**Date**: 2026-01-28
**Issue**: Configuration corruption causing incorrect sensor behavior
**Solution**: Comprehensive validation and automatic correction system
