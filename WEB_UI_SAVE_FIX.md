# Fix: Web UI Save Button Not Working

**Date**: 2026-01-26
**Status**: NEEDS FILESYSTEM UPLOAD
**Priority**: HIGH

## Problem Summary

When clicking "Save Configuration" in the web UI settings page, nothing happens:
- No save confirmation toast appears
- Changes to warning duration, LED brightness, button settings, etc. are not saved
- No visible response in the UI

## Root Cause

The ESP32 has two separate storage areas:
1. **Program Flash** - Contains the firmware (`.bin` file)
2. **Filesystem (LittleFS)** - Contains web UI files (`index.html`, `app.js`, `style.css`)

When you run `pio run -t upload -e esp32c3`, it ONLY uploads the firmware. The web UI files in `data/` directory are served from a separate filesystem partition that must be uploaded using a different command.

### Current State

**Firmware**: ✅ Updated with latest fixes (distance sensor, config save, etc.)
**Filesystem**: ❌ Still has OLD version of app.js without debug logging

The browser is loading the OLD app.js from the ESP32's filesystem, which doesn't have:
- Console logging for debugging
- Any recent fixes to the save functionality

## The Fix

Upload the filesystem to the ESP32:

```bash
# Upload web UI files to ESP32 filesystem
pio run -t uploadfs -e esp32c3
```

This uploads all files from `data/` directory to the ESP32's LittleFS filesystem.

## Verification Steps

### 1. Upload Filesystem

```bash
cd "c:\Users\David\Documents\VSCode Projects\ESP32\StepAware"
pio run -t uploadfs -e esp32c3
```

**Expected output:**
```
Building FS image from 'data' directory to .pio\build\esp32c3\littlefs.bin
Looking for upload port...
...
Writing at 0x00290000... (100 %)
Wrote 1507328 bytes at 0x00290000 in 133.7 seconds
```

### 2. Restart ESP32

After filesystem upload, reset the ESP32:
- Press the RESET button on the board, OR
- Power cycle the device

### 3. Clear Browser Cache

**Important:** Your browser may have cached the old app.js file.

**Chrome/Edge:**
1. Open DevTools (F12)
2. Right-click the refresh button
3. Select "Empty Cache and Hard Reload"

**Firefox:**
1. Open DevTools (F12)
2. Go to Network tab
3. Check "Disable Cache"
4. Reload page (Ctrl+R)

### 4. Test Save Functionality

1. Open web UI at `http://stepaware.local` (or ESP32's IP address)
2. Navigate to Settings page
3. Open browser console (F12 → Console tab)
4. Change "Warning Duration" from 3000ms to 5000ms
5. Click "Save Configuration"

**Expected in Console:**
```
saveConfig: Starting save operation...
saveConfig: Sending config to API: {motion: {warningDuration: 5000, ...}, ...}
saveConfig: Response status: 200 OK
saveConfig: Successfully saved, new config: {...}
```

**Expected in UI:**
- Green success toast: "Configuration saved successfully"

### 5. Verify Persistence

1. Refresh the page (F5)
2. Check that "Warning Duration" still shows 5000ms
3. Trigger motion detection
4. Verify warning displays for 5 seconds (not 3)

## If Save Still Doesn't Work

### Check Browser Console

Look for error messages in the console (F12):

**CORS Error:**
```
Access to fetch at 'http://...' has been blocked by CORS policy
```
**Fix:** Check that ESP32 is responding with CORS headers (should be automatic)

**Network Error:**
```
Failed to fetch
TypeError: NetworkError when attempting to fetch resource
```
**Fix:** Verify ESP32 is connected to WiFi and accessible

**JSON Parse Error:**
```
SyntaxError: Unexpected token < in JSON at position 0
```
**Fix:** ESP32 may be returning HTML error page instead of JSON

### Check Serial Monitor

Enable serial monitoring to see backend logs:

```bash
pio device monitor
```

**Look for:**
```
[WebAPI] POST /api/config - config update request
[ConfigManager] Config updated via API
[ConfigManager] Saved configuration to /config.json
```

**If you see:**
```
[WebAPI] Error parsing JSON
[ConfigManager] Failed to save configuration
```
This indicates a backend issue - report the full error message.

## Technical Details

### File Upload Process

PlatformIO uses the following process for filesystem uploads:

1. **Build Filesystem Image**: Packages `data/` folder into `littlefs.bin`
2. **Erase Flash Region**: Clears the filesystem partition
3. **Write Image**: Uploads `littlefs.bin` to ESP32 at offset 0x290000
4. **Verify**: Checks written data

### Filesystem Partition Layout

```
0x000000  [Bootloader]    16 KB
0x008000  [Partition Table] 3 KB
0x00E000  [NVS]           24 KB  (WiFi credentials, etc.)
0x010000  [App0]        1536 KB  (Firmware)
0x290000  [SPIFFS]      1536 KB  (Web UI files) ← uploadfs writes here
```

### Web UI Files in Filesystem

After `uploadfs`, these files are available on ESP32:

```
/index.html       - Main dashboard HTML
/app.js           - JavaScript with save logic (UPDATED with logging)
/style.css        - Stylesheet
/favicon.ico      - Browser icon
```

The AsyncWebServer serves these files when you browse to the web UI.

## Known Limitations

### 1. Filesystem Upload Erases Everything

When you run `uploadfs`, it completely erases and rewrites the filesystem. Any data stored there (like uploaded files) will be lost.

**Not affected:**
- Configuration in NVS (`/config.json` is saved to NVS, not SPIFFS)
- WiFi credentials
- Firmware

### 2. Upload Takes Time

Filesystem upload is slower than firmware upload:
- Firmware upload: ~30 seconds
- Filesystem upload: ~2-3 minutes

Be patient and don't interrupt the process.

### 3. Browser Caching

Browsers aggressively cache JavaScript files. You MUST do a hard refresh after uploading new web UI files.

## Alternative: Use Mock Web Server for Development

For rapid web UI development without constantly uploading to ESP32:

```bash
# Run mock server (simulates ESP32 API)
python test/mock_web_server.py

# Open browser to localhost
http://localhost:8080
```

Changes to `data/app.js` take effect immediately with page refresh.

## Summary

**Problem**: Web UI save button does nothing
**Root Cause**: Old app.js still on ESP32's filesystem
**Fix**: Upload filesystem with `pio run -t uploadfs -e esp32c3`
**Verification**: Check browser console for save logs

After fixing this, all settings changes (warning duration, LED brightness, button timing, WiFi, power saving) should save correctly and persist across reboots.

---

**Status**: Documented - Ready for user to upload filesystem
**Next Step**: Run `pio run -t uploadfs -e esp32c3`
