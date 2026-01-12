# StepAware Subagent Configuration

This document describes recommended subagents (specialized AI assistants) for automating common StepAware development tasks.

## What Are Subagents?

Subagents are specialized AI agents that:
- Run autonomously to complete specific tasks
- Have access to specific tools and context
- Can be invoked by the main AI assistant or user
- Return results when complete

**Key Benefit**: Offload repetitive tasks while maintaining quality and consistency.

## Recommended Subagents for StepAware

### 1. **test-runner** (High Priority)

**Purpose**: Automatically run tests after code changes

**When to Use**:
- After implementing new features
- After bug fixes
- Before committing code
- After refactoring

**What It Does**:
```
1. Builds ESP32 firmware (pio run -e esp32-devkitlipo)
2. Runs C++ native tests (pio test -e native)
3. Runs all Python tests (test_*.py)
4. Reports pass/fail with detailed output
5. Suggests fixes if tests fail
```

**Example Usage**:
```
User: "I just fixed the ConfigManager validation bug"
Main AI: Launches test-runner subagent
Subagent: Runs all tests, reports results
Main AI: "All 133 tests passing! ✅"
```

**Configuration**:
```yaml
name: test-runner
tools: [Bash, Read, Glob]
working_dir: /workspace
commands:
  - docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo
  - docker-compose run --rm stepaware-dev pio test -e native
  - docker-compose run --rm stepaware-dev python test/test_*.py
auto_invoke: after_code_change
```

---

### 2. **build-validator** (High Priority)

**Purpose**: Validate firmware builds and check for issues

**When to Use**:
- After modifying C++ code
- Before creating pull requests
- After dependency changes
- After platformio.ini updates

**What It Does**:
```
1. Cleans previous builds
2. Builds firmware for esp32-devkitlipo
3. Checks binary size (reports if > 90% flash)
4. Checks RAM usage (reports if > 80% RAM)
5. Validates no compilation warnings
6. Checks for common issues (missing includes, etc.)
```

**Example Usage**:
```
User: "Added MQTT support to src/mqtt_client.cpp"
Main AI: Launches build-validator
Subagent: Builds, checks size (305KB/1.4MB), reports warnings
Main AI: "Build successful! Size: 305KB (22%), 2 warnings to review"
```

**Configuration**:
```yaml
name: build-validator
tools: [Bash, Read, Grep]
validation:
  - max_flash_usage: 90%
  - max_ram_usage: 80%
  - allow_warnings: false
auto_invoke: after_cpp_change
```

---

### 3. **api-tester** (Medium Priority)

**Purpose**: Test REST API endpoints automatically

**When to Use**:
- After modifying web_api.cpp
- After changing API endpoints
- Before deploying changes
- After config schema updates

**What It Does**:
```
1. Starts mock web server
2. Tests all 8 API endpoints with curl
3. Validates JSON responses
4. Tests error cases (invalid input, etc.)
5. Checks CORS headers
6. Verifies status codes
7. Stops mock server when done
```

**Example Usage**:
```
User: "Updated the /api/config endpoint validation"
Main AI: Launches api-tester
Subagent: Tests all endpoints, finds issue with POST /api/config
Main AI: "7/8 endpoints passing. POST /api/config returns 500 on empty battery config"
```

**Configuration**:
```yaml
name: api-tester
tools: [Bash, Read]
endpoints:
  - GET /api/status
  - GET /api/config
  - POST /api/config
  - GET /api/mode
  - POST /api/mode
  - GET /api/logs
  - POST /api/reset
  - GET /api/version
auto_invoke: after_api_change
```

---

### 4. **doc-updater** (Medium Priority)

**Purpose**: Update documentation when code changes

**When to Use**:
- After adding new features
- After API changes
- After configuration schema updates
- After adding new dependencies

**What It Does**:
```
1. Detects what changed (API, config, HAL, etc.)
2. Updates relevant documentation:
   - API.md if endpoints changed
   - README.md if features added
   - DOCKER_GUIDE.md if Docker setup changed
   - data/README.md if web UI changed
3. Ensures examples are accurate
4. Updates version numbers
5. Checks for broken links
```

