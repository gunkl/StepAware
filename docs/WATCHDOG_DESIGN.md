# StepAware Watchdog System Design

## Overview

The Watchdog System monitors all critical modules during runtime to ensure system health and recover from failures automatically. This is especially critical for hardware deployment where the system must remain operational 24/7.

## Design Goals

1. **Reliability**: Detect and recover from module failures automatically
2. **Visibility**: Log all health checks and failures for debugging
3. **Graceful Degradation**: Continue operating with reduced functionality when possible
4. **Recovery**: Automatic restart of failed modules
5. **Safety**: Hardware watchdog timer prevents complete system lockup
6. **Minimal Overhead**: < 1% CPU usage for health monitoring

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Main Loop (setup/loop)                    │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                   Watchdog Manager                           │
│  - Monitors all modules                                      │
│  - Feeds hardware watchdog                                   │
│  - Triggers recovery actions                                 │
│  - Reports system health                                     │
└───────┬─────────────────────────────────────────────────────┘
        │
        ├────► Hardware Watchdog Timer (ESP32 WDT)
        │      - 8 second timeout (configurable)
        │      - System resets if not fed
        │
        └────► Module Health Checks
               │
               ├─► StateMachine Health
               │   - Update() called regularly
               │   - State transitions valid
               │   - No infinite loops
               │
               ├─► ConfigManager Health
               │   - Config valid
               │   - SPIFFS accessible
               │   - Load/save working
               │
               ├─► Logger Health
               │   - Buffer not full
               │   - Writes successful
               │   - No memory leaks
               │
               ├─► HAL Health
               │   - Button responsive
               │   - LED controllable
               │   - PIR sensor readings
               │
               ├─► WebServer Health
               │   - Server running
               │   - Responding to requests
               │   - No hung connections
               │
               └─► Memory Health
                   - Heap available
                   - Stack not overflowing
                   - No memory fragmentation
```

## Component Design

### 1. Hardware Watchdog Timer

**ESP32 Built-in WDT**
```cpp
// Timeout: 8 seconds (configurable)
// Action: System reset if not fed
// Feed interval: Every main loop iteration (target: < 1 second)
```

**Rationale**:
- Catches complete system lockups (infinite loops, deadlocks)
- Independent of software failures
- Last line of defense

### 2. Watchdog Manager

**Responsibilities**:
- Monitor all registered modules
- Feed hardware watchdog when healthy
- Trigger recovery actions on failures
- Log health status

**Interface**:
```cpp
class WatchdogManager {
public:
    enum ModuleID {
        MODULE_STATE_MACHINE,
        MODULE_CONFIG_MANAGER,
        MODULE_LOGGER,
        MODULE_HAL_BUTTON,
        MODULE_HAL_LED,
        MODULE_HAL_PIR,
        MODULE_WEB_SERVER,
        MODULE_MEMORY,
        MODULE_COUNT
    };

    enum HealthStatus {
        HEALTH_OK,
        HEALTH_WARNING,
        HEALTH_CRITICAL,
        HEALTH_FAILED
    };

    struct ModuleHealth {
        ModuleID id;
        HealthStatus status;
        uint32_t lastCheckTime;
        uint32_t failureCount;
        const char* message;
    };

    bool begin(uint32_t checkIntervalMs = 1000);
    void update();

    // Module registration
    void registerModule(ModuleID id, HealthCheckFunc checkFunc);

    // Manual health reporting
    void reportHealth(ModuleID id, HealthStatus status, const char* message = nullptr);

    // Query system health
    HealthStatus getSystemHealth() const;
    HealthStatus getModuleHealth(ModuleID id) const;
    bool isHealthy() const;

    // Recovery actions
    void triggerRecovery(ModuleID id);

private:
    void feedHardwareWatchdog();
    void checkModuleHealth();
    void handleFailure(ModuleID id);
};
```

### 3. Module Health Checks

Each module implements a health check function:

```cpp
WatchdogManager::HealthStatus checkStateMachineHealth() {
    // Check if state machine is responsive
    // Check if stuck in a state too long
    // Validate state transitions
    return HEALTH_OK;
}

