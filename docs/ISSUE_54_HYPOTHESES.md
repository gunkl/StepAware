# Issue #54 — Device Freeze After 1+ Week, No Watchdog Reset

**Firmware**: 0.6.1 (build 0270)
**Date**: 2026-03-07
**GitHub**: https://github.com/gunkl/StepAware/issues/54

## Problem Statement

Device running on battery in MOTION_DETECT mode froze completely after 1+ week
of normal operation — LED matrix stuck displaying a down arrow, short button
presses ignored, no watchdog reset occurred, power cycle required.

## Environment (validated with user)

- Power source: Battery only (no USB)
- Mode: MOTION_DETECT
- First occurrence
- Recovery: Manual power cycle

## Evidence Summary

- **No crash backup** — TWDT never fired during the freeze
- **Boot #230 (hung session)**: LED matrix I2C init failed (`Failed to initialize HT16K33 at address 0x70`), log truncated to first ~3 min
- **Boot #231 (after power cycle)**: LED matrix works, normal operation
- **Core dump**: From old Boot #213 (Feb 12) — not relevant
- **Down arrow on display**: Retained by HT16K33 from previous boot (chip keeps RAM while powered)

## Hypothesis Table

| ID | Hypothesis | Status | Evidence Required | Evidence Found | IDF Doc Reference |
|----|-----------|--------|-------------------|----------------|-------------------|
| H1 | TWDT reinit after light sleep wake silently fails, leaving device permanently without watchdog protection | UNPROVEN | Log showing `Wake step 2/7: TWDT reinit err=N` where N!=0; or reproduction showing reinit failure after many sleep/wake cycles | Boot #230 logs truncated; cannot see wake cycle logs from the freeze period | [TWDT docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/wdts.html) — `esp_task_wdt_init()` returns ESP_ERR_NO_MEM or ESP_ERR_INVALID_STATE if already initialized. Old API: `esp_task_wdt_init(timeout, panic)` |
| H2 | I2C bus lockup causes main loop to block indefinitely on Wire operations during LED matrix update | NOT APPLICABLE | N/A | Boot #230 shows LED matrix failed init -> `m_initialized=false` -> `update()` returns immediately at line 227. Matrix was never used in the hung session. | N/A |
| H3 | WiFi reconnect or NTP sync blocks the main loop indefinitely after prolonged disconnection | UNPROVEN | Logs from the actual freeze moment (unavailable — truncated); or WiFi/NTP timing instrumentation showing >8s blocking | No logs from freeze moment available | [WiFi docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/network/esp_wifi.html) — not documented whether reconnect can block indefinitely in Arduino framework |
| H4 | Heap exhaustion after week-long operation causes allocation failure leading to null pointer dereference or infinite retry loop | UNPROVEN | Heap monitoring over multi-day period showing trend toward zero | Boot #231 shows 35KB free after short uptime; no multi-day data | N/A |
| H5 | `esp_task_wdt_deinit()` failure before sleep leaves TWDT in inconsistent state, causing subsequent `esp_task_wdt_init()` to fail or produce no-op TWDT | UNPROVEN | Confirm deinit failure leads to broken reinit on next wake | Boot #213 crash backup shows `"TWDT deinit FAILED err=259"` — proves deinit CAN fail (ESP_ERR_INVALID_STATE = tasks still subscribed) | [TWDT docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/wdts.html) — `esp_task_wdt_deinit()` returns ESP_ERR_INVALID_STATE (0x103=259) if tasks still subscribed |
| H6 | WebSocket connection leak or async_tcp memory leak gradually exhausts heap/sockets over multi-day operation | UNPROVEN | Periodic heap logging over multi-day period; WebSocket connection count tracking | No data available yet | N/A |

## Priority Ranking

H1 > H5 > H3 > H4 > H6 > ~~H2~~

## Build Log

| Build | Date | Changes | Result | Hypotheses Updated |
|-------|------|---------|--------|-------------------|
| 0270 | 2026-02-23 | Baseline (0.6.1) | Froze after 1+ week | All hypotheses formed |
| 0271 | TBD | TWDT hardening + I2C retry + instrumentation | TBD | TBD |
