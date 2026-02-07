# Troubleshooting Sleep Issues

## Diagnostic Checklist

When device becomes unresponsive:

1. **Note power source** - USB vs battery only
2. **Note which sensors were triggered** - Near PIR (GPIO1), far PIR (GPIO4), or both
3. **Note battery voltage** - Check after recovery
4. **Try to access logs remotely** (if device might be sleeping):
   ```bash
   curl http://<DEVICE_IP>/api/debug/logs/current
   ```
5. **If unresponsive, press GPIO0 button ONCE** - Single brief press
6. **Immediately download logs after recovery**:
   ```bash
   curl http://<DEVICE_IP>/api/debug/logs/current > current.log
   curl http://<DEVICE_IP>/api/debug/logs/boot_1 > boot_1.log
   curl http://<DEVICE_IP>/api/debug/logs/boot_2 > boot_2.log
   ```
7. **Check boot_1/boot_2 for previous boot logs** - May contain failure evidence

## Known Limitations

### GPIO1 (Near PIR) Wake Limitation

**Issue**: GPIO1 (near PIR) is **NOT armed as a wake source** during light sleep.

**Reason**: Hardware workaround for Issue #38 - GPIO1 is the XTAL_32K_N pad on ESP32-C3, which produces spurious HIGH glitches during clock-gate transitions in `esp_light_sleep_start()`.

**Impact**:
- Only GPIO4 (far PIR) and GPIO0 (button) can wake device from light sleep
- If motion occurs only in near PIR coverage area, device will NOT wake
- This is intentional design, not a bug

**Workaround**:
- Position device so far PIR (GPIO4) covers primary approach path
- Or use mode 2 (deep sleep) which may have different wake source configuration

**Code Reference**: `src/power_manager.cpp` lines 789-804

### Battery Voltage Considerations

- **Healthy**: Above 3.6V
- **Low**: 3.3V - 3.6V (functional but approaching brownout)
- **Critical**: Below 3.3V (brownout risk, may cause resets)

Low battery during sleep can cause:
- Brownout reset (device reboots)
- Unstable GPIO wake detection
- Unreliable sensor operation

## Diagnostic Logging (Build 0.5.11+)

Recent builds include comprehensive sleep diagnostics:

### Wake Source Confirmation
```
=== LIGHT SLEEP WAKE SOURCES ===
GPIO1 (near PIR): configured but NOT armed (Issue #38 workaround)
GPIO4 (far PIR): ARMED for HIGH-level wake
GPIO0 (button): ARMED for LOW-level wake (boot button)
================================
```

### Battery Voltage Tracking
```
Pre-sleep battery: 3.67V (47%)
```

### Wake Event Identification
```
=== WAKE EVENT ===
Wake cause: 2
  EXT1 wake (GPIO bitmap: 0x10)
==================
```

These logs help identify:
- Which wake sources are actually configured
- Battery state before sleep
- What caused the device to wake (or not wake)

## Common Scenarios

### Scenario 1: Device Doesn't Wake from Motion

**Symptoms**: LEDs not responding to hand waves, GPIO0 button works

**Likely Cause**: Motion only triggered GPIO1 (near PIR), which cannot wake device

**Verification**: Check logs for "=== LIGHT SLEEP WAKE SOURCES ===" section

**Solution**: Trigger GPIO4 (far PIR) sensor area, or reposition device

### Scenario 2: Device Reboots During Sleep

**Symptoms**: Fresh boot (0 uptime), no RTC restore logs

**Possible Causes**:
1. Battery brownout (check voltage)
2. Watchdog timeout (check for missing watchdog feeds)
3. Software crash during sleep entry/wake

**Verification**: Check boot_1 logs for "Pre-sleep battery" voltage and last log entry before silence

### Scenario 3: Battery Drains Faster Than Expected

**Symptoms**: Battery depletes quickly despite sleep

**Possible Causes**:
1. Device not actually sleeping (check logs for sleep entry)
2. Frequent spurious wakes (check wake event logs)
3. Sensors drawing too much power during sleep

**Verification**: Check logs for number of wake events vs expected

## Using /diagnose-sleep Skill

For systematic diagnosis, use the `/diagnose-sleep` skill:

```bash
/diagnose-sleep <DEVICE_IP>
```

This skill:
- Asks validation questions about your test setup
- Collects all diagnostic data in parallel
- Analyzes sleep cycle patterns
- Identifies root causes with evidence
- Provides specific fix recommendations

See `.claude/skills/diagnose-sleep.md` for details.

## Related Documentation

- [SLEEP_WAKE.md](SLEEP_WAKE.md) - Complete sleep/wake architecture
- [VOLTAGE_MONITORING.md](VOLTAGE_MONITORING.md) - Battery voltage details
- [memory.md](../memory.md) - Sleep diagnostic procedures

---

**Last Updated**: 2026-02-07
**For**: StepAware ESP32 Project
