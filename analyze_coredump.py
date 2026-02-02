import subprocess, sys

PYTHON   = r"C:\Users\David\AppData\Local\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\python.exe"
GDB      = r"C:\Users\David\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-gdb.exe"
ELF      = r".pio\build\esp32c3\firmware.elf"
CORE     = r"coredump.bin"

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
    print("STDERR:", result.stderr, file=sys.stderr)
    sys.exit(result.returncode)
