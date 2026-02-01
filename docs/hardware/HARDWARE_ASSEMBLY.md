# StepAware Hardware Assembly Guide

**Version:** 0.1.0
**Difficulty:** Beginner to Intermediate
**Time Required:** 30-60 minutes
**Cost:** ~$33 USD

---

## Table of Contents

1. [Required Components](#required-components)
2. [Required Tools](#required-tools)
3. [Safety Precautions](#safety-precautions)
4. [Assembly Steps](#assembly-steps)
5. [Wiring Diagram](#wiring-diagram)
6. [Testing Procedures](#testing-procedures)
7. [Troubleshooting](#troubleshooting)
8. [Enclosure Options](#enclosure-options)

---

## Required Components

### Core Components

| Component | Specification | Quantity | Est. Cost | Notes |
|-----------|--------------|----------|-----------|-------|
| **ESP32-C3-DevKit-Lipo** | Olimex development board | 1 | $15 | Includes built-in LiPo charging circuit |
| **AM312 PIR Sensor** | Passive infrared motion sensor | 1 | $3 | Mini PIR, 12m range, 65° detection angle |
| **High-Brightness LED** | White LED, 3.3V compatible | 1 | $1 | 5mm or 3mm, 20mA max current |
| **LED Resistor** | 220Ω, 1/4W | 1 | $0.10 | Limits LED current to safe level |
| **Pull-up Resistor** | 10kΩ, 1/4W | 1 | $0.10 | For button (if not using internal pull-up) |
| **Tactile Pushbutton** | Momentary switch, 6mm | 1 | $0.50 | Optional (can use boot button on board) |
| **LiPo Battery** | 1000mAh, 3.7V, JST connector | 1 | $8 | Must have JST PH 2.0mm connector |
| **Breadboard** | 400 tie-points | 1 | $3 | For prototyping |
| **Jumper Wires** | Male-to-male, assorted | 20 | $2 | Various lengths |

### Optional Components

| Component | Specification | Quantity | Est. Cost | Purpose |
|-----------|--------------|----------|-----------|---------|
| **Photoresistor** | GL5528 LDR | 1 | $0.50 | Ambient light sensing |
| **LDR Resistor** | 10kΩ, 1/4W | 1 | $0.10 | Voltage divider for photoresistor |
| **Battery Divider Resistors** | 100kΩ, 1/4W | 2 | $0.20 | Voltage divider for battery monitoring (see Step 7) |
| **VBUS Divider Resistors** | 10kΩ, 1/4W | 2 | $0.20 | Voltage divider for USB power detection (see Step 7) |
| **Enclosure** | ABS plastic case | 1 | $5-10 | Protective housing |
| **Perf Board** | Single-sided | 1 | $2 | For permanent assembly |

**Total Cost:** ~$33 (without optional components)

---

## Required Tools

### Essential Tools

- **Soldering Iron** (optional for breadboard, required for permanent assembly)
- **Wire Strippers** (if cutting jumper wires)
- **Multimeter** (for testing connections and voltage)
- **USB-C Cable** (for programming and power)
- **Computer** (with Docker or PlatformIO installed)

### Optional Tools

- Helping hands / PCB holder
- Solder and flux
- Desoldering pump or wick
- Small pliers
- Wire cutters
- Heat shrink tubing

---

## Safety Precautions

⚠️ **Important Safety Information:**

1. **Battery Safety:**
   - Never short-circuit the LiPo battery terminals
   - Do not puncture, crush, or expose battery to heat
   - Use only the correct JST PH 2.0mm connector
   - Charge only with appropriate LiPo charger (built into ESP32-C3-DevKit-Lipo)
   - Do not charge unattended
   - Dispose of damaged batteries properly

2. **Electrical Safety:**
   - Disconnect power before making circuit changes
   - Verify polarity before connecting battery
   - Check all connections before powering on
   - Use appropriate resistors to limit current

3. **Soldering Safety:**
   - Work in well-ventilated area
   - Use proper temperature control (300-350°C for lead-free)
   - Avoid breathing solder fumes
   - Keep soldering iron away from flammable materials

---

## Assembly Steps

### Step 1: Prepare the ESP32-C3-DevKit-Lipo Board

1. **Inspect the board** for any damage or loose components
2. **Identify key pins** using the silkscreen labels:
   - GPIO0 (BOOT button - built-in)
   - GPIO1 (for PIR sensor - near zone in dual-PIR mode)
   - GPIO2 (built-in LED)
   - GPIO3 (for hazard LED)
   - GPIO4 (for PIR far zone OR light sensor - see pin mode notes)
   - GPIO5 (for battery monitoring - **NEVER use for external sensors!**)
   - GPIO6 (for VBUS detection)
   - 3.3V and GND pins
3. **Insert the board** into the breadboard with pins on both sides accessible

**Pin Mode Note:** GPIO4 serves dual purpose:
- **Dual-PIR mode**: PIR_FAR sensor (direction detection)
- **Single-PIR mode**: Light sensor (optional)

### Step 2: Connect Power Rails

1. **Connect 3.3V rail:**
   - Run a jumper from the ESP32 3.3V pin to the positive (+) power rail
2. **Connect GND rail:**
   - Run a jumper from the ESP32 GND pin to the negative (-) ground rail
3. **Verify connections** with a multimeter (should read ~3.3V between rails when powered)

### Step 3: Install the PIR Motion Sensor (AM312)

The AM312 has three pins:
- **VCC** → Connect to 3.3V rail
- **OUT** → Connect to ESP32 GPIO1
- **GND** → Connect to GND rail

**Instructions:**
1. Place the AM312 sensor on the breadboard
2. Connect VCC pin to the positive power rail (3.3V)
3. Connect OUT pin to GPIO1 on the ESP32
4. Connect GND pin to the negative ground rail
5. **Important:** Ensure the sensor is positioned where it can detect motion in the desired area
6. Allow 60 seconds for PIR sensor warm-up after power-on

**PIR Sensitivity Adjustment:**
- Some AM312 modules have a small potentiometer on the back
- Turn clockwise to increase sensitivity
- Turn counter-clockwise to decrease sensitivity
- Start with medium sensitivity and adjust based on testing

### Step 4: Install the Hazard Warning LED

1. **Identify LED polarity:**
   - Long leg = Anode (+)
   - Short leg = Cathode (-)
   - Flat side of LED housing = Cathode (-)

2. **Connect LED with resistor:**
   - Place LED on breadboard
   - Connect LED anode (long leg) to one end of 220Ω resistor
   - Connect other end of resistor to ESP32 GPIO3
   - Connect LED cathode (short leg) to GND rail

**LED Circuit:**
```
GPIO3 ---[220Ω]---[LED]--- GND
         Resistor   Anode→Cathode
```

### Step 5: Install the Mode Button (Optional)

**Option A: Use Built-in Boot Button**
- The ESP32-C3-DevKit-Lipo has a built-in boot button on GPIO0
- No additional wiring required
- This is the recommended option for beginners

**Option B: Add External Button**
1. Place tactile button on breadboard
2. Connect one terminal to GPIO0
3. Connect other terminal to GND
4. Internal pull-up resistor will be enabled in software

### Step 6: Install the Light Sensor (Optional)

If you want ambient light sensing for night-only operation:

1. **Create voltage divider:**
   - Connect photoresistor between 3.3V and GPIO4
   - Connect 10kΩ resistor between GPIO4 and GND

**Light Sensor Circuit:**
```
3.3V ---[Photoresistor]---+--- GPIO4
                           |
                        [10kΩ]
                           |
                          GND
```

2. The voltage at GPIO4 will vary based on light level
3. Bright light → Lower resistance → Higher voltage
4. Dark conditions → Higher resistance → Lower voltage

### Step 7: Connect the Battery

1. **Verify battery voltage** with multimeter (should be 3.7-4.2V)
2. **Check JST connector polarity:**
   - Red wire = Positive (+)
   - Black wire = Negative (-)
3. **Connect battery to JST connector** on ESP32-C3-DevKit-Lipo board

**⚠️ Warning:** Ensure correct polarity! Reversed polarity can damage the board.

#### Step 7a: Battery Voltage Monitor (Optional)

> **⚠️ This circuit is required if you enable Battery Monitoring in the web UI config tab.**
> Battery monitoring is **disabled by default** because it requires this external voltage divider.
> Without the divider wired, the ADC reads garbage and the firmware will log a warning on every boot.

The ESP32-C3 ADC range is 0–3.3 V, but a LiPo battery swings 3.0–4.2 V.  A 2:1 voltage divider
brings the battery voltage into range (ADC sees 1.5–2.1 V).

**Components:** two matched 100kΩ resistors (R1, R2)

**Circuit:**
```
Battery+  ──── R1 (100kΩ) ──── GPIO5 ──── R2 (100kΩ) ──── GND
```

**Wiring steps:**
1. Connect one end of R1 to the battery positive terminal (or the JST positive wire)
2. Connect the other end of R1 to GPIO5 on the ESP32
3. Connect one end of R2 to that same GPIO5 node
4. Connect the other end of R2 to GND

**Notes:**
- R1 and R2 must be the same value.  100kΩ is recommended — it draws only ~40 µA at 4.2 V.
- Do **not** connect anything else to GPIO5.  It interferes with USB flashing on the ESP32-C3.
- No pull-up or pull-down resistors are needed — the divider itself holds the pin at a defined voltage.

#### Step 7b: USB Power Detection (Optional)

> **⚠️ This circuit is required if you want accurate USB power detection.**
> Without it, the firmware adds a software pull-down on GPIO6, but a hardware divider is more reliable.

USB VBUS is 5 V; GPIO6 is a 3.3 V input.  A voltage divider brings VBUS into the safe input range.

**Components:** two 10kΩ resistors (R3, R4)

**Circuit:**
```
USB VBUS (5V) ──── R3 (10kΩ) ──── GPIO6 ──── R4 (10kΩ) ──── GND
```

**Wiring steps:**
1. Connect one end of R3 to the USB VBUS line (the positive pin on the USB-C connector, or tap from an existing VBUS trace)
2. Connect the other end of R3 to GPIO6
3. Connect one end of R4 to that same GPIO6 node
4. Connect the other end of R4 to GND

**Notes:**
- When USB is connected GPIO6 reads ~2.5 V (HIGH).  When disconnected R4 pulls it to 0 V (LOW).
- The firmware also enables a software pull-down on GPIO6 as a fallback, but the hardware resistor is more reliable and eliminates any floating-pin ambiguity.

### Step 8: Final Inspection

Before powering on, verify:

- [ ] All power rail connections are correct
- [ ] PIR sensor has proper 3.3V, GND, and signal connections
- [ ] LED polarity is correct and resistor is in place
- [ ] No short circuits between power and ground
- [ ] Battery is connected with correct polarity
- [ ] All jumper wires are firmly seated

---

## Wiring Diagram

### Pin Connection Summary

### Single-PIR Mode Pin Assignments

| ESP32 Pin | Connection | Component |
|-----------|------------|-----------|
| GPIO0 | Input (pull-up) | Mode Button (built-in boot button) |
| GPIO1 | Input | PIR Sensor OUT (deep sleep wakeup ✅) |
| GPIO2 | Output | Status LED (built-in) |
| GPIO3 | Output (PWM) | Hazard LED → 220Ω → GND |
| GPIO4 | Input (ADC) | Photoresistor voltage divider (optional) |
| GPIO5 | Input (ADC) | Battery voltage monitor (requires external 100kΩ/100kΩ divider — see Step 7a) **⚠️ NEVER use for other sensors!** |
| GPIO6 | Input | VBUS detect (requires external 10kΩ/10kΩ divider — see Step 7b) |
| 3.3V | Power | PIR sensor VCC, power rails |
| GND | Ground | All component grounds |

### Dual-PIR Mode Pin Assignments

| ESP32 Pin | Connection | Component |
|-----------|------------|-----------|
| GPIO0 | Input (pull-up) | Mode Button (built-in boot button) |
| GPIO1 | Input | PIR Near Sensor OUT (deep sleep wakeup ✅) |
| GPIO2 | Output | Status LED (built-in) |
| GPIO3 | Output (PWM) | Hazard LED → 220Ω → GND |
| GPIO4 | Input | PIR Far Sensor OUT (deep sleep wakeup ✅) |
| GPIO5 | Input (ADC) | Battery voltage monitor (requires external 100kΩ/100kΩ divider — see Step 7a) **⚠️ NEVER use for other sensors!** |
| GPIO6 | Input | VBUS detect (requires external 10kΩ/10kΩ divider — see Step 7b) |
| 3.3V | Power | PIR sensors VCC, power rails |
| GND | Ground | All component grounds |

**Important Notes:**
- **GPIO5 Programming Issue**: NEVER connect external sensors to GPIO5! It causes programming/flashing failures.
- **Deep Sleep Wakeup**: Only GPIO 0-5 can wake from deep sleep. PIR sensors on GPIO1/GPIO4 are compatible.
- **GPIO4 Conflict**: In dual-PIR mode, light sensor is NOT available (GPIO4 used for PIR_FAR).

### Breadboard Layout

### Single-PIR Mode Layout:
```
                     ESP32-C3-DevKit-Lipo
                    ┌─────────────────────┐
                    │                     │
             3.3V ──┤ 3.3V           GPIO0├── Boot Button (built-in)
              GND ──┤ GND            GPIO1├──── PIR OUT (deep sleep wakeup)
                    │                GPIO2├── Status LED (built-in)
                    │                GPIO3├──[220Ω]──[LED]── GND
                    │                GPIO4├── Light Sensor (optional)
                    │                GPIO5├──[100kΩ R2]── GND          ┐ optional
  Batt+ ──[100kΩ R1]──┘                                               │ voltage
                    │                GPIO6├──[10kΩ  R4]── GND          │ dividers
  VBUS  ──[10kΩ  R3]──┘                                               ┘
                    │                     │
                    └─────────────────────┘
                            │
                        [Battery]
                        (1000mAh)
```

### Dual-PIR Mode Layout:
```
                     ESP32-C3-DevKit-Lipo
                    ┌─────────────────────┐
                    │                     │
             3.3V ──┤ 3.3V           GPIO0├── Boot Button (built-in)
              GND ──┤ GND            GPIO1├──── PIR NEAR OUT (wakeup)
                    │                GPIO2├── Status LED (built-in)
                    │                GPIO3├──[220Ω]──[LED]── GND
                    │                GPIO4├──── PIR FAR OUT (wakeup)
                    │                GPIO5├──[100kΩ R2]── GND          ┐ optional
  Batt+ ──[100kΩ R1]──┘                                               │ voltage
                    │                GPIO6├──[10kΩ  R4]── GND          │ dividers
  VBUS  ──[10kΩ  R3]──┘                                               ┘
                    │                     │
                    └─────────────────────┘
                            │
                        [Battery]
                        (1000mAh)
```

### PIR Sensor Orientation

```
        Front View (Lens Side)
        ┌─────────────────┐
        │                 │
        │   ╔═══════╗     │  ← Fresnel lens
        │   ║       ║     │
        │   ║  PIR  ║     │
        │   ║       ║     │
        │   ╚═══════╝     │
        │                 │
        └─────────────────┘
         │    │    │
        VCC  OUT  GND

Detection Pattern:
     65° cone, 12m range
         \   |   /
          \  |  /
           \ | /
            \|/
           [PIR]
```

---

## Testing Procedures

### Pre-Power-On Checks

1. **Visual Inspection:**
   - Check all connections are secure
   - Verify no loose wires
   - Ensure no solder bridges or shorts

2. **Continuity Testing:**
   - Test 3.3V to GND (should be open circuit / high resistance)
   - Test each GPIO to GND (should be high resistance)
   - Verify LED resistor is in circuit

3. **Voltage Testing (Battery Connected):**
   - Measure battery voltage at JST connector (3.7-4.2V expected)
   - Verify no voltage on GPIO pins before power-on

### Initial Power-On

1. **Connect USB-C cable** (battery can stay connected)
2. **Observe board LEDs:**
   - Power LED should illuminate
   - No smoke or unusual smells
3. **Check voltage at 3.3V pin** with multimeter (should read 3.3V ±0.1V)

### Component Testing

#### Test 1: Status LED (GPIO2)
```bash
# Upload test firmware or use serial console
# Expected: Built-in LED blinks on startup
```

#### Test 2: PIR Motion Sensor
1. **Wait 60 seconds** for PIR warm-up
2. **Wave hand** in front of sensor (within 2-3 meters)
3. **Check serial output** for motion detection messages
4. **Observe hazard LED** should activate on motion

#### Test 3: Hazard LED (GPIO3)
1. Trigger motion detection or set to CONTINUOUS_ON mode
2. LED should blink/pulse brightly
3. Verify PWM brightness control works

#### Test 4: Mode Button
1. Press boot button (GPIO0)
2. Serial console should show mode change
3. LED pattern should change based on mode

#### Test 5: Battery Monitoring (requires Step 7a circuit)
1. Wire the 100kΩ/100kΩ voltage divider on GPIO5 as described in Step 7a
2. Open the web UI config tab and enable Battery Monitoring, then Save
3. Check serial output for `ADC2 raw=` — value should be in the 2000–2600 range
4. Serial should show `Battery: X.XXV  XX%` with a voltage in the 3.0–4.2 V range
5. Disconnect USB and verify device runs on battery

#### Test 6: Light Sensor (if installed)
1. Cover photoresistor with hand → Serial shows low light level
2. Shine flashlight on sensor → Serial shows high light level
3. Threshold should trigger night-only mode

### Serial Monitor Output

Expected output on first boot:
```
[HAL_LED] Initializing LED on pin 3...
[HAL_LED] PWM configured: channel=0, freq=5000Hz, resolution=8-bit
[HAL_LED] ✓ Initialization complete
[HAL_PIR] Initializing PIR sensor on pin 1...
[HAL_PIR] Warming up (60 seconds)...
[Power] Battery: 3.85V (75%)
[WiFi] Connecting to network...
[State] Mode: MOTION_DETECT
```

---

## Troubleshooting

### Device Won't Power On

**Symptoms:** No LEDs, no serial output
**Possible Causes:**
- Battery not charged
- Battery connector not fully seated
- USB cable faulty
- Board damage

**Solutions:**
1. Try different USB cable
2. Check battery voltage with multimeter (should be > 3.0V)
3. Charge battery via USB for 30 minutes
4. Verify JST connector is fully inserted
5. Try powering from USB only (disconnect battery)

### PIR Sensor Not Detecting Motion

**Symptoms:** No motion events in serial log, hazard LED never activates
**Possible Causes:**
- PIR not warmed up (needs 60 seconds)
- Wrong GPIO connection
- Sensor too sensitive/not sensitive enough
- Sensor facing wrong direction

**Solutions:**
1. Wait full 60 seconds after power-on
2. Verify GPIO6 connection
3. Check 3.3V power to PIR sensor
4. Wave hand closer to sensor (within 1-2 meters)
5. Adjust sensitivity potentiometer if present
6. Test with different motion (walk across sensor view)

### LED Not Working

**Symptoms:** LED never lights up or always on
**Possible Causes:**
- LED polarity reversed
- Wrong GPIO connection
- Resistor missing or wrong value
- LED burned out

**Solutions:**
1. Check LED polarity (long leg = anode)
2. Verify GPIO3 connection
3. Measure voltage at GPIO3 (should toggle between 0V and 3.3V)
4. Test LED with multimeter diode mode
5. Verify 220Ω resistor is present
6. Try a different LED

### Battery Shows 0% / ADC Failure Warning

**Symptoms:** Serial shows `Battery: 0.00V  0%` or `WARNING - battery voltage ... suspiciously low (likely ADC failure)`

**Possible Causes:**
- Battery Monitoring is enabled in the web UI but the voltage divider on GPIO5 is not wired
- Voltage divider resistors are wrong value or one is missing
- GPIO5 wire is loose or not connected to the divider midpoint

**Solutions:**
1. If the voltage divider is not wired, disable Battery Monitoring in the web UI config tab to silence the warning
2. Check the GPIO5 voltage divider wiring against Step 7a
3. With a multimeter, measure the voltage at the GPIO5 pin while battery is connected — should be half the battery voltage (1.5–2.1 V)
4. Check serial for `ADC2 raw=` and `timeout_remaining=` — if `timeout_remaining=0` the ADC conversion is not completing (hardware/firmware issue, not wiring)
5. Verify both 100kΩ resistors are the correct value

### Battery Not Charging

**Symptoms:** Battery voltage not increasing when USB connected
**Possible Causes:**
- Battery fully charged
- Charging circuit fault
- Battery protection circuit activated

**Solutions:**
1. Check battery voltage (4.2V = fully charged)
2. Wait longer (1000mAh battery takes ~2-3 hours to charge)
3. Look for charging indicator LED on board
4. Verify USB cable provides data+power (not charge-only)
5. Try different USB power source

### WiFi Won't Connect

**Symptoms:** Device stuck in "CONNECTING" state
**Possible Causes:**
- Wrong WiFi credentials
- 5GHz network (ESP32-C3 only supports 2.4GHz)
- WiFi router out of range
- MAC filtering enabled on router

**Solutions:**
1. Verify SSID and password in config
2. Ensure WiFi network is 2.4GHz
3. Move device closer to router
4. Check router settings for MAC filtering
5. Try factory reset: `POST /api/reset`

### Erratic Behavior / Random Crashes

**Symptoms:** Device resets unexpectedly, corrupted serial output
**Possible Causes:**
- Power supply instability
- Memory corruption
- Watchdog timeout
- Electromagnetic interference

**Solutions:**
1. Use higher quality USB power supply (2A minimum)
2. Add 100µF capacitor across power rails
3. Check for loose connections
4. Move away from sources of EMI
5. Review serial output for watchdog warnings
6. Flash firmware again

---

## Enclosure Options

### Prototype Enclosure (Quick & Easy)

**Option 1: Open-Frame**
- Mount breadboard to acrylic sheet
- Use standoffs to raise PIR sensor
- Pros: Easy access, good for testing
- Cons: Not weatherproof, exposed connections

**Option 2: Cardboard Box**
- Cut holes for PIR sensor lens and LED
- Secure components with hot glue or tape
- Pros: Free, fast, adjustable
- Cons: Not durable, not moisture-resistant

### Production Enclosure (Recommended)

**Option 3: 3D Printed Case**
- Design custom enclosure in CAD software
- Include mounting holes for components
- Add diffuser for LED
- Pros: Custom fit, durable, professional
- Cons: Requires 3D printer access

**Option 4: Commercial ABS/Plastic Box**
- Purchase small project enclosure (80x50x30mm)
- Drill holes for PIR sensor, LED, and button
- Use hot glue or epoxy to secure components
- Pros: Waterproof options available, durable
- Cons: Generic sizing

### Mounting Considerations

1. **PIR Sensor Positioning:**
   - Must face the area to monitor
   - Clear line of sight, no obstructions
   - Lens must be exposed (not behind plastic/glass)
   - Height: 1-2 meters for best detection

2. **LED Visibility:**
   - Should be highly visible from approach direction
   - Consider adding diffuser for wider viewing angle
   - Bright enough for daylight visibility

3. **Button Access:**
   - If using external button, position for easy access
   - Consider labeling mode states near button

4. **Battery Access:**
   - Design for easy battery replacement
   - Allow USB access for charging
   - Ventilation for heat dissipation during charging

---

## Next Steps

After successful assembly and testing:

1. **Upload Production Firmware:**
   ```bash
   docker-compose run --rm stepaware-dev pio run --target upload
   ```

2. **Configure WiFi:**
   - Connect to "StepAware-XXXX" AP
   - Open http://192.168.4.1
   - Enter WiFi credentials

3. **Position Device:**
   - Mount in desired location
   - Test motion detection coverage
   - Adjust PIR sensitivity if needed

4. **Monitor System:**
   - Access web dashboard at http://\<device-ip\>
   - Check battery level regularly
   - Review logs for any issues

5. **Fine-Tune Settings:**
   - Adjust LED brightness via API
   - Set motion timeout duration
   - Configure night-light mode if desired

---

## Additional Resources

- [ESP32-C3-DevKit-Lipo Documentation](https://www.olimex.com/Products/IoT/ESP32-C3/)
- [AM312 PIR Sensor Datasheet](docs/datasheets/AM312_datasheet.pdf)
- [StepAware API Documentation](../api/API.md)
- [Troubleshooting Guide](TROUBLESHOOTING.md)
- [Circuit Diagrams](wiring_diagram.png)

---

**Questions or Issues?**

If you encounter problems not covered in this guide, please:
1. Check the [Troubleshooting Guide](TROUBLESHOOTING.md)
2. Review serial console output for error messages
3. Post issue on GitHub with photos of your setup
4. Include full serial output log

**Happy Building! 🔧**
