# Bug Report: MockButton Test Failures

**Repository**: https://github.com/gunkl/StepAware.git
**Date**: 2026-01-11
**Environment**: Native tests (gcc 15.2.0), Unity test framework

---

## Issue Summary

Three unit tests in `test/test_hal_button/test_button.cpp` are consistently failing:

1. `test_button_debounce`: Expected `EVENT_NONE (0)` but got `EVENT_CLICK (3)`
2. `test_button_click`: Expected `EVENT_PRESSED (1)` but got `EVENT_CLICK (3)`
3. `test_button_long_press`: Expected `EVENT_PRESSED (1)` but got `EVENT_CLICK (3)`

## Test Output

```
test/test_hal_button/test_button.cpp:177:test_button_debounce:FAIL: Expected 0 Was 3
test/test_hal_button/test_button.cpp:191:test_button_click:FAIL: Expected 1 Was 3
test/test_hal_button/test_button.cpp:206:test_button_long_press:FAIL: Expected 1 Was 3

6 Tests 3 Failures 0 Ignored
```

## Root Cause Analysis

The `MockButton` class in the test file has a critical design flaw in the `mockPress()` and `mockRelease()` methods (lines 138-148):

```cpp
void mockPress() {
    pressed = true;
    press_time = millis();
    state = STATE_PRESSED;  // ← Bypasses state machine!
}

void mockRelease() {
    pressed = false;
    release_time = millis();
    state = STATE_RELEASED;  // ← Bypasses state machine!
}
```

**The Problem:**

1. `mockPress()` directly sets `state = STATE_PRESSED`, bypassing the debouncing logic
2. When `update()` is called, it reads `digitalRead(pin)` which always returns `HIGH` (not pressed)
3. The state machine sees: internal state says PRESSED, but pin reads as RELEASED
4. This triggers an immediate release event with a click, before the test expects it

**State Machine Flow Bug:**

```
Test calls mockPress()
  → state = STATE_PRESSED (but digitalRead still returns HIGH)
Test calls update()
  → current = !digitalRead(pin) = !HIGH = false (not pressed)
  → In STATE_PRESSED case (line 97-120)
  → if (!current) evaluates to true (line 98)
  → Immediate EVENT_CLICK triggered
  → Test expects EVENT_NONE or EVENT_PRESSED, gets EVENT_CLICK instead
```

## Expected Behavior

The mock methods should:
1. Set a mockable pin state that `digitalRead()` can read
2. Let the state machine transition naturally through debouncing
3. Return the correct events in the expected sequence

## Proposed Solution

Add a global mock pin state variable that `digitalRead()` reads:

```cpp
uint8_t mock_pin_state = HIGH;

int digitalRead(uint8_t pin) {
    return mock_pin_state;  // Read from mock state
}

void mockPress() {
    mock_pin_state = LOW;  // Set pin LOW (pressed with pull-up)
    // Don't manipulate internal state - let update() handle it
}

void mockRelease() {
    mock_pin_state = HIGH;  // Set pin HIGH (released)
    // Don't manipulate internal state - let update() handle it
}
```

## Affected Files

- `test/test_hal_button/test_button.cpp` (lines 10-34, 138-148)

## Steps to Reproduce

```bash
# Using Docker environment
docker-compose run --rm stepaware-dev pio test -e native

# Or with PlatformIO directly
pio test -e native
```

## Impact

- **Severity**: Medium - Tests are failing but actual HAL implementation appears correct
- **Scope**: Test infrastructure only, not production code
- **Tests Affected**: 3 out of 6 button tests (50% failure rate)

## Additional Notes

The actual `HAL_Button` implementation in `lib/HAL/hal_button.cpp` correctly handles mocking using:
- `m_mockPressed` flag
- Proper `readButton()` method that checks the flag
- State machine that processes the mocked state correctly

The test's `MockButton` should follow the same pattern.

---

**To create this issue on GitHub:**
1. Go to https://github.com/gunkl/StepAware/issues/new
2. Copy the title and content above
3. Add labels: `bug`, `testing`, `good first issue`
