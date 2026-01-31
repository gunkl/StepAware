# CI/CD Linting Strategy

## Overview

StepAware uses PlatformIO's static code analysis (cppcheck) to maintain code quality. This document explains our linting configuration, suppressions, and the rationale behind them.

## Linting Configuration

### Platform Selection: Native Only

**Decision**: CI linting runs on `native` environment only, not ESP32C3.

**Rationale**:
- ESP32C3 cppcheck encounters internal errors (silent failures)
- Native platform provides equivalent coverage of source code
- Same files, same checks, more reliable execution
- ESP32C3 compilation still happens in build job (ensures hardware compatibility)

**Implementation**: `.github/workflows/ci.yml` line 29
```yaml
pio check --fail-on-defect=medium --environment native
```

## Suppression Strategy

### File: `cppcheck-suppressions.txt`

We suppress **false positives only** - legitimate security and logic checks remain active.

### Suppressed Categories

#### 1. badBitmaskCheck (ArduinoJson API Pattern)

**What it is**: cppcheck flags `json["key"] | default` as redundant bitwise OR

**Why it's a false positive**:
- ArduinoJson overloads the `|` operator for providing defaults
- Pattern: `doc["wifi"]["enabled"] | false` returns false if key missing
- This is the **documented API** for the ArduinoJson library
- NOT a bitwise operation - it's operator overloading

**Example**:
```cpp
// config_manager.cpp line 713
m_config.wifiEnabled = doc["wifi"]["enabled"] | false;  // ✅ Correct ArduinoJson usage
```

**Files affected**: `src/config_manager.cpp`, `src/web_api.cpp`

**Documentation**: https://arduinojson.org/v6/api/jsonvariant/or/

---

#### 2. unusedFunction (Library-Style Public APIs)

**What it is**: cppcheck marks public methods as "unused"

**Why it's a false positive**:
- PlatformIO builds entire codebase as single compilation unit
- Public APIs appear unused even though they're legitimate interfaces
- These methods are used for:
  - Component-based architecture (future modular builds)
  - Mock/testing interfaces (e.g., `mockSetMotion()`)
  - External API surface (e.g., `ConfigManager::reset()`)

**Example**:
```cpp
// Public API for resetting configuration
void ConfigManager::reset() {
    // Flagged as "unused" but is legitimate public interface
}
```

**Affected**: 100+ methods across config_manager, logger, sensor_manager, etc.

---

#### 3. noExplicitConstructor (Embedded HAL Constructors)

**What it is**: Single-argument constructors without `explicit` keyword

**Why it's a false positive**:
- In embedded HAL layer, implicit conversions are acceptable
- Example: `HAL_LEDMatrix_8x8(uint8_t address)` - address is unambiguous
- Embedded context differs from general C++ best practices
- No risk of accidental type conversion in this codebase

**Example**:
```cpp
HAL_LEDMatrix_8x8::HAL_LEDMatrix_8x8(uint8_t address)  // ✅ Acceptable in embedded context
```

**Tradeoff**: Style preference vs. practical embedded development

---

## What We DO Check

These critical checks remain **active**:

- ✅ **Security**: Buffer overflows, null pointer dereferences
- ✅ **Memory**: Memory leaks, uninitialized variables
- ✅ **Logic**: Dead code, unreachable code, invalid operations
- ✅ **Concurrency**: Race conditions, deadlocks
- ✅ **Performance**: Inefficient algorithms

**Severity threshold**: medium+ defects (high and medium severity issues block CI)

## Running Linting Locally

### Via PlatformIO CLI

```bash
# Run same check as CI
pio check --environment native --fail-on-defect=medium

# Verbose output for debugging
pio check --environment native --fail-on-defect=medium -v

# Check specific files
pio check --environment native --fail-on-defect=medium --pattern="src/config_manager.cpp"
```

### Via Docker

```bash
# Run linting in Docker container (recommended)
docker-compose run --rm stepaware-dev pio check --environment native --fail-on-defect=medium

# With verbose output
docker-compose run --rm stepaware-dev pio check --environment native --fail-on-defect=medium -v
```

### Via Pre-commit Hooks

