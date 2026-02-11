# StepAware Web API Documentation

**Version:** 0.1.0
**Base URL:** `http://<device-ip>/api`
**Content-Type:** `application/json`

## Overview

The StepAware Web API provides RESTful endpoints for monitoring system status, configuring device settings, and controlling operating modes. All responses are in JSON format with CORS support enabled by default.

## Authentication

**Current Version:** No authentication required
**Future:** API key or basic authentication planned for production deployments

## Common Response Codes

| Code | Meaning | Description |
|------|---------|-------------|
| 200 | OK | Request successful |
| 400 | Bad Request | Invalid JSON or missing required fields |
| 500 | Internal Server Error | Server-side error occurred |

## Error Response Format

All error responses follow this structure:

```json
{
  "error": "Error message describing what went wrong",
  "code": 400
}
```

---

## Endpoints

### GET /api/status

Get comprehensive system status including all subsystems.

**Request:**
```http
GET /api/status HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "uptime": 123456,
  "freeHeap": 98765,
  "stateMachine": {
    "mode": 2,
    "modeName": "MOTION_DETECT",
    "warningActive": false,
    "motionEvents": 42,
    "modeChanges": 7
  },
  "wifi": {
    "state": 3,
    "stateName": "CONNECTED",
    "rssi": -55,
    "ssid": "MyNetwork",
    "ipAddress": "192.168.1.100",
    "failures": 0,
    "reconnects": 2,
    "uptime": 98765
  },
  "power": {
    "state": 0,
    "stateName": "ACTIVE",
    "batteryVoltage": 3.85,
    "batteryPercent": 75,
    "usbPower": false,
    "low": false,
    "critical": false,
    "activeTime": 120000,
    "sleepTime": 0,
    "wakeCount": 1
  },
  "watchdog": {
    "systemHealth": 0,
    "healthName": "OK"
  }
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `uptime` | number | System uptime in milliseconds |
| `freeHeap` | number | Free heap memory in bytes |

**State Machine Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `mode` | number | Operating mode (0=OFF, 1=CONTINUOUS_ON, 2=MOTION_DETECT) |
| `modeName` | string | Human-readable mode name |
| `warningActive` | boolean | Whether warning LED is currently active |
| `motionEvents` | number | Total motion detection events |
| `modeChanges` | number | Total mode change count |

**WiFi Fields** (optional, if WiFi Manager available):

| Field | Type | Description |
|-------|------|-------------|
| `state` | number | WiFi state (0=DISABLED, 1=AP_MODE, 2=CONNECTING, 3=CONNECTED, 4=DISCONNECTED, 5=FAILED) |
| `stateName` | string | Human-readable state name |
| `rssi` | number | Signal strength in dBm (-30 to -90) |
| `ssid` | string | Connected network SSID |
| `ipAddress` | string | Device IP address |
| `failures` | number | Connection failure count |
| `reconnects` | number | Reconnection attempt count |
| `uptime` | number | Connection uptime in milliseconds |

**Power Fields** (optional, if Power Manager available):

| Field | Type | Description |
|-------|------|-------------|
| `state` | number | Power state (0=ACTIVE, 1=LIGHT_SLEEP, 2=DEEP_SLEEP, 3=LOW_BATTERY, 4=CRITICAL_BATTERY, 5=USB_POWER) |
| `stateName` | string | Human-readable state name |
| `batteryVoltage` | number | Battery voltage in volts (3.0-4.2V range) |
| `batteryPercent` | number | Battery percentage (0-100) |
| `usbPower` | boolean | Whether USB power is connected |
| `low` | boolean | Low battery flag (below configured threshold) |
| `critical` | boolean | Critical battery flag (below critical threshold) |
| `activeTime` | number | Total active time in milliseconds |
| `sleepTime` | number | Total sleep time in milliseconds |
| `wakeCount` | number | Number of wake events |

**Watchdog Fields** (optional, if Watchdog Manager available):

| Field | Type | Description |
|-------|------|-------------|
| `systemHealth` | number | Overall health (0=OK, 1=WARNING, 2=CRITICAL, 3=FAILED) |
| `healthName` | string | Human-readable health status |

---

### GET /api/config

Get current device configuration.

**Request:**
```http
GET /api/config HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "mode": "MOTION_DETECT",
  "led_brightness": 255,
  "motion_timeout_ms": 15000,
  "battery_low_threshold": 25,
  "light_dark_threshold": 500,
  "wifi": {
    "ssid": "MyNetwork",
    "password": "********"
  }
}
```

**Note:** Sensitive fields (passwords) may be masked in responses.

---

### POST /api/config

Update device configuration. Configuration is validated and persisted to flash storage.

**Request:**
```http
POST /api/config HTTP/1.1
Host: <device-ip>
Content-Type: application/json

