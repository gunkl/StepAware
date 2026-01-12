# Working with Claude on StepAware

This document provides guidelines for using Claude (or other AI assistants) effectively on the StepAware project.

## Philosophy

Claude is a development assistant that helps with:
- Writing code and tests
- Finding bugs and suggesting fixes
- Writing documentation
- Exploring the codebase
- Answering technical questions

**Important**: The human developer remains responsible for all code review, commits, and architectural decisions.

## Git Workflow

### Commits

**NEVER** have Claude make git commits directly. The human developer should:

1. Review all changes made by Claude
2. Test the changes
3. Write commit messages themselves
4. Execute git commands manually

**NEVER** add co-authoring statements like:
```
Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

The git history should reflect human authorship only.

### Recommended Workflow

```bash
# 1. Claude writes code/fixes
# (AI makes changes to files)

# 2. Human reviews changes
git diff

# 3. Human tests changes
docker-compose run --rm stepaware-dev pio test -e native

# 4. Human commits
git add .
git commit -m "Your commit message here"
git push
```

### Commit Messages

Ask Claude for commit message suggestions, but write them yourself:

**Good Practice:**
```
You: "suggest a commit message for these changes"
Claude: "Suggests: 'Add mock web server for testing dashboard without hardware'"
You: git commit -m "Add mock web server for testing dashboard"
```

**Bad Practice:**
```
Claude executes: git commit -m "..."
```

## Code Review Responsibilities

### Human Reviews
- Architectural decisions
- Security implications
- Performance considerations
- Code style consistency
- Test coverage adequacy

### Claude Can Help With
- Identifying potential bugs
- Suggesting test cases
- Finding edge cases
- Code formatting
- Documentation completeness

## Effective Prompting

### Good Prompts

**Specific and focused:**
```
"Add input validation to the ConfigManager::fromJSON() method
to check that batteryVoltageFull > batteryVoltageLow"
```

**With context:**
```
"The web API is returning 500 errors when posting config.
Check src/web_api.cpp handlePostConfig() for issues."
```

**Incremental:**
```
"First, create the header file for the Logger class"
"Now implement the Logger::log() method"
```

### Less Effective Prompts

**Too vague:**
```
"Make it better"
"Fix the bugs"
```

**Too broad:**
```
"Rewrite the entire codebase"
"Implement all features at once"
```

## Development Workflow

### 1. Planning Phase

**Ask Claude to:**
- Analyze requirements
- Suggest implementation approaches
- Identify potential issues
- Create task breakdowns

**Example:**
```
"I want to add MQTT support for remote monitoring.
What's the best approach for integrating this with
the existing ConfigManager and Logger?"
```

### 2. Implementation Phase

**Ask Claude to:**
- Write initial implementations
- Create test files
- Add documentation
- Suggest error handling

**Example:**
```
"Create a MQTTClient class in include/mqtt_client.h
with methods: connect(), publish(), subscribe().
Include error handling and logging."
```

### 3. Testing Phase

**Ask Claude to:**
- Write test cases
- Suggest edge cases
- Create mock implementations
- Fix failing tests

**Example:**
```
"The test_config_manager.py test is failing on JSON
deserialization. Help me debug and fix it."
```

### 4. Documentation Phase

**Ask Claude to:**
- Write README sections
- Create API documentation
- Add code comments
- Update guides

**Example:**
```
"Create documentation for the REST API endpoints
in a file called API.md with request/response examples"
```

## Docker Usage with Claude

### Mock Web Server

When testing the web UI:

```bash
# Start mock server
docker-compose up mock-server

# Ask Claude to modify the UI
"Add a battery indicator to the dashboard that shows
the current battery percentage"

# Test changes live (auto-refresh browser)
# Review and commit if satisfied
```

### Build and Test

When working on firmware:

```bash
# Ask Claude to make changes
"Implement the deep sleep functionality in src/power_manager.cpp"

# Build and test
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo
docker-compose run --rm stepaware-dev pio test -e native

