/**
 * @file multi_sensor_example.cpp
 * @brief Example demonstrating multi-sensor usage with SensorManager
 *
 * This example shows how to:
 * 1. Set up multiple sensors (PIR + Ultrasonic)
 * 2. Use TRIGGER_MEASURE fusion mode for power efficiency
 * 3. Detect approaching motion with distance measurement
 * 4. Handle direction-aware motion detection
 *
 * Hardware Requirements:
 * - ESP32-C3 (or compatible)
 * - PIR motion sensor on GPIO 6
 * - HC-SR04 ultrasonic sensor (Trigger: GPIO 12, Echo: GPIO 14)
 *
 * Power Consumption:
 * - PIR only: ~65µA
 * - PIR + Ultrasonic (when triggered): ~15mA
 * - Average (assuming 10% trigger time): ~1.5mA
 *
 * Author: StepAware Project
 * License: MIT
 */

#include <Arduino.h>
#include "sensor_manager.h"

// Pin definitions
#define PIN_PIR_SENSOR      6  // Note: Avoid GPIO5 - it can interfere with programming
#define PIN_ULTRASONIC_TRIG 12
#define PIN_ULTRASONIC_ECHO 14
#define PIN_LED             2   // Built-in LED for visual feedback

// Detection parameters
#define PROXIMITY_THRESHOLD_MM  300   // 30cm - trigger warning
#define MAX_RANGE_MM           2000   // 2m - maximum detection range

// Global sensor manager
SensorManager sensorMgr;

// Status tracking
uint32_t lastMotionTime = 0;
uint32_t motionCount = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);  // Wait for serial

    Serial.println("\n\n===========================================");
    Serial.println("  StepAware Multi-Sensor Example");
    Serial.println("  PIR Trigger + Ultrasonic Measurement");
    Serial.println("===========================================\n");

    // Initialize LED
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Initialize sensor manager
    Serial.println("[Setup] Initializing SensorManager...");
    if (!sensorMgr.begin()) {
        Serial.println("[ERROR] Failed to initialize SensorManager");
        while(1) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            delay(200);
        }
    }

    // =========================================================================
    // Sensor 1: PIR Motion Sensor (Primary - Trigger)
    // =========================================================================
    Serial.println("[Setup] Adding PIR sensor (Trigger)...");

    SensorConfig pirConfig;
    pirConfig.type = SENSOR_TYPE_PIR;
    pirConfig.primaryPin = PIN_PIR_SENSOR;
    pirConfig.warmupMs = 60000;  // 60 second warmup
    pirConfig.debounceMs = 100;  // 100ms debounce

    if (!sensorMgr.addSensor(0, pirConfig, "PIR Trigger", true, false)) {
        Serial.printf("[ERROR] Failed to add PIR: %s\n", sensorMgr.getLastError());
        while(1) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            delay(100);
        }
    }
    Serial.println("[Setup] ✓ PIR sensor added");

    // =========================================================================
    // Sensor 2: Ultrasonic Distance Sensor (Secondary - Measurement)
    // =========================================================================
    Serial.println("[Setup] Adding Ultrasonic sensor (Measurement)...");

    SensorConfig usConfig;
    usConfig.type = SENSOR_TYPE_ULTRASONIC;
    usConfig.primaryPin = PIN_ULTRASONIC_TRIG;
    usConfig.secondaryPin = PIN_ULTRASONIC_ECHO;
    usConfig.detectionThreshold = PROXIMITY_THRESHOLD_MM;
    usConfig.enableDirectionDetection = true;  // Enable direction sensing
    usConfig.debounceMs = 50;

    if (!sensorMgr.addSensor(1, usConfig, "Ultrasonic Distance", false, false)) {
        Serial.printf("[ERROR] Failed to add Ultrasonic: %s\n", sensorMgr.getLastError());
        while(1) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            delay(100);
        }
    }
    Serial.println("[Setup] ✓ Ultrasonic sensor added");

    // =========================================================================
    // Configure Fusion Mode
    // =========================================================================
    Serial.println("[Setup] Setting TRIGGER_MEASURE fusion mode...");
    sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);

    // Validate configuration
    if (!sensorMgr.validateConfiguration()) {
        Serial.printf("[ERROR] Invalid configuration: %s\n", sensorMgr.getLastError());
        while(1) delay(1000);
    }
    Serial.println("[Setup] ✓ Configuration validated");

    // Print sensor status
    Serial.println("\n[Setup] Initial sensor status:");
    sensorMgr.printStatus();

    Serial.println("[Setup] ✓ Initialization complete!");
    Serial.println("\n--- System Ready ---");
    Serial.println("Waiting for PIR warmup and motion detection...\n");

    // Flash LED to indicate ready
    for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(100);
        digitalWrite(PIN_LED, LOW);
        delay(100);
    }
}

