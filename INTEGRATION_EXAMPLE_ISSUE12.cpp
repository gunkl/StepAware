/**
 * @file INTEGRATION_EXAMPLE_ISSUE12.cpp
 * @brief Integration example for 8x8 LED Matrix support (Issue #12 Phase 1)
 *
 * This file shows how to integrate the HAL_LEDMatrix_8x8 class into main.cpp
 * to enable LED matrix display functionality with ConfigManager-based configuration.
 *
 * Integration Steps:
 * 1. Add include for hal_ledmatrix_8x8.h
 * 2. Declare global display pointers
 * 3. Initialize display in setup() based on config
 * 4. Update display in loop()
 * 5. Use display in state machine events
 *
 * Date: 2026-01-19
 */

// =============================================================================
// STEP 1: Add Includes at Top of main.cpp
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "logger.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "web_api.h"
#include "hal_led.h"
#include "hal_pir.h"
#include "hal_button.h"
#include "hal_ledmatrix_8x8.h"  // <-- NEW: Add this include

// =============================================================================
// STEP 2: Declare Global Display Pointers
// =============================================================================

// Global component instances
ConfigManager configMgr;
Logger g_logger;
StateMachine stateMachine;
WiFiManager wifiManager;
AsyncWebServer server(80);
WebAPI webAPI(&server, &stateMachine, &configMgr);

// Hardware components
HAL_LED* statusLED = nullptr;
HAL_LED* hazardLED = nullptr;
HAL_PIR* pirSensor = nullptr;
HAL_Button* modeButton = nullptr;

// Display components (NEW)
HAL_LEDMatrix_8x8* ledMatrix = nullptr;  // <-- NEW: Matrix display pointer

// =============================================================================
// STEP 3: Initialize Display in setup()
// =============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(100);

    // Initialize logger
    g_logger.begin(Logger::LEVEL_DEBUG, true, false);
    LOG_INFO("===========================================");
    LOG_INFO("StepAware Starting...");
    LOG_INFO("Build: %s %s", BUILD_DATE, BUILD_TIME);
    LOG_INFO("===========================================");

    // Initialize config manager
    if (!configMgr.begin()) {
        LOG_ERROR("Config manager initialization failed!");
    }

    // Load configuration
    const ConfigManager::Config& config = configMgr.getConfig();

    // Initialize status LED (always present)
    statusLED = new HAL_LED(PIN_STATUS_LED, 1, MOCK_HARDWARE);
    if (!statusLED->begin()) {
        LOG_ERROR("Failed to initialize status LED");
    }

    // Initialize display based on configuration
    // NEW: Check if LED matrix is configured and enabled
    const ConfigManager::DisplaySlotConfig& displayCfg = config.displays[0];

    if (displayCfg.active && displayCfg.enabled &&
        displayCfg.type == DISPLAY_TYPE_MATRIX_8X8) {

        LOG_INFO("Initializing 8x8 LED Matrix...");
        LOG_INFO("  I2C Address: 0x%02X", displayCfg.i2cAddress);
        LOG_INFO("  SDA Pin: GPIO %d", displayCfg.sdaPin);
        LOG_INFO("  SCL Pin: GPIO %d", displayCfg.sclPin);
        LOG_INFO("  Brightness: %d/15", displayCfg.brightness);
        LOG_INFO("  Rotation: %d°", displayCfg.rotation * 90);

        // Create LED matrix instance
        ledMatrix = new HAL_LEDMatrix_8x8(
            displayCfg.i2cAddress,
            displayCfg.sdaPin,
            displayCfg.sclPin,
            MOCK_HARDWARE
        );

        // Initialize LED matrix
        if (ledMatrix->begin()) {
            // Apply configuration settings
            ledMatrix->setBrightness(displayCfg.brightness);
            ledMatrix->setRotation(displayCfg.rotation);

            // Show boot animation
            ledMatrix->startAnimation(
                HAL_LEDMatrix_8x8::ANIM_BOOT_STATUS,
                MATRIX_BOOT_DISPLAY_MS
            );

            LOG_INFO("LED Matrix initialized successfully");
            statusLED->startPattern(HAL_LED::PATTERN_BLINK_FAST, 500);
        } else {
            // Matrix initialization failed - fall back to hazard LED
            LOG_ERROR("LED Matrix initialization failed, falling back to LED");
            delete ledMatrix;
            ledMatrix = nullptr;

            // Initialize fallback hazard LED
            hazardLED = new HAL_LED(PIN_HAZARD_LED, LED_PWM_CHANNEL, MOCK_HARDWARE);
            if (!hazardLED->begin()) {
                LOG_ERROR("Failed to initialize hazard LED");
            }
        }
    } else {
        // No matrix configured - use standard hazard LED
        LOG_INFO("LED Matrix not configured, using single hazard LED");
        hazardLED = new HAL_LED(PIN_HAZARD_LED, LED_PWM_CHANNEL, MOCK_HARDWARE);
        if (!hazardLED->begin()) {
            LOG_ERROR("Failed to initialize hazard LED");
        }
    }

    // Initialize PIR sensor
    const ConfigManager::SensorSlotConfig& sensorCfg = config.sensors[0];
    if (sensorCfg.active && sensorCfg.enabled && sensorCfg.type == SENSOR_TYPE_PIR) {
        LOG_INFO("Initializing PIR sensor on GPIO %d", sensorCfg.primaryPin);

        SensorConfig pirConfig;
        pirConfig.pin = sensorCfg.primaryPin;
        pirConfig.debounceMs = sensorCfg.debounceMs;
        pirConfig.warmupMs = sensorCfg.warmupMs;

        pirSensor = new HAL_PIR(pirConfig, MOCK_HARDWARE);
        if (!pirSensor->begin()) {
            LOG_ERROR("Failed to initialize PIR sensor");
        }
    }

    // Initialize mode button
    modeButton = new HAL_Button(PIN_BUTTON, true, MOCK_HARDWARE);
    if (!modeButton->begin()) {
        LOG_ERROR("Failed to initialize mode button");
    }

    // Initialize state machine (pass display pointers)
    // Note: You may need to extend StateMachine to accept display pointers
    stateMachine.begin();

    // Initialize WiFi
    wifiManager.setCredentials(config.wifiSSID, config.wifiPassword);
    wifiManager.setDeviceName(config.deviceName);
    if (!wifiManager.begin()) {
        LOG_ERROR("WiFi initialization failed");
    }

    // Initialize Web API
    webAPI.begin();

    LOG_INFO("===========================================");
    LOG_INFO("Setup complete - entering main loop");
    LOG_INFO("===========================================");
}

