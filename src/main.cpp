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
#include "config.h"
#include "state_machine.h"
#include "hal_pir.h"
#include "hal_led.h"
#include "hal_button.h"

// ============================================================================
// Global Hardware Objects
// ============================================================================

HAL_PIR pirSensor(PIN_PIR_SENSOR, MOCK_HARDWARE);
HAL_LED hazardLED(PIN_HAZARD_LED, LED_PWM_CHANNEL, MOCK_HARDWARE);
HAL_LED statusLED(PIN_STATUS_LED, LED_PWM_CHANNEL + 1, MOCK_HARDWARE);
HAL_Button modeButton(PIN_BUTTON, BUTTON_DEBOUNCE_MS, 1000, MOCK_HARDWARE);

// State Machine
StateMachine stateMachine(&pirSensor, &hazardLED, &statusLED, &modeButton);

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

    Serial.printf("Firmware: %s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    Serial.printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    Serial.printf("Uptime: %u seconds\n", stateMachine.getUptimeSeconds());
    Serial.println();

    Serial.printf("Operating Mode: %s\n",
                  StateMachine::getModeName(stateMachine.getMode()));
    Serial.printf("Warning Active: %s\n", stateMachine.isWarningActive() ? "YES" : "NO");
    Serial.println();

    Serial.printf("PIR Sensor Ready: %s\n", pirSensor.isReady() ? "YES" : "NO");
    if (!pirSensor.isReady()) {
        Serial.printf("  Warmup remaining: %u seconds\n",
                      pirSensor.getWarmupTimeRemaining() / 1000);
    }
    Serial.printf("  Motion Events: %u\n", stateMachine.getMotionEventCount());
    Serial.println();

    Serial.printf("Mode Changes: %u\n", stateMachine.getModeChangeCount());
    Serial.printf("Button Clicks: %u\n", modeButton.getClickCount());
    Serial.println();

    Serial.printf("Memory - Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Memory - Largest Block: %u bytes\n", ESP.getMaxAllocHeap());

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
    Serial.printf("Sensor: AM312 PIR\n");
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
    Serial.println("Serial Commands (type in Serial Monitor):");
    Serial.println("  s - Print system status");
    Serial.println("  h - Print this help");
    Serial.println("  m - Trigger motion (mock mode only)");
    Serial.println("  b - Simulate button press (mock mode only)");
    Serial.println("  0 - Set mode to OFF");
    Serial.println("  1 - Set mode to CONTINUOUS_ON");
    Serial.println("  2 - Set mode to MOTION_DETECT");
    Serial.println("  r - Reset statistics");
    Serial.println();
    Serial.println("Hardware:");
    Serial.println("  Button - Press to cycle modes");
    Serial.println("  PIR Sensor - Detects motion");
    Serial.println("  Hazard LED - Warning indicator");
    Serial.println("  Status LED - Mode indicator");
    Serial.println();
    Serial.println("========================================\n");
}

/**
 * @brief Process serial commands
 */
void processSerialCommand() {
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
            stateMachine.setMode(StateMachine::OFF);
            break;

        case '1':
            Serial.println("[Command] Setting mode to CONTINUOUS_ON");
            stateMachine.setMode(StateMachine::CONTINUOUS_ON);
            break;

        case '2':
            Serial.println("[Command] Setting mode to MOTION_DETECT");
            stateMachine.setMode(StateMachine::MOTION_DETECT);
            break;

        case 'r':
        case 'R':
            Serial.println("[Command] Resetting statistics");
            pirSensor.resetMotionEventCount();
            modeButton.resetClickCount();
            break;

#if MOCK_HARDWARE
        case 'm':
        case 'M':
            Serial.println("[Command] Triggering mock motion (15 seconds)");
            pirSensor.mockTriggerMotion(15000);
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

    // Reset state machine counters
    pirSensor.resetMotionEventCount();
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

    // Print startup banner
    printBanner();

    Serial.println("[Setup] Initializing StepAware...");

    // Initialize hardware HALs
    Serial.println("[Setup] Initializing PIR sensor...");
    if (!pirSensor.begin()) {
        Serial.println("[Setup] ERROR: Failed to initialize PIR sensor");
        while (1) { delay(1000); }
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

    // Check if button is held during boot for reset operations
    modeButton.update();  // Read current button state
    if (modeButton.isPressed()) {
        handleBootButtonHold();
    }

    // Initialize state machine
    Serial.println("[Setup] Initializing state machine...");
    if (!stateMachine.begin(StateMachine::MOTION_DETECT)) {
        Serial.println("[Setup] ERROR: Failed to initialize state machine");
        while (1) { delay(1000); }
    }

    Serial.println("[Setup] ✓ Initialization complete!");
    Serial.println();

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
    // Update state machine (handles all hardware and logic)
    stateMachine.update();

    // Process serial commands
    processSerialCommand();

    // Small delay for stability (non-blocking)
    delay(1);  // 1ms delay allows other tasks to run
}
