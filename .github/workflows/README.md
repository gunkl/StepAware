# GitHub Actions CI/CD Pipeline

## Overview

StepAware uses GitHub Actions for continuous integration and deployment. This document explains our workflow structure, jobs, and troubleshooting.

## Workflow: CI/CD Pipeline

**File**: `.github/workflows/ci.yml`

**Triggers**:
- Push to `main` or `develop` branches
- Pull requests to `main` branch

## Job Structure

### 1. `lint` - Code Linting

**Purpose**: Static code analysis using cppcheck

**Platform**: Native environment only (not ESP32C3)

**Configuration**:
- Severity: medium+ defects fail the build
- Suppressions: `cppcheck-suppressions.txt` (false positives only)
- See: [docs/CI_LINTING.md](../../docs/CI_LINTING.md)

**Run locally**:
```bash
pio check --environment native --fail-on-defect=medium
```

**Common failures**:
- New code quality issues introduced
- Missing suppression for known false positive
- cppcheck version mismatch

---

### 2. `test-python-unit` - Python Unit Tests

**Purpose**: Run standalone Python unit tests

**What it tests**:
- Configuration manager logic (test_config_manager.py)
- HAL component mocking (test_hal_button_debounce.py, test_hal_led_timing.py)
- State machine transitions (test_state_transitions.py)
- Web API endpoints (test_web_api.py)
- Logger functionality (test_logger.py)
- LED matrix 8x8 (test_hal_ledmatrix_8x8.py)
- Display alignment (test_display_alignment.py)
- Animation templates (test_animation_templates.py)
- Web UI fonts (test_web_ui_fonts.py)
- Watchdog functionality (test_watchdog.py)
- Bug regression tests (test_bug_led_blinking.py)
- General logic tests (test_logic.py)

**Important**: This job runs Python tests **directly**, NOT via `run_tests.py`

**Why**:
- `run_tests.py` is a PlatformIO wrapper for C++ Unity tests
- Python unit tests are standalone and don't require PlatformIO
- Separating concerns improves clarity and reliability

**Run locally**:
```bash
cd test
for f in test_*.py; do python3 "$f"; done
```

**Common failures**:
- Import errors (missing dependencies)
- Test assertion failures
- Mock configuration issues

---

### 3. `build-firmware` - Build ESP32 Firmware

**Purpose**: Compile firmware for ESP32-C3 hardware

**Platform**: esp32c3 environment

**Outputs**:
- Firmware binary: `.pio/build/esp32c3/firmware.bin`
- Uploaded as artifact (30-day retention)

**Run locally**:
```bash
pio run -e esp32c3
```

**Common failures**:
- Compilation errors (syntax, type errors)
- Missing dependencies or libraries
- Platform package version mismatch

---

### 4. `test-native` - Native C++ Tests

**Purpose**: Run C++ Unity tests on native platform

**What it tests**:
- State machine logic (test_state_machine/)
- HAL button implementation (test_hal_button/)
- Power manager (test_power_manager/)
- WiFi manager (test_wifi_manager/)
- WiFi watchdog (test_wifi_watchdog/)
- Integration tests (test_integration/)
- Motion sensor HAL (test_hal_motion_sensor/)
- Ultrasonic sensor HAL (test_hal_ultrasonic/)
- Web API (test_web_api/)
- Button reset functionality (test_button_reset/)

**Important**: This is where C++ Unity tests run (with PlatformIO)

**Run locally**:
```bash
pio test -e native
```

**Common failures**:
- Test assertion failures
- Mock/hardware simulation issues
- Unity framework errors

---

### 5. `test-docker` - Docker Environment Test

**Purpose**: Verify Docker development environment works

**What it does**:
- Builds Docker container
- Runs native tests inside container
- Validates containerized development workflow

**Run locally**:
```bash
docker-compose run --rm stepaware-dev pio test -e native
```

**Common failures**:
- Docker build issues
- Container dependency problems
- Volume mount configuration

---

### 6. `summary` - Build Summary

**Purpose**: Aggregate results and report status

**Depends on**: All previous jobs

**Behavior**:
- Always runs (even if previous jobs fail)
- Reports pass/fail status for each job
- Exits with code 1 if any job failed (enforces quality gate)

