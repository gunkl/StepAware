# StepAware REST API Documentation

Base URL: `http://<device-ip>/api`

All endpoints return JSON responses with appropriate HTTP status codes.

## Table of Contents

- [Authentication](#authentication)
- [Endpoints](#endpoints)
  - [GET /api/status](#get-apistatus)
  - [GET /api/config](#get-apiconfig)
  - [POST /api/config](#post-apiconfig)
  - [GET /api/mode](#get-apimode)
  - [POST /api/mode](#post-apimode)
  - [GET /api/logs](#get-apilogs)
  - [POST /api/reset](#post-apireset)
  - [GET /api/version](#get-apiversion)
- [Error Handling](#error-handling)
- [CORS](#cors)

## Authentication

Currently, no authentication is required. Future versions will support:
- API key authentication
- Password protection
- Session tokens

## Endpoints

### GET /api/status

Get current system status.

**Response**

```json
{
  "uptime": 123456,
  "freeHeap": 200000,
  "mode": 2,
  "modeName": "MOTION_DETECT",
  "warningActive": false,
  "motionEvents": 42,
  "modeChanges": 5
}
```

**Fields**

| Field | Type | Description |
|-------|------|-------------|
| uptime | number | Milliseconds since boot |
| freeHeap | number | Free heap memory (bytes) |
| mode | number | Operating mode (0=OFF, 1=CONTINUOUS_ON, 2=MOTION_DETECT) |
| modeName | string | Human-readable mode name |
| warningActive | boolean | True if warning currently active |
| motionEvents | number | Total motion events detected |
| modeChanges | number | Total mode changes |

---

### GET /api/config

Get current configuration.

**Response**

```json
{
  "motion": {
    "warningDuration": 15000,
    "pirWarmup": 60000
  },
  "button": {
    "debounceMs": 50,
    "longPressMs": 1000
  },
  "led": {
    "brightnessFull": 255,
    "brightnessMedium": 128,
    "brightnessDim": 20,
    "blinkFastMs": 250,
    "blinkSlowMs": 1000,
    "blinkWarningMs": 500
  },
  "battery": {
    "voltageFull": 4200,
    "voltageLow": 3300,
    "voltageCritical": 3000
  },
  "light": {
    "thresholdDark": 500,
    "thresholdBright": 2000
  },
  "wifi": {
    "ssid": "MyNetwork",
    "password": "********",
    "enabled": true
  },
  "device": {
    "name": "StepAware-Basement",
    "defaultMode": 2,
    "rememberMode": false
  },
  "power": {
    "savingMode": 0,
    "deepSleepAfterMs": 3600000
  },
  "logging": {
    "level": 1,
    "serialEnabled": true,
    "fileEnabled": false
  },
  "metadata": {
    "version": "0.1.0",
    "lastModified": 1705000000
  }
}
```

**`power.savingMode`** (integer) — replaces the previous `savingEnabled` boolean.
- `0` — Disabled (no auto-sleep)
- `1` — Light Sleep (idle → light sleep; wakes on PIR/button GPIO, ~1 ms latency)
- `2` — Deep Sleep + ULP (idle → light sleep → deep sleep; ULP coprocessor polls PIR for maximum battery life)

Legacy configs containing `savingEnabled` (bool) are migrated automatically on load: `false` → `0`, `true` → `2`.

---

### POST /api/config

Update configuration.

**Request Body**

Send the full configuration JSON (same structure as GET response).

**Example**

```json
{
  "motion": {
    "warningDuration": 20000
  },
  "button": {
    "debounceMs": 75
  }
  // ... other sections
}
```

**Response**

Returns the updated configuration (same as GET /api/config).

**Error Responses**

- `400 Bad Request` - Invalid JSON or validation failure
- `500 Internal Server Error` - Failed to save configuration

---

### GET /api/mode

Get current operating mode.

**Response**

```json
{
  "mode": 2,
  "modeName": "MOTION_DETECT"
}
```

**Mode Values**

| Value | Name | Description |
|-------|------|-------------|
| 0 | OFF | System off, no warnings |
| 1 | CONTINUOUS_ON | LED continuously blinking |
| 2 | MOTION_DETECT | Blink on motion detection (default) |

---

### POST /api/mode

Set operating mode.

**Request Body**

```json
{
  "mode": 2
}
```

**Response**

Returns the new mode (same as GET /api/mode).

**Error Responses**

- `400 Bad Request` - Invalid mode value or missing field

---

### GET /api/logs

Get recent log entries.

**Response**

```json
{
  "logs": [
    {
      "timestamp": 1234567,
      "level": 1,
      "levelName": "INFO",
      "message": "System started"
    },
    {
      "timestamp": 1234890,
      "level": 2,
      "levelName": "WARN",
      "message": "Battery low: 3200mV"
    }
  ],
  "count": 100,
  "returned": 50
}
```

**Fields**

| Field | Type | Description |
|-------|------|-------------|
| logs | array | Array of log entries (max 50) |
| count | number | Total log entries in buffer |
| returned | number | Number of entries returned |

**Log Levels**

| Value | Name | Description |
|-------|------|-------------|
| 0 | DEBUG | Debug messages |
| 1 | INFO | Informational messages |
| 2 | WARN | Warnings |
| 3 | ERROR | Errors |

---

### POST /api/reset

Factory reset configuration.

**Response**

```json
{
  "success": true,
  "message": "Configuration reset to factory defaults"
}
```

**Note:** This resets all configuration to defaults and saves to SPIFFS.

---

### GET /api/version

Get firmware version information.

**Response**

```json
{
  "firmware": "StepAware",
  "version": "0.1.0",
  "buildDate": "Jan 11 2026",
  "buildTime": "23:45:00"
}
```

---

## Error Handling

All error responses follow this format:

```json
{
  "error": "Error message description",
  "code": 400
}
```

**Common HTTP Status Codes**

| Code | Description |
|------|-------------|
| 200 | Success |
| 400 | Bad Request (invalid input) |
| 500 | Internal Server Error |

---

## CORS

The API supports CORS (Cross-Origin Resource Sharing) for web-based clients.

**CORS Headers**

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

---

## Example Usage

### cURL Examples

**Get Status**

```bash
curl http://192.168.1.100/api/status
```

**Set Mode to OFF**

```bash
curl -X POST http://192.168.1.100/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 0}'
```

**Update Configuration**

```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d @config.json
```

### JavaScript Examples

**Fetch Status**

```javascript
fetch('http://192.168.1.100/api/status')
  .then(response => response.json())
  .then(data => console.log('Status:', data))
  .catch(error => console.error('Error:', error));
```

**Change Mode**

```javascript
fetch('http://192.168.1.100/api/mode', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json'
  },
  body: JSON.stringify({ mode: 2 })
})
  .then(response => response.json())
  .then(data => console.log('New mode:', data))
  .catch(error => console.error('Error:', error));
```

### Python Examples

**Get Logs**

```python
import requests

response = requests.get('http://192.168.1.100/api/logs')
logs = response.json()

for entry in logs['logs']:
    print(f"[{entry['levelName']}] {entry['message']}")
```

---

## Rate Limiting

Currently, no rate limiting is implemented. Future versions may include:
- Request throttling
- Connection limits
- Automatic blocking of abusive clients

---

## WebSocket Support

WebSocket support for real-time updates is planned for future releases:
- `/ws/status` - Real-time status updates
- `/ws/logs` - Live log streaming
- `/ws/events` - System event notifications

---

**Last Updated**: 2026-01-11
**API Version**: 1.0
**Firmware Version**: 0.1.0