void loop() {
    // Update all sensors
    sensorMgr.update();

    // Get combined sensor status
    CombinedSensorStatus status = sensorMgr.getStatus();

    // Check if all sensors are ready (PIR warmup complete)
    static bool sensorsReady = false;
    if (!sensorsReady && sensorMgr.allSensorsReady()) {
        sensorsReady = true;
        Serial.println("\n✓ All sensors ready - PIR warmup complete\n");
    }

    // Motion detection logic (uses fusion mode)
    if (sensorMgr.isMotionDetected()) {
        // PIR triggered - get distance measurement
        uint32_t distance = status.nearestDistance;
        MotionDirection direction = status.primaryDirection;

        // Only process if within range
        if (distance > 0 && distance < MAX_RANGE_MM) {
            uint32_t now = millis();

            // Debounce - only report every 2 seconds
            if (now - lastMotionTime > 2000) {
                lastMotionTime = now;
                motionCount++;

                Serial.println("┌─────────────────────────────────────");
                Serial.printf("│ Motion Event #%u\n", motionCount);
                Serial.println("├─────────────────────────────────────");
                Serial.printf("│ Distance: %u mm (%.1f cm)\n", distance, distance / 10.0f);

                // Direction analysis
                Serial.print("│ Direction: ");
                switch (direction) {
                    case DIRECTION_APPROACHING:
                        Serial.println("APPROACHING ⬇");
                        break;
                    case DIRECTION_RECEDING:
                        Serial.println("RECEDING ⬆");
                        break;
                    case DIRECTION_STATIONARY:
                        Serial.println("STATIONARY ●");
                        break;
                    default:
                        Serial.println("UNKNOWN ?");
                        break;
                }

                // Proximity warning
                if (distance < PROXIMITY_THRESHOLD_MM) {
                    Serial.printf("│ ⚠️  WARNING: Within threshold (<%u mm)\n",
                                PROXIMITY_THRESHOLD_MM);

                    if (direction == DIRECTION_APPROACHING) {
                        Serial.println("│ 🚨 ALERT: Person approaching!");
                        // Activate hazard warning here
                        digitalWrite(PIN_LED, HIGH);
                    }
                } else {
                    Serial.println("│ ✓ Safe distance");
                    digitalWrite(PIN_LED, LOW);
                }

                Serial.printf("│ Detecting sensors: %u / %u\n",
                            status.detectingSensorCount,
                            status.activeSensorCount);
                Serial.printf("│ Total events: %u\n", status.combinedEventCount);
                Serial.println("└─────────────────────────────────────\n");
            }

            // Keep LED on while motion detected and close
            if (distance < PROXIMITY_THRESHOLD_MM) {
                digitalWrite(PIN_LED, HIGH);
            }
        }
    } else {
        // No motion detected - turn off LED
        digitalWrite(PIN_LED, LOW);
    }

    // Status update every 30 seconds
    static uint32_t lastStatusPrint = 0;
    uint32_t now = millis();
    if (now - lastStatusPrint > 30000) {
        lastStatusPrint = now;

        Serial.println("\n--- Periodic Status Update ---");
        Serial.printf("Uptime: %u seconds\n", now / 1000);
        Serial.printf("Motion events: %u\n", motionCount);
        Serial.printf("Active sensors: %u\n", status.activeSensorCount);

        if (status.anyMotionDetected) {
            Serial.printf("Current distance: %u mm\n", status.nearestDistance);
        } else {
            Serial.println("Status: Idle");
        }
        Serial.println("------------------------------\n");
    }

    // Small delay for stability
    delay(10);
}

