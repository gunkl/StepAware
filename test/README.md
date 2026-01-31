# StepAware Testing Guide

This directory contains both Python simulation tests and native C++ unit tests for the StepAware project.

## Test Organization

### Python Tests (Simulation Tests)
Python-based tests simulate C++ components to verify logic without hardware:

- `test_config_manager.py` - Configuration validation and persistence
- `test_hal_ledmatrix_8x8.py` - LED matrix HAL simulation
- `test_hal_led_timing.py` - LED timing and blink patterns
- `test_hal_button_debounce.py` - Button debounce logic
- `test_state_transitions.py` - State machine transitions
- `test_logger.py` - Logging system validation
- `test_web_api.py` - Web API endpoints
- `test_watchdog.py` - System watchdog functionality
- `test_animation_templates.py` - Animation template rendering
- `test_display_alignment.py` - Display layout verification
- `test_web_ui_fonts.py` - Font rendering tests
- `test_logic.py` - Core business logic tests

### Native C++ Tests (PlatformIO)
Native tests run on PC using mock hardware (configured in platformio.ini):

- `test_state_machine/` - State machine logic tests
- `test_hal_button/` - HAL button interface tests
- `test_wifi_manager/` - WiFi Manager functionality tests
- `test_wifi_watchdog/` - WiFi Manager watchdog integration tests

### Test Infrastructure
- `run_tests.py` - Test runner with SQLite result tracking and HTML reports
- `analyze_results.py` - Test result analysis utilities
- `mock_simulator.py` - Mock hardware simulator framework
- `mock_web_server.py` - Development web server with live reload
- `MOCK_SERVER.md` - Mock server documentation

## Running Tests

### Quick Start

**Run all Python tests with reporting:**
```bash
cd test
python3 run_tests.py
```
This generates HTML reports in `test/reports/` with test history tracking.

**Run all native C++ tests:**
```bash
pio test -e native
```

**Run tests in Docker:**
```bash
docker-compose run --rm stepaware-dev pio test -e native
```

### Python Test Execution

**Run individual Python test:**
```bash
cd test
python3 test_config_manager.py
python3 test_hal_ledmatrix_8x8.py
```

**Run with unittest discovery:**
```bash
cd test
python3 -m unittest discover -s . -p "test_*.py" -v
```

### Native C++ Test Execution

**Run all native tests:**
```bash
pio test -e native
```

**Run specific test suite:**
```bash
# Run WiFi Manager tests
pio test -e native -f test_wifi_manager

# Run WiFi Watchdog tests
pio test -e native -f test_wifi_watchdog

# Run State Machine tests
pio test -e native -f test_state_machine

# Run HAL Button tests
pio test -e native -f test_hal_button
```

**Run with verbose output:**
```bash
pio test -e native -v
```

### Test Reports

The `run_tests.py` script creates:
- SQLite database: `test/reports/test_results.db`
- HTML reports: `test/reports/report_<id>.html`
- Latest report: `test/reports/report_latest.html`

**View test history:**
```bash
cd test
python3 run_tests.py --history
```

**Regenerate report for specific run:**
```bash
cd test
python3 run_tests.py --report <run_id>
```

## Test File Structure and Naming

### File Naming Conventions

**Python Tests:**
- Format: `test_<component>.py`
- Examples: `test_config_manager.py`, `test_hal_ledmatrix_8x8.py`
- Use descriptive, hardware-specific names for sensors/components

**Sensor Naming (IMPORTANT):**
- Use descriptive hardware names, not generic terms
- GOOD: `test_hal_ledmatrix_8x8.py` (specifies 8x8 matrix)
- GOOD: `test_hal_ultrasonic_hcsr04.py` (specifies sensor model)
- BAD: `test_hal_display.py` (too generic)
- BAD: `test_hal_distance.py` (describes function, not hardware)

**C++ Tests:**
- Directory: `test_<module>/`
- File: `test_<module>.cpp`
- Example: `test_wifi_manager/test_wifi_manager.cpp`

### Test Class Structure

**Python Test Structure:**
```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test <Component> - Brief description

Detailed description of what this test suite validates.
"""

import unittest


class Mock<Component>:
    """Mock implementation matching C++ HAL_<Component>"""

    def __init__(self, **kwargs):
        # Initialize mock state
        pass

    def method_to_test(self):
        # Mock implementation
        pass


class Test<Component>(unittest.TestCase):
    """Test suite for <Component>"""

    def setUp(self):
        """Initialize test fixtures"""
        self.component = Mock<Component>()

    def test_specific_behavior(self):
        """Test a specific behavior with descriptive name"""
        # Arrange
        # Act
        # Assert
        pass


def run_tests():
    """Run all tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(Test<Component>)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
```

**C++ Test Structure:**
```cpp
#include <unity.h>

// Mock implementations
unsigned long mock_time = 0;
unsigned long millis() { return mock_time; }

// Helper functions
void advance_time(unsigned long ms) {
    mock_time += ms;
}

// Test cases
void test_specific_behavior(void) {
    // Arrange
    // Act
    // Assert
    TEST_ASSERT_EQUAL(expected, actual);
}

// Setup and teardown
void setUp(void) {
    mock_time = 0;
    // Reset test state
}

void tearDown(void) {
    // Clean up resources
}

// Main test runner
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_specific_behavior);
    return UNITY_END();
}
```

