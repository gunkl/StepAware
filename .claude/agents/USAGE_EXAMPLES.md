# Subagent Usage Examples

Practical examples of using StepAware subagents in real development scenarios.

## How to Invoke Subagents (Current Method)

Since these are task templates, you invoke them by asking Claude to execute the task:

### Method 1: Direct Request

```
User: "Run all tests"

Claude: [Reads .claude/agents/test-runner.md]
        [Executes the steps]
        [Returns formatted results]
```

### Method 2: Contextual Request

```
User: "I just fixed the ConfigManager bug, make sure everything still works"

Claude: [Recognizes need for testing]
        [Uses test-runner agent template]
        [Runs all tests]
        [Reports: "All 133 tests passing ✅"]
```

### Method 3: Explicit Agent Reference

```
User: "Use the test-runner agent to validate my changes"

Claude: [Looks up .claude/agents/test-runner.md]
        [Follows the exact template]
        [Returns structured results]
```

---

## Real-World Usage Scenarios

### Scenario 1: Feature Development

**Context**: Adding battery monitoring feature

```
User: "I'm adding battery monitoring to the system"

Claude: "I'll help implement that. Where should we start?"

User: "Create a BatteryMonitor class in include/battery_monitor.h"

Claude: [Creates header file]
        [Creates implementation]
        [Creates test file]
        "Implementation complete. Should I run tests?"

User: "Yes, run all tests"

Claude: [Invokes test-runner agent]

        → Running test-runner agent...

        ✅ Firmware build: 295KB (22.5% flash)
        ✅ C++ tests: 16/16 passing
        ✅ Python tests: 117/117 passing

        "All tests passing! Ready to commit."
```

---

### Scenario 2: Bug Fix Validation

**Context**: Fixed LED timing issue

```
User: "Fixed the LED timing bug in src/hal/hal_led.cpp"

Claude: "Let me validate the fix"

        [Invokes build-validator agent]

        → Running build-validator...

        ✅ Build successful: 287KB
        ✅ 0 warnings
        ✅ No size increase

        [Invokes test-runner agent]

        → Running tests...

        ✅ All tests passing

        "Fix validated! Build clean, all tests pass."

User: "Perfect, I'll commit this"
```

---

### Scenario 3: API Development

**Context**: Adding new /api/battery endpoint

```
User: "Add a new /api/battery endpoint to web_api.cpp"

Claude: [Implements endpoint]
        [Updates web_api.h]
        "Endpoint implemented. Testing..."

        [Invokes api-tester agent]

        → Testing all API endpoints...

        ✅ GET /api/status - 200 OK
        ✅ GET /api/config - 200 OK
        ✅ POST /api/config - 200 OK
        ✅ GET /api/mode - 200 OK
        ✅ POST /api/mode - 200 OK
        ✅ GET /api/logs - 200 OK
        ✅ POST /api/reset - 200 OK
        ✅ GET /api/version - 200 OK
        ✅ GET /api/battery - 200 OK (new)

        "All 9 endpoints working! Should I update the docs?"

User: "Yes please"

Claude: [Updates API.md]
        [Updates data/README.md]
        "Documentation updated!"
```

---

### Scenario 4: Pre-Commit Check

**Context**: Ready to commit multiple changes

```
User: "I've made several changes today. Run a full validation before I commit"

Claude: "Running comprehensive validation..."

        [Invokes build-validator]

        → Build Validation...
        ✅ Firmware: 301KB (22.9% flash)
        ⚠️  Size increased by 14KB from last build
        ✅ 0 warnings

        [Invokes test-runner]

        → Test Execution...
        ✅ Firmware builds successfully
        ✅ C++ tests: 16/16 passing
        ✅ Python tests: 117/117 passing

        [Invokes api-tester]

        → API Testing...
        ✅ All 8 endpoints operational

        "Validation complete! ✅

        Summary:
        - Build: Success (14KB increase due to new feature)
        - Tests: 133/133 passing
        - API: All endpoints working

        Ready to commit! 🎉"

User: "Great! I'll commit now"
```

---

### Scenario 5: Investigating Build Issues

**Context**: Build suddenly failing

```
User: "The build is failing after I added MQTT support"

Claude: "Let me investigate..."

        [Invokes build-validator]

        → Build Validation...

        ❌ Build Failed

        Error in src/mqtt_client.cpp:78:
        'WiFiClient' was not declared in this scope

        Suggestion: Missing include for WiFiClient.h

        "The build is failing because WiFiClient.h is not included.
         Let me fix this..."

        [Adds #include <WiFiClient.h> to mqtt_client.cpp]

        [Invokes build-validator again]

        → Build Validation...
        ✅ Build successful: 412KB (29.4% flash)
        ⚠️  Size increased by 125KB (MQTT library)

        "Build fixed! Note that MQTT support adds 125KB to firmware."
```

---

### Scenario 6: Comprehensive Testing Workflow

**Context**: Major refactoring completed

