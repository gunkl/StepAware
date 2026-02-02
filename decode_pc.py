import subprocess

ADDR2LINE = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-addr2line.exe"
ELF = r".pio\build\esp32c3\firmware.elf"

# Decode the crash PC
addresses = [
    "0x40058eb6",  # MEPC - crash PC
    "0x420cae78",  # From stack trace
]

for addr in addresses:
    result = subprocess.run(
        [ADDR2LINE, '-e', ELF, '-f', '-i', addr],
        capture_output=True, text=True
    )
    print(f"{addr}:")
    print(result.stdout)
