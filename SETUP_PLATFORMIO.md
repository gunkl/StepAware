# PlatformIO Setup Guide

## Quick Fix - Add PlatformIO to PATH

PlatformIO is installed but not in your system PATH. Here are multiple ways to fix this:

---

## Option 1: Use in Current VSCode Project (Easiest)

**The `.vscode/settings.json` file has been created for you!**

1. **Close and reopen VSCode** (or reload the window: `Ctrl+Shift+P` → "Reload Window")
2. Open a new terminal in VSCode
3. Test: `pio --version`

✅ This should work immediately in new terminals!

---

## Option 2: Quick Session Fix (Bash Terminal)

In VSCode terminal (Git Bash), run:
```bash
source setup_pio_path.sh
```

Then test:
```bash
pio --version
```

---

## Option 3: PowerShell Session Fix

In VSCode terminal (PowerShell), run:
```powershell
.\setup_pio_path.ps1
```

Then test:
```powershell
pio --version
```

---

## Option 4: CMD Session Fix

In VSCode terminal (CMD), run:
```cmd
setup_pio_path.bat
```

Then test:
```cmd
pio --version
```

---

## Option 5: Permanent System-Wide Fix (Recommended)

### Windows 10/11:

1. Press `Win + R`, type: `sysdm.cpl` and press Enter
2. Click **Advanced** tab → **Environment Variables** button
3. Under "User variables", select **Path** → Click **Edit**
4. Click **New** and add:
   ```
   C:\Users\David\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\LocalCache\local-packages\Python310\Scripts
   ```
5. Click **OK** on all windows
6. **Restart VSCode** and all terminals
7. Test: `pio --version`

---

## Option 6: Use Python Module (Always Works)

If `pio` still doesn't work, you can always use:
```bash
python3 -m platformio --version
python3 -m platformio run
python3 -m platformio device monitor
```

---

## Verify Installation

After applying any fix above, verify PlatformIO is accessible:

```bash
# Check version
pio --version

# Should output: PlatformIO Core, version 6.1.18

# Build the project
pio run

# Upload to board
pio run --target upload

# Open serial monitor
pio device monitor
```

---

## Common PlatformIO Commands

```bash
# Build project
pio run

# Build specific environment
pio run -e esp32-devkitlipo

# Upload firmware
pio run --target upload

# Clean build files
pio run --target clean

# Serial monitor
pio device monitor

# List connected devices
pio device list

# Run tests
pio test

# Update PlatformIO
pio upgrade

# Install library
pio lib install <library-name>

# Project info
pio project config
```

---

## Troubleshooting

### "pio: command not found"
- Make sure you've applied one of the fixes above
- Restart your terminal or VSCode
- Use `python3 -m platformio` as a fallback

### "Permission denied"
- On PowerShell, you may need to run: `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser`

### VSCode doesn't pick up the PATH
- Make sure `.vscode/settings.json` exists in your project
- Reload the VSCode window: `Ctrl+Shift+P` → "Developer: Reload Window"
- Close and reopen VSCode completely

---

## Current Status

✅ PlatformIO is installed: `6.1.18`
✅ VSCode settings configured: `.vscode/settings.json`
✅ Helper scripts created:
- `setup_pio_path.sh` (Bash)
- `setup_pio_path.bat` (CMD)
- `setup_pio_path.ps1` (PowerShell)

---

**Recommendation**: Use Option 1 (VSCode settings) for this project, or Option 5 for system-wide access.
