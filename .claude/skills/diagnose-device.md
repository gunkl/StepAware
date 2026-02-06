# `/diagnose-device` — General ESP32 Device Diagnostic Framework

Comprehensive diagnostic tool for ESP32 devices. Provides systematic data collection, pattern analysis, hypothesis formation, and fix recommendations for ANY device issue (crashes, reboots, connectivity, sensor failures, etc.).

Uses agent-based architecture for efficient parallel execution.

---

## Agent Architecture

### Coordinator Agent (Background)
- Manages overall diagnostic workflow
- Delegates tasks to parallel agents
- Collects and synthesizes results
- Forms master hypothesis from all findings
- Generates unified fix recommendations

### Parallel Data Collection Agents
- **Agent 1: Log Collector** - Downloads all available logs simultaneously
- **Agent 2: Status Collector** - Queries device APIs (status, config, stats)
- **Agent 3: Core Dump Analyzer** - Attempts download and stack trace analysis
- **Agent 4: File System Inspector** - Checks LittleFS usage, corruption
- Run simultaneously to minimize wait time

### Analysis Agents
- **Pattern Recognition** - Identifies recurring crash points, timing patterns
- **Timeline Reconstructor** - Builds complete device history from boot cycles
- **Configuration Validator** - Checks for misconfigurations or conflicts

### Verification Agent
- Tests proposed fixes against device
- Validates hypothesis with targeted experiments
- Monitors fix effectiveness over time

---

## Step 1: Confirm Device IP

Read `secrets.env` in the project root to extract `DEVICE_IP`. The file uses simple `KEY=VALUE` format.

- If `secrets.env` exists and contains `DEVICE_IP`, use that value as the default. Show it to the user and ask for confirmation or override before continuing.
- If the file does not exist or `DEVICE_IP` is not set, show **no default** and require the user to supply the IP explicitly.
- If user provided IP as argument, use that directly.

Do not proceed until a valid IP is confirmed.

---

## Step 2: Launch Coordinator Agent (Background)

Launch a general-purpose coordinator agent in background to manage the entire diagnostic workflow:

```python
coordinator = Task(
    subagent_type="general-purpose",
    description="Diagnose device issues",
    prompt=f"""
    Coordinate comprehensive device diagnostic for {DEVICE_IP}.

    Issue Description: {ISSUE_DESCRIPTION}

    PHASE 1: Launch 4 Parallel Data Collection Agents
    - Agent 1: Download all logs (current, boot_1, boot_2)
    - Agent 2: Query device APIs (status, config)
    - Agent 3: Attempt core dump download and analysis
    - Agent 4: Check filesystem health

    PHASE 2: Synthesize Results
    - Build boot timeline with gap analysis
    - Identify crash points (last log before silence)
    - Detect configuration conflicts
    - Analyze resource usage trends
    - Create pattern matrix

    PHASE 3: Form Master Hypothesis
    - Classify all issues found (Type A, B, C...)
    - Provide evidence for each
    - Identify root causes
    - Specify affected files and line numbers
    - Recommend exact code changes

    PHASE 4: Generate Fix Strategy
    - Prioritize: Critical > High > Medium > Low
    - Determine parallel vs sequential fixes
    - Create verification test plan

    PHASE 5: Report Findings
    - Multi-issue classification
    - Prioritized fix recommendations
    - Verification plan with success criteria

    PROJECT CONTEXT:
    - Working directory: c:\\Users\\David\\Documents\\VSCode Projects\\ESP32\\StepAware
    - Platform: espressif32
    - Framework: arduino
    - Board: esp32-c3-devkitm-1

    Begin execution now.
    """,
    run_in_background=True
)
```

Inform user that coordinator is running in background and will report when complete.

---

## Step 3: Wait for Coordinator Completion

The coordinator agent will:
1. Launch 4 parallel data collection agents IN SINGLE MESSAGE
2. Wait for all to complete
3. Synthesize results across all data sources
4. Form comprehensive hypothesis
5. Generate prioritized fix recommendations
6. Output verification plan

This happens automatically in the background. No manual intervention needed.

---

## Step 4: Present Results to User

When coordinator completes, present:

### Multi-Issue Classification

For each issue identified:
```
**Issue #N: [Issue Name] ([Type Letter])**
- **Severity**: Critical/High/Medium/Low
- **Pattern**: [What logs/behavior shows this issue]
- **Evidence**: [Specific log lines, core dump findings, config values]
- **Root Cause**: [Technical explanation]
- **Affected Files**: [File paths and line numbers]
- **Recommended Fix**: [Exact code changes or configuration updates]
- **Test Plan**: [How to verify the fix works]
```

### Prioritized Fix Strategy

