# Web UI Feature Comparison: Old (Inline) vs New (Filesystem)

**Date**: 2026-01-26
**Status**: FEATURE GAP IDENTIFIED
**Priority**: HIGH

## Executive Summary

The new filesystem-based UI ([data/index.html](data/index.html)) is missing **significant features** that were present in the old inline HTML (`buildDashboardHTML()`). The backend API supports all these features, but the frontend doesn't expose them.

## Feature Comparison Matrix

| Feature | Old UI (Inline) | New UI (Filesystem) | Backend API Support | Gap |
|---------|----------------|-------------------|-------------------|-----|
| **Basic Configuration** |
| Device name | ✅ | ❌ | ✅ `/api/config` | **MISSING** |
| Default mode | ✅ | ❌ | ✅ `/api/config` | **MISSING** |
| Motion warning duration | ✅ | ✅ | ✅ | OK |
| PIR warmup | ✅ | ✅ | ✅ | OK |
| LED brightness (full/med/dim) | ✅ | ✅ | ✅ | OK |
| Button debounce | ✅ | ✅ | ✅ | OK |
| WiFi SSID/Password | ✅ | ✅ | ✅ | OK |
| Power saving | ✅ | ✅ | ✅ | OK |
| Log level | ✅ | ❌ | ✅ `/api/config` | **MISSING** |
| **Sensor Management** |
| Configure sensor 0 | ❌ (basic) | ✅ (detailed) | ✅ | **IMPROVED** |
| Add/remove multiple sensors | ✅ (up to 4) | ❌ (only sensor 0) | ✅ `/api/sensors` | **MAJOR GAP** |
| Sensor type selection | ✅ | ❌ | ✅ | **MISSING** |
| Pin configuration | ✅ | ❌ | ✅ | **MISSING** |
| Enable/disable sensors | ✅ | ✅ | ✅ | OK |
| Sensor name | ✅ | ✅ | ✅ | OK |
| Detection threshold | ✅ | ✅ | ✅ | OK |
| Sample window size | ❌ | ✅ | ✅ | **IMPROVED** |
| Direction detection | ✅ | ✅ | ✅ | OK |
| **8x8 LED Matrix** |
| Configure displays | ✅ | ❌ | ✅ `/api/displays` | **MAJOR GAP** |
| Add/remove displays | ✅ | ❌ | ✅ | **MAJOR GAP** |
| Enable/disable display | ✅ | ❌ | ✅ | **MAJOR GAP** |
| Pin configuration (DIN/CS/CLK) | ✅ | ❌ | ✅ | **MAJOR GAP** |
| **Animations** |
| View built-in animations | ✅ | ❌ | ✅ `/api/animations` | **MAJOR GAP** |
| Play built-in animations | ✅ | ❌ | ✅ `/api/animations/builtin` | **MAJOR GAP** |
| Download template | ✅ | ❌ | ✅ `/api/animations/template` | **MAJOR GAP** |
| Upload custom animations | ✅ | ❌ | ✅ `/api/animations/upload` | **MAJOR GAP** |
| View custom animations | ✅ | ❌ | ✅ `/api/animations` | **MAJOR GAP** |
| Play custom animations | ✅ | ❌ | ✅ `/api/animations/play` | **MAJOR GAP** |
| Delete animations | ✅ | ❌ | ✅ `DELETE /api/animations/*` | **MAJOR GAP** |
| Assign animations to events | ✅ | ❌ | ✅ `/api/animations/assign` | **MAJOR GAP** |
| View animation assignments | ✅ | ❌ | ✅ `/api/animations/assignments` | **MAJOR GAP** |
| Stop animations | ✅ | ❌ | ✅ `/api/animations/stop` | **MAJOR GAP** |
| **Logs** |
| View logs | ✅ | ✅ | ✅ | OK |
| Filter by log level | ✅ | ❌ | ✅ (client-side) | **MISSING** |
| Search logs | ✅ | ❌ | ✅ (client-side) | **MISSING** |
| Clear log view | ✅ | ❌ | N/A (client-side) | **MISSING** |
| Refresh logs | ✅ | ✅ | ✅ | OK |

## Critical Missing Features

### 1. ❌ Multi-Sensor Management

**Old UI Had**:
- Add up to 4 sensors dynamically
- Remove sensors
- Choose sensor type (PIR, HC-SR04, Grove Ultrasonic)
- Configure pins per sensor
- Individual enable/disable toggles
- Sensor-specific settings