WatchdogManager::HealthStatus checkMemoryHealth() {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 10000) return HEALTH_CRITICAL;  // < 10KB
    if (freeHeap < 50000) return HEALTH_WARNING;   // < 50KB
    return HEALTH_OK;
}
```

## Health Check Criteria

### StateMachine
- ✅ **OK**: State transitions normal, update() called regularly
- ⚠️ **WARNING**: Stuck in same state > 60 seconds
- ❌ **CRITICAL**: No state changes > 300 seconds
- 🔴 **FAILED**: update() throws exception or hangs

**Recovery**: Restart state machine, reset to default state

### ConfigManager
- ✅ **OK**: Config loads, SPIFFS accessible
- ⚠️ **WARNING**: Config load slow (> 500ms)
- ❌ **CRITICAL**: SPIFFS mount failed
- 🔴 **FAILED**: Config corrupted, validation fails

**Recovery**: Load factory defaults, remount SPIFFS

### Logger
- ✅ **OK**: Logs writing successfully
- ⚠️ **WARNING**: Buffer > 80% full
- ❌ **CRITICAL**: Buffer full, logs being dropped
- 🔴 **FAILED**: Cannot write to serial/file

**Recovery**: Clear old logs, reduce log level

### HAL Button
- ✅ **OK**: Button responsive, events generated
- ⚠️ **WARNING**: Stuck in pressed/released > 10 seconds
- ❌ **CRITICAL**: No events for > 60 seconds
- 🔴 **FAILED**: GPIO read fails

**Recovery**: Reset button state, reconfigure GPIO

### HAL LED
- ✅ **OK**: LED controllable
- ⚠️ **WARNING**: Brightness changes slow
- ❌ **CRITICAL**: Cannot set LED state
- 🔴 **FAILED**: GPIO write fails

**Recovery**: Reset LED state, reconfigure GPIO

### HAL PIR
- ✅ **OK**: Sensor readings valid
- ⚠️ **WARNING**: Sensor in warmup
- ❌ **CRITICAL**: No readings for > 300 seconds
- 🔴 **FAILED**: GPIO read fails

**Recovery**: Reset sensor, restart warmup

### WebServer
- ✅ **OK**: Server running, handling requests
- ⚠️ **WARNING**: Response time > 1 second
- ❌ **CRITICAL**: Server not responding
- 🔴 **FAILED**: Server crashed

**Recovery**: Restart web server

### Memory
- ✅ **OK**: > 50KB free heap
- ⚠️ **WARNING**: 10-50KB free heap
- ❌ **CRITICAL**: < 10KB free heap
- 🔴 **FAILED**: Memory allocation failing

**Recovery**: Trigger garbage collection, reduce log buffer

## Check Intervals

Different modules checked at different rates:

| Module | Check Interval | Rationale |
|--------|---------------|-----------|
| Memory | 1 second | Fast detection of leaks |
| StateMachine | 5 seconds | Catch stuck states |
| HAL Buttons/LED/PIR | 10 seconds | GPIO should be stable |
| ConfigManager | 60 seconds | Config rarely changes |
| Logger | 10 seconds | Prevent buffer overflow |
| WebServer | 30 seconds | Network can be slow |

**Hardware WDT Feed**: Every main loop iteration (< 1 second)

## Recovery Actions

### Level 1: Soft Recovery (Preferred)
- Reset module state
- Clear buffers
- Reload configuration
- **No system reboot**

### Level 2: Module Restart
- Deinitialize module
- Reinitialize module
- Restore from config
- **No system reboot**

### Level 3: System Recovery
- Save critical state
- Log failure reason
- **Controlled system reboot**

### Level 4: Hardware Watchdog
- **No software action possible**
- **Hardware-triggered system reset**
- Last resort for complete lockup

## Failure Handling Strategy

```
┌─────────────────────────────────────────┐
│     Module Health Check Fails           │
└───────────────┬─────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────┐
│   Log Failure (with stack trace)        │
└───────────────┬─────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────┐
│   First Failure?                        │
│   Yes → Try Soft Recovery               │
│   No  → Check failure count             │
└───────────────┬─────────────────────────┘
                │
                ├─── 1-2 failures → Soft Recovery
                ├─── 3-5 failures → Module Restart
                ├─── 6+ failures  → System Recovery
                └─── 10+ failures → Stop feeding WDT
                                    (trigger HW reset)
```

## Configuration

```cpp
struct WatchdogConfig {
    // Hardware watchdog
    uint32_t hardwareTimeoutMs = 8000;  // 8 seconds

    // Check intervals
    uint32_t memoryCheckIntervalMs = 1000;
    uint32_t stateMachineCheckIntervalMs = 5000;
    uint32_t halCheckIntervalMs = 10000;
    uint32_t configCheckIntervalMs = 60000;
    uint32_t loggerCheckIntervalMs = 10000;
    uint32_t webServerCheckIntervalMs = 30000;

    // Failure thresholds
    uint8_t softRecoveryThreshold = 2;
    uint8_t moduleRestartThreshold = 5;
    uint8_t systemRecoveryThreshold = 10;

