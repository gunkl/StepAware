# Python Virtual Environment Setup

This guide explains how to set up and use a Python virtual environment for StepAware development.

## Why Use a Virtual Environment?

- Isolates project dependencies from system Python
- Ensures consistent PlatformIO version across team
- Prevents conflicts with other Python projects
- Easy to recreate and share exact dependency versions

## Quick Setup

### Windows (PowerShell)

```powershell
# Run the setup script
.\setup_venv.ps1
```

### Windows (CMD)

```cmd
# Run the setup script
setup_venv.bat
```

### Manual Setup

If you prefer to set up manually:

```powershell
# Create virtual environment
python -m venv venv

# Activate it
.\venv\Scripts\Activate.ps1   # PowerShell
# OR
venv\Scripts\activate.bat       # CMD

# Install dependencies
pip install -r requirements.txt
```

## VS Code Integration

The project is configured to automatically use the virtual environment in VS Code:

1. **Automatic Activation**: VS Code will prompt to use `./venv` as the Python interpreter
2. **Terminal Integration**: New terminals will automatically activate the venv
3. **IntelliSense**: Python language features will use the venv packages

### Manual VS Code Setup

If VS Code doesn't auto-detect:

1. Press `Ctrl+Shift+P`
2. Type "Python: Select Interpreter"
3. Choose `./venv/Scripts/python.exe`

## Using the Virtual Environment

### Activate

```powershell
# PowerShell
.\venv\Scripts\Activate.ps1

# CMD
venv\Scripts\activate.bat

# Git Bash / WSL
source venv/Scripts/activate
```

You'll see `(venv)` in your prompt when activated.

### Deactivate

```bash
deactivate
```

## Running Tests with venv

Once the venv is activated:

```bash
# Run Python logic tests
python test/test_logic.py

# Run full test suite with reporting
python test/run_tests.py

# View test history
python test/run_tests.py --history
```

## Updating Dependencies

```bash
# Activate venv first
.\venv\Scripts\Activate.ps1

# Upgrade PlatformIO
pip install --upgrade platformio

# Update all dependencies
pip install --upgrade -r requirements.txt

# Save current versions (if needed)
pip freeze > requirements-lock.txt
```

## Troubleshooting

### "cannot be loaded because running scripts is disabled"

PowerShell execution policy needs to be set:

```powershell
# Run as Administrator
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### VS Code Not Using venv

1. Close all terminals in VS Code
2. Reload VS Code window (`Ctrl+Shift+P` → "Developer: Reload Window")
3. Open a new terminal - it should show `(venv)` in the prompt

### "Python not found"

Install Python 3.11 or later:
- Download from: https://www.python.org/downloads/
- During installation, check "Add Python to PATH"

## Docker vs Virtual Environment

You can use either approach:

| Approach | When to Use |
|----------|-------------|
| **Docker** | Full isolation, consistent across all platforms, includes GCC for native tests |
| **Virtual Environment** | Faster iteration, native Windows tools, simpler for Python-only work |

Both are configured and can be used interchangeably:

```bash
# Using venv (after activation)
pio test -e native

# Using Docker
docker compose run --rm stepaware-dev pio test -e native
```

## Files Created

- `requirements.txt` - Python package dependencies
- `setup_venv.ps1` - PowerShell setup script
- `setup_venv.bat` - CMD setup script
- `.vscode/settings.json` - VS Code Python configuration
- `venv/` - Virtual environment directory (not in git)

## Next Steps

After setup:

1. **Verify PlatformIO**: `pio --version`
2. **Run tests**: `python test/run_tests.py`
3. **Build firmware**: `pio run -e esp32-devkitlipo`

See [TEST_GUIDE.md](TEST_GUIDE.md) for testing documentation.
