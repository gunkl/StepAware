# StepAware

**ESP32-C3 Motion-Activated Hazard Warning System**

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

## Overview

StepAware is an intelligent IoT device designed to prevent accidents by detecting human motion and warning of hazards (such as a step-down in a hallway) that might be missed or forgotten. The system uses a PIR motion sensor to detect movement and provides bright LED warnings to alert people of the hazard ahead.

### Key Features

- **Motion Detection**: AM312 PIR sensor with 12m range and 65° detection angle
- **LED Warning**: Bright 15+ second hazard notification
- **Multiple Operating Modes**: OFF, Continuous ON, Motion Detection, Night Light modes
- **Battery Powered**: LiPo battery with built-in charging and monitoring
- **WiFi Configuration**: Web-based configuration interface
- **Remote Monitoring**: Real-time status and activity history via web dashboard
- **Power Efficient**: Up to 11 days battery life in motion detection mode

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

- [PlatformIO](https://platformio.org/) installed
- ESP32-C3-DevKit-Lipo board
- USB-C cable for programming

### Installation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/StepAware.git
   cd StepAware
   ```

2. **Build the project**:
   ```bash
   pio run
   ```

3. **Upload to ESP32**:
   ```bash
   pio run --target upload
   ```

4. **Monitor serial output**:
   ```bash
   pio device monitor
   ```

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

### Project Structure

```
StepAware/
├── src/                  # Main source files
├── include/              # Public headers
├── lib/                  # Custom libraries (HAL, etc.)
├── test/                 # Test framework
├── data/                 # Web files, config templates
├── docs/                 # Documentation
├── scripts/              # Utility scripts
└── datasheets/           # Hardware datasheets (not in git)
```

### Building with Mock Hardware

For development without physical hardware:

```bash
pio run -D MOCK_HARDWARE=1
```

This enables mock implementations of all hardware interfaces.

### Running Tests

```bash
# Run all automated tests
pio test

# Run specific test suite
pio test -f test_state_machine

# Run assisted tests (requires hardware)
python scripts/test_runner.py --assisted
```

### Implementation Phases

The project is developed in 6 phases:

1. **Phase 1**: MVP - Core motion detection ✅ (Current)
2. **Phase 2**: WiFi & Web interface
3. **Phase 3**: Testing infrastructure
4. **Phase 4**: Documentation & versioning
5. **Phase 5**: Power management
6. **Phase 6**: Advanced features (light sensing)

See [implementation plan](https://github.com/anthropics/claude-code/plans/pure-sprouting-dream.md) for details.

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
3. Run tests and ensure they pass
4. Commit changes (`git commit -m 'Add AmazingFeature'`)
5. Push to branch (`git push origin feature/AmazingFeature`)
6. Open a Pull Request

See [AGENTS.md](AGENTS.md) for AI development workflow and [ai-workflow-guide.md](ai-workflow-guide.md) for best practices.

## Testing

The project includes comprehensive testing infrastructure:

- **Automated Tests**: Unit and integration tests run via PlatformIO
- **Assisted Tests**: Interactive hardware verification
- **Test Database**: SQLite storage of all test runs
- **Test Reports**: Markdown and HTML reports with historical comparison

## Documentation

- [Hardware Wiring Diagram](docs/hardware/wiring_diagram.png)
- [State Machine Flowchart](docs/architecture/state_machine_diagram.png)
- [API Specification](docs/api/web_api_spec.md)
- [Test Plan](docs/testing/test_plan.md)
- [AI Agent Guide](AGENTS.md)
- [Development Workflow](ai-workflow-guide.md)

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
