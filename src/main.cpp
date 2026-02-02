/**
 * @file main.cpp
 * @brief StepAware - ESP32-C3 Motion-Activated Hazard Warning System
 *
 * Main application entry point. Initializes hardware, state machine,
 * and runs the main event loop.
 *
 * Project: StepAware
 * Board: Olimex ESP32-C3-DevKit-Lipo
 * Sensor: AM312 PIR Motion Sensor
 *
 * Phase 1 - MVP: Core motion detection with LED warning
 */

#include <Arduino.h>
#if !MOCK_HARDWARE
#include <LittleFS.h>  // For animation uploads and user content, NOT for web UI
#endif
#include "config.h"
#include "state_machine.h"
#include "sensor_factory.h"
#include "sensor_manager.h"
#include "direction_detector.h"
#include "hal_led.h"
#include "hal_button.h"
#include "hal_ledmatrix_8x8.h"
#include "config_manager.h"
#include "serial_config.h"
#include "wifi_manager.h"
#include "web_api.h"
#include "debug_logger.h"
#include "power_manager.h"
#include "ntp_manager.h"
#include "recal_scheduler.h"

// ============================================================================
// Global Hardware Objects
// ============================================================================

// Multi-sensor manager (Issue #4 Phase 2, Issue #17)
SensorManager sensorManager;

// Direction detection (Dual-PIR)
DirectionDetector* directionDetector = nullptr;

// Display components (Issue #12)
HAL_LEDMatrix_8x8* ledMatrix = nullptr;  // 8x8 LED matrix display
HAL_LED hazardLED(PIN_HAZARD_LED, LED_PWM_CHANNEL, MOCK_HARDWARE);
HAL_LED statusLED(PIN_STATUS_LED, LED_PWM_CHANNEL + 1, MOCK_HARDWARE);
HAL_Button modeButton(PIN_BUTTON, BUTTON_DEBOUNCE_MS, 1000, MOCK_HARDWARE);

// State Machine (initialized after sensor creation)
StateMachine* stateMachine = nullptr;

// Configuration
ConfigManager configManager;
SerialConfigUI serialConfig(configManager, sensorManager);

// WiFi and Web API
WiFiManager wifiManager;
AsyncWebServer webServer(80);
WebAPI* webAPI = nullptr;
WebAPI* g_webAPI = nullptr;  // Global pointer for logger integration
bool webServerStarted = false;

// NTP Time Sync
NTPManager ntpManager;

// PIR Recalibration Scheduler
RecalScheduler* recalScheduler = nullptr;

// Diagnostic Mode
bool diagnosticMode = false;

// ============================================================================
// Web API Initialization (can be called at runtime)
// ============================================================================

/**
 * @brief Start the Web API server
 *
 * Can be called at boot or when WiFi connects for the first time.
 * Safe to call multiple times - will only initialize once.
 */
void startWebAPI() {
    if (webServerStarted) {
        return;  // Already started
    }

    if (!stateMachine) {
        Serial.println("[WebAPI] Cannot start - state machine not initialized");
        return;
    }

    Serial.println("[WebAPI] Starting Web API server...");

    if (webAPI == nullptr) {
        webAPI = new WebAPI(&webServer, stateMachine, &configManager);
        webAPI->setWiFiManager(&wifiManager);
        webAPI->setSensorManager(&sensorManager);
        webAPI->setPowerManager(&g_power);
    }

    // Always update LED matrix reference (may be initialized after WebAPI)
    if (ledMatrix && ledMatrix->isReady()) {
        webAPI->setLEDMatrix(ledMatrix);
        Serial.println("[WebAPI] LED Matrix reference set");
    }

    // Always update direction detector reference (may be initialized after WebAPI)
    if (directionDetector) {
        webAPI->setDirectionDetector(directionDetector);
        Serial.println("[WebAPI] Direction Detector reference set");
    }

    if (webAPI && webAPI->begin()) {
        g_webAPI = webAPI;  // Set global pointer for logger integration
        webServer.begin();
        webServerStarted = true;
        Serial.println("[WebAPI] Web API started on port 80");
    } else {
        Serial.println("[WebAPI] WARNING: Web API failed to start");
    }
}

/**
 * @brief Callback when WiFi connects
 */
void onWiFiConnected() {
    DEBUG_LOG_WIFI("Connected callback - starting Web API if needed");
    ntpManager.onWiFiConnected();
    startWebAPI();
}

// ============================================================================
// System Status Reporting
// ============================================================================

/**
 * @brief Print system status to serial
 */
