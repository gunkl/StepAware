# GPIO Pin Assignment Rationale for StepAware

**Document Version:** 1.0
**Last Updated:** 2026-01-29
**Hardware:** ESP32-C3-DevKit-Lipo
**Related:** [HARDWARE_ASSEMBLY.md](HARDWARE_ASSEMBLY.md), [config.h](../../include/config.h)

---

## Executive Summary

This document explains the rationale behind GPIO pin assignments for the StepAware project, particularly the critical decisions around dual-PIR sensor placement and deep sleep wakeup compatibility.

**Key Decisions:**
- **GPIO1**: Primary PIR sensor (PIR_NEAR in dual-PIR mode)
- **GPIO4**: Secondary PIR sensor (PIR_FAR) OR light sensor (mutually exclusive)
- **GPIO5**: Battery monitoring ONLY - **NEVER use for external sensors**
- Both PIR sensors on GPIO 0-5 to support deep sleep wakeup

---

## ESP32-C3 GPIO Constraints

### Deep Sleep Wakeup Limitation

The ESP32-C3 has a critical hardware limitation:

> **Only GPIO 0-5 can wake the device from deep sleep mode.**

This constraint drives the entire pin assignment strategy. Since motion detection is the primary function of StepAware, and deep sleep is essential for battery life, **PIR sensors must be assigned to GPIO 0-5**.

### Available GPIO Pins (GPIO 0-5)

| GPIO | Hardware Function | Availability |
|------|------------------|--------------|
| **GPIO0** | Boot button (pull-up) | ❌ **FIXED** - Hardware button, cannot reassign |
| **GPIO1** | General I/O | ✅ **AVAILABLE** - Best choice for PIR_NEAR |
| **GPIO2** | Status LED (built-in) | ❌ **FIXED** - Board LED, cannot reassign |
| **GPIO3** | General I/O (PWM) | ⚠️ **RESERVED** - Hazard LED (core functionality) |
| **GPIO4** | ADC1_CH4 | ✅ **AVAILABLE** - Best choice for PIR_FAR or light sensor |
| **GPIO5** | ADC2_CH0 | ⚠️ **PROBLEMATIC** - Battery monitor, programming issues |

### GPIO5 Programming Interference Issue

**Critical Discovery:** GPIO5 causes programming/flashing failures when external sensors are connected.

**Symptoms:**
- Device cannot be programmed via USB
- Upload hangs or fails during flashing
- Device appears "bricked" until GPIO5 is disconnected

**Root Cause:**
GPIO5 appears to interfere with the ESP32-C3 boot sequence when pulled high/low by external devices during programming.

**Solution:**
- Reserve GPIO5 exclusively for **internal** battery monitoring
- **NEVER** connect external sensors (PIR, ultrasonic, etc.) to GPIO5
- Document this limitation prominently in all hardware guides

---

## Pin Assignment Decision Matrix

### Priorities (Ranked)

1. **Deep sleep wakeup compatibility** - PIR sensors MUST be on GPIO 0-5
2. **Avoid GPIO5 for external sensors** - Programming interference
3. **Preserve hardware-fixed pins** - GPIO0 (button), GPIO2 (LED)
4. **PWM for hazard LED** - GPIO3 has good PWM support
5. **ADC for sensors** - GPIO4 and GPIO5 support ADC

### Final Assignments

| GPIO | Assignment | Mode | Rationale |
|------|-----------|------|-----------|
| **GPIO0** | Boot Button | All | Hardware fixed, built-in pull-up button |
| **GPIO1** | PIR_NEAR / PIR_SENSOR | All | Deep sleep wakeup ✅, no conflicts |
| **GPIO2** | Status LED | All | Hardware fixed, built-in LED |
| **GPIO3** | Hazard LED (PWM) | All | Core functionality, PWM capable |
| **GPIO4** | PIR_FAR | Dual-PIR | Deep sleep wakeup ✅, direction detection |
| **GPIO4** | Light Sensor (ADC) | Single-PIR | ADC capable, optional feature |
| **GPIO5** | Battery Monitor (ADC) | All | Internal only, avoid programming issues |
| **GPIO6** | VBUS Detect | All | No deep sleep wakeup needed for USB detection |
| **GPIO7** | I2C SDA | Optional | LED matrix data (optional feature) |
| **GPIO8** | Ultrasonic Trigger | Optional | Distance sensor (optional feature) |
| **GPIO9** | Ultrasonic Echo | Optional | Distance sensor (optional feature) |
| **GPIO10** | I2C SCL | Optional | LED matrix clock (optional feature) |
| **GPIO11** | Reserved | Future | Available for expansion |

