# StepAware Testing Guide

## Overview

StepAware uses a multi-tiered testing approach to ensure code quality without requiring physical hardware during development.

## Testing Tiers

### Tier 1: Python Logic Tests (Fastest)
**Purpose:** Validate core business logic without C++ compilation
**Runtime:** < 1 second
**Location:** `test/test_logic.py`

```bash
# Run logic tests
python3 test/test_logic.py
```

**What it tests:**
- State machine mode transitions
- Motion detection logic
- Warning timeout behavior
- Button click vs long-press detection
- Edge cases (mode change during warning, etc.)

**Advantages:**
- ✅ Runs instantly on any platform
- ✅ No compilation needed
- ✅ Easy to debug with print statements
- ✅ Perfect for TDD and rapid iteration

**Limitations:**
- ❌ Doesn't test actual C++ code
- ❌ Can't catch C++ compilation errors
- ❌ Logic must be duplicated in Python

### Tier 2: Python Mock Simulator (Interactive)
**Purpose:** Manual testing and demonstration
**Runtime:** Interactive
**Location:** `test/mock_simulator.py`

```bash
# Run interactive simulator
python3 test/mock_simulator.py
```

**Commands:**
- `h` - Help menu
- `s` - System status
- `m` - Trigger motion
- `b` - Button press
- `0/1/2` - Set mode
- `r` - Reset statistics
- `q` - Quit

**Use cases:**
- Demo the system behavior
- Manual exploratory testing
- Validate user experience flow
- Teaching tool for understanding modes

### Tier 3: C++ Unity Tests (Most Accurate)
**Purpose:** Test actual C++ implementation
**Runtime:** 5-10 seconds (with compilation)
**Location:** `test/test_*/test_*.cpp`

```bash
# Native tests (requires GCC/G++)
python3 -m platformio test -e native

# Docker (no local GCC needed)
docker-compose run --rm stepaware-dev pio test -e native
```

**What it tests:**
- Actual HAL_Button implementation
- Actual StateMachine implementation
- C++ compiler warnings/errors
- Memory safety

**Test suites:**
- `test_hal_button` - Button debouncing, click detection, long press
- `test_state_machine` - Mode management, motion handling, LED control

**Advantages:**
- ✅ Tests real C++ code
- ✅ Catches compilation errors
- ✅ Validates C++ specific issues (memory, types, etc.)

**Limitations:**
- ❌ Requires GCC/G++ (or Docker)
- ❌ Slower compilation time
- ❌ More complex debugging

### Tier 4: ESP32 Hardware Tests (Most Realistic)
**Purpose:** Validate on actual hardware
**Runtime:** Manual
**Hardware:** ESP32-C3-DevKitLipo board

```bash
# Build and upload
python3 -m platformio run --target upload

# Monitor serial output
python3 -m platformio device monitor
```

**Serial commands (when MOCK_HARDWARE=1):**
Same as Python simulator: `h`, `s`, `m`, `b`, `0-2`, `r`

**When MOCK_HARDWARE=0:**
- Real PIR sensor triggers motion
- Real LEDs light up
- Real button presses cycle modes

## Test Automation

### Automated Test Runner

```bash
# Run all tests with reporting
python3 test/run_tests.py

# View test history
python3 test/run_tests.py --history

# View specific run report
python3 test/run_tests.py --report 5
```

**Features:**
- Runs PlatformIO tests
- Records results in SQLite database
- Generates HTML reports
- Tracks git commit/branch
- Shows pass/fail trends over time

**Database location:** `test/test_results.db`
**Reports:** `test/report_*.html`, `test/report_latest.html`

### HTML Reports

After running tests, open `test/report_latest.html` in your browser to see:
- Overall pass/fail status
- Individual test results
- Test duration
- Git commit info
- Historical comparison

## Common Testing Patterns

### Testing State Transitions

```python
def test_mode_cycling():
    sm = StateMachine()

    # OFF -> CONTINUOUS_ON
    sm.cycle_mode()
    assert sm.mode == OperatingMode.CONTINUOUS_ON
    assert sm.is_led_on()

    # CONTINUOUS_ON -> MOTION_DETECT
    sm.cycle_mode()
    assert sm.mode == OperatingMode.MOTION_DETECT
    assert not sm.is_led_on()  # LED off until motion
```

### Testing Time-Based Behavior

```python
def test_warning_timeout():
    sm = StateMachine()
    sm.set_mode(OperatingMode.MOTION_DETECT)
    sm.handle_motion()

    # Warning active
    assert sm.warning_active

    # Advance time
    sm.advance_time(16000)  # Past 15-second timeout
    sm.update()

    # Warning expired
    assert not sm.warning_active
```

### Testing Edge Cases

```python
def test_motion_ignored_in_off_mode():
    """Motion should not trigger warning when OFF"""
    sm = StateMachine()
    sm.set_mode(OperatingMode.OFF)
    sm.handle_motion()

    assert sm.motion_events == 0
    assert not sm.warning_active
```

