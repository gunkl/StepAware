# Issue #45: WiFi Fails to Reinitiate After Light Sleep Wake

**Status**: FIXED (build pending)
**GitHub**: https://github.com/gunkl/StepAware/issues/45

---

## Hypotheses

### H1: Low battery disables WiFi
**Status**: DISPROVEN

**Evidence**:
- Battery: 3.81V (61%) — stable throughout 21-cycle sleep session (prev log, boot cycle #215)
- No battery-based WiFi gate exists in code (searched `power_manager.cpp`, `wifi_manager.cpp`)
- Battery thresholds only affect `STATE_LOW_BATTERY` / `STATE_CRITICAL_BATTERY` power states,
  not WiFi enable/disable

---

### H2: `wifiEnabled=false` in config
**Status**: DISPROVEN

**Evidence**:
- Config API (`/api/config`) shows WiFi credentials configured
- Device connects to WiFi successfully at each boot (boot cycle #216: connected within 11.7s)
- WiFiManager `begin()` log shows `"WiFi: Credentials configured, connecting to MeowFi"` at every boot

---

### H3: Max reconnect attempts exhausted → STATE_FAILED
**Status**: DISPROVEN

**Evidence**:
- `maxReconnectAttempts = 0` (unlimited) set in `main.cpp:1301`
- `handleStateFailed()` only fires when `maxReconnectAttempts > 0` and exceeded
- No `STATE_FAILED` entries in any log

---

### H4: `arduino_events` task delay causes stale `WiFi.status()`
**Status**: **PROVEN** ← Primary root cause contributor

**Evidence**:
- Pre-sleep: `WiFi.disconnect(false)` called at `power_manager.cpp:947`
- This issues `esp_wifi_disconnect()` and queues `WIFI_EVENT_STA_DISCONNECTED` in the
  ESP-IDF event loop
- The `arduino_events` FreeRTOS task processes this event and updates the Arduino
  `WiFi.status()` variable (`_status`) to `WL_DISCONNECTED`
- Because `esp_light_sleep_start()` is called immediately after (no yield/delay),
  `arduino_events` may not process the event before sleep
- On wake, `WiFi.status()` returns `WL_CONNECTED` (stale) until `arduino_events` runs
- `handleStateConnected()` in `wifi_manager.cpp:332` polls `WiFi.status()` — only
  transitions to DISCONNECTED when the stale status finally updates
- **Observed delay**: 1 minute to 21+ minutes in prev log across 21 sleep cycles
- Timing correlates with `arduino_events` task scheduling, not with actual connection state

**Log evidence** (from prev log, boot cycle #215):
```
[08:07:49] Pre-sleep: WiFi disconnect
...
[08:29:10] WiFi: Connection lost  ← 21 minutes later!
[08:30:10] WiFi: Attempting reconnect (failure count: 1)
[08:30:10] WiFi: Connected!  ← 1 second to connect once triggered
```

---

### H5: No explicit post-wake WiFi reconnect
**Status**: **PROVEN** ← Primary root cause contributor

**Evidence**:
- `wakeUp()` at `power_manager.cpp:1153`:
  ```cpp
  void PowerManager::wakeUp(uint32_t sleepDurationMs) {
      m_stats.wakeCount++;
      m_lastActivity = millis();
      detectAndRouteWakeSource(sleepDurationMs);
      if (m_onWake) { m_onWake(); }  // m_onWake = nullptr (never registered)
  }
  ```
- `m_onWake` callback: setter exists at `power_manager.h:350` but is NEVER called
  in `main.cpp` — confirmed by codebase search
- Post-wake reconnect relies entirely on passive polling in `handleStateConnected()`
- WiFiManager `reconnect()` method exists and is public (`wifi_manager.cpp:168`) but
  never called on wake path

---

## Fix

Register `g_power.onWake()` callback in `main.cpp` to call `g_wifi.reconnect()` on
every wake. This fires from `wakeUp()` after TWDT re-init and GPIO cleanup.

**Change**: `src/main.cpp` — add after `g_power.setMotionWakePins()` call (~line 1033):

```cpp
    // Issue #45: Explicitly reconnect WiFi on wake.
    // Pre-sleep WiFi.disconnect(false) queues a disconnect event but the
    // arduino_events task may not process it before sleep, leaving WiFi.status()
    // stale (WL_CONNECTED) for 1-21+ min after wake. Calling reconnect() here
    // bypasses the passive polling entirely and ensures WiFi is up within seconds.
    g_power.onWake([]() {
        const char* wifiStateStr = WiFiManager::getStateName(g_wifi.getState());
        DEBUG_LOG_SYSTEM("Post-wake: WiFi state=%s, triggering reconnect", wifiStateStr);
        g_debugLogger.flush();
        g_wifi.reconnect();
    });
```

**Why safe**:
- `WiFiManager::reconnect()` resets `failureCount=0`, calls non-blocking `WiFi.begin()`
- If WiFi disabled in config: `connect()` returns `false` immediately
- Pre-sleep always breaks the connection, so reconnecting on every wake is always correct
- No impact on TWDT (reconnect fires after TWDT re-init at wake step 2)

---

## Verification

After fix, check `/api/debug/logs/current` after a sleep/wake cycle:
- `Post-wake: WiFi state=CONNECTED, triggering reconnect` — confirms stale state caught
- `WiFi: Connecting to MeowFi` — within 1-2 seconds of wake
- `WiFi: Connected! IP:` — within 5-10 seconds of wake
