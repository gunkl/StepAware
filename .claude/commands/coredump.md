# `/coredump` — Download and Analyze ESP32 Core Dump

Downloads a core dump from a StepAware device, decodes it to a full stack trace using the firmware symbol table, and reports the crash analysis.

Supports an optional `clear` argument: if the user invoked this as `/coredump clear`, also delete the core dump from the device after analysis.

---

## Step 1: Confirm device-specific inputs

Before downloading anything, ask the user to confirm or override these two values. Show the defaults so a simple confirm is enough for the common case:

- **Device IP** — default: `10.123.0.98`
- **Firmware ELF** — default: `.pio/build/esp32c3/firmware.elf` (relative to project root)

These are the only inputs that change between sessions. Everything else (chip type, partition offset, toolchain paths) is constant for this project.

---

## Step 2: Download the core dump

```bash
curl -s -o coredump.bin -w "%{http_code} %{size_download}" http://<DEVICE_IP>/api/ota/coredump --connect-timeout 10
```

- HTTP 200 + non-zero size = success. Continue.
- HTTP 404 = no crash recorded on device. Stop and tell the user.
- Any other status or connection failure = report error and stop.

Save as `coredump.bin` in the project root.

---

## Step 3: Gold path — `esp_coredump info_corefile`

This produces the best output (full GDB-decoded stack traces) but **only works when the firmware ELF SHA matches the one that was running at crash time**. A recompile after the crash will break the SHA. Try it first; if it fails with a SHA mismatch, proceed to Step 4 instead.

**CRITICAL — path gotchas learned the hard way:**

- Do NOT use msys `python3`. The `esp_coredump` and `esptool` packages are installed in the Windows Python 3.10 environment. Use the full path:
  `C:\Users\David\AppData\Local\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\python.exe`
- Windows paths with spaces break shell variable expansion in msys bash. Invoke all toolchain commands via Python `subprocess` instead of bash directly.
- The `esp_coredump` subcommand is `info_corefile` (not `info`). The positional argument is the ELF path; the dump file goes in `--core`.

```python
import subprocess, sys

PYTHON   = r"C:\Users\David\AppData\Local\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\python.exe"
GDB      = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-gdb.exe"
ELF      = "<USER_CONFIRMED_ELF_PATH>"          # from Step 1
CORE     = r"<PROJECT_ROOT>\coredump.bin"       # from Step 2

result = subprocess.run([
    PYTHON, "-m", "esp_coredump",
    "--chip", "esp32c3",
    "info_corefile",
    "--gdb", GDB,
    "--core", CORE,
    "--core-format", "raw",
    "--off", "0x310000",
    ELF
], capture_output=True, text=True)

print(result.stdout)
if result.returncode != 0:
    print("STDERR:", result.stderr)
```

If this succeeds (exit code 0), print the full output and skip to Step 5 (credential scan). If it fails with a SHA mismatch error, continue to Step 4.

---

## Step 4: Fallback path — addr2line batch decode

When the SHA doesn't match (any recompile changes it), decode the stack traces manually. The symbols are still valid as long as the source hasn't changed — only the build timestamp in the binary differs.

Run this as a single Python script (again, via the Windows Python path to avoid msys path issues):

