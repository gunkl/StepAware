# StepAware Troubleshooting Guide

**Version:** 0.1.0
**Last Updated:** January 12, 2026

This guide helps diagnose and resolve common issues with the StepAware motion-activated hazard warning system.

---

## Table of Contents

1. [Quick Diagnostic Checklist](#quick-diagnostic-checklist)
2. [Power Issues](#power-issues)
3. [Motion Detection Issues](#motion-detection-issues)
4. [LED Issues](#led-issues)
5. [WiFi Connectivity Issues](#wifi-connectivity-issues)
6. [Battery Issues](#battery-issues)
7. [Web Interface Issues](#web-interface-issues)
8. [Firmware & Upload Issues](#firmware--upload-issues)
9. [Serial Console Issues](#serial-console-issues)
10. [System Health & Watchdog Issues](#system-health--watchdog-issues)
11. [Recovery Procedures](#recovery-procedures)
12. [Interpreting Serial Output](#interpreting-serial-output)

---

## Quick Diagnostic Checklist

Before diving into specific issues, run through this quick checklist:

- [ ] Battery is charged (> 3.3V)
- [ ] USB cable is functional (try different cable)
- [ ] All connections are secure
- [ ] PIR sensor has had 60 seconds to warm up
- [ ] Correct firmware uploaded
- [ ] Device is not in deep sleep mode (press button to wake)
- [ ] Serial console shows output at 115200 baud
- [ ] No physical damage to components

---

## Power Issues

### Device Won't Turn On

**Symptoms:**
- No LEDs illuminate
- No serial output
- Device appears completely dead

**Diagnostic Steps:**

1. **Check Battery Voltage:**
   ```bash
   # Use multimeter to measure battery voltage at JST connector
   # Expected: 3.0V - 4.2V
   # If < 3.0V: Battery is critically discharged
   # If 0V: Battery is dead or disconnected
   ```

2. **Test USB Power:**
   - Disconnect battery
   - Connect USB-C cable
   - Check for power LED on board
   - If no LED: Try different USB cable/port

3. **Check for Short Circuits:**
   - Disconnect all power
   - Use multimeter in continuity mode
   - Test between 3.3V and GND (should be open circuit)
   - If continuity: Check for solder bridges or misplaced wires

**Solutions:**

| Cause | Solution |
|-------|----------|
| Dead battery | Charge via USB for 2-3 hours |
| Bad USB cable | Try data-capable USB cable (not charge-only) |
| JST connector loose | Firmly reseat battery connector |
| Damaged board | Check for burnt components, may need replacement |
| Deep sleep mode | Press boot button to wake device |

### Device Reboots Randomly

**Symptoms:**
- Device restarts unexpectedly
- Serial console shows boot messages repeatedly
- Uptime counter resets frequently

**Diagnostic Steps:**

1. **Check Serial Output for Clues:**
   ```
   Guru Meditation Error: → Memory issue
   Watchdog timeout → Infinite loop or blocking code
   Brownout detector → Power supply problem
   ```

2. **Monitor Supply Voltage:**
   - Use multimeter on 3.3V pin
   - Should remain stable at 3.3V ±0.1V
   - Dips below 3.0V indicate power issue

3. **Check Watchdog Status:**
   ```bash
   # Access API endpoint
   curl http://<device-ip>/api/status
   # Check "watchdog": { "systemHealth": 0, "healthName": "OK" }
   ```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Low battery | Charge or replace battery |
| Insufficient power supply | Use 2A+ USB power adapter |
| Memory leak | Flash latest firmware |
| Watchdog timeout | Check logs for stuck module |
| EMI interference | Add 100µF capacitor across power rails |

---

## Motion Detection Issues

### PIR Sensor Not Detecting Motion

**Symptoms:**
- Hazard LED never activates
- Serial log shows no motion events
- `/api/status` shows `motionEvents: 0` after waving in front of sensor

**Diagnostic Steps:**

1. **Verify PIR Warm-Up Period:**
   - PIR sensors require 60 seconds after power-on
   - Check serial log for: `[HAL_PIR] Warm-up complete`

2. **Test PIR Sensor Directly:**
   ```bash
   # Connect to serial console
   # Move slowly across sensor field of view
   # Look for: "[HAL_PIR] Motion detected!"
   ```

3. **Check Wiring:**
   - PIR VCC → ESP32 3.3V ✓
   - PIR OUT → ESP32 GPIO6 ✓
   - PIR GND → ESP32 GND ✓
   - Measure voltage at PIR VCC: Should be 3.3V

4. **Test GPIO6 Signal:**
   - Use multimeter or oscilloscope
   - Measure voltage at GPIO6
   - Should toggle HIGH (3.3V) when motion detected

**Solutions:**

| Cause | Solution |
|-------|----------|
| Sensor not warmed up | Wait 60 seconds, power cycle if necessary |
| Wrong GPIO pin | Verify GPIO6 connection, check config.h |
| Sensor out of range | Move within 2-3 meters during testing |
| Sensor too sensitive | Adjust potentiometer (if available) clockwise to decrease |
| Sensor not sensitive enough | Adjust potentiometer counter-clockwise to increase |
| Faulty sensor | Replace AM312 PIR sensor |
| Blocked sensor lens | Ensure lens is clean and unobstructed |

### False Motion Detections

**Symptoms:**
- Motion detected when no one is present
- Excessive motion events in logs
- LED activating randomly

**Common Causes:**
- **Environmental:** Moving curtains, pets, fans, heaters
- **Electrical:** Power supply noise, EMI from nearby devices
- **Sensor:** Overly sensitive, poor quality sensor

**Solutions:**

1. **Reduce Sensitivity:**
   - Adjust PIR potentiometer (if available)
   - Start with minimum sensitivity and increase gradually

2. **Relocate Sensor:**
   - Point away from heat sources (radiators, sunlight)
   - Avoid areas with moving objects
   - Mount in stable location

3. **Add Software Filtering:**
   - Increase motion timeout in config
   - Add debouncing delay (modify code)

4. **Shield from EMI:**
   - Keep away from switching power supplies
   - Add ferrite bead on PIR power line
   - Use shielded cable for PIR signal (if long run)

---

## LED Issues

### Hazard LED Not Working

**Symptoms:**
- LED never illuminates
- LED always on (not blinking)
- LED very dim

**Diagnostic Steps:**

1. **Check LED Polarity:**
   - Long leg (anode) should connect to GPIO3 (through resistor)
   - Short leg (cathode) should connect to GND
   - Flat edge on LED housing indicates cathode

2. **Test LED Directly:**
   - Disconnect from circuit
   - Use multimeter diode test mode
   - Should show ~2-3V forward voltage
   - If OL (open): LED is burned out

3. **Check GPIO3 Signal:**
   ```bash
   # Set mode to CONTINUOUS_ON
   curl -X POST http://<device-ip>/api/mode -d '{"mode": 1}'
   # Measure voltage at GPIO3 with multimeter
   # Should toggle between 0V and 3.3V
   ```

4. **Verify Resistor Value:**
   - Measure with multimeter: Should be 220Ω ±5%
   - Missing resistor will cause excessive current

**Solutions:**

| Cause | Solution |
|-------|----------|
| LED reversed | Flip LED (anode to resistor, cathode to GND) |
| Burned out LED | Replace with new LED |
| Wrong GPIO | Verify GPIO3 connection |
| Missing resistor | Add 220Ω resistor in series |
| Wrong resistor value | Use 220Ω (not 22Ω or 2.2kΩ) |
| Software issue | Check mode setting, verify pattern is active |

### LED Pattern Wrong

**Symptoms:**
- LED blinks at wrong rate
- Pulse pattern doesn't fade smoothly
- LED behavior doesn't match mode

**Diagnostic Steps:**

1. **Check Current Mode:**
   ```bash
   curl http://<device-ip>/api/mode
   # Verify "modeName" matches expected
   ```

2. **Check Pattern Constants:**
   - Review `include/config.h` for timing values:
   ```cpp
   #define LED_BLINK_FAST_MS    200   // Fast blink interval
   #define LED_BLINK_SLOW_MS    1000  // Slow blink interval
   #define LED_BLINK_WARNING_MS 100   // Warning blink interval
   ```

3. **Test Each Pattern:**
   - PATTERN_OFF: LED off
   - PATTERN_ON: LED steady on
   - PATTERN_BLINK_FAST: 200ms on/off
   - PATTERN_BLINK_SLOW: 1000ms on/off
   - PATTERN_PULSE: Smooth 2-second fade cycle

**Solutions:**

| Issue | Solution |
|-------|----------|
| Pattern timing wrong | Adjust constants in config.h, rebuild |
| Pulse not smooth | Verify PWM frequency (5000Hz) and resolution (8-bit) |
| Pattern stuck | Reset state machine via `/api/reset` |
| Custom pattern incorrect | Use `setCustomPattern()` to adjust timings |

---

## WiFi Connectivity Issues

### Can't Connect to WiFi Network

**Symptoms:**
- Device shows `STATE_CONNECTING` or `STATE_FAILED`
- Never obtains IP address
- Serial shows "WiFi connection failed" repeatedly

**Diagnostic Steps:**

1. **Verify WiFi Credentials:**
   ```bash
   # Check current config
   curl http://<device-ip>/api/config
   # Verify "ssid" and "password" fields
   ```

2. **Check Network Compatibility:**
   - ESP32-C3 supports **2.4GHz only** (not 5GHz)
   - WPA2-PSK or WPA3 encryption
   - SSID must be broadcasting (not hidden)

3. **Check Signal Strength:**
   - Move device closer to router during testing
   - Metal enclosures can block signal
   - Check for interference from other devices

4. **Review Serial Output:**
   ```
   [WiFi] Connecting to MyNetwork...
   [WiFi] Connection failed: TIMEOUT
   [WiFi] Reconnecting in 2s...
   ```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Wrong password | Update via `/api/config` or AP mode |
| 5GHz network | Connect to 2.4GHz network |
| Weak signal | Move closer to router, improve antenna placement |
| MAC filtering | Add device MAC to router whitelist |
| Hidden SSID | Disable SSID hiding on router |
| Router at capacity | Check router DHCP lease limit |
| Special characters in password | Use only alphanumeric + basic symbols |

### WiFi Disconnects Frequently

**Symptoms:**
- `/api/status` shows high `reconnects` count
- Connection uptime is low
- `rssi` value very negative (< -80 dBm)

**Diagnostic Steps:**

1. **Monitor RSSI:**
   ```bash
   curl http://<device-ip>/api/status | grep rssi
   # Good: -30 to -60 dBm
   # Fair: -60 to -70 dBm
   # Poor: -70 to -80 dBm
   # Very Poor: < -80 dBm
   ```

2. **Check Failure Counts:**
   ```bash
   curl http://<device-ip>/api/status
   # Look at wifi.failures and wifi.reconnects
   ```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Weak signal | Improve router placement, add WiFi extender |
| Router overload | Reduce number of connected devices |
| Power management | Disable WiFi sleep modes in router |
| Channel congestion | Change router to less crowded channel |
| Interference | Move away from microwave, Bluetooth devices |

### Can't Access AP Mode

**Symptoms:**
- Can't see "StepAware-XXXX" WiFi network
- Can't connect to captive portal
- AP mode not starting

**Diagnostic Steps:**

1. **Force AP Mode:**
   - Hold boot button for 10 seconds during power-on
   - Look for serial message: `[WiFi] Starting AP mode`

2. **Check for AP SSID:**
   - Scan for WiFi networks on phone/computer
   - Look for "StepAware-" followed by device ID

3. **Verify AP Mode Config:**
   - Check `include/config.h` for AP mode settings
   - Default IP: 192.168.4.1

**Solutions:**

| Issue | Solution |
|-------|----------|
| AP mode disabled | Check WiFi Manager state in serial log |
| Can't see SSID | Phone may not support 2.4GHz, use different device |
| Can't connect to 192.168.4.1 | Ensure phone connected to StepAware AP, not cellular |
| Captive portal not opening | Manually navigate to http://192.168.4.1 |

---

## Battery Issues

### Battery Not Charging

**Symptoms:**
- Battery voltage not increasing when USB connected
- No charging indicator LED
- Voltage remains constant

**Diagnostic Steps:**

1. **Check Current Voltage:**
   ```bash
   curl http://<device-ip>/api/status | grep batteryVoltage
   # 4.2V = Fully charged (charging will stop)
   # 3.7-4.1V = Charging should be active
   # < 3.5V = Should charge rapidly
   ```

2. **Verify USB Power:**
   - Measure voltage at USB VBUS (should be 5V)
   - Try different USB cable (data-capable, not charge-only)
   - Try different power source (wall adapter, not PC USB)

3. **Check Charging Circuit:**
   - Some boards have charging LED (check if lit)
   - Measure current into battery (should be 100-500mA when charging)

**Solutions:**

| Cause | Solution |
|-------|----------|
| Battery fully charged | Normal - charging stops at 4.2V |
| Bad USB cable | Use data+power cable |
| Weak USB port | Use 2A wall adapter |
| Battery protection active | Disconnect battery for 30s, reconnect |
| Faulty charging circuit | Check board documentation, may need repair |

### Battery Drains Too Quickly

**Symptoms:**
- Battery lasts < 8 hours in motion detect mode
- `batteryPercent` drops rapidly
- Device sleeps but battery still drains

**Expected Battery Life (1000mAh):**
- **Motion Detect Mode:** ~11 days (270 hours)
- **Continuous ON Mode:** ~4-5 hours
- **WiFi Active:** ~5 hours
- **Deep Sleep (OFF):** ~14 days (330 hours)

**Diagnostic Steps:**

1. **Check Power State:**
   ```bash
   curl http://<device-ip>/api/status
   # power.stateName should be "LIGHT_SLEEP" when idle
   # power.activeTime vs power.sleepTime ratio
   ```

2. **Review Wake Count:**
   - High `wakeCount` indicates frequent waking
   - Check for excessive motion events
   - Verify sleep timeout is reasonable

3. **Check for WiFi Reconnect Loops:**
   - High `wifi.reconnects` drains battery
   - Poor signal causes constant retry

**Solutions:**

| Cause | Solution |
|-------|----------|
| Wrong operating mode | Use MOTION_DETECT, not CONTINUOUS_ON |
| WiFi always on | Disable WiFi or improve signal strength |
| Excessive motion events | Reduce PIR sensitivity |
| Sleep mode not working | Check power manager state in logs |
| Old/damaged battery | Replace battery |
| Insufficient sleep timeout | Increase inactivity timeout in config |

### Low Battery Warning Not Working

**Symptoms:**
- Device shuts down without warning
- `batteryPercent` shows incorrect value
- Warning blink pattern never activates

**Diagnostic Steps:**

1. **Check Battery Thresholds:**
   ```bash
   curl http://<device-ip>/api/config
   # Verify "battery_low_threshold" (default: 25%)
   ```

2. **Verify Battery Monitoring:**
   ```bash
   curl http://<device-ip>/api/status
   # Check power.low and power.critical flags
   # batteryVoltage should be accurate
   ```

3. **Test Low Battery Condition:**
   - Let battery drain to ~3.4V
   - Check for warning pattern (fast blink)
   - Verify `power.low: true` in status

**Solutions:**

| Issue | Solution |
|-------|----------|
| Threshold too low | Increase threshold via `/api/config` |
| ADC not calibrated | Battery voltage reading inaccurate |
| Warning pattern not visible | Check LED brightness setting |
| Battery percentage calculation | Verify voltage-to-percentage curve in code |

---

## Web Interface Issues

### Can't Access Web Dashboard

**Symptoms:**
- Browser shows "Connection refused" or "Timeout"
- Can't reach `http://<device-ip>`
- API endpoints return errors

**Diagnostic Steps:**

1. **Verify WiFi Connection:**
   ```bash
   # Check device is connected
   curl http://<device-ip>/api/status
   # If this fails, device not on network
   ```

2. **Find Device IP Address:**
   - Check serial console for: `[WiFi] Connected! IP: 192.168.1.XXX`
   - Check router DHCP lease table
   - Use network scanner (e.g., `nmap`, Fing app)

3. **Test Basic Connectivity:**
   ```bash
   ping <device-ip>
   # Should get responses
   ```

4. **Check Web Server Status:**
   ```bash
   curl -v http://<device-ip>/api/version
   # Should return firmware version JSON
   ```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Wrong IP address | Verify IP from serial console or router |
| WiFi not connected | Fix WiFi connection first |
| Firewall blocking | Disable firewall temporarily to test |
| Web server not started | Check serial logs for web server errors |
| CORS issue | Access from same network, not external |

### API Returns Errors

**Symptoms:**
- `/api/status` returns 500 error
- `/api/config` POST fails
- JSON parse errors

**Common Error Responses:**

```json
// 400 Bad Request
{"error": "Invalid JSON", "code": 400}

// 400 Missing Field
{"error": "Missing 'mode' field", "code": 400}

// 500 Internal Error
{"error": "Failed to save configuration", "code": 500}
```

**Solutions:**

| Error | Cause | Solution |
|-------|-------|----------|
| Invalid JSON | Malformed request | Validate JSON syntax |
| Missing field | Required parameter not provided | Include all required fields |
| Out of memory | Heap exhausted | Reduce request size, restart device |
| Save failed | SPIFFS full or corrupt | Factory reset, reflash filesystem |

---

## Firmware & Upload Issues

### Can't Upload Firmware

**Symptoms:**
- `pio run --target upload` fails
- "Failed to connect to ESP32" error
- Upload timeout

**Diagnostic Steps:**

1. **Check USB Connection:**
   ```bash
   # List serial ports (Linux/Mac)
   ls /dev/tty*
   # Windows
   mode
   ```

2. **Verify Correct Port:**
   - Check `platformio.ini` for `upload_port`
   - May need to manually specify port

3. **Try Boot Mode:**
   - Hold BOOT button (GPIO0)
   - Press and release RESET button
   - Release BOOT button
   - Retry upload

**Solutions:**

| Issue | Solution |
|-------|----------|
| Port not detected | Install USB-to-serial drivers (CP210x or CH340) |
| Permission denied (Linux) | Add user to dialout group: `sudo usermod -a -G dialout $USER` |
| Wrong upload speed | Try lower baud rate: `upload_speed = 115200` |
| Device in deep sleep | Wake device, retry upload |
| Corrupted bootloader | Flash via esptool.py directly |
| GPIO5 interference | Disconnect external sensors from GPIO5 (see below) |

### GPIO5 Programming Interference Issue

**Symptoms:**
- Device cannot be programmed/flashed via USB
- Upload fails or hangs during programming
- Device appears "bricked" but works after successful flash

**Cause:**
GPIO5 on the ESP32-C3 can interfere with the programming/flashing process when external sensors or devices are connected to it. This is particularly problematic with PIR sensors or other devices that may pull the pin high/low during the boot sequence.

**Solution:**
1. **Disconnect any external devices from GPIO5** before programming
2. If you have a PIR sensor or other device connected to GPIO5, **move it to GPIO6** (the new default)
3. After moving the sensor, update your configuration if needed
4. Program the device with no external sensors on GPIO5
5. Reconnect sensors after successful programming

**Prevention:**
- **Always use GPIO6 for PIR sensors** (current default)
- Reserve GPIO5 exclusively for battery monitoring (internal use)
- Avoid connecting any external sensors or pull-up/pull-down resistors to GPIO5

**Note:** This issue was discovered through field testing and the default PIR sensor pin has been changed from GPIO1 to GPIO6, with GPIO5 reserved for internal battery monitoring only.

### Firmware Build Fails

**Symptoms:**
- Compilation errors
- Linking errors
- Out of memory errors

**Common Build Errors:**

```cpp
// Missing library
fatal error: ArduinoJson.h: No such file or directory
Solution: pio lib install "bblanchon/ArduinoJson@^6.21.0"

// Flash overflow
section `.flash.rodata' will not fit in region `irom0_0_seg'
Solution: Enable LTO optimization, reduce binary size

// RAM overflow
section `.bss' will not fit in region `dram0_0_seg'
Solution: Reduce buffer sizes in config.h
```

**Solutions:**

| Error Type | Solution |
|------------|----------|
| Missing library | Run `pio lib install` or check lib_deps in platformio.ini |
| Flash overflow | Enable optimization flags, remove debug logging |
| RAM overflow | Reduce buffer sizes (LOG_BUFFER_SIZE, etc.) |
| Syntax error | Check recent code changes, review error line |

---

## Serial Console Issues

### No Serial Output

**Symptoms:**
- Serial monitor shows nothing
- Blank terminal
- Garbled characters

**Solutions:**

1. **Check Baud Rate:**
   - Must be **115200** baud
   - Set in PlatformIO: `monitor_speed = 115200`

2. **Verify USB Connection:**
   - Try different USB port
   - Ensure cable supports data (not charge-only)
   - Check device manager for COM port

3. **Test with Different Terminal:**
   ```bash
   # PlatformIO
   pio device monitor

   # Screen (Linux/Mac)
   screen /dev/ttyUSB0 115200

   # PuTTY (Windows)
   # COM port, 115200 baud, 8N1
   ```

4. **Check for Silent Boot:**
   - Device may be in deep sleep
   - Press BOOT button to wake
   - Power cycle device

### Garbled Serial Output

**Symptoms:**
- Random characters like `ÿÿÿ���`
- Partially readable text
- Repeating patterns

**Causes:**
- **Wrong Baud Rate:** Terminal set to different speed than device
- **Boot Messages:** ESP32 bootloader uses 74880 baud initially
- **Electrical Noise:** Poor USB connection, EMI

**Solutions:**

| Symptom | Solution |
|---------|----------|
| All garbled | Set baud rate to 115200 |
| Garbled on boot only | Normal - bootloader uses 74880, switches to 115200 |
| Random characters | Check USB cable quality, try different cable |
| Intermittent garble | Move away from EMI sources |

---

## System Health & Watchdog Issues

### Watchdog Warnings in Logs

**Symptoms:**
- Serial shows `[Watchdog] Module X health: WARNING`
- `/api/status` shows `"systemHealth": 1`
- System attempting recovery

**Watchdog Modules:**

1. **MODULE_MEMORY:** Free heap below threshold
2. **MODULE_STATE_MACHINE:** Invalid state detected
3. **MODULE_CONFIG_MANAGER:** Config load/save failures
4. **MODULE_LOGGER:** Log buffer issues
5. **MODULE_HAL_BUTTON:** Button initialization failed
6. **MODULE_HAL_LED:** LED initialization failed
7. **MODULE_HAL_PIR:** PIR initialization failed
8. **MODULE_WEB_SERVER:** Web server errors
9. **MODULE_WIFI_MANAGER:** WiFi connection issues

**Diagnostic Steps:**

```bash
# Check which module is unhealthy
curl http://<device-ip>/api/status | grep healthName

# Review logs for specific errors
curl http://<device-ip>/api/logs
```

**Recovery Actions:**

| Module | Automatic Recovery | Manual Action |
|--------|-------------------|---------------|
| Memory | Heap cleanup, garbage collection | Restart device, reduce buffer sizes |
| State Machine | Reset to MOTION_DETECT mode | Factory reset via `/api/reset` |
| Config Manager | Reload from flash | Factory reset, re-configure |
| Logger | Clear log buffer | Increase LOG_BUFFER_SIZE |
| WiFi Manager | Reconnection attempt | Check network, reset WiFi config |

### Critical Health Status

**Symptoms:**
- `/api/status` shows `"systemHealth": 2` (CRITICAL)
- Device may reboot automatically
- Red warning patterns

**Immediate Actions:**

1. **Check Serial Logs:**
   - Look for CRITICAL level messages
   - Identify failing module

2. **Attempt Graceful Recovery:**
   ```bash
   # Try mode change
   curl -X POST http://<device-ip>/api/mode -d '{"mode": 0}'
   # Wait for sleep/wake cycle

   # Try factory reset
   curl -X POST http://<device-ip>/api/reset
   ```

3. **Power Cycle:**
   - Disconnect USB and battery
   - Wait 10 seconds
   - Reconnect and check health

4. **Reflash Firmware:**
   - Upload known-good firmware
   - Monitor for recurring issues

---

## Recovery Procedures

### Factory Reset (Via API)

```bash
# Reset all configuration to defaults
curl -X POST http://<device-ip>/api/reset

# Expected response:
# {"success": true, "message": "Configuration reset to factory defaults"}
```

**What Gets Reset:**
- WiFi credentials
- Operating mode → MOTION_DETECT
- LED brightness → 255
- All thresholds → defaults
- State machine counters → 0

**What Doesn't Change:**
- Firmware version
- Logs (cleared separately)
- Hardware calibration

### WiFi Credential Reset (Via Button Hold at Boot)

If you need to reconfigure WiFi without losing other settings:

1. **Power off device** (disconnect USB and battery)
2. **Press and hold BOOT button** (GPIO0)
3. **Power on device** (while holding button)
4. **Keep holding for 15 seconds**
   - LED will pulse slowly at first
   - At 15 seconds, LED starts **fast blinking** (WiFi reset pending)
5. **Release button to confirm WiFi reset**
   - LED blinks 3 times (confirmation)
   - Serial shows: `[RESET] WiFi credentials cleared`
6. **Device enters AP mode** on next boot for reconfiguration

**What Gets Reset:**
- WiFi SSID (cleared)
- WiFi password (cleared)

**What Doesn't Change:**
- Operating mode
- LED brightness
- All other settings
- Logs
- State machine counters

### Full Factory Reset (Via Button Hold at Boot)

If you need to completely reset the device to defaults:

1. **Power off device** (disconnect USB and battery)
2. **Press and hold BOOT button** (GPIO0)
3. **Power on device** (while holding button)
4. **Keep holding for 30 seconds**
   - LED pulses slowly at first
   - At 15 seconds: LED fast blinks (WiFi reset pending)
   - At 30 seconds: LED goes **solid on** (Factory reset pending)
5. **Release button to confirm factory reset**
   - LED stays solid for 2 seconds (confirmation)
   - Serial shows: `[RESET] FULL FACTORY RESET TRIGGERED`
   - **Device reboots automatically**
6. **All settings reset to defaults**

**What Gets Reset:**
- WiFi credentials (cleared)
- Operating mode → MOTION_DETECT
- LED brightness → 255 (full)
- All thresholds → defaults
- State machine counters → 0
- All configuration → factory defaults

**What Doesn't Change:**
- Firmware version
- Hardware calibration

**⚠️ Warning:** Full factory reset cannot be undone. All custom configuration will be lost.

**Visual Feedback Summary:**
- **0-15s**: Slow pulse (detecting)
- **15s**: Fast blink (WiFi reset ready)
- **30s**: Solid on (Factory reset ready)
- **After release**: 3 blinks (WiFi) or 2s solid (Factory) = Confirmed

### Firmware Recovery (Full Reflash)

If device is completely unresponsive:

```bash
# Erase flash completely
docker-compose run --rm stepaware-dev pio run --target erase

# Upload fresh firmware
docker-compose run --rm stepaware-dev pio run --target upload

# Upload filesystem (config files)
docker-compose run --rm stepaware-dev pio run --target uploadfs
```

### Bootloader Recovery (Advanced)

If firmware upload fails completely:

```bash
# Use esptool.py directly
pip install esptool

# Erase flash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 erase_flash

# Flash bootloader (get from Espressif)
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 bootloader.bin

# Flash firmware
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x10000 firmware.bin
```

---

## Interpreting Serial Output

### Normal Boot Sequence

```
ESP-ROM:esp32c3-api1-20210207
Build:Feb  7 2021
[HAL_LED] Initializing LED on pin 3...
[HAL_LED] ✓ Initialization complete
[HAL_PIR] Initializing PIR sensor on pin 1...
[HAL_PIR] Warming up (60 seconds)...
[Power] Battery: 3.85V (75%)
[WiFi] Connecting to MyNetwork...
[WiFi] Connected! IP: 192.168.1.100
[Watchdog] All modules healthy
[State] Mode: MOTION_DETECT
```

### Error Messages

| Message | Meaning | Action |
|---------|---------|--------|
| `Guru Meditation Error` | Crash/exception | Check stack trace, reflash firmware |
| `Watchdog timeout` | Task blocked too long | Review blocking code, increase timeout |
| `Out of memory` | Heap exhausted | Reduce buffer sizes, fix memory leak |
| `Failed to connect to WiFi` | WiFi credentials wrong | Update config |
| `Battery critical: 5%` | Low battery | Charge immediately |
| `[Watchdog] Module X: FAILED` | Module non-functional | Check hardware, reflash firmware |

### Debug Logging

Enable verbose logging in `include/config.h`:

```cpp
#define DEBUG_ENABLED 1
#define LOG_LEVEL_DEFAULT LOG_LEVEL_DEBUG
```

Rebuild and upload to see detailed debug output.

---

## Getting Additional Help

If this guide doesn't resolve your issue:

1. **Gather Information:**
   - Full serial console output (from boot)
   - Output of `/api/status` endpoint
   - Recent logs from `/api/logs`
   - Photos of hardware setup
   - Firmware version from `/api/version`

2. **Check GitHub Issues:**
   - Search existing issues: https://github.com/yourusername/StepAware/issues
   - Look for similar problems and solutions

3. **Create New Issue:**
   - Use issue template
   - Include all gathered information
   - Describe steps to reproduce
   - Mention any recent changes

4. **Community Support:**
   - Post in discussions section
   - Include relevant details
   - Be patient and respectful

---

**Remember:** Most issues can be resolved with a power cycle, factory reset, or firmware reflash. Start with simple solutions before complex debugging.