    // Memory thresholds
    uint32_t memoryWarningBytes = 50000;   // 50KB
    uint32_t memoryCriticalBytes = 10000;  // 10KB

    // Enabled modules
    bool enableMemoryCheck = true;
    bool enableStateMachineCheck = true;
    bool enableHALCheck = true;
    bool enableConfigCheck = true;
    bool enableLoggerCheck = true;
    bool enableWebServerCheck = true;
};
```

## Integration with Existing System

### In main.cpp

```cpp
#include "watchdog_manager.h"

WatchdogManager g_watchdog;

void setup() {
    // Initialize hardware watchdog
    g_watchdog.begin();

    // Register modules
    g_watchdog.registerModule(
        WatchdogManager::MODULE_STATE_MACHINE,
        checkStateMachineHealth
    );

    // ... register other modules

    // Initialize other systems
    g_stateMachine.begin();
    g_config.begin();
    // ...
}

void loop() {
    // Update watchdog (feeds HW WDT if healthy)
    g_watchdog.update();

    // Normal operations
    g_stateMachine.update();

    // Watchdog automatically checks module health
    // and triggers recovery if needed
}
```

## Logging and Monitoring

All watchdog events are logged:

```
[INFO] Watchdog: System healthy (all modules OK)
[WARN] Watchdog: Module MEMORY in WARNING state (35KB free)
[ERROR] Watchdog: Module STATE_MACHINE failed health check (stuck in OFF state)
[ERROR] Watchdog: Attempting soft recovery for STATE_MACHINE
[INFO] Watchdog: STATE_MACHINE recovered successfully
[CRITICAL] Watchdog: Module WEB_SERVER failed 3 times, restarting module
[CRITICAL] Watchdog: System recovery triggered after 10 consecutive failures
[EMERGENCY] Watchdog: Stopping HW WDT feed, system will reset
```

## API Endpoints

New REST endpoints for monitoring:

```
GET /api/watchdog
{
    "systemHealth": "OK",
    "uptime": 3600000,
    "lastCheck": 1705000000,
    "modules": [
        {
            "id": "STATE_MACHINE",
            "status": "OK",
            "failureCount": 0,
            "lastCheck": 1705000000
        },
        {
            "id": "MEMORY",
            "status": "WARNING",
            "failureCount": 0,
            "lastCheck": 1705000000,
            "message": "Free heap: 35KB"
        }
    ]
}

GET /api/watchdog/history
{
    "failures": [
        {
            "timestamp": 1705000000,
            "module": "WEB_SERVER",
            "status": "FAILED",
            "action": "MODULE_RESTART",
            "message": "Server not responding"
        }
    ]
}

POST /api/watchdog/reset
{
    "module": "STATE_MACHINE"  // Optional, resets specific module
}
```

## Testing Strategy

### Unit Tests
- Mock hardware watchdog
- Test each health check function
- Verify recovery actions
- Test failure thresholds

### Integration Tests
- Inject failures into modules
- Verify recovery mechanisms
- Test cascading failures
- Verify HW WDT triggering

### Hardware Tests
- Let system run for 24+ hours
- Monitor for memory leaks
- Inject random failures
- Verify automatic recovery

## Performance Impact

Expected overhead:
- **Memory**: ~2KB RAM for module tracking
- **CPU**: < 1% (health checks are lightweight)
- **Flash**: ~8KB code size
- **Latency**: No impact on main loop (async checks)

## Mock Mode Support

For testing without hardware:

```cpp
#ifdef MOCK_MODE
class MockWatchdog : public WatchdogManager {
    // Disable hardware WDT
    // Simulate module failures
    // Record health check calls
};
#endif
```

## Future Enhancements

1. **Remote Monitoring**: Push health status to cloud
2. **Predictive Analysis**: Detect patterns before failures
3. **Self-Tuning**: Adjust thresholds based on history
4. **Battery Aware**: Reduce checks when on battery
5. **Network Health**: Monitor WiFi connectivity
6. **OTA Safety**: Prevent updates during critical failures

## Security Considerations

- Watchdog cannot be disabled via API (safety)
- Only read access to health status via API
- Recovery actions logged for audit
- HW WDT is ultimate safety mechanism

## Benefits

1. **Reliability**: System recovers from failures automatically
2. **Debugging**: Clear logs of what failed and when
3. **Uptime**: Reduces manual intervention needed
4. **Safety**: Prevents complete system lockup
5. **Monitoring**: Real-time health visibility via API

---

**Last Updated**: 2026-01-12
**Version**: 1.0
**Status**: Design Specification
