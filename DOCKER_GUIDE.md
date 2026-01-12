# Docker Development Environment Guide

This guide explains how to use Docker for StepAware development and testing.

## Prerequisites

1. **Install Docker Desktop for Windows**
   - Download from: https://www.docker.com/products/docker-desktop
   - Install and start Docker Desktop
   - Verify: `docker --version`

## Quick Start

### 1. Build the Docker Image

```bash
docker-compose build
```

This creates a container with:
- Python 3.11
- PlatformIO
- GCC/G++ toolchain for native testing
- ESP32 platform pre-installed

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

## Common Workflows

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

1. Try building: `docker-compose run --rm stepaware-dev pio run`
2. Run native tests: `docker-compose run --rm stepaware-dev pio test -e native`
3. Run Python simulator: `docker-compose run --rm stepaware-dev python3 test/mock_simulator.py`
