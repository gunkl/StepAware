# Hardware Configuration UI Added

**Date**: 2026-01-26
**Status**: COMPLETE
**Priority**: HIGH

## Summary

Added a new "Hardware" tab to the web UI Configuration section, allowing users to configure sensor settings including the new sample window size feature.

## Changes Made

### 1. HTML - New Hardware Tab

**File**: [data/index.html](data/index.html)

#### Added Tab Button
Added "Hardware" button to configuration tabs (line ~102):
```html
<button class="tab-btn" onclick="showConfigTab('hardware')">Hardware</button>
```

#### Added Hardware Tab Content
Added complete sensor configuration section (lines ~186-240):

```html
<!-- Hardware Tab -->
<div class="config-tab" id="tab-hardware" style="display: none;">
    <h4>Sensor 0 Configuration</h4>

    <!-- Sensor Enabled -->
    <input type="checkbox" id="sensor0-enabled">

    <!-- Sensor Name -->
    <input type="text" id="sensor0-name" maxlength="32">

    <!-- Detection Threshold (mm) -->
    <input type="number" id="sensor0-threshold" min="100" max="5000" step="50">

    <!-- Measurement Interval (ms) -->
    <input type="number" id="sensor0-debounce" min="60" max="1000" step="10">

    <!-- Sample Window Size (slider) -->
    <input type="range" id="sensor0-window-size" min="3" max="20" value="5">
    <span id="sensor0-window-size-value">5</span>

    <!-- Response Time Display (calculated) -->
    <span id="sensor0-response-time">300ms</span>

    <!-- Direction Detection -->
    <input type="checkbox" id="sensor0-direction-enabled">

    <!-- Invert Logic -->
    <input type="checkbox" id="sensor0-invert-logic">
</div>
```

### 2. JavaScript - Load and Save Hardware Config

**File**: [data/app.js](data/app.js)

#### Updated populateConfigForm()
Added hardware configuration loading (lines ~193-206):

```javascript
// Hardware - Sensor 0
if (config.sensors && config.sensors[0]) {
    const sensor0 = config.sensors[0];
    setElementChecked('sensor0-enabled', sensor0.enabled || false);
    setElementValue('sensor0-name', sensor0.name || '');
    setElementValue('sensor0-threshold', sensor0.detectionThreshold || 1500);
    setElementValue('sensor0-debounce', sensor0.debounceMs || 60);
    setElementValue('sensor0-window-size', sensor0.sampleWindowSize || 5);
    setElementChecked('sensor0-direction-enabled', sensor0.enableDirectionDetection || false);
    setElementChecked('sensor0-invert-logic', sensor0.invertLogic || false);

    // Update response time display
    updateResponseTime();
}
```

#### Updated saveConfig()
Added hardware configuration saving (lines ~287-307):

```javascript
sensors: currentConfig.sensors || []

// Update sensor 0 config if hardware tab values exist
if (document.getElementById('sensor0-enabled')) {
    if (!config.sensors[0]) {
        config.sensors[0] = { ...currentConfig.sensors[0] };
    }
    config.sensors[0].enabled = document.getElementById('sensor0-enabled').checked;
    config.sensors[0].name = document.getElementById('sensor0-name').value;
    config.sensors[0].detectionThreshold = parseInt(document.getElementById('sensor0-threshold').value);
    config.sensors[0].debounceMs = parseInt(document.getElementById('sensor0-debounce').value);
    config.sensors[0].sampleWindowSize = parseInt(document.getElementById('sensor0-window-size').value);
    config.sensors[0].enableDirectionDetection = document.getElementById('sensor0-direction-enabled').checked;
    config.sensors[0].invertLogic = document.getElementById('sensor0-invert-logic').checked;
}
```

#### Updated setupRangeInputs()
Added window size slider handling with response time update:

```javascript
const ranges = [
    'led-brightness-full',
    'led-brightness-medium',
    'led-brightness-dim',
    'sensor0-window-size'  // Added
];

// Update response time when window size or debounce changes
input.addEventListener('input', function() {
    display.textContent = this.value;
    if (id === 'sensor0-window-size') {
        updateResponseTime();
    }
});

// Also listen to debounce changes
const debounceInput = document.getElementById('sensor0-debounce');
if (debounceInput) {
    debounceInput.addEventListener('input', updateResponseTime);
}
```

#### Added updateResponseTime()
New function to calculate and display response time:

```javascript
function updateResponseTime() {
    const windowSizeInput = document.getElementById('sensor0-window-size');
    const debounceInput = document.getElementById('sensor0-debounce');
    const responseTimeDisplay = document.getElementById('sensor0-response-time');

    if (windowSizeInput && debounceInput && responseTimeDisplay) {
        const windowSize = parseInt(windowSizeInput.value);
        const sampleRate = parseInt(debounceInput.value);
        const responseTime = windowSize * sampleRate;
        responseTimeDisplay.textContent = `${responseTime}ms`;
    }
}
```

## Features

### Configurable Settings