// =============================================================================
// STEP 4: Update Display in loop()
// =============================================================================

void loop() {
    // Update all hardware components
    if (statusLED) statusLED->update();
    if (hazardLED) hazardLED->update();
    if (pirSensor) pirSensor->update();
    if (modeButton) modeButton->update();

    // NEW: Update LED matrix (handles animations)
    if (ledMatrix) {
        ledMatrix->update();
    }

    // Update WiFi
    wifiManager.update();

    // Update state machine
    stateMachine.update();

    // Check PIR sensor events
    if (pirSensor && pirSensor->motionDetected()) {
        LOG_INFO("Motion detected!");
        stateMachine.handleEvent(StateMachine::EVENT_MOTION_DETECTED);

        // Trigger warning display
        triggerWarningDisplay(config.motionWarningDuration);
    }

    if (pirSensor && pirSensor->motionCleared()) {
        LOG_INFO("Motion cleared");
        stateMachine.handleEvent(StateMachine::EVENT_MOTION_CLEARED);
    }

    // Check button events
    if (modeButton) {
        if (modeButton->wasPressed()) {
            LOG_INFO("Mode button pressed");
            // Cycle through modes or handle button logic
        }

        if (modeButton->wasLongPressed()) {
            LOG_INFO("Mode button long pressed");
            // Handle long press (e.g., WiFi reset)
        }
    }

    // Periodic tasks
    static uint32_t lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate >= 5000) {
        lastStatusUpdate = millis();
        LOG_DEBUG("System running - Free heap: %u bytes", ESP.getFreeHeap());
    }

    // Small delay to prevent watchdog issues
    delay(10);
}

// =============================================================================
// STEP 5: Helper Function for Display Abstraction
// =============================================================================