/**
 * @brief Print detailed sensor information
 *
 * This function can be called from serial commands or periodically
 * to get a full sensor status report.
 */
void printDetailedStatus() {
    Serial.println("\n========================================");
    Serial.println("  Detailed Sensor Status");
    Serial.println("========================================");

    sensorMgr.printStatus();

    // Individual sensor details
    HAL_MotionSensor* pir = sensorMgr.getSensor(0);
    if (pir) {
        Serial.println("\nPIR Sensor Details:");
        const SensorCapabilities& caps = pir->getCapabilities();
        Serial.printf("  Type: %s\n", caps.sensorTypeName);
        Serial.printf("  Ready: %s\n", pir->isReady() ? "YES" : "NO");
        Serial.printf("  Motion: %s\n", pir->motionDetected() ? "YES" : "NO");
        Serial.printf("  Events: %u\n", pir->getEventCount());

        if (!pir->isReady()) {
            uint32_t remaining = pir->getWarmupTimeRemaining();
            Serial.printf("  Warmup remaining: %u seconds\n", remaining / 1000);
        }
    }

    HAL_MotionSensor* ultrasonic = sensorMgr.getSensor(1);
    if (ultrasonic) {
        Serial.println("\nUltrasonic Sensor Details:");
        const SensorCapabilities& caps = ultrasonic->getCapabilities();
        Serial.printf("  Type: %s\n", caps.sensorTypeName);
        Serial.printf("  Ready: %s\n", ultrasonic->isReady() ? "YES" : "NO");
        Serial.printf("  Motion: %s\n", ultrasonic->motionDetected() ? "YES" : "NO");
        Serial.printf("  Distance: %u mm\n", ultrasonic->getDistance());
        Serial.printf("  Threshold: %u mm\n", ultrasonic->getDetectionThreshold());

        MotionDirection dir = ultrasonic->getDirection();
        Serial.print("  Direction: ");
        switch (dir) {
            case DIRECTION_APPROACHING: Serial.println("Approaching"); break;
            case DIRECTION_RECEDING: Serial.println("Receding"); break;
            case DIRECTION_STATIONARY: Serial.println("Stationary"); break;
            default: Serial.println("Unknown"); break;
        }

        Serial.printf("  Events: %u\n", ultrasonic->getEventCount());
    }

    Serial.println("========================================\n");
}

/**
 * @brief Example of runtime sensor configuration changes
 *
 * This demonstrates how to modify sensor settings at runtime.
 */
void changeSensorSettings() {
    Serial.println("\n[Example] Changing sensor settings...");

    HAL_MotionSensor* ultrasonic = sensorMgr.getSensor(1);
    if (ultrasonic) {
        // Change detection threshold
        ultrasonic->setDetectionThreshold(500);  // Change to 50cm
        Serial.println("[Example] Ultrasonic threshold changed to 500mm");

        // Disable temporarily
        sensorMgr.setSensorEnabled(1, false);
        Serial.println("[Example] Ultrasonic sensor disabled");

        delay(5000);

        // Re-enable
        sensorMgr.setSensorEnabled(1, true);
        Serial.println("[Example] Ultrasonic sensor re-enabled");
    }
}

/**
 * @brief Example of changing fusion mode at runtime
 */
void changeFusionMode() {
    Serial.println("\n[Example] Changing fusion mode...");

    // Switch to ANY mode (more sensitive)
    sensorMgr.setFusionMode(FUSION_MODE_ANY);
    Serial.println("[Example] Fusion mode: ANY (either sensor triggers)");

    delay(10000);

    // Switch back to TRIGGER_MEASURE (power efficient)
    sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
    Serial.println("[Example] Fusion mode: TRIGGER_MEASURE (PIR triggers, US measures)");
}