---

## Operating Modes

### Single-PIR Mode

**Use Case:** Basic motion detection with optional light sensing

**Pin Usage:**
- **GPIO1**: PIR motion sensor (deep sleep wakeup enabled)
- **GPIO4**: Light sensor (ADC) - Optional

**Benefits:**
- Simplest configuration
- Light-based automation available (e.g., only warn in darkness)
- Lower cost (one PIR sensor)

**Deep Sleep:** ✅ Compatible - GPIO1 can wake device

---

### Dual-PIR Mode

**Use Case:** Direction-aware motion detection (approaching vs. departing)

**Pin Usage:**
- **GPIO1**: PIR_NEAR (primary sensor, close to protected area)
- **GPIO4**: PIR_FAR (secondary sensor, farther from protected area)

**Benefits:**
- Direction detection (approaching vs. departing)
- Reduced false alarms
- More intelligent hazard warnings

**Limitations:**
- Light sensor NOT available (GPIO4 conflict)
- Requires two PIR sensors (higher cost)

**Deep Sleep:** ✅ Compatible - Both GPIO1 and GPIO4 can wake device

---

## Alternative Designs Considered

### Alternative 1: PIR on GPIO6

**Considered:** Using GPIO6 for primary PIR sensor

**Rejected Because:**
- GPIO6 is outside the GPIO 0-5 range
- **Cannot wake device from deep sleep** ❌
- Would severely limit battery life (no sleep between detections)

**Lesson Learned:**
Initial implementation used GPIO6, which worked in active mode but broke deep sleep wakeup. Changed to GPIO1 in firmware v0.2.0.

---

### Alternative 2: Both PIRs on GPIO1 and GPIO5

**Considered:** Using GPIO5 for PIR_FAR sensor

**Rejected Because:**
- GPIO5 causes programming interference with external sensors ❌
- Users would be unable to flash firmware with PIR connected
- Poor user experience, frustrating troubleshooting

**Lesson Learned:**
Discovered through field testing that GPIO5 blocks programming. Must reserve for internal use only.

---

### Alternative 3: Single PIR on GPIO3, Hazard LED on GPIO1

**Considered:** Swapping PIR and hazard LED assignments

**Rejected Because:**
- GPIO3 has better PWM support for LED brightness control
- No significant benefit to moving PIR to GPIO3
- GPIO1 works perfectly for PIR

---

## Design Trade-offs

### GPIO4: PIR_FAR vs. Light Sensor

**The Conflict:**
GPIO4 is the only remaining GPIO in the 0-5 range after accounting for fixed pins and GPIO5 limitation.

**Options:**
1. **Dual-PIR mode:** Use GPIO4 for PIR_FAR (direction detection)
2. **Single-PIR mode:** Use GPIO4 for light sensor (light-based automation)

**Cannot have both simultaneously** due to GPIO 0-5 constraint.

**Decision:**
- Make it **user-configurable** via software
- Document the trade-off clearly
- Default to single-PIR mode (simpler, lower cost)
- Advanced users can enable dual-PIR if they have two sensors

---

### Light Sensor on Non-ADC Pin?

**Question:** Could we use a digital light sensor on GPIO6+ to free GPIO4?

**Answer:** Possible future enhancement

**Options:**
- BH1750 digital light sensor (I2C) - would share GPIO7/GPIO10
- TSL2561 digital light sensor (I2C) - would share GPIO7/GPIO10
- TEMT6000 analog sensor - requires ADC, which is limited to GPIO 0-5

**Current Status:**
- Not implemented yet
- Analog photoresistor is simpler and cheaper
- Can be added in future firmware update if demand exists

---

## Deep Sleep Wakeup Implementation

### Wakeup Sources

In deep sleep mode, the device can wake from:

1. **GPIO1 (PIR_NEAR)** - Motion detected (HIGH level)
2. **GPIO4 (PIR_FAR)** - Motion detected in dual-PIR mode (HIGH level)
3. **GPIO0 (Button)** - User presses boot button (LOW level)
4. **Timer** - Periodic wakeup for battery monitoring

### Power Consumption

| Mode | Current Draw | PIR Wakeup |
|------|-------------|-----------|
| Active | ~220 mA | N/A |
| Idle | ~37 mA | N/A |
| Light Sleep | ~3 mA | ✅ Yes |
| Deep Sleep | ~0.02 mA | ✅ Yes (GPIO 0-5 only) |

**Battery Life Calculation:**
- 1000mAh battery
- 95% in deep sleep (0.02 mA)
- 5% active for motion events (220 mA)
- **Estimated:** 20-30 days

