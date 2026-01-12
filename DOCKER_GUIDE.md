# Docker Development Environment Guide

This guide explains how to use Docker for StepAware development and testing.

## ✅ Status

**Docker is installed and working!**
- Docker version: 29.1.3
- Container includes: Python 3.11, PlatformIO 6.1.18, GCC/G++ 14.2.0
- All tools verified and operational

## Prerequisites

1. **Docker Desktop for Windows** ✅ INSTALLED
   - Current version: 29.1.3
   - Status: Running and operational
   - Verify: `docker --version`

## Quick Start

### 1. Build the Docker Image (ALREADY DONE ✅)

The Docker image has been built and contains:
- Python 3.11
- PlatformIO 6.1.18
- GCC/G++ 14.2.0 toolchain for native testing
- ESP32 platform 6.9.0 pre-installed

To rebuild (if needed):
```bash
docker-compose build
```

### 2. Start the Development Container

```bash
docker-compose run --rm stepaware-dev
```

You'll be inside a bash shell with the project mounted at `/workspace`.

### 3. Run Commands Inside Container

```bash
# Build for ESP32
pio run

# Build for native testing (PC simulation)
pio run -e native

# Run native tests
pio test -e native

# Upload to ESP32 (requires USB passthrough - see below)
pio run --target upload

# Monitor serial output
pio device monitor
```

## Available Services

StepAware provides two Docker services:

1. **`stepaware-dev`** - Development container with PlatformIO and build tools
2. **`mock-server`** - Web server for testing the dashboard without ESP32 hardware

## Common Workflows

### Mock Web Server (Test Dashboard)

Test the complete web interface without ESP32 hardware:

```bash
# Start mock server (foreground)
docker-compose up mock-server

# Start in background
docker-compose up -d mock-server

# View logs
docker-compose logs -f mock-server

# Stop server
docker-compose down
```

Then open **http://localhost:8080** in your browser.

**Features:**
- ✅ Complete web dashboard UI
- ✅ All 8 REST API endpoints functional
- ✅ Real-time status updates every 2 seconds
- ✅ Mode switching (OFF, CONTINUOUS, MOTION)
- ✅ Configuration editor with live save
- ✅ Log viewer with simulated events
- ✅ Factory reset functionality
- ✅ Simulated motion detection

See [test/MOCK_SERVER.md](test/MOCK_SERVER.md) for detailed mock server documentation.

### Build and Test (Mock Hardware)

```bash
# Start container
docker-compose run --rm stepaware-dev

# Inside container:
pio run                    # Build ESP32 firmware
pio run -e native          # Build native tests
pio test -e native         # Run tests on PC

# Exit container
exit
```

### Interactive Development Session

```bash
# Start persistent container
docker-compose up -d stepaware-dev

# Attach to running container
docker exec -it stepaware-dev bash

# Work inside container...
pio run
pio test -e native

# When done, stop container
docker-compose down
```

### Clean Build

```bash
docker-compose run --rm stepaware-dev pio run --target clean
docker-compose run --rm stepaware-dev pio run
```

## USB Device Access (Real Hardware)

To upload firmware to a physical ESP32 board from Docker:

### Windows (Docker Desktop)

1. **Edit docker-compose.yml** - Uncomment the devices section:
   ```yaml
   devices:
     - /dev/ttyUSB0:/dev/ttyUSB0  # Adjust COM port
   privileged: true
   ```

2. **Find COM Port**
   ```bash
   # On Windows host
   pio device list
   # Example output: COM3
   ```

3. **Map COM Port to Linux Device**
   - Docker Desktop on Windows requires WSL2
   - Install usbipd-win: https://github.com/dorssel/usbipd-win
   - Share USB device with WSL2

4. **Upload Firmware**
   ```bash
   docker-compose run --rm stepaware-dev pio run --target upload
   ```

## Advantages of Docker Environment

### For Mock Hardware Testing
- ✅ Native tests work out-of-the-box (g++ included)
- ✅ Consistent environment across all developers
- ✅ No need to install PlatformIO on host machine
- ✅ Isolated from Windows PATH issues

### For CI/CD
- ✅ Ready for GitHub Actions
- ✅ Reproducible builds
- ✅ Automated testing

### For Team Development
- ✅ Same toolchain version for everyone
- ✅ New developers: just `docker-compose up`
- ✅ No "works on my machine" problems

## File Locations

### Inside Container
- `/workspace` - Your project (mounted from host)
- `/root/.platformio` - PlatformIO cache (persisted in Docker volume)

### On Host
- Project files are in your normal directory
- Changes sync immediately (volume mount)
- Build artifacts in `.pio/` (can be ignored/deleted)

## Troubleshooting

### Port Already in Use
```bash
docker-compose down
docker-compose up -d
```

### Rebuild After Dependency Changes
```bash
docker-compose build --no-cache
```

### Clean Everything
```bash
docker-compose down -v          # Stop and remove volumes
docker system prune -a          # Clean all Docker resources
```

### Permission Issues (Linux/Mac)
```bash
# Run container as your user
docker-compose run --rm --user $(id -u):$(id -g) stepaware-dev
```

## VS Code Integration

You can use the **Remote - Containers** extension to develop directly inside the container:

1. Install "Dev Containers" extension in VS Code
2. Open project folder
3. Click "Reopen in Container" when prompted
4. VS Code runs inside Docker with full PlatformIO support

## GitHub Actions Example

```yaml
# .github/workflows/build.yml
name: Build and Test

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Build Docker image
      run: docker-compose build

    - name: Build firmware
      run: docker-compose run --rm stepaware-dev pio run

    - name: Run tests
      run: docker-compose run --rm stepaware-dev pio test -e native
```

## Summary

**Without Docker:**
- Install Python, PlatformIO, MinGW (for native tests)
- PATH configuration issues
- Platform-specific problems

**With Docker:**
- `docker-compose run --rm stepaware-dev pio test -e native`
- Everything just works
- Consistent across all platforms

## Next Steps

1. **Test the web dashboard**: `docker-compose up mock-server` → Open http://localhost:8080
2. **Build firmware**: `docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo`
3. **Run C++ tests**: `docker-compose run --rm stepaware-dev pio test -e native`
4. **Run Python tests**: `docker-compose run --rm stepaware-dev python test/test_logic.py`

## Complete Testing Workflow (No Hardware Required)

```bash
# 1. Start mock web server
docker-compose up -d mock-server

# 2. Open browser and test dashboard
# Visit: http://localhost:8080
# Test: mode switching, config changes, logs

# 3. Build firmware in parallel
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo

# 4. Run C++ tests
docker-compose run --rm stepaware-dev pio test -e native

# 5. Run Python tests
docker-compose run --rm stepaware-dev python test/test_logic.py
docker-compose run --rm stepaware-dev python test/test_config_manager.py
docker-compose run --rm stepaware-dev python test/test_logger.py
docker-compose run --rm stepaware-dev python test/test_web_api.py

# 6. View mock server logs
docker-compose logs -f mock-server

# 7. Stop everything
docker-compose down
```

This workflow tests **everything** (firmware build, C++ tests, Python tests, web UI) without any physical hardware!