## When to Update Tests

### Update Tests BEFORE Implementation When:
1. Adding new features (TDD approach)
2. Fixing bugs (write failing test first)
3. Changing API contracts
4. Modifying behavior specifications

### Update Tests AFTER Implementation When:
1. Refactoring internal implementation (tests should still pass)
2. Optimizing performance (behavior unchanged)
3. Adding documentation/comments
4. Fixing typos or formatting

### Update Tests TOGETHER With Implementation When:
1. Changing configuration parameters
2. Adding new validation rules
3. Modifying error handling
4. Extending existing features

## Adding New Tests

### Adding a Python Test

1. **Create test file** in `test/` directory:
   ```bash
   touch test/test_new_component.py
   ```

2. **Follow the standard structure** (see Test Class Structure above)

3. **Implement mock class** matching C++ behavior:
   - Mirror C++ class interface
   - Simulate hardware responses
   - Track internal state

4. **Write test cases** covering:
   - Normal operation
   - Edge cases
   - Error conditions
   - Boundary values

5. **Run the test**:
   ```bash
   cd test
   python3 test_new_component.py
   ```

### Adding a Native C++ Test

1. **Create test directory**:
   ```bash
   mkdir test/test_new_module
   ```

2. **Create test file** `test/test_new_module/test_new_module.cpp`

3. **Include Unity framework**:
   ```cpp
   #include <unity.h>
   ```

4. **Implement mock functions** for hardware dependencies:
   ```cpp
   unsigned long mock_time = 0;
   unsigned long millis() { return mock_time; }

   // Mock Serial for logging
   class MockSerial {
   public:
       void println(const char* msg) { /* ignore */ }
   } Serial;
   ```

5. **Write test cases** using Unity assertions

6. **Add test runner** in `main()` function

7. **Update platformio.ini** if needed for dependencies

8. **Run the test**:
   ```bash
   pio test -e native -f test_new_module
   ```

## Test Coverage Guidelines

### What to Test

**Configuration:**
- Default values are correct
- JSON serialization/deserialization
- Validation rules (min/max/ranges)
- Invalid input handling
- Edge cases (boundary values)

**Hardware Abstraction Layers (HAL):**
- Initialization success/failure
- State transitions
- Timing behavior (debounce, delays)
- Mock mode operation
- Error conditions

**Business Logic:**
- State machine transitions
- Decision logic
- Calculations and algorithms
- Event handling
- Timeout behavior

**Web API:**
- Endpoint responses
- Error handling
- Input validation
- JSON formatting
- Status codes

### What NOT to Test

- External library internals (ESPAsyncWebServer, ArduinoJson)
- Arduino framework functions (unless mocking behavior)
- Hardware-specific timing (test mock timing instead)
- Visual appearance (test data, not rendering)

## Test Quality Standards

### Good Tests Are:

1. **Independent**: Can run in any order, don't depend on other tests
2. **Repeatable**: Same result every time with same inputs
3. **Fast**: Run quickly to encourage frequent execution
4. **Readable**: Clear names, obvious intent
5. **Focused**: Test one thing at a time
6. **Maintainable**: Easy to update when code changes

### Test Naming Convention

**Python:**
- `test_<specific_behavior_being_tested>`
- Examples:
  - `test_default_values`
  - `test_validation_motion_duration_too_short`
  - `test_wifi_credentials`

**C++:**
- `test_<module>_<specific_behavior>`
- Examples:
  - `test_wifi_manager_connection_timeout`
  - `test_button_debounce_timing`
  - `test_state_transition_idle_to_active`

## Test Development Workflow

### Test-Driven Development (TDD) Workflow

1. **Write failing test** for new feature:
   ```bash
   # Create test
   python3 test_new_feature.py
   # Result: FAIL (as expected)
   ```

2. **Implement minimum code** to make test pass:
   ```bash
   # Edit implementation
   # Run test again
   python3 test_new_feature.py
   # Result: PASS
   ```

3. **Refactor** while keeping tests passing:
   ```bash
   # Improve code quality
   # Verify tests still pass
   python3 test_new_feature.py
   # Result: PASS
   ```

4. **Add more test cases** for edge cases

### Bug Fix Workflow

1. **Reproduce bug** with failing test
2. **Fix implementation**
3. **Verify test passes**
4. **Add regression tests** for related scenarios

### Feature Addition Workflow

1. **Design test cases** covering requirements
2. **Implement tests** (they should fail)
3. **Implement feature**
4. **Run all tests** to verify no regressions
5. **Update documentation**

## Common Unity Assertions (C++ Tests)

### Equality Assertions
- `TEST_ASSERT_EQUAL(expected, actual)` - Generic equality
- `TEST_ASSERT_EQUAL_INT(expected, actual)` - Integer equality
- `TEST_ASSERT_EQUAL_STRING(expected, actual)` - String equality
- `TEST_ASSERT_EQUAL_FLOAT(expected, actual)` - Float equality

