# `/build` — Build StepAware Firmware

Build the ESP32-C3 firmware using Docker. Automatically fixes common environment issues.

---

## Usage

```
/build              # Build esp32c3 environment (default)
/build native       # Build native test environment
/build all          # Build both environments
```

---

## Build Procedure

### Step 1: Run the Docker build

```bash
docker-compose -f "c:\Users\David\Documents\VSCode Projects\ESP32\StepAware\docker-compose.yml" run --rm stepaware-dev bash -c "pip install --quiet intelhex 2>/dev/null; pio run -e esp32c3" 2>&1
```

For native tests:
```bash
docker-compose -f "c:\Users\David\Documents\VSCode Projects\ESP32\StepAware\docker-compose.yml" run --rm stepaware-dev pio run -e native 2>&1
```

**Note:** `pip install intelhex` is a known workaround — the Docker image's PlatformIO
toolchain (`tool-esptoolpy >= 4.9`) requires it but the Dockerfile doesn't install it.
The `--quiet` flag and `2>/dev/null` keep the output clean when it's already installed.

### Step 2: Check the result

- **SUCCESS** — Report the build number (shown as `Build number: NNNN` in output),
  flash usage (RAM/Flash percentages), and any new warnings.
- **FAILED** — Continue to Step 3.

### Step 3: Fix build failures

Analyze the error output and fix the issue. Common failure categories:

| Failure | Cause | Fix |
|---------|-------|-----|
| `ModuleNotFoundError: No module named 'X'` | Missing Python dependency in Docker | Add `pip install X` before the `pio run` command |
| Compilation error in `src/*.cpp` | Code bug | Fix the source file, then re-run build |
| Linker error (undefined reference) | Missing implementation or lib | Check `lib_deps` in `platformio.ini`, add missing dependency |
| `UnknownEnvNamesError` | Wrong environment name | Use `esp32c3` (not `esp32-devkitlipo`). Valid envs: `esp32c3`, `native` |
| Platform/package download failure | Network or cache issue | Retry the build; if persistent, try `pio platform install espressif32@6.12.0` first |

After fixing, re-run the build command from Step 1. Repeat until SUCCESS.

### Step 4: Report results

Report to user:
- Build number
- RAM/Flash usage
- Any new warnings (ignore pre-existing `adc_ll.h` warnings — those are SDK-level)
- If fixes were applied, summarize what was changed

---

## Environment Reference

| Item | Value |
|------|-------|
| Platform | `espressif32@6.12.0` |
| Board | `esp32-c3-devkitm-1` |
| Framework | Arduino |
| Build environments | `esp32c3` (firmware), `native` (unit tests) |
| Build number file | `build_number.txt` (auto-incremented by `scripts/increment_build.py`) |
| Docker service | `stepaware-dev` |
| Docker compose file | `docker-compose.yml` |

## Known Pre-existing Warnings (ignore these)

- `adc_ll.h` invalid conversion warnings (SDK-level, `-fpermissive` flag handles them)
- `cc1: warning: command line option '-fpermissive' is valid for C++/ObjC++` on `.c` files
- `version` attribute obsolete warning from docker-compose

---

**Last Updated**: 2026-02-14