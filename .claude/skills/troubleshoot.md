# `/troubleshoot` — Structured Bug Investigation for StepAware

Systematic debugging methodology derived from Issue #44 (IDLE TWDT watchdog crash
investigation, builds 0219–0255). Enforces evidence-first hypothesis tracking, prevents
assumption creep, and ensures we always use the right diagnostic tools.

**Use this skill at the start of any non-trivial bug investigation.**

---

## Why This Skill Exists

Issue #44 burned 35+ builds and multiple weeks because:
- Hypotheses were formed before validating user environment (USB vs battery)
- Wrong logging system used for crash evidence (regular Logger JSON vs DebugLogger text)
- Task subscription assumptions hardcoded without verification (H6)
- Reactive fixes applied without proving the root cause
- No formal tracking → same wrong assumptions repeated across sessions

This skill prevents all of those.

---

## Step 0: Read Memory and Hypotheses Files

Before anything else:

1. Check `MEMORY.md` — it contains hard-won facts about logging systems, IDF API versions,
   known pitfalls, and tooling paths. Do NOT re-discover what is already written there.

2. If a `docs/ISSUE_NN_HYPOTHESES.md` exists for this issue, read it. Continue from where
   the last session left off — do not re-form hypotheses that are already PROVEN or DISPROVEN.

3. Check the GitHub issue (`gh issue view NN --repo gunkl/StepAware`) for the latest
   comment. The comment thread is the source of truth for crash history and build results.

---

## Step 1: Define the Problem Precisely

Write a one-sentence problem statement. It must be **observable** (what the device does),
not a theory (what you think causes it).

**Good:** "Device reboots with TASK_WDT panic approximately 8 seconds after waking from light sleep."
**Bad:** "Watchdog is not being fed." (theory, not observation)

---

## Step 2: Validate User Environment (CRITICAL — Do This Before Any Analysis)

**Use AskUserQuestion before forming any hypothesis.** Wrong assumptions waste builds.

Ask the user:
1. Power source during the failure? (Battery only / USB / USB charging battery)
2. What was directly observed? (LED state, network response, log content, nothing)
3. How was it discovered? (Crash log, unresponsive device, user saw it reboot)
4. How was recovery done? (Button press, power cycle, automatic)
5. Which firmware build was running at time of failure?
6. Was this a first occurrence or recurring? If recurring, how frequently?

**Do NOT skip this.** The Issue #44 investigation wasted multiple builds because USB
connectivity was assumed when the device was battery-only.

---

## Step 3: Collect Evidence

### 3a. Choose the Correct Logging System

The project has TWO logging systems. Using the wrong one discards all relevant data.

| System | Format | API endpoint | Use for |
|--------|--------|-------------|---------|
| **DebugLogger** | Plain text | `/api/debug/logs/crash_backup` | Crashes, watchdog, boot sequence, TWDT |
| **Logger** | Structured JSON | `/api/logs` | Motion events, battery, app events |

**For crash/watchdog investigation: always use `/api/debug/logs/crash_backup`.**
The `DEBUG_LOG_SYSTEM()` macro writes to DebugLogger, NOT to Logger.
Downloading `/api/logs/crash_backup` for a watchdog crash gives you empty or irrelevant data.

### 3b. Collect in This Order

```bash
# 1. Crash backup log (primary — always download first)
curl http://$DEVICE_IP/api/debug/logs/crash_backup

# 2. Current boot session
curl http://$DEVICE_IP/api/debug/logs/current

# 3. Previous boot session
curl http://$DEVICE_IP/api/debug/logs/prev

# 4. Check for core dump
curl http://$DEVICE_IP/api/ota/coredump -o coredump.bin
# If non-empty, use /coredump skill to analyze

# 5. List all debug log files
curl http://$DEVICE_IP/api/debug/logs
```

Read from the device IP in `secrets.env`.

### 3c. Extract Key Facts from Logs

