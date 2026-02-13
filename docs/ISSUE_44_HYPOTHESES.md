# Issue #44 — IDLE Task Watchdog Crash Hypotheses

Tracking document for hypotheses about the TWDT crash root cause during light
sleep on ESP32-C3. Each hypothesis is tagged with its evidence status and
relevant ESP-IDF documentation.

## ESP-IDF Documentation References

Always consult these before making assumptions about IDF behavior:

- **API Reference Index**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/index.html
- **System API Reference**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/index.html
- **Watchdog Timers**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/wdts.html
- **Power Management**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/power_management.html
- **Sleep Modes**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/sleep_modes.html
- **ESP Timer**: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/esp_timer.html

## Hypothesis Table

| ID | Hypothesis | Status | Evidence |
|----|-----------|--------|----------|
| H1 | `setCpuFrequencyMhz()` APB callbacks re-subscribe IDLE to TWDT | **UNPROVEN** | Zero documentation support. PM docs mention APB freq changes but say nothing about TWDT interaction. Based on inference from crash timing only. Build 0247 removes setCPUFrequency calls; if crashes stop AND loop() instrumentation never detects re-subscription, H1 was likely correct. |
| H2 | FreeRTOS tickless-idle / pm module re-subscribes IDLE to TWDT | **UNPROVEN** | Zero documentation support. PM docs describe tickless idle but never mention TWDT. Based on inference that IDLE gets re-subscribed between boot disableCore0WDT() and first sleep. Build 0247 loop() instrumentation will log if IDLE is ever found re-subscribed. |
| H3 | `esp_task_wdt_reconfigure()` with `idle_core_mask=0` unsubscribes already-subscribed IDLE tasks | **NOT APPLICABLE** | This framework version (arduino-esp32 3.20017, ESP-IDF 4.4.x) does NOT have `esp_task_wdt_config_t` or `idle_core_mask`. The old API is `esp_task_wdt_init(uint32_t timeout, bool panic)` — no config struct. `esp_task_wdt_reconfigure()` does not exist. The newer API with `idle_core_mask` requires ESP-IDF 5.2+. Build 0247 uses `esp_task_wdt_deinit()` + `esp_task_wdt_init(8000, true)` + `disableCore0WDT()` instead. |
| H4 | MWDT hardware timer (backing the TWDT) continues running during light sleep | **LIKELY TRUE** | Not documented. But crash #15 core dump shows `task_wdt_isr` fired while loopTask was inside `esp_light_sleep_start()`. Strong empirical evidence. |
| H5 | CPU frequency is preserved across light sleep (resumes at same freq) | **UNPROVEN** | Not documented for ESP32-C3. Sleep docs say "internal states are preserved" but don't mention CPU PLL/frequency specifically. |
| H6 | Only loopTask and IDLE are subscribed to the TWDT (no WiFi/system tasks) | **DISPROVEN** | Build 0254: `esp_task_wdt_deinit()` returned ESP_ERR_INVALID_STATE (259) after removing loopTask + IDLE. Core dump confirmed `esp_timer` task (priority 22) was the crashed task — subscribed to TWDT and unable to feed during light sleep. Build 0255 enumerates ALL tasks dynamically. |
| H7 | WiFi.disconnect(false) is insufficient; esp_wifi_stop() is the correct pre-sleep call | **PARTIALLY DOC-CONFIRMED** | Sleep docs: "the application must disable Wi-Fi and Bluetooth using the appropriate calls." Does not specify which call. WiFi.disconnect() vs esp_wifi_stop() distinction not documented for sleep context. Separate investigation from Issue #44. |

## Documented Facts (used as foundation)

| Fact | Source |
|------|--------|
| `esp_task_wdt_deinit()` unsubscribes idle tasks | Watchdog docs: "This function will deinitialize the TWDT, and unsubscribe any idle tasks." |
| `esp_task_wdt_deinit()` fails if non-IDLE tasks still subscribed | Watchdog docs: "Calling this function whilst other tasks are still subscribed...will result in an error code." |
| `esp_task_wdt_init()` with `idle_core_mask=0` does not subscribe IDLE | Watchdog docs (ESP-IDF 5.2+): "idle_core_mask: Bitmask of the core whose idle task should be subscribed on initialization." **NOT AVAILABLE in this framework version** — old API `esp_task_wdt_init(timeout, panic)` auto-subscribes IDLE based on Kconfig. |
| `esp_task_wdt_status()` queries subscription | Returns ESP_OK (subscribed) or ESP_ERR_NOT_FOUND (not subscribed). |
| esp_timer is suspended during light sleep | ESP Timer docs: counter pauses; advances by sleep duration on wake. |
| Light sleep clock-gates digital peripherals | Sleep docs: "digital peripherals, most of the RAM, and CPUs are clock-gated." |

## Build 0247 Instrumentation

Build 0247 includes instrumentation to validate these hypotheses:

1. **Boot**: `esp_task_wdt_status()` on IDLE after `disableCore0WDT()` — confirms removal worked
2. **Loop**: Every 10 seconds, checks if IDLE is re-subscribed. If detected, logs `Issue #44: IDLE re-subscribed to TWDT! Removing. (H1/H2 confirmed)`
3. **Pre-sleep**: `esp_task_wdt_deinit()` return code logged — failure means H6 is wrong (unknown subscriber)
4. **Post-wake**: `esp_task_wdt_init()` return code logged — failure means TWDT wasn't fully torn down

## Test Results

*(Update this section after each test pass)*

### Build 0254 (implementing build 0247 plan) — First Pass
- Date: 2026-02-12
- Boot IDLE status: 261 (ESP_ERR_NOT_FOUND) — correctly removed at boot
- Loop re-subscription detected: None (H1/H2 not confirmed this pass)
- Pre-sleep deinit result: FAILED err=259 (ESP_ERR_INVALID_STATE — tasks still subscribed)
- Post-wake init result: N/A (crashed during sleep)
- Crash: YES — `esp_timer` task WDT timeout during light sleep
- Root cause: `esp_timer` subscribed to TWDT, not removed pre-sleep (H6 disproven)
- Fix: Build 0255 enumerates ALL FreeRTOS tasks and removes all TWDT subscribers before deinit