1. **Sensor Enabled** - Enable/disable sensor slot
2. **Sensor Name** - Descriptive name (e.g., "Front Door")
3. **Detection Threshold** - Distance threshold in millimeters (100-5000mm)
4. **Measurement Interval** - Time between sensor readings (60-1000ms)
5. **Sample Window Size** - Number of samples to average (3-20)
   - Slider control with live value display
   - Lower = faster response, more noise
   - Higher = slower response, smoother
6. **Response Time Display** - Automatically calculated (window size × sample rate)
7. **Direction Detection** - Enable approaching/departing detection
8. **Invert Logic** - Invert sensor HIGH/LOW logic

### Real-Time Response Time Calculation

The response time display updates automatically when you adjust:
- **Window Size** slider
- **Measurement Interval** input

**Formula**: Response Time = Window Size × Measurement Interval

**Examples**:
- Window 5 × 60ms = 300ms response time
- Window 10 × 60ms = 600ms response time
- Window 5 × 100ms = 500ms response time

## User Experience

### Accessing Hardware Configuration

1. Open web UI at http://stepaware.local
2. Expand "Configuration" section (click header)
3. Click "Hardware" tab
4. Adjust sensor settings
5. Click "Save Configuration"

### Visual Feedback

- **Slider** shows current window size value
- **Response Time** updates in real-time as you adjust settings
- **Help text** explains each setting's purpose and range
- **Toast notification** confirms save success/failure

### Recommended Settings

For fast pedestrian detection (current defaults):
```
Detection Threshold: 1500mm (1.5 meters)
Measurement Interval: 60ms (HC-SR04 minimum)
Sample Window Size: 5 (300ms response)
Direction Detection: Enabled
```

For slower, smoother detection:
```
Detection Threshold: 2000mm (2 meters)
Measurement Interval: 100ms
Sample Window Size: 10 (1000ms response)
Direction Detection: Enabled
```

## Backend Integration

The hardware configuration uses the existing `/api/config` endpoint:

- **GET /api/config** - Returns full config including `sensors` array
- **POST /api/config** - Saves config, applies to live sensors immediately

The backend (web_api.cpp) already handles applying sensor config changes:
- Calls `setSampleWindowSize()` on live sensor objects
- Calls `setDetectionThreshold()` for immediate effect
- No reboot required

## Testing Checklist

### After Upload

1. **Upload Firmware**:
   ```bash
   pio run -t upload -e esp32c3
   ```

2. **Upload Filesystem**:
   ```bash
   pio run -t uploadfs -e esp32c3
   ```

3. **Test Web UI**:
   - Open browser, hard refresh (Ctrl+Shift+F5)
   - Expand Configuration
   - Click "Hardware" tab
   - **Expected**: Hardware configuration form appears

4. **Test Loading**:
   - Check browser console for: `populateConfigForm: Completed successfully`
   - **Expected**: Sensor values populate from current config

5. **Test Slider**:
   - Move "Sample Window Size" slider
   - **Expected**: Value updates, response time recalculates

6. **Test Response Time**:
   - Change window size to 10
   - **Expected**: Response time shows "600ms"
   - Change measurement interval to 100ms
   - **Expected**: Response time shows "1000ms"

7. **Test Save**:
   - Change threshold to 2000mm
   - Change window size to 7
   - Click "Save Configuration"
   - **Expected**: Toast "Configuration saved successfully"
   - Check serial monitor: `[WebAPI] Applied threshold 2000 mm to sensor 0`
   - Refresh page
   - **Expected**: Values persist

8. **Test Live Update**:
   - Change detection threshold
   - Save config
   - Stand at configured distance from sensor
   - **Expected**: Motion triggers at new threshold immediately (no reboot)

## Files Modified

1. **[data/index.html](data/index.html)**
   - Added Hardware tab button
   - Added Hardware tab content with all sensor config fields

2. **[data/app.js](data/app.js)**
   - Updated `populateConfigForm()` to load sensor config
   - Updated `saveConfig()` to save sensor config
   - Updated `setupRangeInputs()` to handle window size slider
   - Updated `updateRangeDisplays()` to include window size
   - Added `updateResponseTime()` function

## Benefits

✅ **User-Friendly Configuration** - No need to edit JSON files or use serial interface
✅ **Real-Time Feedback** - Response time calculated as you adjust settings
✅ **Immediate Effect** - Changes apply without reboot
✅ **Guided Inputs** - Help text explains each setting
✅ **Range Validation** - Input limits prevent invalid values
✅ **Visual Sliders** - Easy adjustment of window size

## Future Enhancements

Possible improvements:
- Add sensor type dropdown (PIR, HC-SR04, Grove)
- Support for all 4 sensor slots (currently only slot 0)
- Visual indicator of current sensor status
- Live distance reading display
- Graph showing distance over time

---

**Status**: Complete and ready to test
**Next Step**: Upload filesystem and test Hardware tab functionality
**Expected Result**: Hardware configuration accessible via web UI, saves work correctly
