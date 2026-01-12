# Test Runner Agent

**Purpose**: Automatically run all tests (firmware build, C++ tests, Python tests) and report results.

## When to Invoke

- After implementing new features
- After bug fixes
- Before committing code
- After refactoring
- When user asks to "run tests" or "test this"

## Task Description Template

When invoking this agent, use:

```
Run all StepAware tests:

1. Build ESP32 firmware (pio run -e esp32-devkitlipo)
2. Run C++ native tests (pio test -e native)
3. Run all Python tests in test/ directory

Report:
- Build status (success/failure, size, warnings)
- C++ test results (X/Y passing)
- Python test results (for each test file)
- Any failures with line numbers and error messages

Use Docker for all commands via docker-compose.
```

## Expected Tools

- Bash (for running docker-compose commands)
- Read (for reading error logs if needed)
- Glob (for finding test files)

## Success Criteria

- All builds complete without errors
- All C++ tests pass (16/16)
- All Python tests pass (~117 tests)
- No compilation warnings

## Example Invocation

```
User: "I just added input validation to ConfigManager"

Main AI: "Let me run the full test suite to verify everything works"

→ Invokes test-runner agent with context: "Modified src/config_manager.cpp"

Agent returns:
✅ Firmware build: 287KB (21.9% flash), 0 warnings
✅ C++ tests: 16/16 passing
✅ Python tests:
   - test_logic.py: 35/35 passing
   - test_state_machine.py: 17/17 passing
   - test_config_manager.py: 17/17 passing
   - test_logger.py: 14/14 passing
   - test_web_api.py: 18/18 passing
   - test_hal.py: 16/16 passing

Main AI: "All 133 tests passing! ✅ Ready to commit."
```

## Failure Handling

If tests fail:

1. Agent reports specific failures with line numbers
2. Agent suggests potential fixes based on error messages
3. Main AI reviews failures
4. Main AI either fixes issues or reports to user

## Example Output Format

```markdown
## Test Results

### Firmware Build
- Status: ✅ Success
- Size: 287KB / 1.4MB (21.9% flash)
- RAM: 21KB / 327KB (6.7%)
- Warnings: 0
- Duration: 17.4s

### C++ Native Tests
- Status: ✅ All Passing
- Results: 16/16 tests passed
- Duration: 4.9s
- Tests:
  - HAL Button: 6/6 ✅
  - State Machine: 10/10 ✅

### Python Tests
- Status: ✅ All Passing
- Total: 117/117 tests passed

| Test File | Results | Duration |
|-----------|---------|----------|
| test_logic.py | 35/35 ✅ | 1.2s |
| test_state_machine.py | 17/17 ✅ | 0.8s |
| test_config_manager.py | 17/17 ✅ | 0.9s |
| test_logger.py | 14/14 ✅ | 0.7s |
| test_web_api.py | 18/18 ✅ | 1.1s |
| test_hal.py | 16/16 ✅ | 0.9s |

## Summary
✅ All tests passing
✅ No warnings
✅ Build successful
✅ Ready for commit
```

## Notes

- This agent should be invoked frequently during development
- It's the most important quality gate before commits
- Should complete in under 30 seconds for fast feedback
- If any test fails, development should stop and fix issues