```bash
# Install pre-commit (one-time setup)
pip install pre-commit
pre-commit install

# Run linting hook manually
pre-commit run cppcheck-native --all-files

# Run before each push (optional)
pre-commit run --all-files
```

## Adding New Suppressions

### When to Suppress

Only suppress if the warning is:
1. **Provably false positive** (like ArduinoJson API)
2. **Intentional design choice** (like library-style public APIs)
3. **Low-priority style issue** (like embedded HAL constructors)

### When NOT to Suppress

NEVER suppress:
- ❌ Security vulnerabilities
- ❌ Memory safety issues
- ❌ Logic errors
- ❌ Concurrency problems

### How to Add Suppression

1. **Identify the warning category** (e.g., `unusedVariable`)
2. **Document the rationale** in `cppcheck-suppressions.txt`
3. **Add suppression entry**:
   ```
   // Rationale: Explain why this is a false positive
   warningCategory:path/to/file.cpp
   ```
4. **Update this documentation** with the new suppression and rationale

**Example**:
```
// Example: Suppress false positive for calculated variable used only in assertion
// The variable is used in debug builds but appears unused in release builds
unusedVariable:src/state_machine.cpp:145
```

## Troubleshooting

### Linting Fails Locally but Passes in CI

**Cause**: Different cppcheck versions or configurations
**Solution**:
- Check cppcheck version: `cppcheck --version`
- Ensure PlatformIO is up to date: `pio upgrade`
- Verify suppression file exists: `ls -l cppcheck-suppressions.txt`
- Use Docker for consistent environment: `docker-compose run --rm stepaware-dev pio check --environment native --fail-on-defect=medium`

### New Code Triggers Warnings

**Cause**: Legitimate code quality issue or new false positive
**Solution**:
1. Review the warning carefully
2. Fix if it's a real issue
3. Document and suppress if it's a false positive (following process above)

### Suppression File Not Working

**Cause**: File path or syntax error
**Solution**:
- Check file location: Must be in project root
- Verify syntax: Comments start with `//`
- Check platformio.ini references: `--suppressions-list=cppcheck-suppressions.txt`
- Ensure suppression format matches cppcheck documentation

### CI Linting Job Fails

**Common causes and solutions**:

1. **New defect introduced**
   - Review the CI log for specific defects
   - Fix the code or add suppression if false positive

2. **cppcheck version change**
   - Check if PlatformIO updated cppcheck
   - Update suppressions if new false positives appear

3. **Suppression file corrupted**
   - Verify file encoding (UTF-8)
   - Check for invalid syntax or comments

## CI/CD Integration

### GitHub Actions Workflow

The linting job runs as part of the CI/CD pipeline:

```yaml
lint:
  name: Code Linting
  runs-on: ubuntu-latest

  steps:
    - name: Checkout code
      uses: actions/checkout@v4

    - name: Set up Python
      uses: actions/setup-python@v5
      with:
        python-version: '3.11'

    - name: Install PlatformIO
      run: |
        python -m pip install --upgrade pip
        pip install platformio

    - name: Run PlatformIO check
      run: pio check --fail-on-defect=medium --environment native
```

**Job behavior**:
- Runs on every push to main/develop
- Runs on all pull requests to main
- Blocks merge if defects found (medium+ severity)
- Can be run locally to verify before pushing

## Performance Considerations

### Linting Speed

- **Native environment**: ~30-60 seconds (typical)
- **ESP32C3 environment**: Variable (often fails with internal errors)
- **Docker overhead**: +10-20 seconds for container startup

**Optimization tips**:
- Use native environment only for linting
- Cache PlatformIO packages in CI
- Run linting in parallel with other CI jobs

## See Also

- [CI/CD Workflow Documentation](.github/workflows/README.md)
- [PlatformIO Check Documentation](https://docs.platformio.org/en/latest/core/userguide/cmd_check.html)
- [cppcheck Manual](https://cppcheck.sourceforge.io/manual.pdf)
- [ArduinoJson API Reference](https://arduinojson.org/)
- [Contributing Guidelines](../CONTRIBUTING.md)

## Version History

| Date | Change |
|------|--------|
| 2026-01-31 | Initial documentation - native-only linting, suppression strategy |

---

**Last Updated**: 2026-01-31
**Maintainer**: StepAware Development Team