From the crash_backup log, identify:
- **Last log line before silence** — this is the crash point
- **Time gap** between last log and next boot — approximates crash duration
- **Boot number** — is this a fresh boot or RTC restore?
- **Reset reason** — TWDT / software reset / power-on / brownout?
- **Task that crashed** — from panic output (not always IDLE)
- **Build number** — confirm expected build was running

**Do NOT form hypotheses until you have read the actual logs.**

---

## Step 4: Hypothesis Tracking

### Format

Every hypothesis must have:
- **ID** (H1, H2, …)
- **Statement** — one sentence, falsifiable
- **Evidence required to prove** — specific log line, return code, or core dump finding
- **Evidence required to disprove** — what would definitively rule it out
- **Current status**: UNPROVEN / PROVEN / DISPROVEN / NOT APPLICABLE
- **IDF doc reference** — always link to docs before assuming IDF behavior

### Status Rules

| Status | Meaning |
|--------|---------|
| **UNPROVEN** | Plausible, no contradicting evidence, not yet tested |
| **PROVEN** | Specific confirming evidence observed in logs or behavior |
| **DISPROVEN** | Specific contradicting evidence observed — do not revisit |
| **NOT APPLICABLE** | Hypothesis is moot given framework/API version constraints |

### Key Rules

1. **Check ESP-IDF docs before writing any hypothesis about IDF behavior.**
   If the docs don't mention the behavior, say so explicitly. Do not assume.
   - Watchdogs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/wdts.html
   - Sleep: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/sleep_modes.html
   - Power Mgmt: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/power_management.html

2. **Never recycle a DISPROVEN hypothesis.** If H6 said "only loopTask and IDLE are
   subscribed" and that was disproven by a `deinit()` failure, do not assume it again in
   a later build without new evidence.

3. **One hypothesis per build** where possible. Testing multiple changes simultaneously
   makes it impossible to know which change (if any) was responsible.

4. **Document UNPROVEN hypotheses even when they stay unproven.** "Zero documentation
   support, inferred from crash timing" is a valid evidence note. It warns future sessions
   not to treat inference as fact.

### Template

```markdown
## Hypothesis Table

| ID | Hypothesis | Status | Evidence |
|----|-----------|--------|----------|
| H1 | [One sentence statement] | UNPROVEN | [What evidence would prove/disprove; current evidence] |
```

Keep this in `docs/ISSUE_NN_HYPOTHESES.md`. Update after every build.

---

## Step 5: Design a Targeted Test

For the highest-priority UNPROVEN hypothesis, design the **minimum change** that would
confirm or deny it.

Ask:
- What specific log output would prove this hypothesis?
- What specific log output would disprove it?
- Can we add instrumentation to see that output without changing the fix?
- Are we testing the hypothesis, or are we trying to fix it?

**Instrumentation before fix.** If you don't yet know the root cause, add logging and
force-flush it (`g_debugLogger.flush()` after each critical step) before writing any fix.
A crash during the diagnostic pass gives you data; a crash during a speculative fix gives
you nothing.

**Force-flush after every log in the critical path:**
```cpp
DEBUG_LOG_SYSTEM("Pre-sleep: about to do X");
g_debugLogger.flush();  // survive crash here
// ... do X ...
DEBUG_LOG_SYSTEM("Pre-sleep: X complete, result=%d", result);
g_debugLogger.flush();
```

---

## Step 6: Evaluate Results

After each test build:

1. Download `crash_backup` (not `current`) — it contains the pre-crash window.
2. Check the return code of the operation you instrumented. A non-zero return from
   `esp_task_wdt_deinit()` means something is still subscribed. Act on the return code.
3. Update the hypothesis table: PROVEN / DISPROVEN / still UNPROVEN?
4. If DISPROVEN: write why, and strike it from further consideration.
5. If PROVEN: design a root-cause fix targeting exactly what was proven.
6. If still UNPROVEN: was the test actually able to confirm or deny the hypothesis?
   If not, redesign the test before moving on.

### Distinguish Reactive from Root-Cause Fixes