**New UI Has**:
- Only sensor slot 0
- No add/remove capability
- No sensor type selection
- No pin configuration
- Can't manage sensors 1-3

**Impact**: Users with multiple sensors cannot configure them via web UI.

### 2. ❌ 8x8 LED Matrix Configuration

**Old UI Had**:
- Add/configure LED matrix displays
- Configure SPI pins (DIN, CS, CLK)
- Enable/disable displays
- Set brightness
- View active animation assignments

**New UI Has**:
- Nothing - completely missing

**Impact**: Users cannot configure or test the 8x8 LED matrix display.

### 3. ❌ Animation Management (Massive Gap)

**Old UI Had**:
```
Built-In Animations:
- MOTION_ALERT (Flash + scroll arrow)
- BATTERY_LOW (Battery percentage)
- BOOT_STATUS (Startup animation)
- WIFI_CONNECTED (Checkmark)

Actions per animation:
- ▶ Play (test the animation)
- ⬇ Download as template
- ✓ Assign to system event

Custom Animations:
- Upload .txt animation files
- List uploaded animations
- Play/test custom animations
- Delete animations
- Assign to events

Animation Assignments:
- Motion Alert: [assigned animation]
- Battery Low: [assigned animation]
- Boot Status: [assigned animation]
- WiFi: [assigned animation]

Test Controls:
- Stop all animations
- Set test duration (0 = loop forever)
```

**New UI Has**:
- Nothing - completely missing

**Impact**: Users cannot:
- Test animations
- Upload custom animations
- Assign animations to events
- Download animation templates
- Manage animation library

### 4. ❌ Log Filtering

**Old UI Had**:
- Filter buttons: All / Error / Warn / Info
- Search box to filter logs by text
- Clear view button
- Color-coded log levels

**New UI Has**:
- Basic log viewer
- No filtering
- No search
- No color coding

**Impact**: Hard to find specific log entries in large log files.

### 5. ❌ Device Configuration

**Old UI Had**:
- Device name configuration
- Default mode setting
- Log level selection

**New UI Has**:
- None of these

**Impact**: Users can't set device name or configure log verbosity.

## Backend API Endpoints Not Used by New UI

These endpoints exist and work, but the new UI doesn't call them:

### Sensors
- `POST /api/sensors` - Add/update/remove multiple sensors
- Returns full sensor configuration for all 4 slots

### Displays
- `GET /api/displays` - Get display configuration
- `POST /api/displays` - Update display configuration

### Animations
- `GET /api/animations/template` - Download animation template
- `GET /api/animations` - List all animations (built-in + custom)
- `POST /api/animations/play` - Play animation for testing
- `POST /api/animations/stop` - Stop current animation
- `POST /api/animations/builtin` - Play built-in animation
- `POST /api/animations/upload` - Upload custom animation file
- `POST /api/animations/assign` - Assign animation to event
- `GET /api/animations/assignments` - Get current assignments
- `DELETE /api/animations/*` - Delete custom animation

## UI Structure Comparison

### Old UI (Inline HTML)

```
📊 Status Tab
  - Control Panel (OFF/ALWAYS ON/MOTION DETECT)
  - Network Details

⚙️ Hardware Tab
  - Sensor Configuration
    * Add Sensor button
    * Sensor cards (0-4) with type, pins, settings
  - LED Matrix Display
    * Add Display button
    * Display cards with SPI pins
    * Active Assignments panel
  - Animation Library
    * Built-in animations dropdown with Play/Download/Assign
    * Custom animations list with actions
    * Upload section
    * Test & Control (Stop All, Duration)

🔧 Configuration Tab
  - Device Settings (name, default mode)
  - WiFi Settings
  - Motion Detection (warning duration)
  - LED Settings (brightness)
  - Logging (level, power saving)
  - Save/Reload buttons

📝 Logs Tab
  - Log status
  - Filter buttons (All/Error/Warn/Info)
  - Search box
  - Log viewer (color-coded, 800px height)
  - Refresh/Clear buttons
```

### New UI (Filesystem)