```
User: "I've refactored the entire HAL layer. Need comprehensive testing"

Claude: "Running full validation suite..."

        [Invokes build-validator]

        → Build Validation...
        ✅ Clean build: 287KB
        ✅ No warnings
        ✅ No size change (good for refactoring!)

        [Invokes test-runner]

        → C++ HAL Tests...
        ✅ HAL Button: 6/6 passing
        ✅ HAL LED: (future tests)
        ✅ HAL PIR: (future tests)

        → Python Integration Tests...
        ✅ Logic: 35/35 passing
        ✅ State Machine: 17/17 passing
        ✅ Config Manager: 17/17 passing
        ✅ Logger: 14/14 passing
        ✅ Web API: 18/18 passing
        ✅ HAL: 16/16 passing

        [Invokes api-tester]

        → API Integration Tests...
        ✅ All endpoints functional

        "Refactoring validated! ✅

        Summary:
        - Code quality: Improved (no warnings)
        - Binary size: Unchanged (perfect!)
        - Test coverage: 100% passing
        - API: Fully functional

        The refactoring was successful with no regressions!"
```

---

## Agent Chaining Examples

### Chain 1: Build → Test → Report

```
User: "Validate everything"

Claude:
  1. build-validator → ✅ Build OK
  2. test-runner → ✅ Tests OK
  3. api-tester → ✅ API OK

  "Everything validated ✅"
```

### Chain 2: Fix → Validate → Document

```
User: "The config validation is broken"

Claude:
  1. [Fixes bug]
  2. build-validator → ✅ Builds clean
  3. test-runner → ✅ Tests pass
  4. [Updates documentation]

  "Bug fixed and validated ✅"
```

---

## Quick Reference Commands

### Test Everything
```
User: "Run all tests"
→ Invokes: test-runner
→ Duration: ~30s
→ Returns: Build + C++ + Python results
```

### Validate Build
```
User: "Check the build"
→ Invokes: build-validator
→ Duration: ~20s
→ Returns: Size, warnings, health report
```

### Test API
```
User: "Test the API endpoints"
→ Invokes: api-tester
→ Duration: ~5s
→ Returns: Endpoint test results
```

### Full Validation
```
User: "Full validation before commit"
→ Invokes: build-validator → test-runner → api-tester
→ Duration: ~60s
→ Returns: Comprehensive report
```

---

## Advanced Usage

### Conditional Execution

```
User: "If the build succeeds, run tests"

Claude:
  1. build-validator → ✅ Success
  2. → test-runner (only because build succeeded)
  3. "Build and tests both successful"
```

### Failure Recovery

```
User: "Fix any test failures automatically"

Claude:
  1. test-runner → ❌ 2 tests failing
  2. [Analyzes failures]
  3. [Attempts fixes]
  4. test-runner → ✅ All passing
  5. "Fixed 2 test failures"
```

### Iterative Testing

```
User: "Keep testing until all tests pass"

Claude:
  1. test-runner → ❌ 3 failures
  2. [Fixes issue 1]
  3. test-runner → ❌ 2 failures
  4. [Fixes issue 2]
  5. test-runner → ❌ 1 failure
  6. [Fixes issue 3]
  7. test-runner → ✅ All passing!
```

---

## Performance Benchmarks

Based on Docker execution:

| Agent | Duration | Use Case |
|-------|----------|----------|
| build-validator | 18-25s | After C++ changes |
| test-runner | 25-35s | After any code change |
| api-tester | 3-7s | After API changes |
| **Full Suite** | **50-70s** | Before commits |

---

## Tips for Effective Agent Use

### ✅ Do:
- Run test-runner frequently during development
- Use build-validator after adding dependencies
- Run api-tester after API changes
- Request full validation before commits

### ❌ Don't:
- Skip testing because "it's a small change"
- Ignore warnings from build-validator
- Commit without running test-runner
- Forget to test after fixing a bug

---

## Integration with Git Workflow

```bash
# Development flow
1. User: Make code changes
2. User: "Run tests"
3. Claude: [Invokes test-runner] ✅
4. User: More changes
5. User: "Run tests again"
6. Claude: [Invokes test-runner] ✅
7. User: "Full validation"
8. Claude: [All agents] ✅
9. User: git commit -m "..." (manual)
10. User: git push (manual)
```

---

## Troubleshooting

### Agent Takes Too Long

```
User: "Why is test-runner taking so long?"

Claude: "Checking..."

        Normal duration: 30s
        Current duration: 2m+

        Possible causes:
        - Docker container not running
        - Network issues
        - Large build cache

        Try: docker-compose down && docker-compose build
```

### Agent Reports Unexpected Failure

```
User: "Tests were passing yesterday, now failing"

Claude: [Invokes test-runner]

        ❌ Tests failing: 5/117

        [Analyzes failures]

        "All failures are in test_config_manager.py
         Root cause: Schema changed in commit abc123
         Tests need updating to match new schema"
```

---

## Future: Automated Invocation

When Claude Code CLI supports agents natively:

```yaml
# Future .claude/agents/test-runner.yaml
triggers:
  - on_code_save: "**/*.cpp"
  - on_code_save: "**/*.h"
  - on_code_save: "**/*.py"
  - on_request: "test*"

auto_run: true
timeout: 60s
```

Then agents would run automatically on file save!

---

**Last Updated**: 2026-01-12
**Agent Templates**: 3 active
**Total Test Coverage**: Build + C++ + Python + API
