# StepAware Mock Web Server

A standalone Python server that simulates the complete StepAware ESP32 system, allowing you to test the web dashboard without physical hardware.

## Features

- **Full API Implementation**: All 8 REST endpoints
- **Web UI Hosting**: Serves the complete dashboard
- **Simulated System**: Mock ConfigManager, Logger, StateMachine
- **Real-time Updates**: Live status changes, motion simulation
- **Persistent State**: Configuration changes persist during session
- **Background Events**: Automatic log generation

## Quick Start

### Option 1: Using Docker (Recommended)

**Easiest method** - no manual dependency installation required.

```bash
# Build and start the mock server
docker-compose up mock-server

# Or run in detached mode
docker-compose up -d mock-server

# Stop the server
docker-compose down
```

Then open: **http://localhost:8080**

### Option 2: Direct Python Execution

If you prefer to run without Docker:

**1. Install Dependencies**

```bash
pip install flask flask-cors
```

**2. Run the Server**

```bash
python test/mock_web_server.py
```

**3. Open Dashboard**

Navigate to: **http://localhost:8080**

## What Gets Simulated

### System Status
- **Uptime**: Real elapsed time since server start
- **Memory**: Random free heap (180-220KB)
- **Motion Events**: Randomly generated in MOTION_DETECT mode
- **Warning Status**: Auto-expires after random duration
- **Mode Changes**: Tracks all mode switches

### Configuration
- **Full Config Structure**: All settings from actual firmware
- **Validation**: Server-side validation (basic)
- **Persistence**: Changes persist during server session
- **Factory Reset**: Restores defaults

### Logging
- **Circular Buffer**: Last 256 entries (returns last 50)
- **Auto-Generation**: Background thread adds system events
- **Level Support**: DEBUG, INFO, WARN, ERROR
- **Timestamps**: Milliseconds since boot

### Operating Modes
- **OFF (0)**: System disabled
- **CONTINUOUS_ON (1)**: Always active
- **MOTION_DETECT (2)**: Motion-triggered (with simulation)

## API Endpoints

All endpoints match the real ESP32 API:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Current system status |
| `/api/config` | GET | Get configuration |
| `/api/config` | POST | Update configuration |
| `/api/mode` | GET | Get current mode |
| `/api/mode` | POST | Change mode |
| `/api/logs` | GET | Retrieve log entries |
| `/api/reset` | POST | Factory reset |
| `/api/version` | GET | Firmware version |

See [API.md](../API.md) for complete API documentation.

## Testing Scenarios

### Test Configuration Changes

1. Open dashboard: http://localhost:8080
2. Click "Configuration" to expand
3. Switch to "LED" tab
4. Adjust brightness sliders
5. Click "Save Configuration"
6. Verify toast notification
7. Refresh page - settings should persist

### Test Mode Switching

1. Click different mode buttons (OFF, CONTINUOUS, MOTION)
2. Watch current mode badge update
3. Check logs for mode change entries
4. Verify motion events only in MOTION mode

### Test Real-Time Updates

1. Keep dashboard open
2. Watch uptime counter increment
3. Observe memory values fluctuate
4. See motion events in MOTION mode
5. Check warning indicator flash

### Test Log Viewer

1. Scroll to "Recent Logs" section
2. Click "Refresh" button
3. Observe color-coded log levels
4. See timestamps in HH:MM:SS.mmm format
5. Watch auto-scroll to latest

### Test Factory Reset

1. Make configuration changes
2. Click "Factory Reset"
3. Confirm dialog
4. Verify settings revert to defaults
5. Check logs for reset entry

### Test Responsive Design

1. Resize browser window
2. Test on mobile device
3. Verify layout adapts (stacked cards)
4. Check touch-friendly buttons

## Command-Line Testing

Test API endpoints directly with curl:

```bash
# Get status
curl http://localhost:8080/api/status

# Get configuration
curl http://localhost:8080/api/config

# Change mode to OFF
curl -X POST http://localhost:8080/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 0}'

# Update warning duration
curl -X POST http://localhost:8080/api/config \
  -H "Content-Type: application/json" \
  -d '{"motion": {"warningDuration": 20000}}'

# Get logs
curl http://localhost:8080/api/logs

# Factory reset
curl -X POST http://localhost:8080/api/reset

# Get version
curl http://localhost:8080/api/version
```

## Differences from Real Hardware

### What's the Same
- ✅ All API endpoints and responses
- ✅ Configuration structure and validation
- ✅ Log format and circular buffer
- ✅ Mode switching logic
- ✅ Web UI (100% identical)

### What's Different
- ❌ No actual GPIO/LED control
- ❌ No PIR sensor input (simulated)
- ❌ No battery monitoring (random values)
- ❌ No SPIFFS persistence (RAM only)
- ❌ No deep sleep or power management
- ❌ Motion events are randomly generated
- ❌ Warning duration is randomized

## Development Workflow

### Web UI Development