---

## Pin Configuration Code Reference

### config.h Definitions

```cpp
// Fixed Hardware Pins
#define PIN_BUTTON          0    // Boot button (GPIO0)
#define PIN_STATUS_LED      2    // Built-in LED (GPIO2)

// Dual-PIR Motion Sensors (GPIO 0-5 for deep sleep wakeup)
#define PIN_PIR_NEAR        1    // Near zone PIR (GPIO1)
#define PIN_PIR_FAR         4    // Far zone PIR (GPIO4)

// Single PIR Mode (backward compatibility)
#define PIN_PIR_SENSOR      PIN_PIR_NEAR  // GPIO1

// Core Functionality
#define PIN_HAZARD_LED      3    // Main hazard LED with PWM (GPIO3)
#define PIN_BATTERY_ADC     5    // Battery monitor (GPIO5) - Internal only!

// Optional Sensors (GPIO 6+)
#define PIN_VBUS_DETECT     6    // USB VBUS detection (GPIO6)
// PIN_LIGHT_SENSOR: GPIO4 in single-PIR mode (conflicts with PIR_FAR)
```

### Deep Sleep Configuration

See [src/power_manager.cpp:454-469](../../src/power_manager.cpp) for implementation details.

---

## Lessons Learned

### 1. Read the Datasheet Carefully

**Issue:** Initially missed the GPIO 0-5 deep sleep limitation
**Impact:** Had to redesign pin assignments
**Lesson:** Always review hardware constraints before finalizing design

### 2. Test Programming Early

**Issue:** Discovered GPIO5 programming issue late in development
**Impact:** Had to change pin assignments and update documentation
**Lesson:** Test all hardware interfaces (including programming) early

### 3. Document Trade-offs

**Issue:** Users confused about GPIO4 dual-purpose
**Impact:** Support requests, unclear documentation
**Lesson:** Explicitly document conflicts and trade-offs

### 4. Plan for Future Expansion

**Issue:** Limited available GPIO pins
**Impact:** Some features mutually exclusive
**Lesson:** Reserve pins strategically, consider I2C expansion

---

## Future Considerations

### ESP32-C3 Pin Limitations

**Total GPIOs:** 22 physical GPIO pins
**Actually Available:** ~12-15 (after strapping pins, flash, USB)
**Available for StepAware:** ~6-8 (after core functionality)

### Possible Enhancements

1. **I2C Expansion**
   - Add I2C GPIO expander (PCF8574)
   - Could provide 8 additional GPIOs
   - Useful for multiple PIR sensors or LEDs

2. **Digital Light Sensor**
   - Replace analog photoresistor with BH1750 (I2C)
   - Frees GPIO4 for dedicated PIR_FAR
   - Enables dual-PIR + light sensing

3. **External ADC**
   - Add ADS1115 (I2C, 16-bit ADC)
   - Provides 4 high-precision ADC channels
   - Could enable more analog sensors

4. **Shift Register for LEDs**
   - Use 74HC595 shift register
   - Control multiple LEDs with 3 GPIOs
   - Useful for status indicators

---

## Verification Checklist

When modifying pin assignments, verify:

- [ ] PIR sensors on GPIO 0-5 (deep sleep requirement)
- [ ] No external sensors on GPIO5 (programming issue)
- [ ] GPIO0 and GPIO2 not reassigned (hardware fixed)
- [ ] PWM available on hazard LED pin
- [ ] ADC available on battery monitor pin
- [ ] Updated config.h with new assignments
- [ ] Updated all documentation (README, HARDWARE_ASSEMBLY, etc.)
- [ ] Updated troubleshooting guides
- [ ] Updated example code and comments
- [ ] Tested programming/flashing with sensors connected
- [ ] Tested deep sleep wakeup functionality
- [ ] Updated pin diagrams and schematics

---

## References

- [ESP32-C3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
- [ESP32-C3-DevKit-Lipo Schematic](https://github.com/OLIMEX/ESP32-C3-DevKit-Lipo)
- [config.h](../../include/config.h) - Pin definitions
- [HARDWARE_ASSEMBLY.md](HARDWARE_ASSEMBLY.md) - Assembly instructions
- [TROUBLESHOOTING.md](../../TROUBLESHOOTING.md) - GPIO5 programming issue

---

**Document History:**
- v1.0 (2026-01-29): Initial documentation of optimized pin assignments
- Changed from GPIO6→GPIO1 for PIR sensor (deep sleep compatibility)
- Documented GPIO5 programming interference issue
- Clarified dual-PIR vs single-PIR trade-offs
