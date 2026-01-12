# Quick Setup Guide

Choose your development approach:

## Option 1: Python Virtual Environment (Recommended for Windows)

**Run this in CMD (not PowerShell):**
```cmd
setup_venv.bat
```

This will:
- Create a Python virtual environment
- Install PlatformIO and dependencies
- Activate the environment

**Then verify:**
```cmd
pio --version
```

See [PYTHON_SETUP.md](PYTHON_SETUP.md) for full details.

## Option 2: Docker (Cross-platform)

**For isolated, consistent environment:**
```bash
docker compose run --rm stepaware-dev bash
```

See [DOCKER_GUIDE.md](DOCKER_GUIDE.md) for full details.

## VS Code Setup

1. Open this folder in VS Code
2. When prompted, select `./venv/Scripts/python.exe` as Python interpreter
3. New terminals will automatically activate the venv using CMD

## Why CMD instead of PowerShell?

PowerShell has script execution policies that prevent running `.ps1` files by default.
Using CMD (`.bat` files) works without changing any system security settings.

## Next Steps

- Build firmware: `pio run -e esp32-devkitlipo`
- Run tests: `python test/run_tests.py`
- See [TEST_GUIDE.md](TEST_GUIDE.md) for testing documentation