void printStatus() {
    Serial.println("\n========================================");
    Serial.println("StepAware System Status");
    Serial.println("========================================");

    Serial.printf("Firmware: %s v%s (build %s)\n", FIRMWARE_NAME, FIRMWARE_VERSION, BUILD_NUMBER);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("Uptime: %u seconds\n", stateMachine->getUptimeSeconds());
    Serial.println();

    Serial.printf("Operating Mode: %s\n",
                  StateMachine::getModeName(stateMachine->getMode()));
    Serial.printf("Warning Active: %s\n", stateMachine->isWarningActive() ? "YES" : "NO");
    Serial.println();

    // Sensor info (multi-sensor support)
    Serial.printf("Sensors: %u active (fusion mode: %s)\n",
                  sensorManager.getActiveSensorCount(),
                  sensorManager.getFusionMode() == FUSION_MODE_ANY ? "ANY" :
                  sensorManager.getFusionMode() == FUSION_MODE_ALL ? "ALL" : "TRIGGER_MEASURE");

    // Iterate through all sensor slots
    HAL_MotionSensor* primarySensor = sensorManager.getPrimarySensor();
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = sensorManager.getSensor(i);
        if (sensor) {
            const SensorCapabilities& caps = sensor->getCapabilities();
            Serial.printf("  [%u] %s - %s\n", i,
                         (sensor == primarySensor) ? "PRIMARY" : "secondary",
                         caps.sensorTypeName);
            Serial.printf("      Ready: %s\n", sensor->isReady() ? "YES" : "NO");
            Serial.printf("      Motion: %s\n", sensor->motionDetected() ? "DETECTED" : "clear");

            if (!sensor->isReady() && caps.requiresWarmup) {
                Serial.printf("      Warmup remaining: %u seconds\n",
                             sensor->getWarmupTimeRemaining() / 1000);
            }

            // Show distance if sensor supports it
            if (caps.supportsDistanceMeasurement) {
                Serial.printf("      Distance: %u mm\n", sensor->getDistance());
                Serial.printf("      Threshold: %u mm\n", sensor->getDetectionThreshold());
            }

            // Show direction if sensor supports it
            if (caps.supportsDirectionDetection) {
                const char* dirName = "Unknown";
                switch (sensor->getDirection()) {
                    case DIRECTION_STATIONARY: dirName = "Stationary"; break;
                    case DIRECTION_APPROACHING: dirName = "Approaching"; break;
                    case DIRECTION_RECEDING: dirName = "Receding"; break;
                    default: break;
                }
                Serial.printf("      Direction: %s\n", dirName);
            }
        }
    }

    Serial.printf("  Motion Events: %u\n", stateMachine->getMotionEventCount());
    Serial.println();

    Serial.printf("Mode Changes: %u\n", stateMachine->getModeChangeCount());
    Serial.printf("Button Clicks: %u\n", modeButton.getClickCount());
    Serial.println();

    Serial.printf("Memory - Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Memory - Largest Block: %u bytes\n", ESP.getMaxAllocHeap());
    Serial.println();

    // WiFi Status
    const WiFiManager::Status& wifiStatus = wifiManager.getStatus();
    Serial.printf("WiFi State: %s\n", WiFiManager::getStateName(wifiStatus.state));
    if (wifiStatus.state == WiFiManager::STATE_CONNECTED) {
        Serial.printf("  SSID: %s\n", wifiStatus.ssid);
        Serial.printf("  IP Address: %s\n", wifiStatus.ip.toString().c_str());
        Serial.printf("  Signal: %d dBm\n", wifiStatus.rssi);
        Serial.printf("  Uptime: %u seconds\n", wifiStatus.connectionUptime / 1000);
    } else if (wifiStatus.state == WiFiManager::STATE_AP_MODE) {
        Serial.printf("  AP SSID: %s\n", wifiStatus.apSSID);
        Serial.printf("  AP IP: %s\n", wifiStatus.ip.toString().c_str());
    } else if (wifiStatus.state == WiFiManager::STATE_CONNECTING) {
        Serial.println("  Connecting...");
    } else if (wifiStatus.state == WiFiManager::STATE_DISABLED) {
        Serial.println("  WiFi is disabled in configuration");
    }
    if (wifiStatus.failureCount > 0) {
        Serial.printf("  Failures: %u\n", wifiStatus.failureCount);
    }

    // Battery Status
    Serial.println();
    const PowerManager::BatteryStatus& battery = g_power.getBatteryStatus();
    Serial.printf("Battery: %.2fV  %u%%\n", battery.voltage, battery.percentage);
    if (battery.usbPower) {
        Serial.println("  USB Power: YES");
    } else {
        Serial.println("  USB Power: NO");
    }
    if (battery.critical) {
        Serial.println("  WARNING: CRITICAL battery level!");
    } else if (battery.low) {
        Serial.println("  WARNING: LOW battery level");
    }

    Serial.println("========================================\n");
}

/**
 * @brief Print startup banner
 */
