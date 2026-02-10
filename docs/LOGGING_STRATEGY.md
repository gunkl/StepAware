# StepAware Logging Strategy

## Overview

StepAware uses LittleFS on a 896KB flash partition for persistent debug logging.
The logging system manages space automatically to prevent filesystem overflow while
preserving crash evidence.

---

## Log File Structure

| File | Purpose |
|------|---------|
| `/logs/boot_current.log` | Active running log for current boot |
| `/logs/boot_overflow.log` | Last 25% of current log saved during runtime rotation |
| `/logs/boot_prev.log` | Previous boot's complete log |
| `/logs/crash_backup.log` | Last ~30KB of log before a crash (32KB max, overwritten each crash) |
| `/logs/boot_info.txt` | Boot cycle counter |
| `/logs/rotation_debug.txt` | Pre-rotation filesystem diagnostics |

---

## Space Budget

- 32KB is always reserved for `crash_backup.log`
- The remaining space is shared between the current, overflow, and prev logs
- Runtime rotation triggers when `current + overflow >= 95%` of the available budget

---

## Runtime Log Rotation

Rotation is checked at every flush interval (every 20 writes or 5 seconds).

When the threshold is reached:

1. The last 25% of the current log is saved to `boot_overflow.log`
2. The current log is truncated
3. This frees approximately 75% of consumed space while preserving recent context
4. The watchdog is fed before slow filesystem operations to prevent a watchdog reset
   during rotation

---

## Boot Sequence

1. `preserveCrashLog()` runs **first** to save crash evidence before any log rotation
2. `rotateLogs()` renames `boot_current.log` to `boot_prev.log` and deletes any old
   overflow file
3. A new empty `boot_current.log` is opened for writing
4. Crash detection uses `esp_reset_reason()` and triggers on:
   - `ESP_RST_PANIC`
   - `ESP_RST_TASK_WDT`
   - `ESP_RST_INT_WDT`
   - `ESP_RST_WDT`
   - `ESP_RST_BROWNOUT`

---

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/debug/logs/current` | GET | Download active log |
| `/api/debug/logs/overflow` | GET | Download overflow log |
| `/api/debug/logs/prev` | GET | Download previous boot log |
| `/api/debug/logs/crash_backup` | GET | Download crash backup |
| `/api/debug/logs/rotation_debug` | GET | Download rotation diagnostics |
| `/api/debug/logs/clear` | POST | Delete all log files |

---

## Why This Design

The previous logging system had no runtime space enforcement:

- `writeToFile()` had zero space checking
- The filesystem filled to 99.6% during extended operation
- On reboot, space management deleted old logs, destroying crash evidence

The new design addresses both problems:

- **Prevents overflow**: Runtime rotation keeps the filesystem below 95% capacity
- **Preserves crash evidence**: `crash_backup.log` is written before rotation on
  every crash boot, so the last ~30KB of pre-crash log is always available
- **Dedicated crash file**: Crash evidence is stored separately and is never deleted
  by the normal rotation cycle (it is only overwritten by a subsequent crash)
