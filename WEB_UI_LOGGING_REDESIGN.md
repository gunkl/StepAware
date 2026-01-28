# Web UI Logging Section Redesign

## Summary

Redesigned the web UI logging section to properly display and manage LittleFS filesystem logs instead of in-memory logs.

## Problem

The previous implementation had several issues:
1. Web UI showed in-memory logs that differed from console output and LittleFS logs
2. Filter buttons and search didn't apply to persistent logs
3. Auto-reload interfered with log viewing
4. No way to view/download the actual persistent logs stored in LittleFS

## Solution

### 1. Backend Changes (src/web_api.cpp)

#### A. Added DELETE Endpoints for Individual Log Files

Added three new DELETE endpoints to allow users to erase individual log files:

```cpp
// DELETE /api/debug/logs/current - Clears current log (doesn't delete, just truncates)
// DELETE /api/debug/logs/boot_1 - Deletes boot_1.log
// DELETE /api/debug/logs/boot_2 - Deletes boot_2.log
```

**Important**: The `current` log is cleared (truncated) rather than deleted because the logger has it open for writing.

#### B. Enhanced handleGetDebugLogs()

Updated the endpoint to return additional system information:

```json
{
  "bootCycle": 5,
  "firmware": "v0.10.0",
  "freeHeap": 234567,
  "filesystemUsage": 45,
  "totalLogsSize": 123456,
  "logs": [
    {"name": "current", "size": 45678, "path": "/logs/boot_current.log"},
    {"name": "boot_1", "size": 23456, "path": "/logs/boot_1.log"},
    {"name": "boot_2", "size": 12345, "path": "/logs/boot_2.log"}
  ]
}
```

### 2. Frontend Changes (HTML/JavaScript in buildDashboardHTML())

#### A. Simplified "Boot Information" Section

Replaced the complex log viewer with a simple static information display:

```html
<div class="card">
  <h2>Boot Information</h2>
  <div id="bootInfo">
    <p><strong>Boot Cycle:</strong> <span id="bootCycle">-</span></p>
    <p><strong>Firmware:</strong> <span id="firmware">-</span></p>
    <p><strong>Free Heap:</strong> <span id="freeHeap">-</span> bytes</p>
    <p><strong>Filesystem Usage:</strong> <span id="fsUsage">-</span>%</p>
    <p><strong>Total Logs Size:</strong> <span id="logsSize">-</span> bytes</p>
    <p><strong>Current Log Level:</strong> <span id="currentLogLevel">-</span></p>
  </div>
</div>
```

**Features:**
- Displays once on page load
- No auto-refresh
- No filter buttons or search box
- Just basic system information

#### B. New "Debug Logs (LittleFS)" Section

Added a new section for managing persistent logs:

```html
<div class="card">
  <h2>Debug Logs (LittleFS)</h2>
  <select id="logSelect">
    <option value="">-- Select a log file --</option>
  </select>
  <div id="logActions">
    <button onclick="downloadLog()">Download</button>
    <button onclick="eraseLog()">Erase</button>
    <button onclick="eraseAllLogs()">Erase All Logs</button>
  </div>
  <div id="logInfo">
    <!-- Shows selected log details -->
  </div>
</div>
```

**Features:**
- Dropdown populated with available log files
- Shows file name and size in dropdown
- Action buttons appear when a log is selected
- Displays detailed info about selected log

#### C. New JavaScript Functions

Added comprehensive log management functions:

**`loadAvailableLogs()`**
- Fetches list of available logs from `/api/debug/logs`
- Updates boot information display
- Populates dropdown with log files
- Shows file sizes in KB

**`onLogSelect()`**
- Handles dropdown selection change
- Shows/hides action buttons and info panel
- Displays selected log details

**`downloadLog()`**
- Downloads selected log file
- Uses browser download mechanism
- Automatically names file: `stepaware_<logname>.log`

**`eraseLog()`**
- Deletes individual log file via DELETE endpoint
- Shows confirmation dialog
- Reloads log list after deletion

**`eraseAllLogs()`**
- Clears all log files via POST to `/api/debug/logs/clear`
- Shows confirmation dialog with warning
- Reloads log list after deletion

### 3. Removed Old Code

Removed all in-memory logging code from the web UI:
- `fetchLogs()` function
- `setLogFilter()` function
- `filterLogs()` function
- `clearLogView()` function
- Auto-refresh timer for logs
- Filter buttons (All, Error, Warn, Info, Debug, Verbose)
- Search box for log filtering
- Log viewer component

## Benefits

1. **Consistency**: Web UI now shows the same logs that are written to LittleFS and appear in console
2. **Simplicity**: No confusing filters or search that don't work with persistent logs
3. **Functionality**: Users can download actual log files for debugging
4. **Control**: Users can manage disk space by deleting old logs
5. **No Interference**: No auto-refresh disrupting log viewing

## Testing Checklist

- [ ] Load page - should show boot info and log dropdown
- [ ] Select a log - should show size and action buttons
- [ ] Download log - should download the actual LittleFS file
- [ ] Erase individual log - should remove file and refresh dropdown
- [ ] Erase all logs - should clear all files and refresh dropdown
- [ ] Verify current log can be cleared but continues to work
- [ ] Check that log level is displayed correctly
- [ ] Confirm no auto-refresh of logs tab

## API Endpoints

### GET /api/debug/logs
Returns list of available logs with system info

**Response:**
```json
{
  "bootCycle": 5,
  "firmware": "v0.10.0",
  "freeHeap": 234567,
  "filesystemUsage": 45,
  "totalLogsSize": 123456,
  "logs": [...]
}
```

### GET /api/debug/logs/{name}
Downloads specific log file (already existed)

### DELETE /api/debug/logs/{name}
Deletes specific log file (NEW)

**Response:**
```json
{
  "success": true,
  "message": "Log deleted"
}
```

### POST /api/debug/logs/clear
Clears all log files (already existed)

**Response:**
```json
{
  "success": true,
  "message": "All logs cleared"
}
```

### GET /api/debug/config
Returns current debug logger configuration (already existed)

**Response:**
```json
{
  "level": "DEBUG",
  "categoryMask": 255
}
```

## Implementation Details

### Safe Deletion of Active Logs

The current log file is handled specially:
- DELETE `/api/debug/logs/current` - **Truncates** the file instead of deleting
- Opens file in write mode and immediately closes it
- Logger continues to work with the file handle it already has

Older logs can be safely deleted:
- DELETE `/api/debug/logs/boot_1` - **Deletes** the file
- DELETE `/api/debug/logs/boot_2` - **Deletes** the file
- These files are not actively being written to

### File Size Display

File sizes are shown in two formats:
- In dropdown: "current (45.2 KB)"
- In info panel: "45678 bytes (45.2 KB)"

### Error Handling

All operations include proper error handling:
- 404 if log file doesn't exist
- 500 if filesystem operation fails
- 501 in mock mode (LittleFS not available)
- Alert dialogs show errors to user

## Files Modified

- `src/web_api.cpp` - All backend and frontend changes

## Related Documentation

- See `DEBUG_LOGGING_IMPLEMENTATION.md` for debug logger details
- See `WEB_UI_ARCHITECTURE_DECISION.md` for web UI design decisions

---

**Date**: 2026-01-27
**Issue**: Web UI Logging Redesign