## Known Issues & Silly Bugs to Watch For

### ✓ Fixed Issues

1. **Setting mode to same mode incremented counter**
   - **Bug:** `setMode(CONTINUOUS_ON)` twice → counter goes from 1 to 2
   - **Fix:** Early return in `setMode()` if mode unchanged
   - **Test:** `test_set_mode_same_mode()`

2. **Long press incremented click count**
   - **Bug:** Holding button >1s should be long press, not click
   - **Fix:** Check duration in button release handler
   - **Test:** `test_button_long_press()`

### ⚠️  Potential Issues to Investigate

1. **Mode change during active warning**
   - **Behavior:** Warning continues even after leaving MOTION_DETECT mode
   - **Question:** Should warning clear when mode changes?
   - **Test:** `test_mode_change_during_warning()` documents current behavior
   - **Decision needed:** Is this a bug or feature?

2. **Multiple motion events extend warning**
   - **Behavior:** New motion resets 15-second timer
   - **Question:** Should timer reset or continue from original?
   - **Current:** Timer resets (probably desired)

3. **PIR warm-up handling**
   - **Issue:** 60-second warm-up must complete before sensing
   - **Question:** What happens to motion during warm-up?
   - **Current:** Ignored (probably correct)

## Test Coverage Goals

### Phase 1 (Current)
- ✅ State machine logic - 100%
- ✅ Button handling - 100%
- ⏳ PIR sensor HAL - 75% (warm-up tested, interrupt not tested)
- ⏳ LED HAL - 50% (patterns tested, PWM not fully tested)

### Future Phases
- ⏳ WiFi connection management
- ⏳ Web server endpoints
- ⏳ Configuration persistence
- ⏳ Battery monitoring
- ⏳ Light sensor integration

## Continuous Integration

### GitHub Actions (Future)

```yaml
# .github/workflows/test.yml
name: Run Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Run Python Logic Tests
        run: python3 test/test_logic.py

      - name: Build Docker Image
        run: docker-compose build

      - name: Run C++ Tests
        run: docker-compose run --rm stepaware-dev pio test -e native

      - name: Generate Test Report
        run: python3 test/run_tests.py

      - name: Upload Report
        uses: actions/upload-artifact@v3
        with:
          name: test-report
          path: test/report_latest.html
```

## Testing Best Practices

### 1. Test-Driven Development (TDD)
```
1. Write failing test
2. Run test → ✗ FAIL
3. Implement minimal code
4. Run test → ✓ PASS
5. Refactor
6. Run test → ✓ PASS
```

### 2. Test Naming Convention
```
test_<component>_<scenario>_<expected_result>

Examples:
- test_button_short_press_increments_click_count
- test_state_machine_motion_in_off_mode_ignored
- test_led_warning_pattern_blinks_15_seconds
```

### 3. Always Test Edge Cases
- Boundary conditions (0, max values)
- Mode transitions
- Simultaneous events
- Timeout edge cases
- Null/invalid inputs

### 4. Keep Tests Fast
- Python tests: < 1 second total
- C++ tests: < 10 seconds total
- Use mocks for slow operations

### 5. Make Tests Deterministic
- ✅ Use mock time, don't sleep()
- ✅ Reset state in setUp()
- ✅ Don't rely on execution order
- ❌ Never use random values without seeding

## Debugging Failed Tests

### Python Test Failures
```bash
# Run with verbose output
python3 test/test_logic.py -v

# Add print statements
def test_something():
    sm = StateMachine()
    print(f"Mode: {sm.mode}")  # Debug output
    sm.cycle_mode()
    print(f"Mode after cycle: {sm.mode}")
    assert sm.mode == expected
```

### C++ Test Failures
```bash
# Run with verbose output
python3 -m platformio test -e native -vv

# Check Unity output
# Look for line numbers and assertion messages
```

### Docker Issues
```bash
# Build without cache
docker-compose build --no-cache

# Run interactive shell
docker-compose run --rm stepaware-dev bash

# Inside container:
pio test -e native -vv
```

## Quick Reference

| Task | Command |
|------|---------|
| Run Python logic tests | `python3 test/test_logic.py` |
| Run interactive simulator | `python3 test/mock_simulator.py` |
| Run C++ tests (Docker) | `docker-compose run --rm stepaware-dev pio test -e native` |
| Run all tests with report | `python3 test/run_tests.py` |
| View test history | `python3 test/run_tests.py --history` |
| Build ESP32 firmware | `python3 -m platformio run` |
| Upload to ESP32 | `python3 -m platformio run --target upload` |
| Monitor serial | `python3 -m platformio device monitor` |

## Summary

StepAware's testing approach prioritizes:
1. **Speed** - Python tests run instantly for rapid feedback
2. **Accuracy** - C++ tests validate actual implementation
3. **Accessibility** - No hardware needed for development
4. **Automation** - Test results tracked and reported
5. **Confidence** - High coverage before hardware testing

Start with Python tests for logic, use C++ tests for validation, and use hardware tests for final verification.
