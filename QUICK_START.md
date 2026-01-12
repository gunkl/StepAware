# StepAware - Quick Start Guide

## ✅ Setup Complete!

Your StepAware project is built and ready to test!

---

## 🚀 How to Use PlatformIO

### Method 1: VSCode Terminal (Recommended)

**After reloading VSCode window**, the `pio` command will work:

```bash
pio --version      # Check version
pio run            # Build project
pio device monitor # Open serial monitor
```

**To reload VSCode window:**
- Press `Ctrl+Shift+P`
- Type: "Developer: Reload Window"
- Press Enter

---

### Method 2: Local Wrapper Script

In the project folder, you can use:

```bash
./pio --version
./pio run
./pio device monitor
```

---

### Method 3: Python Module (Always Works)

```bash
python3 -m platformio --version
python3 -m platformio run
python3 -m platformio device monitor
```

---

## 📋 Common Commands

### Build the Project
```bash
pio run
# or
./pio run
# or
python3 -m platformio run
```

### Upload to ESP32 (when connected)
```bash
pio run --target upload
```

### Open Serial Monitor
```bash
pio device monitor
# Press Ctrl+C to exit
```

### Clean Build
```bash
pio run --target clean
```

### Build Specific Environment
```bash
pio run -e esp32-devkitlipo
```

---

## 🧪 Testing with Mock Hardware

Since you don't have the hardware yet, the code runs in **MOCK_HARDWARE** mode.

### 1. Build and Monitor
```bash
# In VSCode terminal
pio run && pio device monitor
```

### 2. Use Serial Commands

Once the monitor is open, type these commands:

- `h` - Show help menu
- `s` - Print system status
- `m` - Trigger mock motion detection
- `b` - Simulate button press
- `0` - Set mode to OFF
- `1` - Set mode to CONTINUOUS_ON
- `2` - Set mode to MOTION_DETECT
- `r` - Reset statistics

### Example Session:
```
> h              (shows help)
> s              (shows status)
> m              (triggers motion - LED should blink for 15 seconds)
> b              (simulates button press - cycles to next mode)
> s              (check new status)
```

---

## 📦 Project Structure

```
StepAware/
├── platformio.ini          # Build config
├── src/main.cpp           # Main application
├── include/               # Headers
│   ├── config.h          # Pin definitions & constants
│   └── state_machine.h   # State machine
├── lib/HAL/              # Hardware abstraction
│   ├── hal_pir.*         # PIR motion sensor
│   ├── hal_led.*         # LED control
│   └── hal_button.*      # Button input
└── .vscode/
    └── settings.json     # VSCode + PlatformIO config
```

---

## 🔧 When Hardware Arrives

### Wiring Diagram

```
ESP32-C3          Component
--------          ---------
GPIO0   -----> Button ----> GND (pull-up enabled)
GPIO1   <----- AM312 OUT
3.3V    -----> AM312 VCC
GND     -----> AM312 GND
GPIO3   -----> LED (+) ----> 220Ω ----> GND
BAT     <----- LiPo battery
```

### Steps:
1. Wire components as shown above
2. Connect ESP32 via USB-C
3. Upload firmware:
   ```bash
   pio run --target upload
   ```
4. Open monitor:
   ```bash
   pio device monitor
   ```

---

## 📊 Build Status

✅ **Phase 1 MVP Complete**

- Memory Usage:
  - RAM: 6.8% (22,240 / 327,680 bytes)
  - Flash: 21.8% (285,933 / 1,310,720 bytes)

- Features:
  - ✅ PIR Motion Detection
  - ✅ LED Warning (15+ seconds)
  - ✅ Mode Switching (Button)
  - ✅ Mock Hardware Support
  - ✅ Serial Commands
  - ✅ Statistics Tracking

---

## 📚 Additional Resources

- Full README: [README.md](README.md)
- PlatformIO Setup: [SETUP_PLATFORMIO.md](SETUP_PLATFORMIO.md)
- Project Plan: [Implementation Plan](~/.claude/plans/pure-sprouting-dream.md)
- Agent Guidelines: [AGENTS.md](AGENTS.md)

---

## ❓ Need Help?

### PlatformIO Issues
See [SETUP_PLATFORMIO.md](SETUP_PLATFORMIO.md) for detailed troubleshooting

### Build Errors
```bash
# Clean and rebuild
pio run --target clean
pio run
```

### Serial Monitor Not Working
```bash
# List devices
pio device list

# Specify port manually
pio device monitor -p COM3  # Replace COM3 with your port
```

---

## 🎯 Next Steps

1. **Reload VSCode** to enable `pio` command
2. **Build project**: `pio run`
3. **Test with mock hardware**: `pio device monitor` and use serial commands
4. **Order hardware components** (see README.md for shopping list)
5. **Proceed to Phase 2** when ready (WiFi & Web Interface)

---

**Happy coding! 🚀**
