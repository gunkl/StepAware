# Bug Report: LED Keeps Blinking in MOTION_DETECT Mode

## Reported Issue
When switching from CONTINUOUS_ON mode to MOTION_DETECT mode, an LED continues blinking even though no motion has been detected and no warning timer is active.

## Investigation

### Expected Behavior
- **CONTINUOUS_ON mode:** Hazard LED blinks continuously
- **MOTION_DETECT mode (no motion):** Hazard LED OFF, Status LED blinks fast

### Two LEDs in System
1. **Status LED** (GPIO2) - Built-in board LED
   - Shows system status
   - SHOULD blink in MOTION_DETECT mode

2. **Hazard LED** (GPIO3) - Main warning LED
   - Shows hazard warning
   - Should only be ON in CONTINUOUS_ON or during motion warning

## Diagnosis

### Question 1: Which LED is blinking?
**If STATUS LED is blinking:** This is CORRECT behavior!
- Status LED should blink fast in MOTION_DETECT mode
- See `state_machine.cpp:277`: `m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST)`

**If HAZARD LED is blinking:** This is a BUG!
- Hazard LED should be OFF
- See `state_machine.cpp:276`: `m_hazardLED->stopPattern()`

### Code Analysis

Mode transition flow (CONTINUOUS_ON → MOTION_DETECT):

```cpp
// state_machine.cpp:164-182
void StateMachine::setMode(OperatingMode mode) {
    if (mode == m_currentMode) return;  // Early exit if same mode

    exitMode(m_currentMode);   // Line 173 - Exit CONTINUOUS_ON
    m_currentMode = mode;
    enterMode(mode);            // Line 181 - Enter MOTION_DETECT
}

// Line 304-307: exitMode(CONTINUOUS_ON)
case CONTINUOUS_ON:
    m_hazardLED->stopPattern();  // ✓ Stops pattern
    break;

// Line 274-278: enterMode(MOTION_DETECT)
case MOTION_DETECT:
    m_hazardLED->stopPattern();  // ✓ Stops pattern AGAIN (redundant but safe)
    m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);  // ✓ Status LED blinks
    break;
```

The hazard LED is stopped **twice** - once in `exitMode()` and once in `enterMode()`. This should definitely work.

### Possible Root Causes

1. **User is seeing STATUS LED (most likely)**
   - Status LED is SUPPOSED to blink in MOTION_DETECT
   - This is correct behavior, not a bug

2. **Timing race condition**
   - LED `update()` happens before mode change in same loop iteration
   - LED left in ON state momentarily
   - Should self-correct on next `update()` call

3. **Mock hardware issue**
   - If using mock simulator, LED state tracking might be off
   - Serial output might show old LED state

4. **Hardware wiring issue**
   - If two LEDs are wired together
   - If GPIO pins are swapped

## Testing

### Test 1: Identify Which LED is Blinking

Add debug output:

```cpp
// In state_machine.cpp, enterMode(MOTION_DETECT):
case MOTION_DETECT:
    m_hazardLED->stopPattern();
    m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);

    // ADD THIS:
    DEBUG_PRINTLN("[StateMachine] MOTION_DETECT mode:");
    DEBUG_PRINTF("  Hazard LED (GPIO %d): should be OFF\n", PIN_HAZARD_LED);
    DEBUG_PRINTF("  Status LED (GPIO %d): should be BLINKING\n", PIN_STATUS_LED);
    break;
```

### Test 2: Force Hazard LED Off

If it's definitely the hazard LED, add this defensive code:

```cpp
// In enterMode(MOTION_DETECT):
case MOTION_DETECT:
    m_hazardLED->stopPattern();
    m_hazardLED->off();  // ADD THIS: Force LED off, not just pattern
    m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);
    break;
```

### Test 3: Python Mock Simulator

Run the mock simulator and check LED states:

```bash
python3 test/mock_simulator.py
```

Commands:
```
1             # Set to CONTINUOUS_ON
s             # Check status - hazard LED should be ON
2             # Set to MOTION_DETECT
s             # Check status - hazard LED should be OFF, status LED blinking
```

## Proposed Fixes

### Fix Option 1: Defensive LED Clearing

Add explicit `off()` call to ensure LED is physically off:

```cpp
// File: src/state_machine.cpp
// Line: ~276

case MOTION_DETECT:
    // Wait for motion events
    m_hazardLED->stopPattern();
    m_hazardLED->off();          // ← ADD THIS LINE
    m_statusLED->setPattern(HAL_LED::PATTERN_BLINK_FAST);
    break;
```

**Rationale:** Double-ensure the LED is off, even if `stopPattern()` should already do this.

### Fix Option 2: Reorder Update Loop

Move LED updates to end of loop after all state changes:

```cpp
// File: src/state_machine.cpp
// Function: update()

void StateMachine::update() {
    if (!m_initialized) return;

    // Update input HALs FIRST
    m_button->update();

    // Process events and state changes
    if (m_button->hasEvent(HAL_Button::EVENT_CLICK)) {
        handleEvent(EVENT_BUTTON_PRESS);
    }

    handleMotionDetection();
    processMode();
    updateWarning();
    updateStatusLED();
    checkTransitions();

    // Update output HALs LAST (after state changes)
    m_hazardLED->update();   // ← MOVED TO END
    m_statusLED->update();   // ← MOVED TO END
}
```

**Rationale:** Ensures LED state reflects current mode, not previous mode.

### Fix Option 3: Clear LED State in HAL

Ensure `stopPattern()` resets all internal state:

```cpp
// File: lib/HAL/hal_led.cpp
// Function: stopPattern()

void HAL_LED::stopPattern() {
    setPattern(PATTERN_OFF);  // Calls off() internally
    m_patternDuration = 0;
    m_ledState = false;       // ← ADD THIS: Ensure state is cleared
    off();                    // ← ADD THIS: Force LED off
    DEBUG_PRINTLN("[HAL_LED] Pattern stopped");
}
```

**Rationale:** Belt-and-suspenders approach to ensure LED is fully off.

## Recommended Action

1. **First:** Verify which LED is actually blinking (Status vs Hazard)
   - If Status LED → No bug, this is correct behavior
   - If Hazard LED → Proceed to fix

2. **If bug confirmed:** Apply **Fix Option 1** (defensive LED clearing)
   - Simplest and safest
   - No impact on timing or architecture
   - Just adds one extra `off()` call

3. **Add test case** to automated tests:
   ```python
   def test_hazard_led_off_in_motion_detect_mode():
       """Hazard LED should be OFF in MOTION_DETECT when no motion"""
       sm = StateMachine()
       sm.set_mode(OperatingMode.CONTINUOUS_ON)
       assert sm.hazard_led.is_on(), "Hazard LED should be ON"

       sm.set_mode(OperatingMode.MOTION_DETECT)
       assert not sm.hazard_led.is_on(), "Hazard LED should be OFF"
       assert not sm.hazard_led.is_pattern_active(), "No pattern should be active"
   ```

## Status
- [x] Bug reported
- [x] Investigation complete
- [x] Root cause analysis done
- [ ] User confirmation of which LED
- [ ] Fix applied
- [ ] Test added
- [ ] Verified on hardware

## Related Files
- `src/state_machine.cpp` - Mode transitions
- `lib/HAL/hal_led.cpp` - LED control
- `include/config.h` - Pin definitions
- `test/test_bug_led_blinking.py` - Bug reproduction test