**Example Usage**:
```
User: "Added new /api/battery endpoint"
Main AI: Launches doc-updater
Subagent: Updates API.md with endpoint docs, adds to README features
Main AI: "Documentation updated: API.md (new endpoint), README.md (feature list)"
```

**Configuration**:
```yaml
name: doc-updater
tools: [Read, Edit, Grep, Glob]
watches:
  - src/web_api.cpp -> API.md
  - include/config_manager.h -> API.md, README.md
  - data/*.html -> data/README.md
auto_invoke: after_feature_add
```

---

### 5. **code-reviewer** (Low Priority)

**Purpose**: Automated code review for common issues

**When to Use**:
- Before committing
- After implementing features
- During code review process

**What It Does**:
```
1. Checks for common bugs:
   - Buffer overflows
   - Memory leaks
   - Uninitialized variables
   - Missing null checks
2. Validates code style:
   - Naming conventions
   - Indentation (4 spaces)
   - Line length (100 chars)
3. Security checks:
   - Input validation
   - Credential storage
   - SQL injection risks
4. Best practices:
   - HAL abstraction usage
   - Proper error handling
   - Logging usage
```

**Example Usage**:
```
User: "Review the new PowerManager class"
Main AI: Launches code-reviewer
Subagent: Analyzes src/power_manager.cpp
Main AI: "3 issues found: Missing null check (line 45), buffer could overflow (line 78), unused variable (line 102)"
```

**Configuration**:
```yaml
name: code-reviewer
tools: [Read, Grep, Glob]
checks:
  - security: high
  - style: medium
  - performance: medium
  - best_practices: high
auto_invoke: false  # Manual only
```

---

### 6. **dependency-updater** (Low Priority)

**Purpose**: Check for and suggest dependency updates

**When to Use**:
- Monthly maintenance
- Before major releases
- After security advisories

**What It Does**:
```
1. Checks platformio.ini for library versions
2. Searches for newer versions
3. Checks compatibility
4. Tests builds with updates
5. Reports breaking changes
6. Suggests safe updates
```

**Example Usage**:
```
User: "Check for dependency updates"
Main AI: Launches dependency-updater
Subagent: Checks all libraries, tests updates
Main AI: "3 safe updates available: ArduinoJson 6.21.3→6.21.5, AsyncTCP 1.1.1→1.1.4, ESPAsyncWebServer 1.2.3→1.2.6"
```

**Configuration**:
```yaml
name: dependency-updater
tools: [Read, Edit, Bash, WebFetch]
safety: conservative
test_after_update: true
auto_invoke: false  # Manual only
```

---

### 7. **mock-data-generator** (Low Priority)

**Purpose**: Generate realistic test data for mock server

**When to Use**:
- Setting up mock server scenarios
- Testing edge cases
- Demo preparation
- UI development

**What It Does**:
```
1. Generates realistic log entries
2. Creates config scenarios (low battery, etc.)
3. Simulates motion event patterns
4. Creates time-series data
5. Updates mock_web_server.py with scenarios
```

**Example Usage**:
```
User: "Create a low battery scenario for testing"
Main AI: Launches mock-data-generator
Subagent: Updates mock server with low battery state, warning logs
Main AI: "Mock server updated with low battery scenario (3200mV, warning logs added)"
```

**Configuration**:
```yaml
name: mock-data-generator
tools: [Read, Edit]
scenarios:
  - low_battery
  - high_motion_activity
  - wifi_disconnected
  - memory_pressure
auto_invoke: false  # Manual only
```

---

## Subagent Workflow Examples

### Example 1: Feature Development

```
User: "Add support for multiple WiFi networks"

Main AI: "I'll implement this feature with subagent assistance"

1. Main AI: Implements feature in src/config_manager.cpp
2. Launches test-runner → All tests pass ✅
3. Launches build-validator → Build successful, 295KB ✅
4. Launches doc-updater → API.md and README.md updated ✅
5. Main AI: "Feature complete! All tests passing, docs updated"

User: Reviews and commits
```

### Example 2: Bug Fix

```
User: "The LED blinks at wrong rate"

Main AI: "Let me investigate and fix this"

1. Main AI: Analyzes src/hal/hal_led.cpp, finds timer issue
2. Main AI: Fixes the bug
3. Launches test-runner → Tests pass ✅
4. Launches build-validator → No warnings ✅
5. Main AI: "Bug fixed! Timer now correctly set to 500ms"

User: Tests on hardware, commits
```