```
**Fix Order:**
1. **Critical** (blocks all operation) - Apply immediately
2. **High** (frequent crashes) - Fix in Phase 1
3. **Medium** (occasional issues) - Fix in Phase 2
4. **Low** (edge cases) - Fix when convenient

**Parallel Fixes:** [List issues that can be fixed simultaneously]
**Sequential Fixes:** [List issues that must be fixed in order with dependencies]
```

### Verification Plan

```
**Success Criteria:**
- [ ] Issue reproducible before fix
- [ ] Issue not reproducible after fix
- [ ] No new issues introduced
- [ ] Performance within acceptable range
- [ ] All tests pass

**Verification Steps:**
1. Apply fixes
2. Monitor device for [duration]
3. Confirm issue resolution
4. Check for regressions
5. Validate success criteria
```

---

## Step 5: Optional Verification Agent

If user approves fixes and wants automated monitoring:

```python
verification = Task(
    subagent_type="general-purpose",
    description="Verify device fixes",
    prompt=f"""
    Monitor device {DEVICE_IP} for {TEST_DURATION} to verify fixes:

    ISSUES FIXED:
    {LIST_OF_FIXES_APPLIED}

    VERIFICATION TASKS:
    1. Monitor device continuously
    2. Check logs for issue recurrence
    3. Validate success criteria met
    4. Detect any new issues introduced
    5. Report findings at {REPORT_INTERVAL}

    SUCCESS CRITERIA:
    {SUCCESS_CRITERIA_CHECKLIST}

    Report status every {REPORT_INTERVAL}.
    """,
    run_in_background=True
)
```

---

## Skill Arguments

**Default (no args):**
```bash
/diagnose-device
```
- Uses `DEVICE_IP` from secrets.env
- Prompts user for issue description interactively

**With IP:**
```bash
/diagnose-device 10.123.0.98
```
- Uses specified IP
- Prompts for issue description

**With IP and Issue:**
```bash
/diagnose-device 10.123.0.98 "device crashes during sleep"
```
- Uses specified IP and issue description
- Runs fully automated

**Data-only Mode:**
```bash
/diagnose-device 10.123.0.98 --data-only
```
- Collects data but skips analysis
- Useful for manual investigation

---

## Execution Flow Example

```
User: /diagnose-device 10.123.0.98 "device rebooting unexpectedly"

Step 1: Confirm IP (10.123.0.98 ✓)

Step 2: Launch coordinator agent (background)
  ├─ Phase 1: Launch 4 parallel data collectors
  │   ├─ Agent 1: Download logs (current, boot_1, boot_2) ✓
  │   ├─ Agent 2: Query status and config ✓
  │   ├─ Agent 3: Attempt core dump download ✓
  │   └─ Agent 4: Check filesystem health ✓
  ├─ Phase 2: Synthesize results
  │   └─ Boot timeline, crash points, config validation ✓
  ├─ Phase 3: Form hypotheses
  │   └─ Issue #1 (Critical): Watchdog timeout
  │   └─ Issue #2 (High): Memory leak
  ├─ Phase 4: Generate fix strategy
  │   └─ Parallel fixes possible for Issue #1 and #2
  └─ Phase 5: Report findings ✓

Step 3: Present results to user
  • 2 issues identified
  • Prioritized fix recommendations
  • Verification plan provided

Step 4: User reviews and approves fixes

Step 5: (Optional) Launch verification agent
  • Monitor device for 24 hours
  • Report every 4 hours
  • Validate success criteria
```

---

## Related Skills

- **`/device-logs`** - Download individual device log files
- **`/coredump`** - Download and analyze ESP32 core dumps
- **`/diagnose-sleep`** - Specialized sleep/power management diagnostics

---

## When to Use This Skill

✅ **Use `/diagnose-device` when:**
- Device behaving unexpectedly (crashes, reboots, hangs)
- Need systematic root cause analysis
- Multiple symptoms that may be related
- Want comprehensive diagnostics without manual iteration
- First time investigating a device issue

❌ **Don't use when:**
- Issue already well understood (use targeted fixes directly)
- Need only logs (use `/device-logs` instead)
- Need only core dump (use `/coredump` instead)
- Issue is sleep-specific (use `/diagnose-sleep` for more focused analysis)

---

## Success Metrics

This skill is working well when:
- ✅ All issues identified in single pass (no repeated investigations)
- ✅ Root causes correctly identified with evidence
- ✅ Fix recommendations are actionable (specific files/lines)
- ✅ Applied fixes resolve issues without regressions
- ✅ Verification confirms success

---

**Last Updated**: 2026-02-06
**For**: StepAware ESP32 Project
**Framework**: Agent-based diagnostic coordination
