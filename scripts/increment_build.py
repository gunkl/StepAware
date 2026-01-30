Import("env")
import os

# Auto-increment build number on every build.
# Reads from build_number.txt, increments, writes back, and generates build_number.h
# This runs immediately when the script is loaded, before any compilation starts.

build_file = "build_number.txt"
header_file = "include/build_number.h"

# Read current build number (default to 0 if file doesn't exist)
if os.path.exists(build_file):
    try:
        with open(build_file, 'r') as f:
            current = int(f.read().strip())
    except (ValueError, IOError):
        print("Warning: Could not read build_number.txt, starting from 0")
        current = 0
else:
    current = 0

# Increment by 1, roll over at 9999
next_build = (current + 1) if current < 9999 else 1

# Write back to build_number.txt (zero-padded to 4 digits)
try:
    with open(build_file, 'w') as f:
        f.write(f"{next_build:04d}")
except IOError as e:
    print(f"Error: Could not write to {build_file}: {e}")
    Import("sys")
    sys.exit(1)

# Generate include/build_number.h
header_content = f"""#ifndef BUILD_NUMBER_H
#define BUILD_NUMBER_H
#define BUILD_NUMBER "{next_build:04d}"
#endif
"""

try:
    with open(header_file, 'w') as f:
        f.write(header_content)
except IOError as e:
    print(f"Error: Could not write to {header_file}: {e}")
    Import("sys")
    sys.exit(1)

print(f"Build number: {next_build:04d}")
