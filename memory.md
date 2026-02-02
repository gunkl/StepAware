# StepAware — Tooling & Environment Notes

This file is the source of truth for build tooling, executable locations,
and environment quirks discovered during development.  Claude reads this
at the start of any session to avoid re-discovering known facts.

---

## Docker

All builds and tests run inside the project's Docker container:

```bash
docker-compose run --rm stepaware-dev <command>
```

The container image is defined in `docker-compose.yml`.  PlatformIO,
all toolchains, and package caches are pre-installed inside it.

---

## PlatformIO

| Command | What it does |
|---|---|
| `pio run -e esp32c3` | Build firmware for ESP32-C3 |
| `pio test -e native` | Run unit tests on host (no hardware needed) |
| `pio run -e esp32c3 --upload-port <port>` | Flash firmware |

Package cache (inside container): `/root/.platformio/packages/`

Key paths inside the container:
- Framework headers: `/root/.platformio/packages/framework-arduinoespressif32/tools/sdk/<chip>/include/`
- ESP32-C3 SDK: `.../sdk/esp32c3/`
- Toolchains: `toolchain-riscv32-esp` (RISC-V cross-compiler), `toolchain-esp32ulp` (legacy FSM ULP assembler — ESP32 original only, not C3)

---

## Python

Use `python3`, not `python`.  The container does not symlink a bare
`python` command.

---

## ESP32-C3 ULP RISC-V

**The ESP32-C3 hardware DOES have a RISC-V ULP coprocessor** (see TRM §29).
However, `framework-arduinoespressif32` (v3.0.0 / espressif32@6.5.0) does
**not** bundle ULP support for C3:

- `esp_ulp_riscv.h` — missing for C3 (only present under `sdk/esp32s2/` and `sdk/esp32s3/`)
- `libulp.a` — missing for C3
- `ulp_riscv.h` linker script — missing for C3

**Current workaround (Issue #27):**
- ULP code in `src/power_manager.cpp` is gated behind `#ifdef HAS_ULP_RISCV`
- Mode 2 (Deep Sleep) falls back to standard GPIO deep-sleep wakeup automatically
- The ULP source (`ulp/ulp_pir_monitor.c`) is kept in the repo, ready to activate

**To enable ULP when the framework adds C3 support**, uncomment the three
lines in `platformio.ini` marked `HAS_ULP_RISCV`.  Do not assume this is a
hardware limitation — it is a framework packaging gap.