1. Start mock server: `python test/mock_web_server.py`
2. Edit files in `data/` directory:
   - `index.html` - Dashboard structure
   - `style.css` - Styling
   - `app.js` - API integration
3. Refresh browser to see changes
4. Test with simulated backend data

### API Integration Testing

1. Modify mock responses in `mock_web_server.py`
2. Restart server
3. Test web UI with new data
4. Verify error handling

### Multi-Device Testing

Server binds to `0.0.0.0`, accessible on network:

**With Docker:**
```bash
docker-compose up mock-server

# On mobile device (same WiFi)
http://<your-pc-ip>:8080
```

**Direct Python:**
```bash
python test/mock_web_server.py

# On mobile device (same WiFi)
http://<your-pc-ip>:8080
```

## Docker Details

### Docker Compose Service

The `mock-server` service is defined in `docker-compose.yml`:

- **Image**: Uses the same base image as `stepaware-dev` (Python 3.11 + Flask)
- **Port**: Exposes port 8080 to host
- **Volumes**: Mounts project directory for live code updates
- **Auto-start**: Launches mock server on container start

### Docker Commands

```bash
# Start mock server (foreground)
docker-compose up mock-server

# Start in background
docker-compose up -d mock-server

# View logs
docker-compose logs -f mock-server

# Stop server
docker-compose down

# Rebuild after dependency changes
docker-compose build mock-server

# Run interactive bash in dev container
docker-compose run --rm stepaware-dev

# Run Python tests in container
docker-compose run --rm stepaware-dev python test/test_logic.py
```

### Container Benefits

- ✅ **No local Python setup** - Everything runs in container
- ✅ **Consistent environment** - Same as CI/CD
- ✅ **Easy cleanup** - `docker-compose down` removes everything
- ✅ **Network isolation** - Controlled port exposure
- ✅ **Cross-platform** - Works on Windows, Mac, Linux

## Customization

### Change Port

Edit `mock_web_server.py`, line at bottom:

```python
app.run(host='0.0.0.0', port=8080, debug=False)
```

### Adjust Simulation Speed

Edit background simulation interval:

```python
def run():
    while True:
        time.sleep(10)  # Change to 5 for faster events
```

### Add Custom Logs

```python
state.add_log(1, "Your custom message")
# Levels: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
```

### Modify Default Config

Edit `MockState.__init__()` in `mock_web_server.py`.

## Troubleshooting

### Docker: Server Won't Start

**Error**: `Address already in use` or port binding fails

**Solution 1**: Stop any existing containers
```bash
docker-compose down
docker-compose up mock-server
```

**Solution 2**: Change port in `docker-compose.yml`
```yaml
ports:
  - "9090:8080"  # Use port 9090 on host instead
```

**Solution 3**: Kill process using port 8080
```bash
# Windows
netstat -ano | findstr :8080
taskkill /PID <pid> /F

# Linux/Mac
lsof -ti:8080 | xargs kill -9
```

### Docker: Dashboard Won't Load

**Error**: `404 Not Found` for static files

**Solution**: Ensure volume mount is working
```bash
# Check if files are visible in container
docker-compose exec mock-server ls -la /workspace/data

# Restart with fresh build
docker-compose down
docker-compose build mock-server
docker-compose up mock-server
```

### Docker: Container Exits Immediately

**Error**: Container starts then stops

**Solution**: Check logs for Python errors
```bash
docker-compose logs mock-server

# Common issues:
# - Missing Flask dependencies (rebuild: docker-compose build)
# - Syntax errors in mock_web_server.py (check file)
```

### Python: Server Won't Start

**Error**: `ModuleNotFoundError: No module named 'flask'`

**Solution**: Install dependencies
```bash
pip install flask flask-cors
```

### Python: Dashboard Won't Load

**Error**: `404 Not Found` for static files

**Solution**: Ensure you're running from project root:

```bash
cd /path/to/StepAware
python test/mock_web_server.py
```

### CORS Errors

**Error**: `Access-Control-Allow-Origin` errors

**Solution**: Already handled by Flask-CORS, but if issues persist:

```python
# Add to mock_web_server.py
@app.after_request
def after_request(response):
    response.headers.add('Access-Control-Allow-Origin', '*')
    return response
```

### Changes Not Persisting

**Note**: Mock server uses RAM only - state resets on restart.

**Workaround**: For persistent testing, modify default config in `MockState.__init__()`.

## Production Deployment

This is a **development tool only**. For production:

1. Build real ESP32 firmware: `pio run -e esp32-devkitlipo`
2. Upload filesystem: `pio run -e esp32-devkitlipo -t uploadfs`
3. Flash firmware: `pio run -e esp32-devkitlipo -t upload`
4. Access on ESP32: `http://stepaware.local/`

## License

Same as StepAware project (MIT)

---

**Created**: 2026-01-12
**Purpose**: Hardware-less web UI testing
**Compatibility**: StepAware Web UI v1.0.0