```python
import subprocess, struct, re

ADDR2LINE = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-addr2line.exe"
ELF       = "<USER_CONFIRMED_ELF_PATH>"
CORE      = r"<PROJECT_ROOT>\coredump.bin"

data = open(CORE, 'rb').read()

# --- 1. Parse CORE segments (task stacks) ---
# Each task stack is marked by the ASCII string "CORE" followed by register context.
# The SP (stack pointer) is at offset +0x20 from each CORE marker (little-endian u32).
# The EXTRA_INFO segment (marked "CORE_DUMP_INFO") contains the crash-origin task SP
# as its first dword after the null-terminated label.

core_markers = []
pos = 0
while True:
    pos = data.find(b'CORE', pos)
    if pos == -1:
        break
    # Skip CORE_DUMP_INFO — that's metadata, not a task stack
    if data[pos:pos+14] == b'CORE_DUMP_INFO':
        pos += 1
        continue
    sp = struct.unpack_from('<I', data, pos + 0x20)[0] if pos + 0x24 <= len(data) else 0
    core_markers.append((pos, sp))
    pos += 1

# --- 2. Find crash-origin task from EXTRA_INFO ---
extra_pos = data.find(b'EXTRA_INFO')
crash_sp = None
if extra_pos != -1:
    # First dword after the label + null padding (label is 10 bytes + 2 null = offset 12)
    # But EXTRA_INFO is preceded by its own header. The crash SP is the first u32 in EXTRA_INFO data.
    # Search for the dword that matches one of our task SPs.
    for i in range(extra_pos + 10, min(extra_pos + 64, len(data)) - 3, 4):
        candidate = struct.unpack_from('<I', data, i)[0]
        for marker_pos, sp in core_markers:
            if candidate == sp and sp != 0:
                crash_sp = sp
                break
        if crash_sp:
            break

# --- 3. Extract candidate code addresses ---
# ESP32-C3 memory map for flash-mapped code (where symbols resolve):
CODE_RANGES = [
    (0x3C000000, 0x3C100000),   # Instruction cache mapped flash
]

candidates = set()
for i in range(0, len(data) - 3, 4):
    val = struct.unpack_from('<I', data, i)[0]
    for lo, hi in CODE_RANGES:
        if lo <= val < hi:
            candidates.add(val)
            break

# --- 4. Batch resolve via addr2line ---
addr_list = sorted(candidates)
input_str = '\n'.join(f'0x{a:08X}' for a in addr_list)

result = subprocess.run(
    [ADDR2LINE, '-e', ELF, '-f', '-i'],
    input=input_str, capture_output=True, text=True
)

# Parse output: pairs of (function_name, file:line)
lines = result.stdout.strip().split('\n')
resolved = {}
for idx in range(0, len(lines) - 1, 2):
    func = lines[idx]
    loc  = lines[idx + 1]
    addr = addr_list[idx // 2] if (idx // 2) < len(addr_list) else None
    if addr and func != '??' :
        resolved[addr] = (func, loc)

# --- 5. Correlate back to task stacks ---
# For each CORE segment, show which resolved addresses fall within its stack region.
# The region after each CORE marker (roughly 0x80–0x100 bytes) contains that task's
# saved registers and top-of-stack frames.

print("=== CORE DUMP STACK TRACE (addr2line fallback) ===\n")
for idx, (marker_pos, sp) in enumerate(core_markers):
    is_crash = (sp == crash_sp) if crash_sp else (idx == 0)
    tag = " <-- CRASH ORIGIN" if is_crash else ""
    print(f"--- Task #{idx} | SP: 0x{sp:08X}{tag} ---")

    # Scan the segment data for resolved addresses
    seg_start = marker_pos
    seg_end = min(marker_pos + 0x100, len(data))
    for i in range(seg_start, seg_end - 3, 4):
        val = struct.unpack_from('<I', data, i)[0]
        if val in resolved:
            func, loc = resolved[val]
            print(f"  0x{val:08X}  {func}")
            print(f"              {loc}")
    print()

# --- 6. Extract embedded log messages ---
print("=== LAST LOG MESSAGES BEFORE CRASH ===\n")
log_pattern = rb'\[\d{2}-\d{2} \d{2}:\d{2}:\d{2}\]'
for m in re.finditer(log_pattern, data):
    start = m.start()
    end = start
    while end < len(data) and 32 <= data[end] < 127:
        end += 1
    if end - start > 10:
        print(f"  {data[start:end].decode('ascii', errors='replace')}")

# --- 7. Identify task names ---
print("\n=== TASK NAMES FOUND IN HEAP ===\n")
known_tasks = [b'loopTask', b'async_tcp', b'sys_evt', b'arduino_events',
               b'mdns', b'esp_timer', b'wifi']
for name in known_tasks:
    if data.find(name) != -1:
        print(f"  {name.decode()}")
```

Print all output. Continue to Step 5.

---

## Step 5: Credential scan (always run)

Scan the raw dump for known sensitive string patterns. If found, **warn but do NOT print the values**. Report only that they exist and at what byte offsets.

Patterns to scan for:
- Any string adjacent to or near the `wifi` task name or config keys like `ssid`, `password`
- Strings matching common credential patterns (alphanumeric sequences of 6+ chars near WiFi-related keys)

Output format (redacted):
```
[SECURITY] WiFi credentials found in core dump (DRAM snapshot).
  - SSID at offset 0x13C9 [REDACTED]
  - Password at offset 0x1409 [REDACTED]
  Do not share this core dump file publicly.
```

---

## Step 6 (conditional): Clear the core dump

Only if the user invoked `/coredump clear`: issue the DELETE request to wipe the crash data from the device's coredump partition.

```bash
curl -s -X DELETE http://<DEVICE_IP>/api/ota/coredump
```

Report the response.

---

## Project constants (do not prompt for these)

| Value | Source |
|-------|--------|
| Chip | `esp32c3` | platformio.ini |
| Coredump partition offset | `0x310000` | partitions.csv line 14 |
| Windows Python 3.10 | `C:\Users\David\AppData\Local\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\python.exe` | Only env with esp_coredump installed |
| RISC-V GDB | `C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-gdb.exe` | PlatformIO toolchain |
| RISC-V addr2line | `C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-addr2line.exe` | PlatformIO toolchain |
| API endpoint | `GET /api/ota/coredump` (download), `DELETE /api/ota/coredump` (clear) | src/web_api.cpp |