### Boolean Assertions
- `TEST_ASSERT_TRUE(condition)` - Condition is true
- `TEST_ASSERT_FALSE(condition)` - Condition is false

### Null Assertions
- `TEST_ASSERT_NOT_NULL(pointer)` - Pointer is not NULL
- `TEST_ASSERT_NULL(pointer)` - Pointer is NULL

### Comparison Assertions
- `TEST_ASSERT_GREATER_THAN(threshold, actual)` - Value greater than threshold
- `TEST_ASSERT_LESS_THAN(threshold, actual)` - Value less than threshold
- `TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual)` - Value >= threshold
- `TEST_ASSERT_LESS_OR_EQUAL(threshold, actual)` - Value <= threshold

See [Unity documentation](https://github.com/ThrowTheSwitch/Unity) for complete list.

## Common Python Assertions

### unittest Assertions
- `self.assertEqual(a, b)` - Values are equal
- `self.assertNotEqual(a, b)` - Values are not equal
- `self.assertTrue(x)` - x is True
- `self.assertFalse(x)` - x is False
- `self.assertIs(a, b)` - a is b (identity)
- `self.assertIsNone(x)` - x is None
- `self.assertIn(a, b)` - a in b
- `self.assertIsInstance(a, b)` - a is instance of b
- `self.assertRaises(exc, callable, ...)` - Callable raises exception
- `self.assertGreater(a, b)` - a > b
- `self.assertLess(a, b)` - a < b
- `self.assertGreaterEqual(a, b)` - a >= b
- `self.assertLessEqual(a, b)` - a <= b

## Test Coverage Tracking

### View Test History
```bash
cd test
python3 run_tests.py --history --limit 20
```

### Generate Coverage Report
The `run_tests.py` script tracks:
- Total tests run
- Pass/fail counts
- Test duration
- Git commit and branch
- Historical trends

View reports at: `test/reports/report_latest.html`

## Continuous Integration

### GitHub Actions Example

```yaml
name: Run Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.x'

      - name: Install PlatformIO
        run: pip install platformio

      - name: Run Native Tests
        run: pio test -e native

      - name: Run Python Tests
        run: |
          cd test
          python3 run_tests.py

      - name: Upload Test Reports
        uses: actions/upload-artifact@v3
        with:
          name: test-reports
          path: test/reports/
```

## Troubleshooting

### Python Tests

**Issue: Import errors**
```
Solution: Ensure you're in the test/ directory or add parent to PYTHONPATH
```

**Issue: Tests pass locally but fail in CI**
```
Solution: Check for hardcoded paths, timing dependencies, or missing mocks
```

**Issue: Mock doesn't match C++ behavior**
```
Solution: Review C++ implementation and update mock to match
```

### Native C++ Tests

**Issue: Tests don't compile**
```
Solution:
- Verify mock implementations for all hardware dependencies
- Check include paths in platformio.ini
- Ensure test_build_src = no in [env:native]
```

**Issue: Tests timeout**
```
Solution:
- Check for infinite loops in test code
- Verify mock time is advancing correctly
- Ensure tests have reasonable timeouts
```

**Issue: Tests fail on hardware but pass in native**
```
Solution:
- Timing differences between mock and real hardware
- Hardware-specific behavior not captured in mocks
- Test assumptions about hardware state
```

### Test Reports

**Issue: Reports not generating**
```
Solution:
- Check reports/ directory permissions
- Verify SQLite database is writable
- Ensure run_tests.py has execute permissions
```

**Issue: History shows wrong git info**
```
Solution:
- Ensure git is in PATH
- Check .git directory exists
- Verify git commands work in terminal
```

## Best Practices Summary

### DO:
- Write tests before fixing bugs (TDD for bug fixes)
- Use descriptive, hardware-specific names for sensors
- Mock all hardware dependencies
- Test edge cases and boundary values
- Keep tests independent and repeatable
- Use setUp/tearDown to reset state
- Run tests frequently during development
- Commit test code with implementation

### DON'T:
- Skip writing tests for "simple" code
- Use generic names like "test_sensor" or "test_display"
- Test external library internals
- Make tests depend on execution order
- Hardcode timing values without mocking time
- Ignore failing tests
- Commit code without running tests
- Mix test and production code

## Resources

### Documentation
- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)
- [Python unittest](https://docs.python.org/3/library/unittest.html)
- [PlatformIO Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)

### Project Docs
- [MOCK_SERVER.md](MOCK_SERVER.md) - Mock web server for UI testing
- [README_WEB_UI_FONTS.md](README_WEB_UI_FONTS.md) - Font testing guide
- [../CLAUDE.md](../CLAUDE.md) - Development workflow with AI assistance

### Test Examples
- Simple test: `test_config_manager.py`
- HAL test: `test_hal_ledmatrix_8x8.py`
- Complex test: `test_state_transitions.py`
- Native test: `test_wifi_manager/`

---

**Last Updated**: 2026-01-30
**Test Frameworks**: Unity (C++), unittest (Python)
**Platform**: Native (PC simulation)
**CI Ready**: Yes