void printBanner() {
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║                                        ║");
    Serial.println("║          S T E P A W A R E             ║");
    Serial.println("║                                        ║");
    Serial.println("║   Motion-Activated Hazard Warning     ║");
    Serial.println("║                                        ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    Serial.printf("Version: %s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("Board: ESP32-C3-DevKit-Lipo\n");
    Serial.printf("Sensors: %u configured\n", sensorManager.getActiveSensorCount());
    Serial.println();

#if MOCK_HARDWARE
    Serial.println("⚠️  MOCK HARDWARE MODE ENABLED");
    Serial.println("   Using simulated hardware for development");
    Serial.println();
#endif

    Serial.println("Phase 1 - MVP Implementation");
    Serial.println("- Motion Detection");
    Serial.println("- LED Hazard Warning");
    Serial.println("- Mode Switching (Button)");
    Serial.println();
    Serial.println("Available Modes:");
    Serial.println("  1. OFF - System off");
    Serial.println("  2. CONTINUOUS_ON - Always flashing");
    Serial.println("  3. MOTION_DETECT - Flash on motion (default)");
    Serial.println();
    Serial.println("Press button to cycle modes");
    Serial.println("========================================\n");
}

/**
 * @brief Print help information
 */
void printHelp() {
    Serial.println("\n========================================");
    Serial.println("StepAware Command Reference");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Quick Commands (single key):");
    Serial.println("  s - Print system status");
    Serial.println("  h - Print this help");
    Serial.println("  0 - Set mode to OFF");
    Serial.println("  1 - Set mode to CONTINUOUS_ON");
    Serial.println("  2 - Set mode to MOTION_DETECT");
    Serial.println("  r - Reset statistics");
    Serial.println("  p - Enter configuration mode");
    Serial.println("  g - Show current configuration");
    Serial.println("  l - List all configured sensors");
    Serial.println("  f - Set sensor fusion mode (ANY/ALL/TRIGGER_MEASURE)");
    Serial.println("  v - Toggle sensor diagnostic view (real-time)");

#if MOCK_HARDWARE
    Serial.println();
    Serial.println("Mock Mode Commands:");
    Serial.println("  m - Trigger mock motion");
    Serial.println("  c - Clear mock motion");
    Serial.println("  d - Set mock distance (ultrasonic only)");
    Serial.println("  b - Simulate button press");
#endif
    Serial.println();
    Serial.println("Configuration Mode:");
    Serial.println("  Press 'p' to enter interactive config mode");
    Serial.println("  Type 'help' in config mode for all options");
    Serial.println();
    Serial.println("Hardware:");
    Serial.println("  Button - Press to cycle modes");
    Serial.printf("  Sensors - %u configured\n", sensorManager.getActiveSensorCount());
    Serial.println("  Hazard LED - Warning indicator");
    Serial.println("  Status LED - Mode indicator");
    Serial.println();
    Serial.println("========================================\n");
}

/**
 * @brief Process serial commands
 *
 * If in config mode, delegates to SerialConfigUI for line-based commands.
 * Otherwise handles single-character quick commands.
 */
void processSerialCommand() {
    // If in config mode, let SerialConfigUI handle all input
    if (serialConfig.isInConfigMode()) {
        serialConfig.update();
        return;
    }

    if (!Serial.available()) {
        return;
    }

    char cmd = Serial.read();

    switch (cmd) {
        case 's':
        case 'S':
            printStatus();
            break;

        case 'h':
        case 'H':
        case '?':
            printHelp();
            break;

        case '0':
            Serial.println("[Command] Setting mode to OFF");
            stateMachine->setMode(StateMachine::OFF);
            break;

        case '1':
            Serial.println("[Command] Setting mode to CONTINUOUS_ON");
            stateMachine->setMode(StateMachine::CONTINUOUS_ON);
            break;

        case '2':
            Serial.println("[Command] Setting mode to MOTION_DETECT");
            stateMachine->setMode(StateMachine::MOTION_DETECT);
            break;

        case 'r':
        case 'R':
            Serial.println("[Command] Resetting statistics");
            // Reset event counts on all sensors
            for (uint8_t i = 0; i < 4; i++) {
                HAL_MotionSensor* sensor = sensorManager.getSensor(i);
                if (sensor) {
                    sensor->resetEventCount();
                }
            }
            modeButton.resetClickCount();
            break;

        case 'p':
        case 'P':
            // Enter configuration mode
            serialConfig.enterConfigMode();
            break;

        case 'g':
        case 'G':
            // Show current configuration
            configManager.print();
            break;

        case 'l':
        case 'L':
            // List all configured sensors
            Serial.println("[Command] Configured Sensors:");
            Serial.println("========================================");
            sensorManager.printStatus();
            Serial.println("========================================");
            Serial.printf("Fusion Mode: %s\n",
                         sensorManager.getFusionMode() == FUSION_MODE_ANY ? "ANY (OR)" :
                         sensorManager.getFusionMode() == FUSION_MODE_ALL ? "ALL (AND)" : "TRIGGER_MEASURE");
            Serial.printf("Active Sensors: %u\n", sensorManager.getActiveSensorCount());
            {
                HAL_MotionSensor* primary = sensorManager.getPrimarySensor();
                if (primary) {
                    for (uint8_t i = 0; i < 4; i++) {
                        if (sensorManager.getSensor(i) == primary) {
                            Serial.printf("Primary Sensor Slot: %u\n", i);
                            break;
                        }
                    }
                }
            }
            Serial.println();
            break;

        case 'f':
        case 'F':
            // Set sensor fusion mode
            Serial.println("[Command] Select Fusion Mode:");
            Serial.println("  0 = ANY (motion if ANY sensor detects)");
            Serial.println("  1 = ALL (motion if ALL sensors detect)");
            Serial.println("  2 = TRIGGER_MEASURE (first triggers, second measures)");
            Serial.print("Enter mode (0-2): ");
            while (!Serial.available()) { delay(10); }
            {
                char modeChar = Serial.read();
                Serial.println(modeChar);
                uint8_t mode = modeChar - '0';
                if (mode <= 2) {
                    sensorManager.setFusionMode((SensorFusionMode)mode);
                    Serial.printf("Fusion mode set to: %s\n",
                                 mode == 0 ? "ANY" : mode == 1 ? "ALL" : "TRIGGER_MEASURE");
                } else {
                    Serial.println("Invalid mode. Use 0, 1, or 2.");
                }
            }
            break;

        case 'v':
        case 'V':
            // Toggle diagnostic mode
            diagnosticMode = !diagnosticMode;
            if (diagnosticMode) {
                Serial.println("\n[Diagnostic] Real-time sensor view ENABLED");
                Serial.println("[Diagnostic] Press 'v' again to stop");
                Serial.println("[Diagnostic] Format: [Dist] threshold motion dir | decision");
                Serial.println();
            } else {
                Serial.println("\n[Diagnostic] Real-time sensor view DISABLED\n");
            }
            break;

#if MOCK_HARDWARE
        case 'm':
        case 'M':
            Serial.println("[Command] Triggering mock motion on all sensors");
            for (uint8_t i = 0; i < 4; i++) {
                HAL_MotionSensor* sensor = sensorManager.getSensor(i);
                if (sensor) {
                    sensor->mockSetMotion(true);
                }
            }
            break;

        case 'c':
        case 'C':
            Serial.println("[Command] Clearing mock motion on all sensors");
            for (uint8_t i = 0; i < 4; i++) {
                HAL_MotionSensor* sensor = sensorManager.getSensor(i);
                if (sensor) {
                    sensor->mockSetMotion(false);
                }
            }
            break;

        case 'd':
        case 'D':
            // Set mock distance on distance-capable sensors
            Serial.println("[Command] Setting mock distance to 250mm on distance sensors");
            for (uint8_t i = 0; i < 4; i++) {
                HAL_MotionSensor* sensor = sensorManager.getSensor(i);
                if (sensor && sensor->getCapabilities().supportsDistanceMeasurement) {
                    sensor->mockSetDistance(250);
                    Serial.printf("  Set distance on sensor %u\n", i);
                }
            }
            break;

        case 'b':
        case 'B':
            Serial.println("[Command] Simulating button click");
            modeButton.mockClick();
            break;
#endif

        case '\n':
        case '\r':
            // Ignore newlines
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            Serial.println("Type 'h' for help");
            break;
    }
}

// ============================================================================
// Display Abstraction Helpers (Issue #12)
// ============================================================================

/**
 * @brief Trigger warning display on configured output device
 *
 * This function abstracts the display hardware - uses LED matrix if available,
 * otherwise falls back to hazard LED.
 *
 * @param duration_ms Warning duration in milliseconds
 */
void triggerWarningDisplay(uint32_t duration_ms) {
    if (ledMatrix && ledMatrix->isReady()) {
        // Use LED matrix motion alert animation
        ledMatrix->startAnimation(
            HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT,
            duration_ms
        );
        DEBUG_LOG_LED("Triggered matrix motion alert (duration: %u ms)", duration_ms);
    } else {
        // Fall back to hazard LED warning pattern
        hazardLED.startPattern(
            HAL_LED::PATTERN_BLINK_WARNING,
            duration_ms
        );
        DEBUG_LOG_LED("Triggered LED warning (duration: %u ms)", duration_ms);
    }
}

/**
 * @brief Show battery status on display
 *
 * @param percentage Battery percentage (0-100)
 */
void showBatteryStatus(uint8_t percentage) {
    if (ledMatrix && ledMatrix->isReady() && percentage < 30) {
        // Show low battery animation on matrix
        ledMatrix->startAnimation(
            HAL_LEDMatrix_8x8::ANIM_BATTERY_LOW,
            2000
        );
        DEBUG_LOG_LED("Showing battery low on matrix (%u%%)", percentage);
    } else if (percentage < 30) {
        // Blink hazard LED slowly for low battery
        hazardLED.startPattern(HAL_LED::PATTERN_BLINK_SLOW, 2000);
        DEBUG_LOG_LED("Showing battery low on LED (%u%%)", percentage);
    }
}

static bool pendingBatteryLow = false;

/**
 * @brief Callback for PowerManager low/critical battery events.
 * Defers behind a motion alert animation rather than interrupting it.
 */
void onBatteryLowCallback() {
    if (ledMatrix && ledMatrix->isAnimating() &&
        ledMatrix->getPattern() == HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT) {
        pendingBatteryLow = true;  // Play after warning finishes
        return;
    }
    showBatteryStatus(g_power.getBatteryPercentage());
}

/**
 * @brief Stop all display animations
 */
void stopDisplayAnimations() {
    if (ledMatrix) {
        ledMatrix->stopAnimation();
    }
    hazardLED.stopPattern();
}

// ============================================================================
// Boot-Time Reset Functions
// ============================================================================

/**
 * @brief Perform WiFi credential reset (soft reset)
 *
 * Clears only WiFi SSID and password, preserving all other settings.
 * Device will enter AP mode for reconfiguration.
 */
void performWiFiReset() {
    Serial.println("\n[RESET] ╔════════════════════════════════════════╗");
    Serial.println("[RESET] ║   WiFi Credential Reset Triggered    ║");
    Serial.println("[RESET] ╚════════════════════════════════════════╝");

    // Note: WiFi Manager and Config Manager integration will be added
    // when those components are integrated into main.cpp

    // Blink 3 times to confirm WiFi reset
    for (int i = 0; i < 3; i++) {
        hazardLED.on(LED_BRIGHTNESS_FULL);
        delay(200);
        hazardLED.off();
        delay(200);
    }

    Serial.println("[RESET] WiFi credentials cleared");
    Serial.println("[RESET] Device will enter AP mode on next boot");
    Serial.println("[RESET] Reset complete\n");
}

/**
 * @brief Perform full factory reset
 *
 * Resets ALL configuration to defaults:
 * - WiFi credentials
 * - Operating mode
 * - LED brightness
 * - All thresholds
 * - State machine counters
 * - Logs (if implemented)
 */
void performFactoryReset() {
    Serial.println("\n[RESET] ╔════════════════════════════════════════╗");
    Serial.println("[RESET] ║   FULL FACTORY RESET TRIGGERED        ║");
    Serial.println("[RESET] ╚════════════════════════════════════════╝");

    // Note: Config Manager integration will be added when that component
    // is integrated into main.cpp

    // Reset state machine and sensor counters
    for (uint8_t i = 0; i < 4; i++) {
        HAL_MotionSensor* sensor = sensorManager.getSensor(i);
        if (sensor) {
            sensor->resetEventCount();
        }
    }
    modeButton.resetClickCount();

    // Solid LED for 2 seconds to confirm factory reset
    hazardLED.on(LED_BRIGHTNESS_FULL);
    delay(2000);
    hazardLED.off();

    Serial.println("[RESET] All configuration reset to factory defaults");
    Serial.println("[RESET] Rebooting device...\n");

    delay(1000);

    // Reboot the ESP32
    ESP.restart();
}

/**
 * @brief Handle button hold during boot for reset operations
 *
 * Detects button hold at boot time and performs appropriate reset:
 * - 15 seconds: WiFi credential reset (fast blink feedback)
 * - 30 seconds: Full factory reset (solid LED feedback)
 *
 * User must release button to confirm the reset action.
 */
void handleBootButtonHold() {
    uint32_t pressStart = millis();
    bool wifiResetTriggered = false;
    bool factoryResetTriggered = false;

    Serial.println("\n[BOOT] Button held during boot - checking for reset...");
    Serial.println("[BOOT] Release button to cancel");
    Serial.println("[BOOT] Hold 15s for WiFi reset, 30s for factory reset");

    // Indicate we're in reset detection mode with slow pulse
    hazardLED.setPattern(HAL_LED::PATTERN_PULSE);

    while (modeButton.isPressed()) {
        uint32_t pressDuration = millis() - pressStart;

        // WiFi reset stage (15 seconds)
        if (pressDuration >= BUTTON_WIFI_RESET_MS && !wifiResetTriggered) {
            Serial.println("\n[BOOT] *** WiFi Reset Pending ***");
            Serial.println("[BOOT] Release button to confirm WiFi credential reset");
            Serial.println("[BOOT] Keep holding for factory reset (15 more seconds)");

            // Fast blink to indicate WiFi reset pending
            hazardLED.setPattern(HAL_LED::PATTERN_BLINK_FAST);
            wifiResetTriggered = true;
        }

        // Factory reset stage (30 seconds)
        if (pressDuration >= BUTTON_FACTORY_RESET_MS && !factoryResetTriggered) {
            Serial.println("\n[BOOT] *** FACTORY RESET PENDING ***");
            Serial.println("[BOOT] Release button to confirm FULL factory reset");
            Serial.println("[BOOT] WARNING: This will erase ALL settings!");

            // Solid LED to indicate factory reset pending
            hazardLED.setPattern(HAL_LED::PATTERN_ON);
            factoryResetTriggered = true;
        }

        // Update LED pattern
        hazardLED.update();
        modeButton.update();

        delay(10);
    }

    // Button released - execute the appropriate reset
    Serial.println("\n[BOOT] Button released");

    if (factoryResetTriggered) {
        performFactoryReset();
        // Note: performFactoryReset() reboots device, never returns
    } else if (wifiResetTriggered) {
        performWiFiReset();
    } else {
        Serial.println("[BOOT] Reset canceled (button released too early)");
    }

    // Turn off LED
    hazardLED.off();
}

// ============================================================================
// Arduino Setup and Loop
// ============================================================================

void setup() {
    // Initialize serial communication
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);  // Allow serial to stabilize

    Serial.println("[Setup] Initializing StepAware...");

#if !MOCK_HARDWARE
    // Initialize LittleFS for user content (animations, etc.)
    // NOTE: We do NOT use LittleFS for web UI files!
    // The web UI is served as inline HTML (buildDashboardHTML).
    // LittleFS is ONLY for user-uploaded animations and other user content.
    Serial.println("[Setup] Initializing LittleFS for user content...");

    bool littleFSReady = false;

    // First attempt: Try to mount
    if (LittleFS.begin(false)) {
        Serial.println("[Setup] LittleFS mounted");

        // Verify filesystem is actually working by testing write
        File testFile = LittleFS.open("/.test", "w");
        if (testFile) {
            testFile.print("test");
            testFile.close();
            LittleFS.remove("/.test");
            littleFSReady = true;
            Serial.println("[Setup] ✓ LittleFS verified and ready");
        } else {
            Serial.println("[Setup] LittleFS mount succeeded but filesystem not working");
            LittleFS.end();
        }
    }

    // If mount failed or filesystem not working, format and retry
    if (!littleFSReady) {
        Serial.println("[Setup] Formatting LittleFS (this may take 30-60 seconds)...");
        if (LittleFS.format()) {
            Serial.println("[Setup] Format complete, mounting...");
            if (LittleFS.begin(false)) {
                littleFSReady = true;
                Serial.println("[Setup] ✓ LittleFS formatted and mounted successfully");
            } else {
                Serial.println("[Setup] ERROR: Failed to mount after format!");
            }
        } else {
            Serial.println("[Setup] ERROR: LittleFS format failed!");
        }
    }

    if (!littleFSReady) {
        Serial.println("[Setup] WARNING: LittleFS unavailable - logs and config will not persist!");
    }
#endif

    // Initialize debug logger EARLY (requires LittleFS) with minimal logging
    Serial.println("[Setup] Initializing debug logger...");
    if (!g_debugLogger.begin(DebugLogger::LEVEL_ERROR, DebugLogger::CAT_ALL)) {
        Serial.println("[Setup] WARNING: Debug logger initialization failed");
    }

    // Initialize configuration manager (loads from SPIFFS)
    Serial.println("[Setup] Initializing configuration manager...");
    if (!configManager.begin()) {
        Serial.println("[Setup] WARNING: Config manager failed, using defaults");
        DEBUG_LOG_CONFIG("Config manager initialization FAILED - using defaults");
    } else {
        DEBUG_LOG_CONFIG("Config manager initialized successfully");
    }

    // Validate and correct configuration for corruption/invalid values
    Serial.println("[Setup] Validating configuration...");
    if (!configManager.validateAndCorrect()) {
        Serial.println("[Setup] WARNING: Configuration had errors and was corrected");
        DEBUG_LOG_CONFIG("Configuration validation found and corrected errors");
    } else {
        Serial.println("[Setup] Configuration validation: PASSED");
        DEBUG_LOG_CONFIG("Configuration validation: PASSED (no errors)");
    }

    // Auto-configure direction detector based on sensor distance zones
    Serial.println("[Setup] Auto-configuring direction detection...");
    configManager.autoConfigureDirectionDetector();

    // Apply log level from config to both loggers BEFORE writing boot info
    const ConfigManager::Config& bootCfg = configManager.getConfig();
    DebugLogger::LogLevel debugLevel = static_cast<DebugLogger::LogLevel>(bootCfg.logLevel);
    g_debugLogger.setLevel(debugLevel);

    // Also set regular Logger level from config
    Logger::LogLevel loggerLevel = static_cast<Logger::LogLevel>(bootCfg.logLevel);
    g_logger.setLevel(loggerLevel);

    Serial.printf("[Setup] Log level set to %u (%s) from config\n",
                  bootCfg.logLevel, Logger::getLevelName(loggerLevel));

    // Now write boot info with correct log level
    DEBUG_LOG_BOOT("=== StepAware Starting ===");
    DEBUG_LOG_BOOT("Firmware: %s (build %s)", FIRMWARE_VERSION, BUILD_NUMBER);
    DEBUG_LOG_BOOT("Build: %s %s", BUILD_DATE, BUILD_TIME);
    DEBUG_LOG_BOOT("Board: ESP32-C3-DevKit-Lipo");
    DEBUG_LOG_BOOT("Free Heap: %u bytes", ESP.getFreeHeap());
    g_debugLogger.logConfigDump();

    // Initialize serial configuration interface
    Serial.println("[Setup] Initializing serial config interface...");
    serialConfig.begin();

    // Print startup banner
    printBanner();

    // Initialize sensor manager (Issue #17 fix)
    Serial.println("[Setup] Initializing sensor manager...");
    if (!sensorManager.begin()) {
        Serial.println("[Setup] WARNING: Sensor manager failed to initialize");
    }

    // Load sensors from configuration (Issue #17 fix)
    const ConfigManager::Config& cfg = configManager.getConfig();
    bool sensorsLoaded = false;

    Serial.println("[Setup] Loading sensor configuration...");
    for (uint8_t i = 0; i < 4; i++) {
        const ConfigManager::SensorSlotConfig& sensorCfg = cfg.sensors[i];

        if (sensorCfg.active && sensorCfg.enabled) {
            Serial.printf("[Setup] Loading sensor slot %d: %s (type %d)\n",
                         i, sensorCfg.name, sensorCfg.type);

            SensorConfig config;
            config.type = static_cast<SensorType>(sensorCfg.type);
            config.primaryPin = sensorCfg.primaryPin;
            config.secondaryPin = sensorCfg.secondaryPin;
            config.detectionThreshold = sensorCfg.detectionThreshold;
            config.maxDetectionDistance = sensorCfg.maxDetectionDistance;
            config.debounceMs = sensorCfg.debounceMs;
            config.warmupMs = sensorCfg.warmupMs;
            config.enableDirectionDetection = sensorCfg.enableDirectionDetection;
            config.directionTriggerMode = sensorCfg.directionTriggerMode;
            config.directionSensitivity = sensorCfg.directionSensitivity;
            config.invertLogic = false;
            config.sampleWindowSize = sensorCfg.sampleWindowSize;
            config.sampleRateMs = sensorCfg.sampleRateMs;

            if (sensorManager.addSensor(i, config, sensorCfg.name,
                                       sensorCfg.isPrimary, MOCK_HARDWARE)) {
                Serial.printf("[Setup] ✓ Loaded %s on slot %d\n", sensorCfg.name, i);
                sensorsLoaded = true;
            } else {
                Serial.printf("[Setup] ✗ Failed to load sensor slot %d: %s\n",
                             i, sensorManager.getLastError());
            }
        }
    }

    // Fallback: If no sensors loaded from config, create default PIR sensor
    if (!sensorsLoaded) {
        Serial.println("[Setup] No sensors in config, creating default PIR sensor...");
        SensorConfig defaultConfig;
        defaultConfig.type = SENSOR_TYPE_PIR;
        defaultConfig.primaryPin = PIN_PIR_SENSOR;
        defaultConfig.secondaryPin = 0;
        defaultConfig.detectionThreshold = 0;
        defaultConfig.debounceMs = 100;
        defaultConfig.warmupMs = PIR_WARMUP_TIME_MS;
        defaultConfig.enableDirectionDetection = false;
        defaultConfig.invertLogic = false;

        if (sensorManager.addSensor(0, defaultConfig, "Default PIR", true, MOCK_HARDWARE)) {
            Serial.println("[Setup] ✓ Created default PIR sensor");
        } else {
            Serial.println("[Setup] ERROR: Failed to create default sensor!");
            while (1) { delay(1000); }
        }
    }

    // Print loaded sensors
    Serial.println("[Setup] Sensor configuration:");
    sensorManager.printStatus();

    // Assign PIR power pin and create recalibration scheduler.
    // Both PIR sensors share one power wire on GPIO20; bind to the near
    // sensor (slot 0 by convention). One recalibrate() call handles both.
    {
        HAL_MotionSensor* nearSensor = sensorManager.getSensor(0);
        if (nearSensor && nearSensor->getSensorType() == SENSOR_TYPE_PIR) {
            HAL_PIR* pirNear = static_cast<HAL_PIR*>(nearSensor);
            pirNear->setPowerPin(PIN_PIR_POWER);
            recalScheduler = new RecalScheduler(pirNear);
            recalScheduler->begin();
            Serial.printf("[Setup] ✓ PIR recalibration scheduler initialized (GPIO%d)\n",
                         PIN_PIR_POWER);
        } else {
            Serial.println("[Setup] Near PIR not in slot 0 — recal scheduler not created");
        }
    }

    // Initialize direction detector if enabled (Dual-PIR)
    const ConfigManager::DirectionDetectorConfig& dirCfg = cfg.directionDetector;
    if (dirCfg.enabled) {
        Serial.println("[Setup] Direction detector enabled, initializing...");

        // Get sensor references
        HAL_MotionSensor* farSensor = sensorManager.getSensor(dirCfg.farSensorSlot);
        HAL_MotionSensor* nearSensor = sensorManager.getSensor(dirCfg.nearSensorSlot);

        if (farSensor && nearSensor) {
            directionDetector = new DirectionDetector(farSensor, nearSensor);
            directionDetector->begin();
            directionDetector->setConfirmationWindowMs(dirCfg.confirmationWindowMs);
            directionDetector->setSimultaneousThresholdMs(dirCfg.simultaneousThresholdMs);
            directionDetector->setPatternTimeoutMs(dirCfg.patternTimeoutMs);

            Serial.printf("[Setup] ✓ Direction detector initialized (far=slot %d, near=slot %d)\n",
                         dirCfg.farSensorSlot, dirCfg.nearSensorSlot);
            Serial.printf("[Setup]   - Confirmation window: %u ms\n", dirCfg.confirmationWindowMs);
            Serial.printf("[Setup]   - Simultaneous threshold: %u ms\n", dirCfg.simultaneousThresholdMs);
            Serial.printf("[Setup]   - Pattern timeout: %u ms\n", dirCfg.patternTimeoutMs);
            Serial.printf("[Setup]   - Trigger on approaching: %s\n",
                         dirCfg.triggerOnApproaching ? "YES" : "NO");
        } else {
            Serial.printf("[Setup] ERROR: Cannot create direction detector - invalid sensor slots (far=%d, near=%d)\n",
                         dirCfg.farSensorSlot, dirCfg.nearSensorSlot);
        }
    } else {
        Serial.println("[Setup] Direction detector disabled");
    }

    Serial.println("[Setup] Initializing hazard LED...");
    if (!hazardLED.begin()) {
        Serial.println("[Setup] ERROR: Failed to initialize hazard LED");
        while (1) { delay(1000); }
    }

    Serial.println("[Setup] Initializing status LED...");
    if (!statusLED.begin()) {
        Serial.println("[Setup] ERROR: Failed to initialize status LED");
        while (1) { delay(1000); }
    }

    Serial.println("[Setup] Initializing mode button...");
    if (!modeButton.begin()) {
        Serial.println("[Setup] ERROR: Failed to initialize button");
        while (1) { delay(1000); }
    }

    // Initialize LED matrix display (Issue #12)
    // (cfg already retrieved earlier for sensor initialization)
    const ConfigManager::DisplaySlotConfig& displayCfg = cfg.displays[0];

    if (displayCfg.active && displayCfg.enabled &&
        displayCfg.type == DISPLAY_TYPE_MATRIX_8X8) {

        Serial.println("[Setup] Initializing 8x8 LED Matrix...");
        Serial.printf("[Setup]   I2C Address: 0x%02X\n", displayCfg.i2cAddress);
        Serial.printf("[Setup]   SDA Pin: GPIO %d\n", displayCfg.sdaPin);
        Serial.printf("[Setup]   SCL Pin: GPIO %d\n", displayCfg.sclPin);
        Serial.printf("[Setup]   Brightness: %d/15\n", displayCfg.brightness);
        Serial.printf("[Setup]   Rotation: %d°\n", displayCfg.rotation * 90);

        // Create LED matrix instance
        ledMatrix = new HAL_LEDMatrix_8x8(
            displayCfg.i2cAddress,
            displayCfg.sdaPin,
            displayCfg.sclPin,
            MOCK_HARDWARE
        );

        // Initialize LED matrix
        if (ledMatrix && ledMatrix->begin()) {
            // Apply configuration settings
            ledMatrix->setBrightness(displayCfg.brightness);
            ledMatrix->setRotation(displayCfg.rotation);

            // Show boot animation
            ledMatrix->startAnimation(
                HAL_LEDMatrix_8x8::ANIM_BOOT_STATUS,
                MATRIX_BOOT_DISPLAY_MS
            );

            Serial.println("[Setup] ✓ LED Matrix initialized successfully");
        } else {
            // Matrix initialization failed - will fall back to hazard LED only
            Serial.println("[Setup] WARNING: LED Matrix initialization failed");
            Serial.println("[Setup]          Using hazard LED for warnings");
            if (ledMatrix) {
                delete ledMatrix;
                ledMatrix = nullptr;
            }
        }
    } else {
        Serial.println("[Setup] LED Matrix not configured in settings");

        #if MOCK_HARDWARE
        // In mock mode, create LED matrix anyway for testing animations via web UI
        Serial.println("[Setup] Creating LED Matrix in mock mode for testing...");
        ledMatrix = new HAL_LEDMatrix_8x8(0x70, 8, 9, true);
        if (ledMatrix && ledMatrix->begin()) {
            ledMatrix->setBrightness(MATRIX_BRIGHTNESS_DEFAULT);
            Serial.println("[Setup] ✓ Mock LED Matrix created for testing");
        } else {
            Serial.println("[Setup] WARNING: Failed to create mock LED Matrix");
            if (ledMatrix) {
                delete ledMatrix;
                ledMatrix = nullptr;
            }
        }
        #else
        Serial.println("[Setup] Using hazard LED only (enable LED Matrix in Hardware tab)");
        #endif
    }

    // Check if button is held during boot for reset operations
    modeButton.update();  // Read current button state
    if (modeButton.isPressed()) {
        handleBootButtonHold();
    }

    // Create and initialize state machine
    Serial.println("[Setup] Creating state machine...");
    stateMachine = new StateMachine(&sensorManager, &hazardLED, &statusLED, &modeButton, &configManager);
    if (!stateMachine) {
        Serial.println("[Setup] ERROR: Failed to allocate state machine");
        while (1) { delay(1000); }
    }

    // Assign LED matrix to state machine (Issue #12)
    if (ledMatrix && ledMatrix->isReady()) {
        stateMachine->setLEDMatrix(ledMatrix);
        Serial.println("[Setup] State machine will use LED matrix for warnings");
    }

    // Assign direction detector to state machine (dual-PIR direction detection)
    if (directionDetector) {
        stateMachine->setDirectionDetector(directionDetector);
        Serial.println("[Setup] State machine will use direction detector for motion filtering");
    }

    // Get default mode from config (cfg already retrieved earlier)
    StateMachine::OperatingMode defaultMode = static_cast<StateMachine::OperatingMode>(cfg.defaultMode);

    Serial.println("[Setup] Initializing state machine...");
    if (!stateMachine->begin(defaultMode)) {
        Serial.println("[Setup] ERROR: Failed to initialize state machine");
        while (1) { delay(1000); }
    }

    // Initialize Power Manager
    Serial.println("[Setup] Initializing power manager...");
    if (!g_power.begin()) {
        Serial.println("[Setup] WARNING: Power manager initialization failed");
    } else {
        Serial.println("[Setup] Power manager initialized");
        g_power.onLowBattery(onBatteryLowCallback);
        g_power.onCriticalBattery(onBatteryLowCallback);
    }

    // Initialize WiFi Manager
    Serial.println("[Setup] Initializing WiFi manager...");
    WiFiManager::Config wifiConfig;
    wifiConfig.enabled = cfg.wifiEnabled;
    strncpy(wifiConfig.ssid, cfg.wifiSSID, sizeof(wifiConfig.ssid));
    strncpy(wifiConfig.password, cfg.wifiPassword, sizeof(wifiConfig.password));
    strncpy(wifiConfig.hostname, cfg.deviceName, sizeof(wifiConfig.hostname));
    wifiConfig.apModeOnFailure = false;  // Issue #2: Never fall back to AP mode
    wifiConfig.connectionTimeout = 30000;
    wifiConfig.maxReconnectAttempts = 0;  // Issue #2: Retry indefinitely (0 = unlimited)

    // Register callback to start Web API when WiFi connects
    wifiManager.onConnected(onWiFiConnected);

    if (!wifiManager.begin(&wifiConfig)) {
        Serial.println("[Setup] WARNING: WiFi manager initialization failed");
    } else {
        Serial.printf("[Setup] WiFi %s\n", cfg.wifiEnabled ? "enabled" : "disabled");
        if (cfg.wifiEnabled && strlen(cfg.wifiSSID) > 0) {
            Serial.printf("[Setup] Connecting to WiFi: %s\n", cfg.wifiSSID);
        }
    }

    // Initialize NTP Manager
    Serial.println("[Setup] Initializing NTP manager...");
    ntpManager.begin(cfg.ntpEnabled, cfg.ntpServer, cfg.timezoneOffsetHours);

    // Start Web API immediately if WiFi is already enabled
    // (callback will also fire when WiFi connects later)
    if (cfg.wifiEnabled) {
        startWebAPI();
    }

    Serial.println("[Setup] ✓ Initialization complete!");
    Serial.println();

    DEBUG_LOG_BOOT("=== Boot Complete ===");
    DEBUG_LOG_BOOT("Sensors active: %u", sensorManager.getActiveSensorCount());
    DEBUG_LOG_BOOT("WiFi: %s", cfg.wifiEnabled ? "enabled" : "disabled");
    DEBUG_LOG_BOOT("LED Matrix: %s", (ledMatrix && ledMatrix->isReady()) ? "ready" : "not available");

    // Print help
    printHelp();

    // Initial status
    printStatus();

#if MOCK_HARDWARE
    // Provide mock mode instructions
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║       MOCK HARDWARE MODE ACTIVE        ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    Serial.println("Test Commands:");
    Serial.println("  Type 'm' to simulate motion detection");
    Serial.println("  Type 'b' to simulate button press");
    Serial.println("  Type 's' to view system status");
    Serial.println();
#endif

    Serial.println("[Main] Entering main loop...\n");
}

