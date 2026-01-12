# GitHub Actions CI/CD Pipeline

This directory contains the automated CI/CD pipeline for the StepAware project.

## Workflows

### CI/CD Pipeline (`ci.yml`)

Runs on every push to `main` or `develop` branches and on all pull requests.

#### Jobs

1. **Python Tests** (52 tests)
   - Logic tests (12 tests)
   - LED bug regression tests (1 test)
   - Display alignment tests (11 tests)
   - LED timing tests (9 tests)
   - Button debouncing tests (8 tests)
   - State transition tests (12 tests)

2. **C++ Build & Test** (16 tests)
   - Builds firmware for ESP32-DevKit-Lipo
   - Runs native C++ Unity tests
   - Uploads firmware artifacts
   - Uploads test results

3. **Code Linting** (informational)
   - Static code analysis
   - Continues even if warnings found

4. **Build Summary**
   - Provides overall status summary
   - Shows pass/fail for all jobs

## Test Coverage

**Total: 68 tests**
- Python: 52 tests (100% pass rate)
- C++: 16 tests (100% pass rate)

## Artifacts

The pipeline generates the following artifacts:

1. **firmware** (retention: 30 days)
   - `firmware.bin` - Ready to flash to ESP32

2. **test-results** (retention: 30 days)
   - Native test output
   - Test logs and reports

## Running Locally

### Python Tests
```bash
# Run all Python tests
python test/test_logic.py
python test/test_bug_led_blinking.py
python test/test_display_alignment.py
python test/test_hal_led_timing.py
python test/test_hal_button_debounce.py
python test/test_state_transitions.py
```

### C++ Build & Tests
```bash
# Using Docker (recommended)
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo
docker-compose run --rm stepaware-dev pio test -e native

# Native (requires PlatformIO installed)
pio run -e esp32-devkitlipo
pio test -e native
```

## Caching

The pipeline uses GitHub Actions cache to speed up builds:
- PlatformIO packages
- PlatformIO platforms
- Build artifacts

Cache key: `${{ runner.os }}-pio-${{ hashFiles('**/platformio.ini') }}`

## Status Badges

Add these to your README.md:

```markdown
![Build](https://github.com/yourusername/StepAware/workflows/CI%2FCD%20Pipeline/badge.svg)
![Tests](https://img.shields.io/badge/tests-68%20passing-brightgreen)
```

## Troubleshooting

### Python Tests Fail
- Ensure all test files are executable
- Check Python version (requires 3.11+)
- Verify no dependency issues

### C++ Build Fails
- Check `platformio.ini` configuration
- Verify all header files exist
- Check for syntax errors in C++ code

### C++ Tests Fail
- Review test output in artifacts
- Check mock implementations
- Verify test assertions

## Future Enhancements

- [ ] Code coverage reports
- [ ] Performance benchmarks
- [ ] Automated releases on tag
- [ ] Docker image builds
- [ ] Documentation deployment
- [ ] Security scanning
- [ ] Dependency updates (Dependabot)

## Maintenance

- Artifacts are retained for 30 days
- Cache is invalidated when `platformio.ini` changes
- Failed builds send notifications to repository maintainers

---

**Last Updated**: 2026-01-11
**Maintained By**: Development Team
