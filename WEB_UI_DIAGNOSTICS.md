# Web UI Save Button Diagnostic Checklist

**Date**: 2026-01-26
**Status**: DIAGNOSING
**Issue**: Save button doesn't work, browser shows old inline JavaScript errors

## Current Status

Code fixes are in place:
- ✅ [src/main.cpp](src/main.cpp#L693-L709) - LittleFS initialization added
- ✅ [src/web_api.cpp](src/web_api.cpp#L1163-L1170) - Serves index.html from LittleFS
- ✅ [src/web_api.cpp](src/web_api.cpp#L42-L57) - Routes for app.js and style.css
- ✅ [data/app.js](data/app.js) - Updated with comprehensive logging

After uploading firmware + filesystem, browser still shows:
- ❌ Errors at `(index):1:19810` and `(index):1:21115` (inline JavaScript)
- ❌ NO console logs from app.js ("app.js: File loaded successfully" missing)
- ❌ Save button still doesn't work

## Diagnostic Steps

### Step 1: Verify Firmware Upload

**Action**: Check that the firmware with LittleFS support was actually uploaded.

**How**:
```bash
pio run -t upload -e esp32c3
```

**Expected**: Build succeeds, upload completes with "Writing at 0x000xxxxx... (100%)"

**Result**: [ ] Pass  [ ] Fail

**Notes**:
_______________________________________________

---

### Step 2: Check Serial Output for LittleFS Mount

**Action**: Connect to serial monitor and look for LittleFS initialization messages.

**How**:
```bash
pio device monitor
```

Then press the RESET button on the ESP32.

**Expected Serial Output**:
```
[Setup] Initializing LittleFS filesystem...
[Setup] LittleFS mounted successfully
[Setup] LittleFS contents:
  - /index.html (11746 bytes)
  - /app.js (16747 bytes)
  - /style.css (8833 bytes)
```

**Actual Output**:
```
(paste what you see here)


```

**Result**: [ ] Pass  [ ] Fail

**Analysis**:
- If you see "LittleFS mounted successfully" with file listing → **Go to Step 4** (browser cache issue)
- If you see "ERROR: LittleFS mount failed!" → **Go to Step 3** (filesystem not uploaded)
- If you don't see LittleFS messages at all → **Go back to Step 1** (firmware didn't upload correctly)

---

### Step 3: Upload Filesystem

**Action**: Upload the web UI files to the ESP32's LittleFS partition.

**How**:
```bash
pio run -t uploadfs -e esp32c3
```

**Expected Output**:
```
Building LittleFS filesystem image...
Looking for upload port...
Uploading...
Writing at 0x00XXXXXX... (100%)
```

**Result**: [ ] Pass  [ ] Fail

**Notes**:
_______________________________________________

**After Upload**: Reboot ESP32 and **return to Step 2** to verify files appear in serial output.

---

### Step 4: Clear Browser Cache (IMPORTANT)

**Action**: Completely clear browser cache for the ESP32 site.

**Method A - Hard Refresh (Try First)**:
1. Open the ESP32 web UI
2. Open DevTools (F12)
3. Go to Network tab
4. Check "Disable cache" checkbox
5. Hard refresh: **Ctrl + Shift + F5** (Windows) or **Cmd + Shift + R** (Mac)

**Method B - Clear Site Data (If Method A Fails)**:
1. Open DevTools (F12)
2. Go to Application tab → Storage
3. Click "Clear site data"
4. Refresh page

**Method C - Incognito Window (If All Else Fails)**:
1. Open new incognito/private window
2. Navigate to ESP32 IP address
3. Check console for app.js logs

**Result**: [ ] Pass  [ ] Fail

---

### Step 5: Verify Browser Network Requests

**Action**: Check if app.js is being requested and what content is returned.

**How**:
1. Open DevTools (F12)
2. Go to **Network** tab
3. Refresh page (Ctrl+F5)
4. Look for these requests:
   - `index.html`
   - `app.js`
   - `style.css`

**Expected for app.js**:
- Status: **200 OK**
- Type: **javascript**
- Size: **~16-17 KB**

**Actual for app.js**:
- Status: __________
- Type: __________
- Size: __________

**Check Response Content**:
1. Click on `app.js` in Network tab
2. Go to "Response" sub-tab
3. First line should be: `console.log('app.js: File loaded successfully');`

**Actual first line**:
```
(paste here)


```

**Result**: [ ] Pass (200 OK, correct content)  [ ] Fail (404 or wrong content)

---

### Step 6: Check Browser Console Output

**Action**: Verify app.js is executing and logging correctly.

**How**:
1. Open DevTools (F12)
2. Go to **Console** tab
3. Clear console (trash icon)
4. Refresh page

**Expected Console Output**:
```
app.js: File loaded successfully
app.js: Loading from: http://stepaware.local/
StepAware Dashboard initializing...
DOM elements check:
- warning-duration: true
- config-body: true
- Save button exists: true
refreshConfig: Fetching config from /api/config
```

**Actual Console Output**:
```
(paste here)


```

**Result**: [ ] Pass  [ ] Fail

---

### Step 7: Test Save Button

**Action**: Try saving configuration and watch console logs.

**How**:
1. Navigate to Settings page
2. Change "Warning Duration" to 5000ms
3. Open Console (F12)
4. Click "Save Configuration"
5. Watch console output

**Expected Console Output**:
```
saveConfig: Function called
saveConfig: Starting save operation...
saveConfig: Sending POST to /api/config
saveConfig: Response received, status: 200 OK
saveConfig: Successfully saved
```

**Expected UI**:
- Green toast: "Configuration saved successfully"

**Actual Console Output**:
```
(paste here)


```

**Result**: [ ] Pass (saves work)  [ ] Fail (no save)

---

## Troubleshooting Based on Results

### Issue: LittleFS Mount Failed (Step 2)

**Possible Causes**:
1. Filesystem partition not uploaded
2. Partition table doesn't allocate space for LittleFS
3. Flash corruption

**Solution**:
```bash
# Option 1: Re-upload filesystem
pio run -t uploadfs -e esp32c3

# Option 2: Erase everything and start fresh
pio run -t erase -e esp32c3
pio run -t upload -e esp32c3
pio run -t uploadfs -e esp32c3
```

### Issue: Files Not in LittleFS (Step 2)

**Symptoms**: Mount succeeds but no files listed, or only some files listed.

**Solution**:
1. Check `data/` directory has index.html, app.js, style.css
2. Re-upload filesystem:
   ```bash
   pio run -t uploadfs -e esp32c3
   ```

### Issue: Browser Still Shows Old Content (Step 5)

**Symptoms**:
- Serial shows files uploaded correctly
- Browser Network tab shows 200 OK for app.js
- But Response content is wrong (old code)

**Possible Causes**:
1. Browser cache extremely aggressive
2. Service worker caching (if you have one)
3. Router/proxy caching

**Solution**:
1. Try incognito window
2. Try different browser
3. Try from different device on same network
4. Check if you have a service worker registered (DevTools → Application → Service Workers)

### Issue: app.js Returns 404 (Step 5)

**Symptoms**: Network tab shows app.js request fails with 404

**Possible Causes**:
1. File not in LittleFS
2. Route not registered in web_api.cpp
3. Filename case mismatch

**Solution**:
1. Check serial output - is `/app.js` listed?
2. Verify route in web_api.cpp (should be there)
3. Re-upload filesystem

### Issue: Console Shows Old Errors (Step 6)

**Symptoms**: Console shows `(index):1:xxxxx` errors, no app.js logs

**Root Cause**: ESP32 is serving inline HTML, NOT files from LittleFS

**Why**:
- LittleFS.exists("/index.html") returned false
- Web server fell back to buildDashboardHTML()

**Solution**:
1. Verify LittleFS mounted (Step 2)
2. Verify files uploaded (Step 3)
3. Check serial output for file listing

---

## Quick Reference: Upload Commands

```bash
# Upload firmware only
pio run -t upload -e esp32c3

# Upload filesystem only
pio run -t uploadfs -e esp32c3

# Upload both (do separately)
pio run -t upload -e esp32c3
pio run -t uploadfs -e esp32c3

# Nuclear option: erase everything and start fresh
pio run -t erase -e esp32c3
pio run -t upload -e esp32c3
pio run -t uploadfs -e esp32c3
```

---

## Success Criteria

✅ All these must be true for save functionality to work:

1. **Serial monitor shows**:
   - `[Setup] LittleFS mounted successfully`
   - File listing with `/index.html`, `/app.js`, `/style.css`

2. **Browser console shows**:
   - `app.js: File loaded successfully`
   - DOM elements all found (no null errors)
   - saveConfig function logs appear when clicking save

3. **Browser Network tab shows**:
   - app.js: 200 OK, ~16-17 KB
   - Response content starts with `console.log('app.js: File loaded successfully');`

4. **Save button works**:
   - Clicking Save shows console logs
   - POST request to /api/config succeeds
   - Toast message appears
   - Values persist after refresh

---

## Current Hypothesis

Based on previous messages, after uploading firmware + filesystem, you still see:
- Errors at `(index):1:19810` and `(index):1:21115`
- NO app.js console logs

**This means**: ESP32 is serving inline HTML, not files from LittleFS.

**Most likely causes** (in order of probability):
1. **Filesystem not uploaded** - LittleFS mounted but empty
2. **LittleFS mount failed** - Falls back to inline HTML
3. **Firmware didn't upload** - Still running old firmware without LittleFS support
4. **Browser cache** - Extremely unlikely given multiple attempts

**Recommended Action**:
**Start with Step 2** - Check serial monitor output to see actual LittleFS status.

---

## Next Steps

Please work through the diagnostic steps above and record your results. The serial output from Step 2 is the most critical piece of information.

**When reporting results, please include**:
1. Serial output showing LittleFS initialization
2. Browser Network tab screenshot showing app.js request
3. Browser Console tab screenshot showing errors/logs

This will pinpoint exactly where the issue is.