```
📊 System Status Section
  - Uptime, Free Memory, Motion Events, Mode Changes
  - Warning status

⚙️ Operating Mode Section
  - OFF / CONTINUOUS / MOTION buttons
  - Current mode display

🔧 Configuration Section (Collapsible)
  - Motion Tab (warning duration, PIR warmup)
  - LED Tab (brightness sliders)
  - Button Tab (debounce, long press)
  - Power Tab (power saving, deep sleep)
  - WiFi Tab (SSID, password, enabled)
  - Hardware Tab (sensor 0 only - NEW)
  - Save Configuration / Factory Reset

📝 Recent Logs Section
  - Basic log viewer
  - Refresh button
```

## What Was Lost

The transition from inline HTML to filesystem-based UI resulted in loss of:

1. **Hardware Tab** - 90% functionality missing
   - Multi-sensor management
   - 8x8 LED matrix configuration
   - Animation library and management

2. **Configuration Tab** - 30% functionality missing
   - Device name
   - Default mode
   - Log level

3. **Logs Tab** - 50% functionality missing
   - Filter by level
   - Search functionality
   - Clear view

4. **Status Tab** - Mostly preserved
   - Network details less comprehensive

## Recommendations

### Priority 1: Restore Critical Features (MUST HAVE)

1. **Multi-Sensor Management**
   - Add "Sensors" section to Hardware tab
   - Support all 4 sensor slots
   - Add sensor type dropdown (PIR, HC-SR04, Grove)
   - Pin configuration UI
   - Add/Remove sensor buttons

2. **8x8 LED Matrix Configuration**
   - Add "Displays" section to Hardware tab
   - SPI pin configuration (DIN, CS, CLK)
   - Enable/disable toggle
   - Brightness control

3. **Animation Management**
   - Add "Animations" section to Hardware tab or separate tab
   - Built-in animations with Play/Download/Assign
   - Custom animation upload
   - Animation library view
   - Test controls (Play/Stop/Duration)
   - Assignment management

### Priority 2: Restore Useful Features (SHOULD HAVE)

4. **Device Configuration**
   - Add device name to Settings
   - Add default mode to Settings
   - Add log level dropdown to Settings

5. **Log Filtering**
   - Add filter buttons (All/Error/Warn/Info)
   - Add search box
   - Color-code log levels
   - Add Clear View button

### Priority 3: Nice to Have

6. **Better Status Display**
   - More detailed WiFi info
   - Sensor status indicators
   - Battery status (if applicable)

## Implementation Strategy

### Option A: Incremental (Recommended)

Add features incrementally to avoid breaking current functionality:

1. Phase 1: Multi-sensor management (Hardware tab expansion)
2. Phase 2: LED matrix config (Hardware tab expansion)
3. Phase 3: Animation management (New tab or Hardware tab section)
4. Phase 4: Enhanced logging (Logs section expansion)
5. Phase 5: Device config (Settings tab additions)

### Option B: Port Old UI

Port the entire `buildDashboardHTML()` inline HTML to filesystem files:

1. Extract HTML structure from buildDashboardHTML()
2. Extract CSS to style.css
3. Extract JavaScript to app.js
4. Update API calls to match new structure
5. Test all features

**Pros**: Gets all features back quickly
**Cons**: Loses improvements in new UI (better structure, logging, etc.)

### Option C: Hybrid

Use new UI as base, selectively port features from old UI:

1. Keep current Status/Settings/Logs sections
2. Replace Hardware tab with old UI's Hardware tab
3. Add Animations section from old UI
4. Enhance with new features (window size slider, response time)

**Pros**: Best of both worlds
**Cons**: More complex integration

## Next Steps

1. **Decision**: Which features to restore first?
2. **Design**: How to integrate into current UI layout?
3. **Implementation**: Build and test each feature
4. **Testing**: Verify backend API compatibility

## Files to Review

- **Backend API**: [src/web_api.cpp](src/web_api.cpp) - All endpoints and handlers
- **Old UI**: [src/web_api.cpp](src/web_api.cpp#L1177) - `buildDashboardHTML()` function
- **New UI HTML**: [data/index.html](data/index.html)
- **New UI JS**: [data/app.js](data/app.js)
- **New UI CSS**: [data/style.css](data/style.css)

---

**Status**: Analysis complete, major feature gaps identified
**Recommendation**: Restore multi-sensor, LED matrix, and animation features as Priority 1
**Effort**: Significant - requires porting ~2000+ lines of HTML/JS/CSS

