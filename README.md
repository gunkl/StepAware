# StepAware

**ESP32-C3 Motion-Activated Hazard Warning System**

![Version](https://img.shields.io/badge/version-0.1.1-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)
![Build](https://github.com/yourusername/StepAware/workflows/CI%2FCD%20Pipeline/badge.svg)
![Tests](https://img.shields.io/badge/tests-95%20passing-brightgreen)

## Overview

StepAware is an intelligent IoT device designed to prevent accidents by detecting human motion and warning of hazards (such as a step-down in a hallway) that might be missed or forgotten. The system uses a PIR motion sensor to detect movement and provides bright LED warnings to alert people of the hazard ahead.

### Primary Use Case

Detect a human entering a specific area (e.g., hallway) and notify them of a hazard (a step down) that they might not see or might forget exists, preventing trips and falls.

### Key Features

#### Core Functionality
- **Motion Detection**: AM312 PIR sensor with 12m range and 65° detection angle
- **LED Warning**: Bright, visible, repeated blinking for 15+ seconds when motion detected
- **8x8 LED Matrix Display** (Optional): Animated visual warnings and status displays with custom animation support
- **Multiple Operating Modes**: OFF, Continuous ON, Motion Detection, Night Light modes
  - **OFF Mode**: Deep sleep with button wake capability (maximum power savings)
  - **Continuous ON**: Always flashing hazard warning
  - **Motion Detection**: LED activates only when motion detected (default)
  - **Motion + Light Sensing**: Activate only in darkness using ambient light sensor
  - **Night Light Mode**: Low brightness steady/flashing glow (with or without motion sensing)

#### Power Management
- **Battery Powered**: 1000mAh LiPo battery with built-in charging and monitoring
- **Low Battery Notification**: Distinctive blink pattern when battery below 25%
- **Charge Indication**: LED patterns indicate charging status and charge level
- **Power Efficient**: Up to 11 days battery life in motion detection mode

#### Configuration & Control
- **Physical Controls**: Mode button for cycling through operational modes
- **WiFi Configuration**: Web-based configuration interface with authentication
- **Configuration Files**: Simple JSON files editable on device
- **Remote Monitoring**: Real-time status and activity history via web dashboard
- **Secure Access**: HTTPS/SSH support with proper security keys

#### User Input Methods
- **Mode Button**: Physical button to switch between operational modes
- **Web Interface**: Browser-based configuration and monitoring
- **Config Files**: Direct file editing for advanced users

## Hardware Requirements

### Components

| Component | Specification | Quantity |
|-----------|--------------|----------|
| **ESP32-C3-DevKit-Lipo** | Olimex development board | 1 |
| **AM312 PIR Sensor** | Passive infrared motion sensor | 1 |
| **LED** | High-brightness white LED | 1 |
| **Resistors** | 220Ω (LED), 10kΩ (pull-up) | 2 |
| **Pushbutton** | Momentary tactile switch | 1 |
| **Photoresistor** | For ambient light sensing (optional) | 1 |
| **8x8 LED Matrix** | Adafruit Mini w/I2C Backpack (optional) | 1 |
| **LiPo Battery** | 1000mAh, 3.7V | 1 |
| **Breadboard** | For prototyping | 1 |
| **Jumper Wires** | Male-to-male | 20 |

**Total Cost**: ~$33 (+ $8 for optional LED Matrix)

### Pin Connections (ESP32-C3)

| GPIO Pin | Function | Mode | Notes |
|----------|----------|------|-------|
| GPIO0 | Mode Button | All | Built-in boot button, pull-up ⚠️ **FIXED** |
| GPIO1 | PIR Near | Dual-PIR | Primary motion sensor, deep sleep wakeup ✅ |
| GPIO1 | PIR Sensor | Single-PIR | Motion sensor, deep sleep wakeup ✅ |
| GPIO2 | Status LED | All | Built-in LED on board ⚠️ **FIXED** |
| GPIO3 | Hazard LED | All | Main warning LED (PWM) |
| GPIO4 | PIR Far | Dual-PIR | Direction detection, deep sleep wakeup ✅ |
| GPIO4 | Light Sensor | Single-PIR | Photoresistor (ADC, optional) |
| GPIO5 | Battery Monitor | All | Built-in voltage divider. **⚠️ NEVER use for external sensors!** |
| GPIO6 | VBUS Detect | All | USB power detection |
| GPIO7 | I2C SDA | All | LED Matrix data (optional) |
| GPIO8 | Ultrasonic/IR | All | Distance sensor (optional) |
| GPIO9 | Ultrasonic/IR | All | Distance sensor (optional) |
| GPIO10 | I2C SCL | All | LED Matrix clock (optional) |
| GPIO11 | (Reserved) | - | Available for future expansion |

### Pin Assignment Notes:

**Deep Sleep Wakeup Requirement:**
- ESP32-C3 only supports GPIO 0-5 for deep sleep wakeup
- Both PIR sensors MUST be on GPIO 0-5 in dual-PIR mode
- This is why GPIO1 (PIR_NEAR) and GPIO4 (PIR_FAR) were chosen

**GPIO5 Programming Issue:**
- GPIO5 interferes with device programming/flashing when external sensors are connected
- Reserved exclusively for internal battery monitoring
- **NEVER connect PIR sensors or other external devices to GPIO5**
- If you cannot program the device, disconnect all external sensors from GPIO5

**Dual-PIR vs Single-PIR Mode:**
- **Dual-PIR**: Uses GPIO1 (near) + GPIO4 (far) for direction detection. Light sensor NOT available.
- **Single-PIR**: Uses GPIO1 only. Light sensor available on GPIO4 (optional).

See [docs/hardware/wiring_diagram.png](docs/hardware/) for complete wiring schematic.

## Software Architecture

```
┌─────────────────────────────────────┐
│         Main Application            │
│    (Event Loop & Coordination)      │
└──────────┬──────────────────────────┘
           │
     ┌─────┴─────┐
     │           │
┌────▼────┐  ┌──▼─────────┐
│  State  │  │  Hardware  │
│ Machine │  │ Abstraction│
│         │  │   Layer    │
└─────────┘  └──┬─────────┘
                │
        ┌───────┼───────┐
        │       │       │
    ┌───▼──┐ ┌──▼──┐ ┌─▼───┐
    │ PIR  │ │ LED │ │ ADC │
    └──────┘ └─────┘ └─────┘

┌────────────────────────────────────┐
│     Support Services               │
├────────────┬──────────┬────────────┤
│ Config Mgr │ Logger   │ Web Server │
└────────────┴──────────┴────────────┘
```

### Major Subsystems

#### WiFi Manager
Robust WiFi connectivity with automatic reconnection and exponential backoff:
- **Access Point (AP) Mode**: Captive portal for initial setup
- **Station Mode**: Connects to existing WiFi networks
- **Auto-Reconnection**: Exponential backoff (1s → 2s → 4s → 8s → max 60s)
- **Signal Monitoring**: RSSI tracking and connection quality metrics
- **Status Reporting**: Connection uptime, failure counts, reconnection attempts

#### Power Manager
Intelligent battery management with multiple power states:
- **Battery Monitoring**: Voltage reading with 10-sample moving average filter
- **State Management**: Active, Light Sleep, Deep Sleep, Low Battery, Critical Battery, Charging
- **Smart Sleep**: Automatic sleep based on inactivity timeout
- **Wake Events**: Button, PIR motion, timer-based wake
- **Power Statistics**: Active time, sleep time, wake count tracking
- **Battery Percentage**: Non-linear voltage-to-percentage conversion (3.0V-4.2V range)

#### Watchdog Manager
Comprehensive system health monitoring with automatic recovery:
- **9 Health Check Modules**: Memory, State Machine, Config Manager, Logger, HAL Button, HAL LED, HAL PIR, Web Server, WiFi Manager
- **5 Recovery Functions**: Memory cleanup, State Machine reset, Config Manager reload, Logger reset, WiFi reconnection
- **Health Status Levels**: OK, Warning, Critical, Failed
- **Automatic Recovery**: Attempts recovery when modules report Warning/Critical status
- **Periodic Checks**: Configurable health check interval (default: 30 seconds)

#### Optional Displays & Sensors
- **8x8 LED Matrix**: Enhanced visual feedback with built-in and custom animations
  - 4 built-in animations (Motion Alert, Battery Low, Boot Status, WiFi Connected)
  - Custom animation support via text file upload
  - Web-based animation management and template download
  - Up to 8 custom animations loaded simultaneously
  - Assign animations to system functions (motion, battery, boot, WiFi)
- **Multi-Sensor Support** (PIR, IR, Ultrasonic): Advanced motion detection with sensor fusion
  - Up to 4 sensors simultaneously with flexible fusion modes
  - Distance-based motion detection (20mm to 4m range)
  - Direction sensing (approaching vs. leaving)
  - Configurable thresholds and modes per sensor
  - Low-power trigger + high-power measurement combinations
  - See [Multi-Sensor Documentation](docs/MULTI_SENSOR.md)

## Quick Start

### Prerequisites

**Development Tools (All Installed ✅):**
- Docker Desktop 29.1.3 - For containerized development
- GitHub CLI 2.83.2 - For GitHub operations
- PlatformIO 6.1.18 (via Docker) - For building firmware
- GCC/G++ 14.2.0 (via Docker) - For native testing

**Hardware (Optional for testing):**
- ESP32-C3-DevKit-Lipo board
- USB-C cable for programming

**Note:** You can develop and test using Docker without the physical hardware. See [DOCKER_GUIDE.md](DOCKER_GUIDE.md) for details.

### Installation

1. **Clone the repository** (Already done ✅)

2. **Build using Docker** (Recommended):
   ```bash
   # Build the firmware
   docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo

   # Run native tests
   docker-compose run --rm stepaware-dev pio test -e native
   ```

3. **Upload to ESP32** (when hardware is connected):
   ```bash
   docker-compose run --rm stepaware-dev pio run --target upload
   ```

4. **Monitor serial output**:
   ```bash
   docker-compose run --rm stepaware-dev pio device monitor
   ```

See [DOCKER_GUIDE.md](DOCKER_GUIDE.md) for detailed Docker usage and [SETUP_PLATFORMIO.md](SETUP_PLATFORMIO.md) for native PlatformIO installation.

### First-Time Setup

1. Power on the device
2. Connect to the WiFi network "StepAware-XXXX"
3. Open browser to `http://192.168.4.1`
4. Configure WiFi credentials and settings
5. Device will connect to your network

## Operating Modes

| Mode | Description | LED Behavior |
|------|-------------|--------------|
| **OFF** | Deep sleep, button wake only | Off |
| **Continuous ON** | Always flashing hazard warning | Constant blinking |
| **Motion Detection** | Activate on motion (default) | 15s warning on motion |
| **Motion + Light** | Activate only in darkness | Warning in dark only |
| **Night Light** | Low brightness always on | Dim steady glow |
| **Night Light Motion** | Dim light on motion | Dim on motion detect |

### Mode Switching

- **Physical Button**: Press the mode button (GPIO0) to cycle through modes
- **Web Interface**: Select mode from configuration dashboard
- **Configuration File**: Edit `/data/config/config.json` on device

## Configuration

### Web Interface

Access the web dashboard at `http://<device-ip>` to configure:

- Operating mode
- LED brightness
- Motion sensitivity
- WiFi credentials
- Battery thresholds
- Night light settings

### Configuration File (Advanced)

The device stores configuration in `/data/config/config.json`:

```json
{
  "mode": "MOTION_DETECT",
  "led_brightness": 255,
  "motion_timeout_ms": 15000,
  "battery_low_threshold": 25,
  "light_dark_threshold": 500,
  "wifi": {
    "ssid": "YourNetwork",
    "password": "YourPassword"
  }
}
```

## API Documentation

RESTful API endpoints for programmatic access:

- `GET /api/status` - Current device status
- `POST /api/config` - Update configuration
- `GET /api/history` - Activity log
- `GET /api/version` - Firmware version
- `GET /events` - Server-Sent Events stream

See [docs/api/web_api_spec.md](docs/api/) for complete API reference.

## Development

### For Developers

This project uses PlatformIO for building and testing. You can develop natively or using Docker.

**Quick Commands:**
```bash
# Build firmware (Docker)
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo

# Run tests (Docker)
docker-compose run --rm stepaware-dev pio test -e native

# Build firmware (Native)
pio run -e esp32-devkitlipo

# Run tests (Native)
pio test -e native
```

See [AGENTS.md](AGENTS.md) for complete development workflows, build procedures, and code quality standards.

## Power Consumption

| Mode | Current | Battery Life (1000mAh) |
|------|---------|----------------------|
| Active Warning | ~220mA | - |
| Motion Detect | ~37µA | ~270 hours (11 days) |
| WiFi Active | ~200mA | ~5 hours |
| Deep Sleep (OFF) | ~3mA | ~330 hours (14 days) |

## Troubleshooting

For common issues and solutions, see the comprehensive [Troubleshooting Guide](TROUBLESHOOTING.md).

**Quick Reference:**

| Issue | Quick Fix |
|-------|-----------|
| Device won't boot | Check battery charge, try different USB cable |
| Motion not detected | Wait 60s PIR warm-up, verify GPIO6 connection |
| WiFi won't connect | Check 2.4GHz network, verify credentials |
| Battery drains quickly | Use MOTION_DETECT mode, reduce WiFi usage |
| Can't access web UI | Verify IP address from serial console |
| LED not working | Check polarity (long leg = anode), verify 220Ω resistor |
| Can't program/flash | Disconnect external sensors from GPIO5 during programming |

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for detailed diagnostic procedures and recovery options.

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Run tests and ensure they pass (`pio test -e native`)
4. Commit changes (`git commit -m 'Add AmazingFeature'`)
5. Push to branch (`git push origin feature/AmazingFeature`)
6. Open a Pull Request

For AI-assisted development, see [AGENTS.md](AGENTS.md) for development workflows and code quality standards.

## Testing

The project includes comprehensive testing infrastructure with **95 tests** across 8 test suites:

### Test Suite Breakdown

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| **Button Reset** | 7 | WiFi/factory reset timing, threshold detection, LED feedback |
| **HAL Button** | 6 | Button debouncing, state changes, press detection |
| **Integration** | 14 | Multi-component interaction, system scenarios |
| **Power Manager** | 20 | Battery monitoring, power states, sleep modes, filtering |
| **State Machine** | 10 | Mode transitions, motion detection, warning behavior |
| **Web API** | 12 | Component integration, status reporting, mock components |
| **WiFi Manager** | 14 | Connection states, reconnection, RSSI monitoring |
| **WiFi Watchdog** | 12 | WiFi health monitoring, signal strength, recovery actions |

### Testing Infrastructure

- **C++ Unity Tests**: Native tests via PlatformIO (`pio test -e native`)
- **Mock-Based Testing**: Hardware abstraction allows testing without physical components
- **Docker Support**: Containerized test environment for consistency
- **Test Database**: SQLite storage of all test runs with historical tracking
- **Test Reports**: HTML reports with visual results
- **Test Analysis**: Detailed analytics via `test/analyze_results.py`

### Running Tests

```bash
# Run all C++ Unity tests (Docker)
docker-compose run --rm stepaware-dev pio test -e native

# Run all C++ Unity tests (Native)
pio test -e native

# Run specific test suite
pio test -e native -f test_power_manager

# View test analysis
python3 test/analyze_results.py
```

All test outputs are stored in `test/reports/` and excluded from git. When using Docker, the volume mount ensures all test artifacts are immediately accessible in your local filesystem.

## Documentation

### User Documentation
- **[Hardware Assembly Guide](docs/hardware/HARDWARE_ASSEMBLY.md)** - Step-by-step assembly instructions with wiring diagrams
- **[Troubleshooting Guide](TROUBLESHOOTING.md)** - Common issues, diagnostic procedures, and recovery options
- **[API Reference](docs/api/API.md)** - Complete REST API documentation with examples
- **[LED Matrix Custom Animations](data/animations/README.md)** - Guide for creating and managing custom animations
- **[Multi-Sensor Guide](docs/MULTI_SENSOR.md)** - Comprehensive guide for using multiple sensors with fusion modes
- [Docker Guide](DOCKER_GUIDE.md) - Docker-based development environment
- [PlatformIO Setup](SETUP_PLATFORMIO.md) - Native PlatformIO installation

### Developer Documentation
- [AGENTS.md](AGENTS.md) - AI agent development workflows and standards
- [WiFi Manager Design](docs/WIFI_MANAGER_DESIGN.md) - WiFi connectivity architecture
- [Power Manager Design](docs/POWER_MANAGER_DESIGN.md) - Battery and power management
- [Watchdog System Design](docs/WATCHDOG_DESIGN.md) - Health monitoring and recovery
- [Test Plan](docs/testing/test_plan.md) - Testing strategies and procedures
- **[LED Matrix Implementation](ISSUE12_COMPLETE.md)** - Complete 8x8 LED Matrix support documentation
- **[Multi-Sensor Implementation](ISSUE4_STATUS.md)** - Hardware universality and sensor support status (Issue #4)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- ESP32-C3 development board by Olimex
- AM312 PIR sensor datasheet and specifications
- PlatformIO build system
- Arduino framework for ESP32
- Community contributions and feedback

## Contact

- **Project Homepage**: [GitHub Repository](https://github.com/yourusername/StepAware)
- **Issues**: [Bug Reports & Feature Requests](https://github.com/yourusername/StepAware/issues)
- **Documentation**: [Full Documentation](docs/)

---

**StepAware** - Preventing accidents, one step at a time. 🚶‍♂️⚠️
