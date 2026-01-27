# Web Server Fix Applied: Serve Files from LittleFS

**Date**: 2026-01-26
**Status**: CODE UPDATED - READY TO UPLOAD
**Priority**: CRITICAL

## Problem Summary

The web server was generating HTML inline instead of serving files from LittleFS filesystem. This meant:
- Filesystem uploads had no effect
- app.js never loaded
- Changes to data/ files didn't appear
- Old JavaScript ran (hardcoded in firmware)

## Root Cause

**[src/web_api.cpp](src/web_api.cpp#L1144)**:
```cpp
void WebAPI::handleRoot(AsyncWebServerRequest* request) {
    String html = buildDashboardHTML();  // ← Generated inline!
    request->send(200, "text/html", html);
}
```

The `buildDashboardHTML()` function generated the entire UI as hardcoded strings.

## Fix Applied

### 1. Initialize LittleFS ([src/main.cpp](src/main.cpp))

Added LittleFS initialization in `setup()`:
```cpp
#include <LittleFS.h>

// In setup():
if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed!");
} else {
    Serial.println("LittleFS mounted successfully");
    // List files for debugging
}
```

### 2. Serve Static Files ([src/web_api.cpp](src/web_api.cpp))

Modified `handleRoot()` to serve index.html from filesystem:
```cpp
void WebAPI::handleRoot(AsyncWebServerRequest* request) {
#ifndef MOCK_HARDWARE
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
        return;
    }
#endif
    // Fallback to inline HTML if filesystem not available
    String html = buildDashboardHTML();
    request->send(200, "text/html", html);
}
```

### 3. Add Routes for app.js and style.css ([src/web_api.cpp](src/web_api.cpp))

Added explicit routes for JavaScript and CSS files:
```cpp
m_server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (LittleFS.exists("/app.js")) {
        request->send(LittleFS, "/app.js", "application/javascript");
    } else {
        request->send(404, "text/plain", "app.js not found");
    }
});

m_server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (LittleFS.exists("/style.css")) {
        request->send(LittleFS, "/style.css", "text/css");
    } else {
        request->send(404, "text/plain", "style.css not found");
    }
});
```

## Files Modified

1. **[src/main.cpp](src/main.cpp)**
   - Added `#include <LittleFS.h>`
   - Initialize LittleFS in `setup()`
   - List files for debugging

2. **[src/web_api.cpp](src/web_api.cpp)**
   - Modified `handleRoot()` to serve index.html from LittleFS
   - Added routes for `/app.js` and `/style.css`
   - Keep fallback to inline HTML if filesystem unavailable

## Next Steps

### 1. Upload Firmware

```bash
pio run -t upload -e esp32c3
```

This uploads the new firmware with LittleFS support.

### 2. Upload Filesystem

```bash
pio run -t uploadfs -e esp32c3
```

This uploads index.html, app.js, and style.css to LittleFS.

### 3. Verify in Serial Monitor

After upload, check serial output:

```bash
pio device monitor
```

**Expected output:**
```
[Setup] Initializing LittleFS filesystem...
[Setup] LittleFS mounted successfully
[Setup] LittleFS contents:
  - /index.html (11746 bytes)
  - /app.js (16747 bytes)
  - /style.css (8833 bytes)
```

### 4. Test Web UI

1. Open browser to http://stepaware.local (or ESP32 IP)
2. **Hard refresh**: Ctrl+Shift+F5
3. **Open console** (F12)

**Expected console output:**
```
app.js: File loaded successfully
app.js: Loading from: http://stepaware.local/
StepAware Dashboard initializing...
DOM elements check:
- warning-duration: true
- config-body: true
- Save button exists: true
refreshConfig: Fetching config from /api/config
...
```

### 5. Test Save Button

1. Navigate to Settings page
2. Change "Warning Duration" to 5000ms
3. Click "Save Configuration"

**Expected:**
```
saveConfig: Function called
saveConfig: Starting save operation...
saveConfig: Sending POST to /api/config
saveConfig: Response received, status: 200 OK
saveConfig: Successfully saved
```

**Expected in UI:**
- Green toast: "Configuration saved successfully"

## Troubleshooting

### If LittleFS mount fails

Check serial output. If you see:
```
[Setup] ERROR: LittleFS mount failed!
```

**Causes:**
1. Filesystem not uploaded (`pio run -t uploadfs`)
2. Partition table doesn't have LittleFS partition
3. Flash corruption

**Fix**: Upload filesystem again:
```bash
pio run -t uploadfs -e esp32c3
```

### If files not found (404)

Check serial output for file listing. If files are missing:

**Fix**: Re-upload filesystem:
```bash
pio run -t uploadfs -e esp32c3
```

### If still seeing old JavaScript

**Browser cache issue:**
1. Hard refresh: Ctrl+Shift+F5
2. Or disable cache in DevTools (F12 → Network → "Disable cache")

### If app.js loads but errors persist

Check the actual content being served:
1. DevTools → Network tab
2. Click on `app.js` request
3. Check Response tab
4. First line should be: `console.log('app.js: File loaded successfully');`

If it shows old code, the ESP32 is still serving cached content. Try:
```bash
pio run -t erase -e esp32c3
pio run -t upload -e esp32c3
pio run -t uploadfs -e esp32c3
```

## Benefits

✅ **Dynamic web UI updates** - Change HTML/JS/CSS without reflashing firmware
✅ **Proper file serving** - Files loaded from filesystem as intended
✅ **Debugging capability** - Console logs work correctly
✅ **Save functionality** - Configuration saves work properly
✅ **Faster development** - Update web UI without firmware recompile

## Fallback Behavior

If LittleFS fails to mount, the system falls back to inline HTML generation (`buildDashboardHTML()`). This ensures the web UI is always available, even if the filesystem has issues.

---

**Status**: Code updated, ready to upload
**Expected Result**: Web UI loads from filesystem, save button works
**Test Required**: Upload firmware + filesystem, verify console logs