/**
 * @brief Trigger warning display on configured output device
 *
 * This function abstracts the display hardware - uses LED matrix if available,
 * otherwise falls back to single hazard LED.
 *
 * @param duration_ms Warning duration in milliseconds
 */
void triggerWarningDisplay(uint32_t duration_ms) {
    if (ledMatrix) {
        // Use LED matrix motion alert animation
        ledMatrix->startAnimation(
            HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT,
            duration_ms
        );
        LOG_INFO("Triggered matrix motion alert (duration: %u ms)", duration_ms);
    } else if (hazardLED) {
        // Fall back to single LED warning pattern
        hazardLED->startPattern(
            HAL_LED::PATTERN_BLINK_WARNING,
            duration_ms
        );
        LOG_INFO("Triggered LED warning (duration: %u ms)", duration_ms);
    }
}

/**
 * @brief Show battery status on display
 *
 * @param percentage Battery percentage (0-100)
 */
void showBatteryStatus(uint8_t percentage) {
    if (ledMatrix && percentage < 30) {
        // Show low battery animation on matrix
        ledMatrix->startAnimation(
            HAL_LEDMatrix_8x8::ANIM_BATTERY_LOW,
            2000
        );
        LOG_INFO("Showing battery low on matrix (%u%%)", percentage);
    } else if (hazardLED && percentage < 30) {
        // Blink LED slowly for low battery
        hazardLED->startPattern(HAL_LED::PATTERN_BLINK_SLOW, 2000);
        LOG_INFO("Showing battery low on LED (%u%%)", percentage);
    }
}

/**
 * @brief Stop all display animations
 */
void stopDisplayAnimations() {
    if (ledMatrix) {
        ledMatrix->stopAnimation();
    }
    if (hazardLED) {
        hazardLED->stopPattern();
    }
}

// =============================================================================
// ALTERNATIVE: StateMachine Integration
// =============================================================================

/**
 * If you prefer to integrate directly into StateMachine class:
 *
 * 1. Add display pointers to StateMachine private members:
 *    class StateMachine {
 *    private:
 *        HAL_LEDMatrix_8x8* m_ledMatrix;
 *        HAL_LED* m_hazardLED;
 *    };
 *
 * 2. Add setter methods:
 *    void setLEDMatrix(HAL_LEDMatrix_8x8* matrix) { m_ledMatrix = matrix; }
 *    void setHazardLED(HAL_LED* led) { m_hazardLED = led; }
 *
 * 3. Update state transition logic:
 *    void StateMachine::onMotionDetected() {
 *        if (m_ledMatrix) {
 *            m_ledMatrix->startAnimation(
 *                HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT,
 *                m_config.motionWarningDuration
 *            );
 *        } else if (m_hazardLED) {
 *            m_hazardLED->startPattern(
 *                HAL_LED::PATTERN_BLINK_WARNING,
 *                m_config.motionWarningDuration
 *            );
 *        }
 *    }
 *
 * 4. In main.cpp setup():
 *    stateMachine.setLEDMatrix(ledMatrix);
 *    stateMachine.setHazardLED(hazardLED);
 */

// =============================================================================
// CUSTOM PIXEL CONTROL EXAMPLE
// =============================================================================

/**
 * @brief Example: Draw custom pattern on LED matrix
 *
 * Shows how to draw custom frames directly to the matrix.
 * Useful for Phase 2 custom animations.
 */
void drawCustomPattern() {
    if (!ledMatrix) return;

    // Stop any running animation
    ledMatrix->stopAnimation();

    // Example: Draw heart symbol
    uint8_t heartPattern[] = {
        0b01100110,
        0b11111111,
        0b11111111,
        0b11111111,
        0b01111110,
        0b00111100,
        0b00011000,
        0b00000000
    };

    ledMatrix->drawFrame(heartPattern);
}

/**
 * @brief Example: Draw individual pixels
 */
void drawPixelExample() {
    if (!ledMatrix) return;

    ledMatrix->clear();

    // Draw a plus sign
    ledMatrix->setPixel(3, 3, true);  // Center
    ledMatrix->setPixel(2, 3, true);  // Left
    ledMatrix->setPixel(4, 3, true);  // Right
    ledMatrix->setPixel(3, 2, true);  // Top
    ledMatrix->setPixel(3, 4, true);  // Bottom
}