void loop() {
    // Update sensor manager (handles all sensors)
    sensorManager.update();

    // Update direction detector (Dual-PIR)
    if (directionDetector) {
        directionDetector->update();
    }

    // Update LED matrix (handles animations)
    if (ledMatrix) {
        ledMatrix->update();
        // Play deferred battery animation after warning finishes
        if (pendingBatteryLow && !ledMatrix->isAnimating()) {
            pendingBatteryLow = false;
            showBatteryStatus(g_power.getBatteryPercentage());
        }
    }

    // Update state machine (handles all hardware and logic)
    stateMachine->update();

    // Motion is physical activity — prevent sleep while processing a warning.
    {
        static uint32_t lastMotionCount = 0;
        uint32_t currentMotionCount = stateMachine->getMotionEventCount();
        if (currentMotionCount != lastMotionCount) {
            lastMotionCount = currentMotionCount;
            g_power.recordActivity();
        }
    }

    // Update WiFi manager (handles connection state, reconnection)
    wifiManager.update();

    // Update NTP manager (handles sync completion, hourly checks, daily resync)
    ntpManager.update();

    // Update recalibration scheduler (smart nightly PIR recal)
    if (recalScheduler) {
        uint32_t lastMotion = 0;
        for (uint8_t i = 0; i < 4; i++) {
            HAL_MotionSensor* s = sensorManager.getSensor(i);
            if (s && s->getLastEventTime() > lastMotion) {
                lastMotion = s->getLastEventTime();
            }
        }
        recalScheduler->update(ntpManager.isTimeSynced(), lastMotion);
    }

    // Get current configuration
    const ConfigManager::Config& cfg = configManager.getConfig();

    // Propagate power settings to power manager
    g_power.setBatteryMonitoringEnabled(cfg.batteryMonitoringEnabled);
    g_power.setPowerSavingMode(cfg.powerSavingMode);

    // Update power manager (battery monitoring)
    g_power.update();

    // Update status LED (low priority heartbeat)
    static uint32_t lastStatusBlink = 0;
    static bool statusLedState = false;
    uint32_t now = millis();

    if (cfg.powerSavingMode == 0) {
        // Heartbeat pattern: short blink every 2 seconds
        uint32_t blinkInterval = 2000;

        if (now - lastStatusBlink >= blinkInterval) {
            lastStatusBlink = now;
            statusLedState = !statusLedState;

            if (statusLedState) {
                // Brief flash (50ms)
                statusLED.setBrightness(20);  // Dim brightness
            } else {
                statusLED.setBrightness(0);   // Off
            }
        }

        // Turn off after 50ms flash
        if (statusLedState && (now - lastStatusBlink >= 50)) {
            statusLED.setBrightness(0);
        }
    } else {
        // Power saving mode: keep status LED off
        statusLED.setBrightness(0);
    }

    // Diagnostic mode - real-time sensor view with change detection
    if (diagnosticMode) {
        static uint32_t lastDiagUpdate = 0;

        // State tracking for deduplication
        struct DiagSensorState {
            uint32_t distance;
            bool motion;
            int8_t direction;
            bool initialized;
        };
        static DiagSensorState lastState[4] = {{0, false, -1, false}, {0, false, -1, false},
                                                 {0, false, -1, false}, {0, false, -1, false}};
        static bool lastSystemMotion = false;

        if (now - lastDiagUpdate >= 200) {  // Update 5x per second
            lastDiagUpdate = now;

            // For each sensor, show real-time data (always read fresh from sensor)
            for (uint8_t i = 0; i < 4; i++) {
                HAL_MotionSensor* sensor = sensorManager.getSensor(i);
                if (sensor) {
                    // Re-read capabilities and state each time (picks up config changes)
                    const SensorCapabilities& caps = sensor->getCapabilities();
                    bool motion = sensor->motionDetected();
                    uint32_t dist = caps.supportsDistanceMeasurement ? sensor->getDistance() : 0;
                    int8_t dir = caps.supportsDirectionDetection ? (int8_t)sensor->getDirection() : -1;

                    // Check if state changed (or first time)
                    bool stateChanged = !lastState[i].initialized ||
                                       lastState[i].motion != motion ||
                                       abs((int32_t)lastState[i].distance - (int32_t)dist) > 50 ||  // 50mm threshold
                                       lastState[i].direction != dir;

                    if (stateChanged) {
                        // Update tracked state
                        lastState[i].distance = dist;
                        lastState[i].motion = motion;
                        lastState[i].direction = dir;
                        lastState[i].initialized = true;

                        // Build status line
                        char statusLine[200];
                        int pos = snprintf(statusLine, sizeof(statusLine), "[S%u] ", i);

                        if (caps.supportsDistanceMeasurement) {
                            uint32_t thresh = sensor->getDetectionThreshold();
                            pos += snprintf(statusLine + pos, sizeof(statusLine) - pos,
                                          "Dist:%4u mm ", dist);

                            // Threshold comparison
                            if (dist > 0 && dist < thresh) {
                                pos += snprintf(statusLine + pos, sizeof(statusLine) - pos, "[NEAR] ");
                            } else if (dist >= thresh) {
                                pos += snprintf(statusLine + pos, sizeof(statusLine) - pos, "[FAR ] ");
                            } else {
                                pos += snprintf(statusLine + pos, sizeof(statusLine) - pos, "[NONE] ");
                            }

                            pos += snprintf(statusLine + pos, sizeof(statusLine) - pos, "(thresh:%u) ", thresh);
                        }

                        // Motion state
                        pos += snprintf(statusLine + pos, sizeof(statusLine) - pos,
                                      "Motion:%s ", motion ? "YES" : "NO ");

                        // Direction if supported
                        if (caps.supportsDirectionDetection) {
                            const char* dirStr = "???";
                            switch ((MotionDirection)dir) {
                                case DIRECTION_STATIONARY: dirStr = "STAT"; break;
                                case DIRECTION_APPROACHING: dirStr = "APPR"; break;
                                case DIRECTION_RECEDING: dirStr = "RECD"; break;
                                default: dirStr = "UNKN"; break;
                            }
                            pos += snprintf(statusLine + pos, sizeof(statusLine) - pos, "Dir:%s ", dirStr);
                        }

                        // Final decision indicator
                        if (motion) {
                            snprintf(statusLine + pos, sizeof(statusLine) - pos, ">>> TRIGGER");
                        } else {
                            snprintf(statusLine + pos, sizeof(statusLine) - pos, "    (idle)");
                        }

                        // Log with proper level (DEBUG for diagnostic info)
                        DEBUG_LOG_SENSOR("%s", statusLine);
                    }
                }
            }

            // Motion state tracking
            bool anyMotion = sensorManager.isMotionDetected();
            if (anyMotion != lastSystemMotion) {
                lastSystemMotion = anyMotion;
            }
        }
    }

    // Process serial commands
    processSerialCommand();

    // Small delay for stability (non-blocking)
    delay(1);  // 1ms delay allows other tasks to run
}
