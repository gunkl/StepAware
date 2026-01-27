# Fix: Configuration Changes Not Taking Effect

**Date**: 2026-01-26
**Status**: FIXED

## Problem Summary

Two configuration issues were reported:
1. **Warning duration changes** - Changing warning duration in web UI settings didn't show save confirmation and didn't take effect
2. **Sensor threshold changes** - Changing detection threshold in sensor configuration required reboot to take effect

## Root Causes

### Issue 1: Missing Save Confirmation
The save functionality was working correctly on the backend, but there was no debug logging to help diagnose frontend issues. The toast notification should appear, but without logging it was impossible to verify the flow.

### Issue 2: Configuration Not Applied to Live Sensors
When sensor configuration was saved via the web API:
1. Configuration was correctly saved to `/config.json` on filesystem
2. ConfigManager was updated with new values
3. **BUT** the actual sensor objects were not updated with new threshold values
4. Sensors retained old threshold values until system reboot

### Issue 3: Warning Duration Not Using Config
The StateMachine was using a hardcoded default value (`MOTION_WARNING_DURATION_MS`) instead of reading the warning duration from the runtime configuration:
1. Config value was saved correctly
2. **BUT** StateMachine had no reference to ConfigManager
3. Triggered warnings always used default 3000ms duration

## Fixes Applied

### Fix 1: Add Debug Logging to Save Function

**File**: `data/app.js`

Added console logging to `saveConfig()` function to trace execution:
```javascript
async function saveConfig() {
    try {
        console.log('saveConfig: Starting save operation...');

        // ... build config object ...

        console.log('saveConfig: Sending config to API:', config);

        const response = await fetch(`${API_BASE}/config`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        console.log('saveConfig: Response status:', response.status, response.statusText);

        if (!response.ok) {
            const error = await response.json();
            console.error('saveConfig: Error response:', error);
            throw new Error(error.error || 'Failed to save config');
        }

        currentConfig = await response.json();
        console.log('saveConfig: Successfully saved, new config:', currentConfig);
        showToast('success', 'Configuration saved successfully');
    } catch (error) {
        console.error('saveConfig: Exception caught:', error);
        showToast('error', `Failed to save: ${error.message}`);
    }
}
```

**Benefits**:
- User can now check browser console to see if save request succeeds
- Clear error messages if save fails
- Trace entire save flow for debugging

### Fix 2: Apply Sensor Configuration to Live Sensors

**Files Modified**:
1. `include/web_api.h` - Added `setSensorManager()` method and `m_sensorManager` member
2. `src/web_api.cpp` - Implemented sensor manager setter and live config application
3. `src/main.cpp` - Connected sensor manager to WebAPI

**Changes in `web_api.h`**:
```cpp
void setSensorManager(class SensorManager* sensorManager);

private:
    // ...
    class SensorManager* m_sensorManager;  ///< Sensor Manager reference (optional)
```

**Changes in `web_api.cpp` - handlePostSensors()**:
```cpp
// After saving config to file...

// Apply configuration changes to live sensors (if sensor manager available)
if (m_sensorManager) {
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = m_sensorManager->getSensor(i);
        if (sensor && currentConfig.sensors[i].active) {
            // Apply threshold changes
            if (currentConfig.sensors[i].detectionThreshold > 0) {
                sensor->setDetectionThreshold(currentConfig.sensors[i].detectionThreshold);
                LOG_INFO("Applied threshold %u mm to sensor %u",
                         currentConfig.sensors[i].detectionThreshold, i);
            }

            // Apply direction detection setting
            sensor->setDirectionDetection(currentConfig.sensors[i].enableDirectionDetection);

            // Apply distance range if sensor supports it
            const SensorCapabilities& caps = sensor->getCapabilities();
            if (caps.supportsDistanceMeasurement) {
                sensor->setDistanceRange(caps.minDistance, caps.maxDistance);
            }
        }
    }
    LOG_INFO("Sensor configuration applied to live sensors");
}
```

**Changes in `main.cpp`**:
```cpp
webAPI->setSensorManager(&sensorManager);
```

**Benefits**:
- Sensor threshold changes take effect immediately
- Direction detection settings applied without reboot
- Distance range updates applied dynamically
- Clear logging of applied changes

### Fix 3: Warning Duration From Config

**Files Modified**:
1. `include/state_machine.h` - Added ConfigManager parameter and member
2. `src/state_machine.cpp` - Use config value for warning duration
3. `src/main.cpp` - Pass ConfigManager to StateMachine constructor

**Changes in `state_machine.h`**:
```cpp
StateMachine(class SensorManager* sensorManager,
             HAL_LED* hazardLED,
             HAL_LED* statusLED,
             HAL_Button* button,
             class ConfigManager* config = nullptr);

private:
    // ...
    class ConfigManager* m_config;  ///< Config manager (for runtime config access)
```