# Review output and iterate with Claude if needed
```

## What to Share with Claude

### ✅ Safe to Share
- Error messages and stack traces
- Build output
- Test results
- Code snippets
- Configuration files (without secrets)
- Architecture diagrams
- Requirements documents

### ⚠️ Be Careful With
- API keys (remove before sharing)
- WiFi passwords (use placeholders)
- Personal information
- Production credentials

### ❌ Never Share
- Private user data
- Production secrets
- Proprietary business logic (if applicable)

## Common Tasks

### Adding a New Feature

```
1. You: "I want to add feature X"
2. Claude: Suggests implementation approach
3. You: Review and approve approach
4. Claude: Creates files and writes code
5. You: Test with Docker
6. Claude: Fixes issues if tests fail
7. You: Review final code
8. You: Commit changes yourself
```

### Debugging

```
1. You: Share error message/unexpected behavior
2. Claude: Analyzes code and suggests fixes
3. You: Apply fixes
4. You: Test again
5. Iterate until resolved
6. You: Commit fix yourself
```

### Refactoring

```
1. You: "Refactor the HAL layer to use dependency injection"
2. Claude: Shows refactored code
3. You: Review changes
4. Claude: Ensures tests still pass
5. You: Test manually
6. You: Commit refactoring yourself
```

## Best Practices

### Do:
- ✅ Review all AI-generated code
- ✅ Test thoroughly before committing
- ✅ Ask for explanations when unclear
- ✅ Iterate and refine suggestions
- ✅ Use Claude for documentation
- ✅ Leverage Claude for test creation
- ✅ Keep conversations focused

### Don't:
- ❌ Blindly accept all suggestions
- ❌ Let Claude make git commits
- ❌ Skip testing AI-generated code
- ❌ Use overly complex AI suggestions
- ❌ Ignore security implications
- ❌ Let AI make architectural decisions alone

## File Organization

When working with Claude, organize requests by component:

```
Hardware Abstraction Layer (HAL):
- include/hal/*.h
- src/hal/*.cpp
- test/test_hal_*.py

Business Logic:
- include/*.h (config_manager, logger, state_machine)
- src/*.cpp
- test/test_*.py

Web Interface:
- include/web_api.h
- src/web_api.cpp
- data/*.html, *.css, *.js
- test/mock_web_server.py

Documentation:
- README.md, API.md, CLAUDE.md, etc.
```

This helps Claude understand context and make appropriate changes.

## Troubleshooting Claude Interactions

### Claude Makes Incorrect Assumptions

**Issue**: Claude suggests code that doesn't fit the architecture

**Solution**: Provide more context
```
"Actually, we use the HAL abstraction layer for hardware access.
Don't directly call Arduino functions - use the HAL interfaces."
```

### Claude Generates Too Much Code

**Issue**: Response is overwhelming

**Solution**: Break into smaller tasks
```
"Just create the header file first, we'll implement the
methods in the next step."
```

### Claude's Code Doesn't Compile

**Issue**: Build errors

**Solution**: Share the full error message
```
"The build failed with this error: [paste error]
The issue is in src/config_manager.cpp line 45"
```

### Changes Aren't Working

**Issue**: Code runs but doesn't work as expected

**Solution**: Describe the unexpected behavior
```
"The LED is blinking at the wrong rate. Expected 500ms,
but it's blinking much faster. Check the timer configuration
in src/hal/hal_led.cpp"
```

## Version Control Integration

### Branching Strategy

Create branches for major features:

```bash
# Human creates branch
git checkout -b feature/mqtt-support

# Claude helps implement
"Implement MQTT client functionality..."

# Human tests and commits
git add .
git commit -m "Add MQTT client implementation"

# Human merges when ready
git checkout main
git merge feature/mqtt-support
```

### Pull Requests

When creating PRs:

1. Human creates PR with description
2. Human reviews all changes in diff
3. Claude can help write PR description
4. Human approves and merges

**Don't** let Claude create or merge PRs automatically.

## Testing Strategy

### Test-Driven Development with Claude

```
1. You: "Create a test for ConfigManager validation"
2. Claude: Writes test_config_manager.py with test cases
3. You: Run tests (should fail initially)
4. You: "Now implement the validation logic"
5. Claude: Implements ConfigManager::validate()
6. You: Run tests (should pass)
7. You: Review and commit
```

### Test Coverage

Ask Claude to help improve coverage:
```
"Analyze test_state_machine.py and suggest missing test cases"
```

## Documentation Collaboration

### README Updates

```
You: "Update README.md to include Docker setup instructions"
Claude: Adds section with examples
You: Review for accuracy
You: Commit updated README
```

### Code Comments

```
You: "Add comprehensive comments to src/state_machine.cpp"
Claude: Adds inline documentation
You: Review for correctness
You: Commit documented code
```

## Security Considerations

### Code Review Focus Areas

When Claude writes security-sensitive code, extra review needed:

- Input validation
- Buffer overflow protection
- Authentication/authorization
- Credential storage
- Network security
- SQL injection (if using databases)

**Always test security features manually.**

## Performance Optimization

When asking Claude for performance help:

```
"Profile the motion detection loop and suggest optimizations
to reduce power consumption. Current battery life is 8 days,
target is 14 days."
```

Claude can suggest algorithmic improvements, but **you** should:
- Measure actual performance
- Verify power consumption
- Test on real hardware

## Continuous Integration

### GitHub Actions

Claude can help with CI/CD:

```
"Create a GitHub Actions workflow that builds the firmware,
runs all tests, and uploads artifacts"
```

But **you** should:
- Review the workflow YAML
- Test it on a branch first
- Verify it works correctly

## Learning from Claude

Use Claude as a learning tool:

```
"Explain how the ESPAsyncWebServer handles concurrent
requests differently from a synchronous server"

"What are the trade-offs between using SPIFFS vs LittleFS
for the configuration storage?"

"Why does the PIR sensor need a warmup period?"
```

## Project-Specific Guidelines

### StepAware Architecture

When asking Claude for changes, reference these principles:

1. **HAL Abstraction**: Hardware access through HAL layer only
2. **Mock Support**: All components support mock mode for testing
3. **Single Responsibility**: Each class has one clear purpose
4. **Testability**: All logic has corresponding test coverage
5. **Configuration**: All settings in ConfigManager, stored as JSON
6. **Logging**: All events logged through Logger class

### Code Style

Claude should follow existing conventions:
- Class names: `PascalCase`
- Methods: `camelCase`
- Constants: `UPPER_SNAKE_CASE`
- Private members: `m_camelCase`
- Indentation: 4 spaces
- Line length: 100 characters max

## Limitations of AI Assistance

### Claude Cannot:
- ❌ Test on real hardware
- ❌ Make architectural decisions alone
- ❌ Understand your specific requirements without context
- ❌ Replace human code review
- ❌ Debug hardware-specific issues
- ❌ Make judgment calls on UX/design

### Claude Can:
- ✅ Write boilerplate code quickly
- ✅ Suggest alternative approaches
- ✅ Find common bugs
- ✅ Create comprehensive tests
- ✅ Write documentation
- ✅ Explain complex concepts
- ✅ Refactor code for clarity

## Emergency Procedures

### If Claude Suggests Breaking Changes

1. **Don't apply immediately**
2. Create a backup branch
3. Apply changes on test branch
4. Run full test suite
5. Test manually with mock server
6. Only merge if everything works

### If Stuck in a Loop

If Claude keeps suggesting the same fix that doesn't work:

1. **Stop and reassess**
2. Provide more context
3. Share the full error log
4. Try a different approach
5. Consider manual debugging

## Success Metrics

You're using Claude effectively when:

- ✅ Development speed increased
- ✅ Test coverage improved
- ✅ Documentation is comprehensive
- ✅ Bugs caught earlier
- ✅ Code quality maintained
- ✅ You understand all changes made

You need to adjust when:

- ❌ Making changes you don't understand
- ❌ Tests failing frequently
- ❌ Build times increasing
- ❌ Code becoming more complex
- ❌ Losing track of architecture

## Resources

### Project Documentation
- [README.md](README.md) - Project overview
- [DOCKER_GUIDE.md](DOCKER_GUIDE.md) - Docker setup and usage
- [API.md](API.md) - REST API reference
- [QUICKSTART_DOCKER.md](QUICKSTART_DOCKER.md) - Quick start guide

### StepAware Components
- State Machine: [include/state_machine.h](include/state_machine.h)
- Config Manager: [include/config_manager.h](include/config_manager.h)
- Logger: [include/logger.h](include/logger.h)
- Web API: [include/web_api.h](include/web_api.h)

### Testing
- Mock Server: [test/MOCK_SERVER.md](test/MOCK_SERVER.md)
- Python Tests: [test/](test/)
- C++ Tests: [test/](test/)

## Conclusion

Claude is a powerful development assistant, but **you** are the developer. Use Claude to:

- Speed up development
- Improve code quality
- Learn new concepts
- Catch bugs early

But always:

- Review all changes
- Test thoroughly
- Make final decisions
- Own the git history

Happy coding! 🚀

---

**Last Updated**: 2026-01-12
**For**: StepAware ESP32 Project
**AI**: Claude Sonnet 4.5