// =============================================================================
// CONFIGURATION UPDATE EXAMPLE
// =============================================================================

/**
 * @brief Example: Update matrix settings at runtime
 *
 * Shows how to respond to configuration changes from web UI.
 * Note: Currently requires restart to take effect.
 */
void onConfigurationChanged() {
    // Reload configuration
    const ConfigManager::Config& config = configMgr.getConfig();
    const ConfigManager::DisplaySlotConfig& displayCfg = config.displays[0];

    if (ledMatrix && displayCfg.active && displayCfg.enabled) {
        // Update brightness
        ledMatrix->setBrightness(displayCfg.brightness);

        // Update rotation
        ledMatrix->setRotation(displayCfg.rotation);

        LOG_INFO("Matrix settings updated: brightness=%d, rotation=%d",
                 displayCfg.brightness, displayCfg.rotation);
    }
}

// =============================================================================
// TESTING HELPERS
// =============================================================================

/**
 * @brief Test all matrix animations sequentially
 *
 * Useful for hardware validation after first flash.
 */
void testAllAnimations() {
    if (!ledMatrix) {
        LOG_WARN("No LED matrix available for testing");
        return;
    }

    LOG_INFO("Testing all LED matrix animations...");

    // Test 1: Motion alert
    LOG_INFO("Test 1/3: Motion Alert");
    ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_MOTION_ALERT, 2500);
    delay(3000);

    // Test 2: Battery low
    LOG_INFO("Test 2/3: Battery Low");
    ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_BATTERY_LOW, 2000);
    delay(2500);

    // Test 3: Boot status
    LOG_INFO("Test 3/3: Boot Status");
    ledMatrix->startAnimation(HAL_LEDMatrix_8x8::ANIM_BOOT_STATUS, 1000);
    delay(1500);

    ledMatrix->clear();
    LOG_INFO("Animation tests complete");
}

/**
 * @brief Test brightness levels
 */
void testBrightnessLevels() {
    if (!ledMatrix) return;

    LOG_INFO("Testing brightness levels...");

    uint8_t heartPattern[] = {
        0b01100110, 0b11111111, 0b11111111, 0b11111111,
        0b01111110, 0b00111100, 0b00011000, 0b00000000
    };

    for (uint8_t brightness = 0; brightness <= 15; brightness++) {
        ledMatrix->setBrightness(brightness);
        ledMatrix->drawFrame(heartPattern);
        LOG_INFO("Brightness: %d/15", brightness);
        delay(500);
    }

    ledMatrix->clear();
    LOG_INFO("Brightness test complete");
}

// =============================================================================
// NOTES AND BEST PRACTICES
// =============================================================================

/*
 * INTEGRATION CHECKLIST:
 *
 * 1. [x] Include hal_ledmatrix_8x8.h
 * 2. [x] Declare global ledMatrix pointer
 * 3. [x] Initialize in setup() based on config
 * 4. [x] Update in loop()
 * 5. [x] Create display abstraction helper
 * 6. [ ] Extend StateMachine if needed
 * 7. [ ] Add runtime config reload support
 * 8. [ ] Test all animations
 * 9. [ ] Document wiring in README
 * 10. [ ] Add troubleshooting guide
 *
 * POWER CONSUMPTION NOTES:
 * - Matrix idle: ~5mA
 * - Matrix animation: ~20-40mA
 * - Matrix full bright: up to 120mA
 * - Ensure power supply can handle peak current
 * - Consider battery impact for portable use
 *
 * PERFORMANCE NOTES:
 * - Update frequency: Call update() at least every 10-100ms
 * - Animation frame rate: Automatically managed
 * - I2C transaction time: ~1ms per update
 * - CPU overhead: Negligible (~50µs per frame)
 *
 * TROUBLESHOOTING:
 * - If matrix doesn't initialize: Check wiring and I2C address
 * - If animations don't play: Ensure update() is called in loop
 * - If display is dim: Check brightness setting (0-15)
 * - If display is rotated wrong: Adjust rotation (0-3)
 * - If I2C errors occur: Check pull-up resistors (may need 4.7kΩ)
 */

// =============================================================================
// END OF INTEGRATION EXAMPLE
// =============================================================================
