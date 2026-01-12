# StepAware Unit Tests

This directory contains unit tests for the StepAware project using the PlatformIO Unity test framework.

## Test Structure

Tests are organized by module:

- `test_state_machine/` - State machine logic tests
- `test_hal_button/` - HAL button interface tests
- `test_wifi_manager/` - WiFi Manager functionality tests
- `test_wifi_watchdog/` - WiFi Manager watchdog integration tests

## Running Tests

### Run All Tests

```bash
pio test -e native
```

### Run Specific Test

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

### Run Tests with Verbose Output

```bash
pio test -e native -v
```

## Test Coverage

### WiFi Manager Tests (`test_wifi_manager`)

**Basic State Tests:**
- WiFi disabled state
- AP mode with no credentials
- Connect with credentials

**Connection Management:**
- Connection timeout handling
- Automatic reconnection
- Exponential backoff (5s → 10s → 20s → 40s → 60s)
- Max reconnect attempts (10 failures → FAILED state)
- Manual reconnect resets failure count
- Disconnect functionality

**Feature Tests:**
- RSSI reporting
- AP mode fallback

### WiFi Watchdog Tests (`test_wifi_watchdog`)

**Health Check Tests:**
- Disabled state (HEALTH_OK)
- AP mode (HEALTH_OK)
- Connecting state (HEALTH_OK)
- Connected with good signal (HEALTH_OK)
- Connected with weak signal < -85 dBm (HEALTH_WARNING)
- Disconnected state (HEALTH_WARNING)
- Failed state (HEALTH_CRITICAL)

**Recovery Tests:**
- Soft recovery (reconnect)
- Module restart recovery (disconnect + reconnect)
- Unsupported action handling

**Edge Cases:**
- Signal strength thresholds (-85 dBm boundary)
- Health state transitions

## Test Results

Tests run on the native platform (your PC) using mock hardware implementations. This allows for:

- Fast test execution (no hardware required)
- Deterministic time control (mock `millis()`)
- Comprehensive coverage of edge cases
- Easy CI/CD integration

## Adding New Tests

To add a new test module:

1. Create a new directory under `test/` (e.g., `test_my_module/`)
2. Create a test file (e.g., `test_my_module.cpp`)
3. Include Unity framework: `#include <unity.h>`
4. Implement mock functions if needed (time, Serial, hardware)
5. Write test cases using Unity assertions
6. Add test runner in `main()` function

### Example Test Structure

```cpp
#include <unity.h>

// Mock implementations
unsigned long mock_time = 0;
unsigned long millis() { return mock_time; }

// Test cases
void test_example(void) {
    TEST_ASSERT_EQUAL(1, 1);
}

// Setup and teardown
void setUp(void) {
    mock_time = 0;
}

void tearDown(void) {
    // Clean up
}

// Main test runner
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_example);
    return UNITY_END();
}
```

## Unity Assertions

Common Unity assertions used in tests:

- `TEST_ASSERT_EQUAL(expected, actual)` - Values are equal
- `TEST_ASSERT_TRUE(condition)` - Condition is true
- `TEST_ASSERT_FALSE(condition)` - Condition is false
- `TEST_ASSERT_NOT_NULL(pointer)` - Pointer is not NULL
- `TEST_ASSERT_NULL(pointer)` - Pointer is NULL
- `TEST_ASSERT_EQUAL_STRING(expected, actual)` - Strings are equal
- `TEST_ASSERT_EQUAL_INT(expected, actual)` - Integers are equal

See [Unity documentation](https://github.com/ThrowTheSwitch/Unity) for complete assertion list.

## Continuous Integration

Tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run Tests
  run: pio test -e native
```

## Test Development Best Practices

1. **Test Independence**: Each test should be independent and not rely on other tests
2. **Use setUp/tearDown**: Reset state between tests
3. **Mock Time**: Use `mock_time` and `advance_time()` for deterministic timing
4. **Test Edge Cases**: Test boundary conditions and error paths
5. **Descriptive Names**: Use clear, descriptive test function names
6. **Single Responsibility**: Each test should verify one specific behavior
7. **Assert Messages**: Use assertion messages to clarify failures

## Troubleshooting

### Tests Don't Run

- Ensure PlatformIO is installed: `pio --version`
- Check test environment is configured in `platformio.ini`
- Verify test file is in correct directory structure

### Compilation Errors

- Check that mock implementations are provided for all required functions
- Ensure header files are included correctly
- Verify build flags in `platformio.ini`

### Test Failures

- Check test assertions match expected behavior
- Verify mock time is advanced correctly
- Ensure state is properly reset in setUp()

## Future Test Plans

Tests planned for upcoming phases:

- **Phase 4**: Web server API tests, configuration manager tests
- **Phase 5**: Power manager tests, battery monitoring tests
- **Phase 6**: Advanced feature integration tests
- **Phase 7**: Data logging tests, statistics tests
- **Phase 8**: OTA update tests, upgrade path tests

---

**Last Updated**: 2026-01-12
**Test Framework**: Unity (PlatformIO)
**Platform**: Native (PC)