### Example 3: API Change

```
User: "Add authentication to API endpoints"

Main AI: "I'll add API authentication"

1. Main AI: Implements auth in src/web_api.cpp
2. Launches build-validator → Build successful ✅
3. Launches api-tester → Tests fail on auth header ❌
4. Main AI: Fixes auth header handling
5. Launches api-tester → All tests pass ✅
6. Launches doc-updater → API.md updated with auth docs ✅
7. Main AI: "Authentication added and tested!"

User: Reviews security, commits
```

---

## Setting Up Subagents

### Claude Code CLI Integration

If using Claude Code CLI, subagents can be configured in `.claude/agents/`:

```
.claude/
├── agents/
│   ├── test-runner.yaml
│   ├── build-validator.yaml
│   ├── api-tester.yaml
│   ├── doc-updater.yaml
│   ├── code-reviewer.yaml
│   ├── dependency-updater.yaml
│   └── mock-data-generator.yaml
└── settings.local.json
```

### Example Agent Configuration

**`.claude/agents/test-runner.yaml`**:
```yaml
name: test-runner
description: "Run all tests (C++ and Python) and report results"

triggers:
  - after_code_change
  - manual

tools:
  - Bash
  - Read
  - Glob

script: |
  # Build firmware
  echo "Building ESP32 firmware..."
  docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo

  # Run C++ tests
  echo "Running C++ tests..."
  docker-compose run --rm stepaware-dev pio test -e native

  # Run Python tests
  echo "Running Python tests..."
  for test_file in test/test_*.py; do
    docker-compose run --rm stepaware-dev python "$test_file"
  done

  echo "All tests complete!"

success_criteria:
  - exit_code: 0
  - no_test_failures

report_format: |
  ## Test Results
  - Firmware Build: {build_status}
  - C++ Tests: {cpp_test_results}
  - Python Tests: {python_test_results}

  {failures_detail}
```

---

## Subagent Communication Protocol

### Main AI → Subagent

```
Main AI: "I need to test the changes"
→ Launches test-runner subagent
→ Provides context: "Modified src/config_manager.cpp"
```

### Subagent → Main AI

```
Subagent: Returns result
→ Status: success/failure
→ Details: Test results, warnings, errors
→ Suggestions: "Fix line 45 in test_config_manager.py"
```

### Main AI → User

```
Main AI: "Tests complete! 132/133 passing"
→ Shows subagent results
→ Suggests next actions
```

---

## Best Practices

### When to Use Subagents

✅ **Good Use Cases**:
- Repetitive tasks (running tests)
- Multi-step workflows (build → test → report)
- Background tasks (checking dependencies)
- Specialized analysis (code review)

❌ **Poor Use Cases**:
- Architectural decisions
- Security-critical code
- Git operations (commits, pushes)
- Hardware-specific debugging

### Subagent Boundaries

**Subagents CAN**:
- Read files
- Run commands
- Analyze code
- Generate reports
- Suggest fixes

**Subagents CANNOT**:
- Make git commits
- Push to remote
- Make architectural decisions
- Modify code without approval
- Access production systems

### Error Handling

If a subagent fails:

1. **Main AI reviews error**
2. **Main AI attempts fix** (if simple)
3. **Main AI reports to user** (if complex)
4. **User decides** next action

---

## Monitoring Subagents

### Logging

Each subagent should log:
- Start time
- Commands executed
- Results/output
- Duration
- Success/failure status

**Log Location**: `.claude/logs/subagent_<name>_<timestamp>.log`

### Metrics

Track subagent performance:
- Success rate
- Average duration
- False positive rate (for code-reviewer)
- User satisfaction

---

## Advanced: Custom Subagents

### Creating a Custom Subagent

**Example: hardware-flasher**

```yaml
name: hardware-flasher
description: "Flash firmware to ESP32 hardware"

prerequisites:
  - ESP32 connected via USB
  - Firmware built successfully

tools:
  - Bash
  - Read

safety_checks:
  - verify_hardware_connected
  - confirm_firmware_valid
  - backup_current_firmware

script: |
  # Check hardware connection
  if ! pio device list | grep -q "USB"; then
    echo "ERROR: No ESP32 detected"
    exit 1
  fi

  # Flash firmware
  docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo --target upload

  # Monitor initial boot
  timeout 10 docker-compose run --rm stepaware-dev pio device monitor

manual_only: true
confirmation_required: true
```

