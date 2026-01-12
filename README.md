# StepAware

**ESP32-C3 Motion-Activated Hazard Warning System**

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

## Overview

StepAware is an intelligent IoT device designed to prevent accidents by detecting human motion and warning of hazards (such as a step-down in a hallway) that might be missed or forgotten. The system uses a PIR motion sensor to detect movement and provides bright LED warnings to alert people of the hazard ahead.

### Primary Use Case

Detect a human entering a specific area (e.g., hallway) and notify them of a hazard (a step down) that they might not see or might forget exists, preventing trips and falls.

### Key Features

#### Core Functionality
- **Motion Detection**: AM312 PIR sensor with 12m range and 65° detection angle
- **LED Warning**: Bright, visible, repeated blinking for 15+ seconds when motion detected
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
| **LiPo Battery** | 1000mAh, 3.7V | 1 |
| **Breadboard** | For prototyping | 1 |
| **Jumper Wires** | Male-to-male | 20 |

**Total Cost**: ~$33

### Pin Connections (ESP32-C3)

| GPIO Pin | Function | Notes |
|----------|----------|-------|
| GPIO0 | Mode Button | Built-in boot button, pull-up |
| GPIO1 | PIR Sensor | AM312 output signal |
| GPIO2 | Status LED | Built-in LED on board |
| GPIO3 | Hazard LED | Main warning LED (PWM) |
| GPIO4 | Light Sensor | Photoresistor (optional) |
| GPIO5 | Battery Monitor | Built-in voltage divider |

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

### Device won't boot
- Check battery charge level
- Verify USB-C connection
- Press reset button
- Check serial output for errors

### Motion not detected
- Wait 1 minute for PIR warm-up
- Check sensor wiring (GPIO1)
- Verify mode is set to MOTION_DETECT
- Test sensor range (within 12m, 65° angle)

### WiFi connection fails
- Verify credentials in configuration
- Check WiFi signal strength
- Try factory reset (hold button 10s)
- Check router allows 2.4GHz connections

### Battery drains quickly
- Check operating mode (continuous ON uses more power)
- Verify no WiFi reconnection loops
- Check for excessive motion events
- Consider deep sleep mode when not in use

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

The project includes comprehensive testing infrastructure with automated and assisted tests:

- **Python Logic Tests**: Fast unit tests via `test/run_tests.py`
- **C++ Unity Tests**: Native tests via PlatformIO (`pio test -e native`)
- **Mock Simulator**: Interactive testing tool (`test/mock_simulator.py`)
- **Test Database**: SQLite storage of all test runs with historical tracking
- **Test Reports**: HTML reports with visual results
- **Test Analysis**: Detailed analytics via `test/analyze_results.py`

### Running Tests

```bash
# Run Python logic tests (generates HTML report in test/reports/)
python3 test/run_tests.py

# Run C++ Unity tests
pio test -e native

# View test analysis
python3 test/analyze_results.py

# Interactive mock simulator
python3 test/mock_simulator.py
```

All test outputs are stored in `test/reports/` and excluded from git. When using Docker, the volume mount ensures all test artifacts are immediately accessible in your local filesystem.

## Documentation

### User Documentation
- [Hardware Wiring Diagram](docs/hardware/wiring_diagram.png) - Physical component connections
- [API Specification](docs/api/web_api_spec.md) - REST API reference
- [Docker Guide](DOCKER_GUIDE.md) - Docker-based development
- [PlatformIO Setup](SETUP_PLATFORMIO.md) - Native PlatformIO installation

### Developer Documentation
- [AGENTS.md](AGENTS.md) - AI agent development workflows and standards
- [State Machine Flowchart](docs/architecture/state_machine_diagram.png) - Program flow diagrams
- [Test Plan](docs/testing/test_plan.md) - Testing strategies and procedures

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