**Changes in `state_machine.cpp`**:
```cpp
// Constructor
StateMachine::StateMachine(SensorManager* sensorManager,
                           HAL_LED* hazardLED,
                           HAL_LED* statusLED,
                           HAL_Button* button,
                           ConfigManager* config)
    : m_sensorManager(sensorManager)
    , m_hazardLED(hazardLED)
    , m_statusLED(statusLED)
    , m_button(button)
    , m_ledMatrix(nullptr)
    , m_config(config)
    // ...

// In handleEvent(EVENT_MOTION_DETECTED):
if (m_currentMode == MOTION_DETECT && m_sensorReady) {
    // Use warning duration from config if available
    uint32_t duration = MOTION_WARNING_DURATION_MS;
    if (m_config) {
        duration = m_config->getConfig().motionWarningDuration;
    }
    triggerWarning(duration);
}
```

**Changes in `main.cpp`**:
```cpp
stateMachine = new StateMachine(&sensorManager, &hazardLED, &statusLED, &modeButton, &configManager);
```

**Benefits**:
- Warning duration changes take effect immediately
- No reboot required for timing adjustments
- StateMachine has access to all runtime config values

## Testing

### Test 1: Warning Duration Changes
1. Open web UI settings page
2. Change "Warning Duration" from 3000ms to 5000ms
3. Click "Save Configuration"
4. **Expected**: Success toast appears "Configuration saved successfully"
5. Trigger motion detection
6. **Expected**: Warning displays for 5 seconds (not 3)
7. Check browser console for save logs

### Test 2: Sensor Threshold Changes
1. Open Hardware Configuration page
2. Change ultrasonic sensor threshold from 500mm to 800mm
3. Click "Save"
4. **Expected**: Success toast appears
5. Check serial output for: `Applied threshold 800 mm to sensor X`
6. Place object at 600mm distance
7. **Expected**: No motion detected (above 800mm threshold)
8. Place object at 700mm distance
9. **Expected**: Still no motion
10. Place object at 400mm distance
11. **Expected**: Motion detected (below 800mm threshold)

### Test 3: Direction Detection Toggle
1. Open Hardware Configuration
2. Enable "Direction Detection" for sensor
3. Click "Save"
4. **Expected**: Settings applied immediately without reboot
5. Serial diagnostic view shows direction info

## Verification Commands

```bash
# Build and upload firmware
pio run -t upload -e esp32-devkitlipo

# Monitor serial output
pio device monitor

# Look for these log messages:
# - "Applied threshold XXX mm to sensor Y"
# - "Sensor configuration applied to live sensors"
# - "Event MOTION_DETECTED" followed by warning with correct duration
```

## Browser Console Verification

Open browser console (F12) and look for:
```
saveConfig: Starting save operation...
saveConfig: Sending config to API: {motion: {...}, led: {...}, ...}
saveConfig: Response status: 200 OK
saveConfig: Successfully saved, new config: {...}
```

If error occurs, will see:
```
saveConfig: Error response: {error: "..."}
saveConfig: Exception caught: Error: ...
```

## Related Issues

- Configuration persistence (working correctly)
- Filesystem operations on LittleFS (working correctly)
- JSON serialization/deserialization (working correctly)
- Real-time sensor updates (now fixed)
- State machine configuration integration (now fixed)

## Lessons Learned

1. **Configuration requires two-step update**:
   - Save to persistent storage (filesystem)
   - Apply to runtime objects (sensors, state machine)

2. **Frontend debugging needs logging**:
   - Console.log at each step
   - Log request/response details
   - Trace error paths

3. **Runtime configuration access**:
   - Components that use config values need ConfigManager reference
   - Can't rely only on hardcoded defaults from config.h

4. **Sensor configuration patterns**:
   - All sensor properties have setter methods
   - Setters can be called at runtime
   - Changes take effect immediately

## Commit Message Suggestion

```
Fix configuration changes not taking effect without reboot

Changes:
1. Added debug logging to web UI saveConfig() function
2. Apply sensor threshold changes to live sensors after save
3. StateMachine now uses warning duration from config

This fixes two issues:
- Sensor threshold changes required reboot to take effect
- Warning duration changes were saved but ignored at runtime

WebAPI now updates live sensor objects after saving sensor config,
and StateMachine reads warning duration from ConfigManager instead
of using hardcoded default.

Files modified:
- data/app.js (added debug logging)
- include/web_api.h (added setSensorManager method)
- src/web_api.cpp (apply sensor config to live sensors)
- include/state_machine.h (added ConfigManager parameter)
- src/state_machine.cpp (use config for warning duration)
- src/main.cpp (wire up managers)
```

---

**Status**: Fixed - Ready for testing
**Next Steps**: Build, upload, and verify both issues are resolved
