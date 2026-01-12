# Build Validator Agent

**Purpose**: Validate firmware builds, check resource usage, and report warnings.

## When to Invoke

- After modifying C++ code
- After changing platformio.ini
- After adding dependencies
- Before creating pull requests
- When binary size is a concern

## Task Description Template

```
Validate StepAware firmware build:

1. Clean previous build artifacts
2. Build firmware for esp32-devkitlipo target
3. Analyze build output:
   - Flash usage (report if > 90%)
   - RAM usage (report if > 80%)
   - Compilation warnings (list all)
   - Build errors (detailed)
4. Compare with previous build (if size increased significantly)

Report build health and any concerns.

Use Docker: docker-compose run --rm stepaware-dev
```

## Expected Tools

- Bash (for build commands)
- Read (for analyzing build output)
- Grep (for finding warnings/errors)

## Success Criteria

- Build completes successfully
- Flash usage < 90% (< 1.26MB of 1.4MB)
- RAM usage < 80% (< 262KB of 327KB)
- Zero compilation warnings (preferred)
- No build errors

## Example Invocation

```
User: "Added MQTT client implementation"

Main AI: "Let me validate the build"

→ Invokes build-validator agent

Agent returns:
⚠️ Build successful with concerns
- Flash: 412KB / 1.4MB (29.4%) - increased by 125KB
- RAM: 28KB / 327KB (8.5%) - increased by 7KB
- Warnings: 2
  - src/mqtt_client.cpp:45: unused variable 'retry_count'
  - src/mqtt_client.cpp:78: comparison between signed and unsigned

Recommendations:
- Remove unused variable (line 45)
- Fix comparison warning (line 78)
- Consider: Build size increased by 125KB (43% increase)

Main AI: "Build successful but 2 warnings found. Let me fix those..."
```

## Validation Thresholds

### Flash Usage
- ✅ 0-70%: Excellent
- ⚠️ 70-90%: Monitor closely
- ❌ 90-100%: Critical - reduce size

### RAM Usage
- ✅ 0-60%: Excellent
- ⚠️ 60-80%: Monitor closely
- ❌ 80-100%: Critical - optimize

### Warnings
- ✅ 0: Perfect
- ⚠️ 1-3: Review and fix if possible
- ❌ 4+: Must fix before commit

## Example Output Format

```markdown
## Build Validation Report

### Build Status: ✅ Success

### Resource Usage

| Resource | Used | Available | Percentage | Status |
|----------|------|-----------|------------|--------|
| Flash | 287KB | 1.4MB | 21.9% | ✅ Excellent |
| RAM | 21KB | 327KB | 6.7% | ✅ Excellent |

### Build Details
- Target: esp32-devkitlipo
- Platform: ESP32 (6.9.0)
- Framework: Arduino
- Duration: 17.4 seconds

### Warnings: 0
No compilation warnings ✅

### Comparison with Previous Build
- Flash: No change
- RAM: No change

### Recommendations
✅ Build is healthy
✅ Plenty of room for features
✅ No optimization needed

## Summary
Build validated successfully. Ready for testing.
```

## Failure Handling

If build fails:

1. Agent captures full error output
2. Identifies error location (file:line)
3. Suggests common fixes:
   - Missing includes
   - Syntax errors
   - Linker errors
   - Dependency issues
4. Returns detailed error for main AI to fix

## Size Increase Alerts

If build size increases by > 20%:

```
⚠️ Significant Size Increase Detected

Previous build: 287KB
Current build: 412KB
Increase: 125KB (43%)

Possible causes:
- New library added (check platformio.ini)
- Large string constants (check for embedded data)
- Unoptimized code (check -O2 flag)
- Debug symbols included (should be stripped)

Recommendations:
- Review recent changes
- Check if all features are necessary
- Consider code optimization
```

## Notes

- Should complete in 20-30 seconds
- Critical for maintaining firmware size constraints
- ESP32-C3 has limited flash, so monitoring is essential
- Warnings should be treated seriously (often indicate bugs)
