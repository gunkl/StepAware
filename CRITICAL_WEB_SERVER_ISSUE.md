# CRITICAL: Web Server Not Serving Filesystem Files

**Date**: 2026-01-26
**Status**: ROOT CAUSE IDENTIFIED
**Severity**: CRITICAL

## Problem

The web server is **NOT serving files from the filesystem**. Instead, it generates HTML inline with embedded CSS/JavaScript, which is why:

1. ❌ Filesystem uploads have no effect
2. ❌ app.js never loads
3. ❌ Changes to data/ files don't appear
4. ❌ Old JavaScript runs (hardcoded in firmware)

## Root Cause

**File**: [src/web_api.cpp:1144-1147](src/web_api.cpp#L1144-L1147)

```cpp
void WebAPI::handleRoot(AsyncWebServerRequest* request) {
    String html = buildDashboardHTML();  // ← Generates HTML inline!
    request->send(200, "text/html", html);
}
```

The `buildDashboardHTML()` function generates the entire web interface as a single hardcoded string with embedded styles and scripts. This means:

- **index.html from filesystem**: NEVER LOADED
- **app.js from filesystem**: NEVER LOADED
- **style.css from filesystem**: NEVER LOADED

The filesystem files in `data/` are uploaded but **never served**.

## The Fix

We need to:

1. **Initialize LittleFS** in main.cpp
2. **Serve static files** instead of inline HTML
3. **Route `/` to serve index.html** from filesystem

### Step 1: Initialize LittleFS

Add to `src/main.cpp` in the `setup()` function:

```cpp
#include <LittleFS.h>

void setup() {
    // ... existing setup code

    // Initialize filesystem
    Serial.println("[Setup] Initializing LittleFS...");
    if (!LittleFS.begin(true)) {  // true = format on fail
        Serial.println("[Setup] ERROR: LittleFS mount failed!");
        // Continue anyway - web server will serve inline HTML as fallback
    } else {
        Serial.println("[Setup] LittleFS mounted successfully");

        // List files for debugging
        File root = LittleFS.open("/");
        File file = root.openNextFile();
        while (file) {
            Serial.printf("[LittleFS] %s (%d bytes)\\n", file.name(), file.size());
            file = root.openNextFile();
        }
    }

    // ... rest of setup
}
```

### Step 2: Serve Static Files

Replace the `handleRoot()` function in `src/web_api.cpp`:

```cpp
#include <LittleFS.h>

void WebAPI::handleRoot(AsyncWebServerRequest* request) {
    // Try to serve index.html from filesystem first
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        // Fallback to inline HTML if filesystem not available
        String html = buildDashboardHTML();
        request->send(200, "text/html", html);
    }
}
```

### Step 3: Add Static File Routes

Add these routes in `WebAPI::begin()`:

```cpp
void WebAPI::begin() {
    LOG_INFO("WebAPI: Registering endpoints");

    // Serve static files from LittleFS
    m_server->serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("max-age=600");  // Cache for 10 minutes

    // Serve specific files with correct MIME types
    m_server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/app.js")) {
            request->send(LittleFS, "/app.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "app.js not found");
        }
    });

    m_server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/style.css")) {
            request->send(LittleFS, "/style.css", "text/css");
        } else {
            request->send(404, "text/plain", "style.css not found");
        }
    });

    // API endpoints (existing code)
    m_server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        this->handleGetStatus(req);
    });

    // ... rest of API routes
}
```

## Alternative: Quick Test

To quickly test if this fixes the issue, you can modify just the root handler:

**In src/web_api.cpp, find `handleRoot()` and replace with**:

```cpp
void WebAPI::handleRoot(AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html");
}
```

Then in `main.cpp` setup(), add before starting web server:

```cpp
if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
}
```

## Why This Wasn't Caught Earlier

The inline HTML generation was likely a temporary development approach that was never replaced with proper filesystem serving. The `data/` directory exists and files are uploaded, but the web server was never configured to serve them.

## After Applying Fix

1. **Upload firmware** with LittleFS initialization
2. **Upload filesystem** (pio run -t uploadfs)
3. **Reboot ESP32**
4. **Hard refresh browser** (Ctrl+Shift+F5)
5. **Check console** - should see "app.js: File loaded successfully"
6. **Save button works** - config saves properly

## Verification

After fix, you should see in serial output:

```
[Setup] Initializing LittleFS...
[LittleFS] mounted successfully
[LittleFS] /index.html (11746 bytes)
[LittleFS] /app.js (16747 bytes)
[LittleFS] /style.css (8833 bytes)
```

And in browser console:

```
app.js: File loaded successfully
app.js: Loading from: http://stepaware.local/
StepAware Dashboard initializing...
```

---

**Status**: Fix documented, ready to implement
**Priority**: CRITICAL - blocks all web UI improvements
**Estimated Fix Time**: 15 minutes