| Approach | Example | When to use |
|----------|---------|-------------|
| **Reactive** | Call `disableCore0WDT()` every 2s to catch re-subscription | Only acceptable as a temporary mitigation while root cause is being found |
| **Root cause** | `esp_task_wdt_deinit()` before sleep so there is no subscription state to restore | Always the goal |

**Do not ship a reactive fix as the final answer.** Reactive fixes mask symptoms and
make the root cause harder to find. They also fail when the reaction interval exceeds
the failure window (as happened with the 2s poll vs 8s WDT timeout in Issue #44).

---

## Step 7: Update Documentation

After each build — whether it crashes or succeeds:

1. **Update `docs/ISSUE_NN_HYPOTHESES.md`** with test results.
2. **Post a GitHub issue comment** with:
   - Build number
   - What was changed and why
   - Log excerpts showing key evidence
   - Updated hypothesis status
   - What the next build will test
3. **Update `memory.md`** if a new hard-won fact was discovered (IDF API behavior,
   tooling path, logging system gotcha, etc.).

---

## Step 8: Know When to Stop Adding Instrumentation

Add instrumentation when you don't know where the crash is.
Stop adding instrumentation when you know exactly where the crash is.

Signs you have enough instrumentation:
- The crash_backup log shows the last step before the crash clearly
- You can pinpoint the exact function call that was executing
- You can see the return codes of the operations that preceded the crash

Signs you need more:
- The crash_backup log ends at a step many lines before the crash point
- Return codes of key operations are not being logged
- The "last log before silence" is a general "about to do X" with no per-step detail

---

## Skill Arguments

```
/troubleshoot
```
No arguments — interactive. Skill walks through Steps 0–8 for the current issue.

```
/troubleshoot <issue-number>
```
Start with a specific GitHub issue loaded. Reads existing hypotheses file if present.

---

## Checklist

Before forming any hypothesis:
- [ ] Validated user environment with AskUserQuestion
- [ ] Downloaded `crash_backup` from `/api/debug/logs/` (NOT `/api/logs/`)
- [ ] Read `MEMORY.md` for previously discovered facts
- [ ] Checked IDF documentation for any IDF behavior assumptions
- [ ] Read existing `ISSUE_NN_HYPOTHESES.md` if it exists

For each hypothesis:
- [ ] Falsifiable one-sentence statement
- [ ] IDF doc reference (or explicit "not documented")
- [ ] Specific evidence required to prove/disprove
- [ ] Status: UNPROVEN until evidence says otherwise

For each build:
- [ ] One hypothesis being tested
- [ ] Force-flush after every critical log statement
- [ ] Return codes of key operations logged
- [ ] `crash_backup` downloaded and read after crash
- [ ] Hypothesis table updated
- [ ] GitHub issue comment posted

---

## Common Pitfalls (from Issue #44)

| Pitfall | What happened | Rule |
|---------|--------------|------|
| Wrong log endpoint | Used `/api/logs/crash_backup` (JSON, empty) instead of `/api/debug/logs/crash_backup` (text, full) — wasted 3 crash analyses | Always use DebugLogger for crash diagnosis |
| Assumption hardcoded | Assumed only loopTask + IDLE subscribed to TWDT → `deinit()` failed because `esp_timer` was also subscribed (H6) | Enumerate dynamically; never hardcode subscriber lists |
| Reactive not root cause | 2s `disableCore0WDT()` poll couldn't win the race against 8s WDT timeout | Prove root cause; reactive workarounds only as interim |
| USB assumption | Diagnosed Serial.flush() hang as root cause — device was on battery, no USB host | Validate power source before forming hypothesis |
| Multiple changes at once | WiFi.mode() change AND disableCore0WDT() change in same build — couldn't isolate which helped | One hypothesis per build |
| Skipped IDF docs | Assumed `esp_task_wdt_reconfigure()` exists (it doesn't in IDF 4.4.x) | Check docs first; note API version constraints |

---

**Last Updated**: 2026-02-13
**For**: StepAware ESP32 Project
**Derived from**: Issue #44 investigation, builds 0219–0255
