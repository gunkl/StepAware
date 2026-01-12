# StepAware Subagent Definitions

This directory contains specialized subagent definitions for the StepAware project.

## Available Agents

### High Priority (Use Frequently)

1. **[test-runner](test-runner.md)** ⭐⭐⭐
   - Runs all tests (firmware build, C++ tests, Python tests)
   - Use after: Code changes, before commits
   - Duration: ~30 seconds

2. **[build-validator](build-validator.md)** ⭐⭐⭐
   - Validates firmware builds and resource usage
   - Use after: C++ changes, dependency updates
   - Duration: ~20 seconds

### Medium Priority (Use As Needed)

3. **[api-tester](api-tester.md)** ⭐⭐
   - Tests all REST API endpoints
   - Use after: API changes, config updates
   - Duration: ~5 seconds

## How to Use These Agents

### Option 1: Manual Invocation (Current)

Copy the task description from the agent's .md file and ask Claude to execute it:

```
User: "Run all tests"

You: [Reads test-runner.md task template]
     [Executes the steps described]
     [Returns formatted results]
```

### Option 2: Automated Invocation (Future)

If Claude Code CLI supports agent files, these can be used directly:

```bash
# Run specific agent
claude agent test-runner

# Auto-invoke based on context
[Code change detected] → test-runner auto-runs
```

### Option 3: Task Tool Integration (Current Best Practice)

Use the Task tool to invoke specialized agents:

```
User: "Test all the API endpoints"

Main AI: [Calls Task tool]
  subagent_type: "general-purpose"
  description: "Test StepAware API endpoints"
  prompt: [Content from api-tester.md]

Task agent: [Executes and returns results]
Main AI: [Reports to user]
```

## Agent Workflow Examples

### Example 1: After Feature Implementation

```
User: "Added MQTT support"

Main AI Actions:
1. Code review completed
2. → Invoke test-runner
   - Build firmware ✅
   - Run C++ tests ✅
   - Run Python tests ✅
3. → Invoke build-validator
   - Check size increase ⚠️ +125KB
   - Check warnings ✅ None
4. Report to user with results
```

### Example 2: API Development

```
User: "Updated the /api/config endpoint"

Main AI Actions:
1. Review changes
2. → Invoke api-tester
   - Test all endpoints
   - Found issue in validation ❌
3. Fix validation issue
4. → Invoke api-tester again
   - All tests pass ✅
5. Report to user
```

## Agent Communication Protocol

### Input to Agent

Each agent receives:
- **Context**: What changed (e.g., "Modified src/config_manager.cpp")
- **Task**: Specific instructions from the .md file
- **Tools**: Bash, Read, Grep, etc.

### Output from Agent

Agent returns:
- **Status**: Success/Failure
- **Results**: Detailed test results, build info, etc.
- **Issues**: Any problems found
- **Suggestions**: Recommended fixes (if applicable)

## Creating New Agents

To create a new agent:

1. **Copy template**:
   ```bash
   cp test-runner.md my-new-agent.md
   ```

2. **Define agent**:
   - Purpose
   - When to invoke
   - Task description template
   - Expected tools
   - Success criteria
   - Example output format

3. **Test agent**:
   - Manually invoke using the task template
   - Verify it works as expected
   - Refine based on results

4. **Document**:
   - Add to this README
   - Add to SUBAGENTS.md
   - Include examples

## Agent Best Practices

### ✅ Good Agent Design

- **Focused**: Single clear purpose
- **Autonomous**: Can run without user input
- **Deterministic**: Same inputs → same outputs
- **Fast**: Complete in < 60 seconds
- **Informative**: Clear, actionable output

### ❌ Poor Agent Design

- **Vague**: Unclear what it does
- **Interactive**: Requires user input mid-run
- **Slow**: Takes > 2 minutes
- **Noisy**: Too much irrelevant output
- **Unreliable**: Different results each time

## Integration with Development Workflow

```
┌─────────────────────────────────────────────────┐
│                User Request                     │
│        "Add feature X" or "Fix bug Y"          │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│              Main AI (Claude)                   │
│   - Understands request                         │
│   - Writes/modifies code                        │
│   - Decides which agents to invoke              │
└────────────────┬────────────────────────────────┘
                 │
                 ├──────────────┐
                 ▼              ▼
    ┌──────────────────┐  ┌──────────────────┐
    │  test-runner     │  │ build-validator  │
    │  - Build firmware│  │ - Check size     │
    │  - Run C++ tests │  │ - Check warnings │
    │  - Run Py tests  │  │ - Report issues  │
    └────────┬─────────┘  └────────┬─────────┘
             │                     │
             └──────────┬──────────┘
                        ▼
           ┌────────────────────────┐
           │   Results Aggregated   │
           └────────────┬───────────┘
                        │
                        ▼
           ┌────────────────────────┐
           │  Report to User        │
           │  "Feature X complete!" │
           │  "All tests passing ✅"│
           └────────────────────────┘
```

## Monitoring Agent Performance

Track these metrics:

- **Success Rate**: % of successful runs
- **Duration**: Average completion time
- **Utility**: How often used
- **Accuracy**: False positive/negative rate

## Maintenance Schedule

- **Weekly**: Review agent logs
- **Monthly**: Analyze usage patterns
- **Quarterly**: Update agent definitions
- **Yearly**: Deprecate unused agents

## Security Considerations

Agents must **NEVER**:
- Make git commits
- Push to remote repositories
- Access production systems
- Modify .git/ directory
- Execute destructive commands (rm -rf, etc.)

Agents **MAY**:
- Read source code
- Run builds
- Execute tests
- Generate reports
- Write to test/reports/ and .claude/logs/

## Future Enhancements

Planned agents (see [SUBAGENTS.md](../../SUBAGENTS.md)):

- **doc-updater**: Auto-update documentation
- **code-reviewer**: Automated code review
- **dependency-updater**: Check for lib updates
- **mock-data-generator**: Generate test scenarios

## Support

For questions or issues:
1. Check [SUBAGENTS.md](../../SUBAGENTS.md) for detailed guide
2. Check [CLAUDE.md](../../CLAUDE.md) for AI usage guidelines
3. Review agent .md files for specific instructions

---

**Last Updated**: 2026-01-12
**Agent Count**: 3
**Total Coverage**: Build, Test, API validation
