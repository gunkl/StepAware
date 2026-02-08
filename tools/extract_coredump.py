#!/usr/bin/env python3
"""
Extract ESP32-C3 core dump from flash partition via esptool.py

This script extracts the core dump directly from flash when the HTTP endpoint
fails with "Insufficient memory" error. No firmware changes required.

Usage:
    python tools/extract_coredump.py [--port COM3]
"""

import subprocess
import sys
import os
from pathlib import Path

# Project constants
CHIP = "esp32c3"
COREDUMP_OFFSET = 0x310000
COREDUMP_SIZE = 0x10000
OUTPUT_FILE = "coredump.bin"

# Windows Python 3.10 with esptool installed
PYTHON_PATH = r"C:\Users\David\AppData\Local\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\python.exe"

def auto_detect_port():
    """Auto-detect ESP32 COM port (Windows only)"""
    try:
        result = subprocess.run(
            [PYTHON_PATH, "-m", "serial.tools.list_ports"],
            capture_output=True,
            text=True,
            check=True
        )
        # Look for ESP32 USB-Serial-JTAG port
        for line in result.stdout.split('\n'):
            if 'USB' in line and 'Serial' in line:
                port = line.split()[0]
                return port
    except:
        pass
    return None

def extract_coredump(port=None):
    """Extract core dump from flash partition"""

    if port is None:
        port = auto_detect_port()
        if port:
            print(f"Auto-detected port: {port}")
        else:
            print("ERROR: Could not auto-detect COM port")
            print("Please specify port manually: python extract_coredump.py --port COM3")
            return False

    print(f"\nExtracting core dump from ESP32-C3...")
    print(f"  Chip: {CHIP}")
    print(f"  Port: {port}")
    print(f"  Offset: 0x{COREDUMP_OFFSET:06X}")
    print(f"  Size: 0x{COREDUMP_SIZE:05X} ({COREDUMP_SIZE} bytes)")
    print(f"  Output: {OUTPUT_FILE}")

    cmd = [
        PYTHON_PATH, "-m", "esptool",
        "--chip", CHIP,
        "--port", port,
        "read_flash",
        f"0x{COREDUMP_OFFSET:X}",
        f"0x{COREDUMP_SIZE:X}",
        OUTPUT_FILE
    ]

    print(f"\nRunning: {' '.join(cmd)}\n")

    result = subprocess.run(cmd)

    if result.returncode == 0:
        file_size = Path(OUTPUT_FILE).stat().st_size
        print(f"\n✓ Core dump extracted successfully ({file_size} bytes)")
        print(f"\nNext step: Analyze with `/coredump` skill or:")
        print(f"  python -m esp_coredump info_corefile --chip {CHIP} --core {OUTPUT_FILE} .pio/build/esp32c3/firmware.elf")
        return True
    else:
        print(f"\n✗ Extraction failed (exit code {result.returncode})")
        return False

if __name__ == "__main__":
    port = None
    if len(sys.argv) > 2 and sys.argv[1] == "--port":
        port = sys.argv[2]

    success = extract_coredump(port)
    sys.exit(0 if success else 1)