## Python vs C++ Tests: Why Separate?

### Python Unit Tests (test-python-unit job)
- **Files**: test_*.py in test/ directory
- **Framework**: Python unittest
- **Purpose**: Test business logic, state machines, configurations
- **Execution**: Direct Python execution
- **No PlatformIO needed**: Standalone tests

### C++ Unity Tests (test-native job)
- **Files**: test_*/test_*.cpp in test/ directory
- **Framework**: Unity (C++ testing framework)
- **Purpose**: Test hardware abstraction layer, integration
- **Execution**: Via PlatformIO (`pio test -e native`)
- **Requires PlatformIO**: Unity framework, native compilation

This separation ensures:
- Clear responsibility boundaries
- Faster Python test execution (no PlatformIO overhead)
- Proper isolation of test environments

## Troubleshooting

### Lint Job Fails

**Check**:
1. Run locally: `pio check --environment native --fail-on-defect=medium -v`
2. Review cppcheck output for specific warnings
3. See if it's a known false positive (check `cppcheck-suppressions.txt`)
4. Read: [docs/CI_LINTING.md](../../docs/CI_LINTING.md)

**Fix**:
- Address legitimate code quality issues
- Add suppression for documented false positives (with rationale)

---

### Python Test Job Fails

**Check**:
1. Run locally: `cd test && for f in test_*.py; do python3 "$f"; done`
2. Check for missing dependencies
3. Verify mock configurations

**Fix**:
- Install missing Python packages
- Fix test assertions or logic
- Update mocks if hardware interface changed

---

### Build Job Fails

**Check**:
1. Run locally: `pio run -e esp32c3`
2. Check compilation errors in output
3. Verify all includes and dependencies

**Fix**:
- Fix syntax/type errors
- Update library versions in platformio.ini
- Ensure framework packages are correct version

---

### Native Test Job Fails

**Check**:
1. Run locally: `pio test -e native`
2. Identify failing test case
3. Check test expectations vs. implementation

**Fix**:
- Fix implementation to match test expectations
- Update test if implementation intentionally changed
- Check mock/hardware simulation logic

---

### Docker Test Job Fails

**Check**:
1. Run locally: `docker-compose run --rm stepaware-dev pio test -e native`
2. Verify Docker build succeeds: `docker-compose build`
3. Check docker-compose.yml configuration

**Fix**:
- Update Dockerfile dependencies
- Fix volume mounts in docker-compose.yml
- Ensure base image is correct

---

## Running Jobs Locally

### All Checks (Recommended Before Push)

```bash
# Linting
pio check --environment native --fail-on-defect=medium

# Python tests
cd test && for f in test_*.py; do python3 "$f"; done && cd ..

# Build firmware
pio run -e esp32c3

# C++ tests
pio test -e native

# Docker tests (optional)
docker-compose run --rm stepaware-dev pio test -e native
```

### Individual Jobs

```bash
# Just linting
pio check --environment native --fail-on-defect=medium

# Just Python tests
cd test && python3 test_config_manager.py

# Just build
pio run -e esp32c3

# Just C++ tests
pio test -e native
```

## Workflow Modifications

### Adding New Job

1. Add job definition to `.github/workflows/ci.yml`
2. Update `summary` job `needs:` array to include new job
3. Add result check in summary validation
4. Document in this README

### Changing Triggers

Edit the `on:` section in ci.yml:

```yaml
on:
  push:
    branches: [ main, develop, feature/* ]  # Add feature branches
  pull_request:
    branches: [ main, develop ]  # Add develop PRs
```

## CI/CD Best Practices

1. **Run locally before push**: Catch issues early
2. **Use pre-commit hooks**: Automate local validation
3. **Monitor CI failures**: Don't ignore intermittent failures
4. **Keep jobs independent**: Each job should run standalone
5. **Document suppressions**: Explain why checks are disabled

## See Also

- [CI Linting Strategy](../../docs/CI_LINTING.md)
- [Test Documentation](../../test/README.md)
- [Docker Guide](../../DOCKER_GUIDE.md)
- [Contributing Guidelines](../../CONTRIBUTING.md)

---

**Last Updated**: 2026-01-31
**Maintainer**: StepAware Development Team