{
  "mode": "CONTINUOUS_ON",
  "led_brightness": 200,
  "motion_timeout_ms": 20000,
  "battery_low_threshold": 20,
  "wifi": {
    "ssid": "NewNetwork",
    "password": "NewPassword123"
  }
}
```

**Response (200 OK):**
Returns the updated configuration (same format as GET /api/config).

**Response (400 Bad Request):**
```json
{
  "error": "Invalid configuration: led_brightness must be 0-255",
  "code": 400
}
```

**Response (500 Internal Server Error):**
```json
{
  "error": "Failed to save configuration",
  "code": 500
}
```

---

### GET /api/mode

Get current operating mode.

**Request:**
```http
GET /api/mode HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "mode": 2,
  "modeName": "MOTION_DETECT"
}
```

**Mode Values:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | OFF | Deep sleep, button wake only |
| 1 | CONTINUOUS_ON | Always flashing hazard warning |
| 2 | MOTION_DETECT | Activate warning on motion detection |

---

### POST /api/mode

Set operating mode.

**Request:**
```http
POST /api/mode HTTP/1.1
Host: <device-ip>
Content-Type: application/json

{
  "mode": 1
}
```

**Response (200 OK):**
Returns the updated mode (same format as GET /api/mode).

**Response (400 Bad Request):**
```json
{
  "error": "Invalid mode value",
  "code": 400
}
```

---

### GET /api/logs

Get recent application log entries from the in-memory ring buffer (last 50 entries by default).

> **Note:** This endpoint returns the `g_logger` (Logger class) ring buffer — structured JSON
> entries for application events (motion, mode changes, battery, etc.). It does **not** contain
> watchdog crash diagnostics, boot-sequence messages, or system events. For crash analysis,
> use the `/api/debug/logs/*` endpoints instead.

**Request:**
```http
GET /api/logs HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "count": 150,
  "returned": 50,
  "logs": [
    {
      "timestamp": 123456,
      "level": 2,
      "levelName": "INFO",
      "message": "Motion detected"
    },
    {
      "timestamp": 123457,
      "level": 1,
      "levelName": "WARNING",
      "message": "Battery low: 25%"
    }
  ]
}
```

**Log Level Values:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | DEBUG | Detailed debug information |
| 1 | WARNING | Warning conditions |
| 2 | INFO | Informational messages |
| 3 | ERROR | Error conditions |

---

### GET /api/debug/logs

List all DebugLogger log files stored on LittleFS.

> **Note:** These endpoints serve the `g_debugLogger` (DebugLogger class) files from LittleFS.
> This is the correct system for crash diagnostics — it captures watchdog events, boot
> sequence, reset reasons, state machine transitions, and all `DEBUG_LOG_*` macro output.
> Responses are `text/plain` (not JSON) unless noted.

**Request:**
```http
GET /api/debug/logs HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):** JSON array listing available log files and their sizes.

---

### GET /api/debug/logs/crash_backup

Download the crash backup log — the **primary endpoint for crash diagnostics**.

The DebugLogger preserves the tail of the previous session to `/logs/crash_backup.log`
at boot when a crash reset reason is detected. This file survives the normal log rotation
that would otherwise overwrite boot_current.log.

**Response:** `text/plain` — last ~32KB of log from the crashing session, including
timestamps, log levels, categories, and `[SYSTEM]` messages such as:
- `"Core 0 IDLE task removed from watchdog (Issue #44 fix)"`
- `"Light sleep: Re-added loopTask, re-removed IDLE from watchdog after wake"`
- `"Issue #44: IDLE re-subscribed (status=N) — removing"`

```http
GET /api/debug/logs/crash_backup HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text log content, or `404` if no crash backup exists.

---

### GET /api/debug/logs/current

Download the current boot session log.

```http
GET /api/debug/logs/current HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text log content.

---

### GET /api/debug/logs/prev

Download the previous boot session log (the session before the current one).

```http
GET /api/debug/logs/prev HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text log content, or `404` if not present.

---

### GET /api/debug/logs/overflow

Download the overflow log. Written when the current session log grows too large
and must be truncated — the tail of the large log is saved here before truncation.

```http
GET /api/debug/logs/overflow HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text log content, or `404` if not present.

---

### GET /api/debug/logs/rotation_debug

Download the log rotation diagnostic file. Written before each boot rotation attempt,
captures filesystem state and file existence. Useful for diagnosing rotation failures.

```http
GET /api/debug/logs/rotation_debug HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text diagnostic content, or `404` if not present.

---

### GET /api/debug/logs/boot_1

Download the first-boot log (earliest recorded session still on device).

```http
GET /api/debug/logs/boot_1 HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with plain-text log content, or `404` if not present.

---

### DELETE /api/debug/logs/{name}

Clear a specific debug log file from LittleFS.

Valid `{name}` values: `crash_backup`, `current`, `prev`, `overflow`, `rotation_debug`, `boot_1`

```http
DELETE /api/debug/logs/crash_backup HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{ "success": true }
```

---

### GET /api/ota/coredump

Download the raw ESP32 core dump binary from the dedicated coredump flash partition.
A core dump is recorded automatically by the IDF when a Guru Meditation Error (panic)
occurs. It contains a snapshot of all task stacks and registers at crash time.

```http
GET /api/ota/coredump HTTP/1.1
Host: <device-ip>
```

**Response:** `200 OK` with `application/octet-stream` binary data (raw core dump).
Returns `404` if no core dump has been recorded.

To decode: use the `/coredump` skill (runs `esp_coredump info_corefile` or addr2line fallback).

---

### DELETE /api/ota/coredump

Clear the core dump from the flash partition.

```http
DELETE /api/ota/coredump HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{ "success": true }
```

---

### POST /api/reset

Perform factory reset. Resets all configuration to defaults and clears state machine counters.

**Request:**
```http
POST /api/reset HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Configuration reset to factory defaults"
}
```

**Response (500 Internal Server Error):**
```json
{
  "error": "Failed to reset configuration",
  "code": 500
}
```

**Warning:** This operation cannot be undone. All custom configuration will be lost.

---

### GET /api/version

Get firmware version information.

**Request:**
```http
GET /api/version HTTP/1.1
Host: <device-ip>
```

**Response (200 OK):**
```json
{
  "firmware": "StepAware",
  "version": "0.1.0",
  "buildDate": "Jan 12 2026",
  "buildTime": "14:30:00"
}
```

---

## CORS Support

All endpoints support CORS (Cross-Origin Resource Sharing) to allow browser-based applications to access the API. The following headers are included in responses:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

OPTIONS requests are supported for CORS preflight checks on all endpoints.

---

## Rate Limiting

**Current Version:** No rate limiting
**Future:** Rate limiting may be implemented to prevent API abuse

---

## WebSocket / SSE Support

**Current Version:** Not implemented
**Future:** Server-Sent Events (SSE) endpoint planned for real-time status updates

---

## Example Usage

### JavaScript (Fetch API)

```javascript
// Get system status
fetch('http://192.168.1.100/api/status')
  .then(response => response.json())
  .then(data => {
    console.log('Battery:', data.power.batteryPercent + '%');
    console.log('WiFi RSSI:', data.wifi.rssi + 'dBm');
  });

// Change operating mode
fetch('http://192.168.1.100/api/mode', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ mode: 1 })
})
  .then(response => response.json())
  .then(data => console.log('Mode changed to:', data.modeName));
```

### Python (requests)

```python
import requests

# Get system status
response = requests.get('http://192.168.1.100/api/status')
status = response.json()
print(f"Battery: {status['power']['batteryPercent']}%")
print(f"Free Heap: {status['freeHeap']} bytes")

# Update configuration
config = {
    'led_brightness': 200,
    'motion_timeout_ms': 20000
}
response = requests.post('http://192.168.1.100/api/config', json=config)
print(f"Config updated: {response.json()}")
```

### cURL

```bash
# Get system status
curl http://192.168.1.100/api/status

# Change mode to CONTINUOUS_ON
curl -X POST http://192.168.1.100/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 1}'

# Get logs
curl http://192.168.1.100/api/logs
```

---

## Additional Notes

- All timestamps are in milliseconds since device boot
- WiFi, Power, and Watchdog sections are only included in /api/status if the respective managers are initialized
- Configuration changes take effect immediately but some may require a reboot
- The device IP address can be found via serial console output or by connecting to the AP mode captive portal
