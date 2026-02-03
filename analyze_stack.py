import subprocess, struct, re

ADDR2LINE = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-addr2line.exe"
ELF       = r".pio/build/esp32c3/firmware.elf"
CORE      = r"coredump.bin"

data = open(CORE, 'rb').read()

# ESP32-C3 memory map for flash-mapped code
CODE_RANGES = [
    (0x3C000000, 0x3C200000),   # Flash mapped to instruction cache
    (0x42000000, 0x42200000),   # Flash mapped code (XIP)
]

# Extract candidate code addresses
candidates = set()
for i in range(0, len(data) - 3, 4):
    val = struct.unpack_from('<I', data, i)[0]
    for lo, hi in CODE_RANGES:
        if lo <= val < hi:
            candidates.add(val)
            break

# Batch resolve via addr2line
addr_list = sorted(candidates)
input_str = '\n'.join(f'0x{a:08X}' for a in addr_list)

result = subprocess.run(
    [ADDR2LINE, '-e', ELF, '-f', '-i'],
    input=input_str, capture_output=True, text=True
)

# Parse output
lines = result.stdout.strip().split('\n')
resolved = {}
for idx in range(0, len(lines) - 1, 2):
    func = lines[idx]
    loc  = lines[idx + 1]
    addr = addr_list[idx // 2] if (idx // 2) < len(addr_list) else None
    if addr and func != '??':
        # Filter to only our source files
        if any(x in loc for x in ['src/', 'include/', '.cpp', '.h']):
            resolved[addr] = (func, loc)

# Print resolved addresses from our code
print("=== OUR CODE ADDRESSES IN CRASH STACK ===\n")
for addr in sorted(resolved.keys()):
    func, loc = resolved[addr]
    print(f"0x{addr:08X}  {func}")
    print(f"             {loc}")
    print()

# Look for PowerManager logging calls specifically
print("\n=== POWER MANAGER CALLS IN STACK ===\n")
for addr in sorted(resolved.keys()):
    func, loc = resolved[addr]
    if 'PowerManager' in func or 'power_manager.cpp' in loc:
        print(f"0x{addr:08X}  {func}")
        print(f"             {loc}")
        print()
