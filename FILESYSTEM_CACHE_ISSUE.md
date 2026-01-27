# Critical Issue: ESP32 Serving Old Cached Web Files

**Date**: 2026-01-26
**Status**: IDENTIFIED
**Severity**: HIGH

## Problem

The ESP32 is serving an **old version** of the web UI files, not the current versions in the `data/` directory.

### Evidence

1. **Console errors reference `(index):1:xxxxx`** - This means JavaScript is inline in index.html, but current index.html loads app.js externally
2. **No console logs from app.js** - The updated app.js has logging like "app.js: File loaded successfully" but nothing appears
3. **Errors at character positions 19810 and 21115** - These are inline JavaScript positions in an old HTML file
4. **404 for favicon.ico** - Expected, but shows which files are being requested

### Root Cause

The ESP32's LittleFS filesystem contains **old cached versions** of the web files. When you upload the filesystem, it may not be:
- Fully erasing old files
- Overwriting existing files correctly
- Clearing the web server's internal cache

## Solution

You need to **erase and re-upload the filesystem** completely.

### Option 1: Erase Flash and Re-upload Everything (Recommended)

This completely wipes the ESP32's flash memory and starts fresh:

```bash
# 1. Erase entire flash (WARNING: Loses ALL data including config)
pio run -t erase -e esp32c3

# 2. Upload firmware
pio run -t upload -e esp32c3

# 3. Upload filesystem
pio run -t uploadfs -e esp32c3
```

**WARNING**: This erases your saved configuration (/config.json). You'll need to reconfigure sensors, WiFi, etc.

### Option 2: Erase Filesystem Only (Better)

If PlatformIO supports it:

```bash
# Erase only the filesystem partition
pio run -t erase_fs -e esp32c3

# Re-upload filesystem
pio run -t uploadfs -e esp32c3
```

**Note**: Check if `erase_fs` target exists in your platformio.ini. If not, use Option 1.

### Option 3: Manual Filesystem Management (Advanced)

If the ESP32 has a web-based file manager or serial command interface:

1. Connect to serial monitor: `pio device monitor`
2. Use commands to:
   - List files: `ls /`
   - Delete old files: `rm /index.html`, `rm /app.js`, `rm /style.css`
   - Verify deletion: `ls /`
3. Upload filesystem: `pio run -t uploadfs -e esp32c3`

### Option 4: Force Overwrite (Quick Try)

Try uploading filesystem multiple times to force overwrite:

```bash
# Upload 3 times in a row
pio run -t uploadfs -e esp32c3
pio run -t uploadfs -e esp32c3
pio run -t uploadfs -e esp32c3
```

Sometimes the first upload fails silently, and subsequent uploads succeed.

## Verification

After re-uploading, verify the files are correct:

### 1. Check Browser Console

Refresh the page with hard reload (Ctrl+Shift+F5) and check for:

```
app.js: File loaded successfully
app.js: Loading from: http://stepaware.local/
StepAware Dashboard initializing...
refreshConfig: Fetching config from /api/config
```

If you see these logs, app.js loaded successfully.

### 2. Check Network Tab

Open DevTools → Network tab:
- Clear network log
- Refresh page
- Look for these requests:
  - `index.html` - 200 OK, ~11-12 KB
  - `app.js` - 200 OK, ~16-17 KB
  - `style.css` - 200 OK, ~8-9 KB

If any return 404 or wrong size, files aren't uploaded correctly.

### 3. Check File Contents via DevTools

In DevTools → Sources tab:
- Expand the domain (stepaware.local)
- Click on `app.js`
- Check if first line is: `console.log('app.js: File loaded successfully');`

If you see old code or HTML, the ESP32 is serving cached/wrong content.

## Why This Happens

### LittleFS Caching Issues

The ESP32's LittleFS filesystem can have issues with:
- **Wear leveling**: Old data may persist in flash blocks
- **File fragmentation**: Deleted files leave fragments
- **Partition corruption**: Incomplete uploads can corrupt the filesystem

### Web Server Caching

The AsyncWebServer might cache files in RAM:
- First request loads file into buffer
- Subsequent requests serve from buffer
- Rebooting clears this cache

### Browser Caching

Browsers aggressively cache static files:
- `.js` and `.css` files cached for hours/days
- Must do hard refresh (Ctrl+Shift+F5) to bypass
- Or disable cache in DevTools (Network tab → "Disable cache")

## Prevention

### 1. Add Cache-Busting Headers

Modify the web server to send no-cache headers for development:

```cpp
// In web_api.cpp or wherever static files are served
server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/app.js", "application/javascript", false, nullptr, [](const String& var){
        return String();
    });
    // Add no-cache headers
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/app.js", "application/javascript");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
});
```

### 2. Version Query Parameters

Add version numbers to script tags:

```html
<script src="app.js?v=2"></script>
```

Increment version after each upload to force browser reload.

### 3. Development vs Production Builds

Use different cache headers for dev vs production:
- Development: No cache, immediate reload
- Production: Long cache, version in filename

## Current File Sizes (Expected)

Based on the `data/` directory:

```
index.html:  11,746 bytes
app.js:      16,747 bytes (after logging updates)
style.css:    8,833 bytes
```

If the ESP32 serves different sizes, files are wrong.

## Debug: Check What ESP32 Actually Has

If you have a serial command interface, you can list files:

```bash
# Connect to serial
pio device monitor

# Commands to try (if supported):
ls /
cat /index.html | head -50
stat /app.js
```

This will show what files the ESP32 actually has in its filesystem.

## Next Steps

1. **Erase and re-upload** using Option 1 or 2 above
2. **Hard refresh** browser (Ctrl+Shift+F5)
3. **Check console** for "app.js: File loaded successfully"
4. **Test save button** - should now work

If the issue persists after erasing and re-uploading, there may be a deeper problem with:
- The web server configuration
- The filesystem partition definition
- The PlatformIO upload process

---

**Status**: Ready for user to erase flash and re-upload
**Expected Result**: Web UI loads new app.js with full logging and working save functionality
