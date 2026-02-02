import subprocess, struct, re

ADDR2LINE = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-addr2line.exe"
ELF       = r".pio\build\esp32c3\firmware.elf"
CORE      = r"coredump.bin"

data = open(CORE, 'rb').read()

# --- 1. Parse CORE segments (task stacks) ---
core_markers = []
pos = 0
while True:
    pos = data.find(b'CORE', pos)
    if pos == -1:
        break
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
    for i in range(extra_pos + 10, min(extra_pos + 64, len(data)) - 3, 4):
        candidate = struct.unpack_from('<I', data, i)[0]
        for marker_pos, sp in core_markers:
            if candidate == sp and sp != 0:
                crash_sp = sp
                break
        if crash_sp:
            break

# --- 3. Extract candidate code addresses ---
CODE_RANGES = [
    (0x3C000000, 0x3C100000),
    (0x40380000, 0x403E0000),
    (0x42000000, 0x42100000),
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
    if addr and func != '??':
        resolved[addr] = (func, loc)

# --- 5. Correlate back to task stacks ---
print("=== CORE DUMP STACK TRACE (addr2line fallback) ===\n")
for idx, (marker_pos, sp) in enumerate(core_markers):
    is_crash = (sp == crash_sp) if crash_sp else (idx == 0)
    tag = " <-- CRASH ORIGIN" if is_crash else ""
    print(f"--- Task #{idx} | SP: 0x{sp:08X}{tag} ---")

    seg_start = marker_pos
    seg_end = min(marker_pos + 0x200, len(data))
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
matches = []
for m in re.finditer(log_pattern, data):
    start = m.start()
    end = start
    while end < len(data) and 32 <= data[end] < 127:
        end += 1
    if end - start > 10:
        matches.append(data[start:end].decode('ascii', errors='replace'))

# Show last 20 log lines
for line in matches[-20:]:
    print(f"  {line}")

# --- 7. Identify task names ---
print("\n=== TASK NAMES FOUND IN HEAP ===\n")
known_tasks = [b'loopTask', b'async_tcp', b'sys_evt', b'arduino_events',
               b'mdns', b'esp_timer', b'wifi']
for name in known_tasks:
    if data.find(name) != -1:
        print(f"  {name.decode()}")