### Custom Subagent Templates

**Investigation Subagent**:
```yaml
name: bug-investigator
purpose: Analyze bug reports and find root cause
inputs:
  - bug_description
  - error_logs
  - expected_behavior
outputs:
  - root_cause_analysis
  - affected_files
  - suggested_fix
```

**Performance Analyzer**:
```yaml
name: performance-analyzer
purpose: Profile code and suggest optimizations
inputs:
  - target_function
  - performance_metrics
outputs:
  - bottleneck_analysis
  - optimization_suggestions
  - estimated_improvement
```

---

## Integration with GitHub Actions

Subagents can be mirrored in CI/CD:

```yaml
# .github/workflows/ci.yml
jobs:
  test-runner:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build firmware
        run: docker-compose run --rm stepaware-dev pio run
      - name: Run C++ tests
        run: docker-compose run --rm stepaware-dev pio test -e native
      - name: Run Python tests
        run: |
          docker-compose run --rm stepaware-dev python test/test_logic.py
          docker-compose run --rm stepaware-dev python test/test_config_manager.py
          # ... etc
```

This ensures:
- Same tests run locally (via subagent) and in CI
- Consistent environment
- Faster feedback during development

---

## Maintenance

### Regular Review

Monthly review of subagents:
- ✅ Are they being used?
- ✅ Are they saving time?
- ✅ Are results accurate?
- ✅ Do they need updates?

### Deprecation

Remove subagents that:
- Haven't been used in 3 months
- Have high false positive rates
- Are no longer needed
- Have been replaced by better tools

---

## Security Considerations

### Subagent Permissions

Subagents should have **minimal permissions**:

```yaml
permissions:
  read: [src/, include/, test/, data/]
  write: [test/reports/, .claude/logs/]
  execute: [docker-compose, pio]
  forbidden: [git commit, git push, rm -rf]
```

### Sensitive Data

Subagents must **never**:
- Access production credentials
- Commit sensitive data
- Push to remote repositories
- Modify .git/ directory
- Access user's private files

---

## Future Enhancements

### Potential New Subagents

1. **power-profiler**: Analyze power consumption
2. **memory-profiler**: Track heap/stack usage
3. **coverage-reporter**: Generate test coverage reports
4. **changelog-generator**: Auto-generate CHANGELOG.md
5. **release-preparer**: Prepare release artifacts
6. **migration-helper**: Assist with breaking changes

### AI-Driven Improvements

As AI capabilities improve:
- Smarter root cause analysis
- Better optimization suggestions
- Automated refactoring
- Predictive bug detection

---

## Quick Reference

### Invoking Subagents

**Automatic**:
```
User makes code change → Main AI detects → Launches appropriate subagent
```

**Manual**:
```
User: "Run all tests"
Main AI: Launches test-runner subagent
```

**Chained**:
```
User: "Implement feature X"
Main AI: Implements → test-runner → build-validator → doc-updater
```

### Common Commands

```bash
# List available subagents
claude agents list

# Run specific subagent
claude agent run test-runner

# Check subagent logs
cat .claude/logs/subagent_test-runner_*.log

# Disable subagent
claude agent disable code-reviewer

# Enable subagent
claude agent enable code-reviewer
```

---

## Conclusion

Subagents supercharge development by:
- ✅ Automating repetitive tasks
- ✅ Ensuring consistency
- ✅ Catching issues early
- ✅ Saving developer time
- ✅ Maintaining quality

**Recommended Priority**:
1. **test-runner** (Immediate)
2. **build-validator** (Immediate)
3. **api-tester** (Short-term)
4. **doc-updater** (Short-term)
5. **code-reviewer** (Nice-to-have)

Start with test-runner and build-validator, then add others as needed!

---

**Last Updated**: 2026-01-12
**For**: StepAware ESP32 Project
**Related**: [CLAUDE.md](CLAUDE.md), [DOCKER_GUIDE.md](DOCKER_GUIDE.md)
