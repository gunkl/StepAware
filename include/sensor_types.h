#ifndef STEPAWARE_SENSOR_TYPES_H
#define STEPAWARE_SENSOR_TYPES_H

#include <stdint.h>

/**
 * @file sensor_types.h
 * @brief Common sensor type definitions and structures
 *
 * Defines the sensor abstraction types used throughout the StepAware system
 * to support multiple sensor types (PIR, IR, Ultrasonic, etc.)
 */

/**
 * @brief Supported sensor types
 */
enum SensorType {
    SENSOR_TYPE_PIR = 0,        ///< Passive Infrared (motion detection)
    SENSOR_TYPE_IR = 1,         ///< Infrared beam break sensor
    SENSOR_TYPE_ULTRASONIC = 2, ///< Ultrasonic distance sensor (HC-SR04, etc.)
    SENSOR_TYPE_PASSIVE_IR = 3, ///< Alternative passive IR implementation
    SENSOR_TYPE_COUNT           ///< Number of sensor types (for iteration)
};

/**
 * @brief Motion event types
 */
enum MotionEvent {
    MOTION_EVENT_NONE = 0,           ///< No event
    MOTION_EVENT_DETECTED,           ///< Motion detected (rising edge)
    MOTION_EVENT_CLEARED,            ///< Motion cleared (falling edge)
    MOTION_EVENT_THRESHOLD_CROSSED,  ///< Distance threshold crossed (for distance sensors)
    MOTION_EVENT_APPROACHING,        ///< Object approaching (direction detection)
    MOTION_EVENT_RECEDING            ///< Object receding (direction detection)
};

/**
 * @brief Direction of detected motion
 */
enum MotionDirection {
    DIRECTION_UNKNOWN = 0,    ///< Direction cannot be determined
    DIRECTION_STATIONARY = 1, ///< Object stationary
    DIRECTION_APPROACHING = 2,///< Object moving toward sensor
    DIRECTION_RECEDING = 3    ///< Object moving away from sensor
};

/**
 * @brief Sensor capabilities structure
 *
 * Describes what features a particular sensor supports.
 * Used to dynamically adapt behavior and UI based on sensor type.
 */
struct SensorCapabilities {
    bool supportsBinaryDetection;     ///< Simple motion yes/no detection
    bool supportsDistanceMeasurement; ///< Can measure distance in mm
    bool supportsDirectionDetection;  ///< Can detect approaching vs receding
    bool requiresWarmup;              ///< Needs warmup period before reliable readings
    bool supportsDeepSleepWake;       ///< Can wake device from deep sleep
    uint32_t minDetectionDistance;    ///< Minimum detection range (mm), 0 if N/A
    uint32_t maxDetectionDistance;    ///< Maximum detection range (mm), 0 if N/A
    uint16_t detectionAngleDegrees;   ///< Field of view in degrees, 0 if N/A
    uint16_t typicalWarmupMs;         ///< Typical warmup time in ms, 0 if none
    uint16_t typicalCurrentMa;        ///< Typical current consumption (mA)
    const char* sensorTypeName;       ///< Human-readable sensor type name
};

/**
 * @brief Sensor status structure
 *
 * Runtime status information for a sensor.
 */
struct SensorStatus {
    bool ready;                   ///< Sensor is ready for detection
    bool motionDetected;          ///< Current motion state
    uint32_t lastEventTime;       ///< Timestamp of last event (ms)
    uint32_t eventCount;          ///< Total events since reset
    uint32_t distance;            ///< Current distance reading (mm), 0 if N/A
    MotionDirection direction;    ///< Current direction, UNKNOWN if N/A
    MotionEvent lastEvent;        ///< Last event type
};

/**
 * @brief Sensor configuration structure
 *
 * Runtime-configurable sensor parameters.
 */
struct SensorConfig {
    SensorType type;              ///< Sensor type
    uint8_t primaryPin;           ///< Primary GPIO pin (motion/trigger)
    uint8_t secondaryPin;         ///< Secondary GPIO pin (echo for ultrasonic)
    uint32_t detectionThreshold;  ///< Distance threshold for detection (mm)
    uint32_t debounceMs;          ///< Debounce time (ms)
    uint32_t warmupMs;            ///< Warmup time override (ms), 0 = use default
    bool enableDirectionDetection;///< Enable direction detection if supported
    bool invertLogic;             ///< Invert detection logic (active low)
};

/**
 * @brief Get default capabilities for a sensor type
 *
 * @param type Sensor type
 * @return Default capabilities for the sensor type
 */
inline SensorCapabilities getDefaultCapabilities(SensorType type) {
    SensorCapabilities caps = {};

    switch (type) {
        case SENSOR_TYPE_PIR:
            caps.supportsBinaryDetection = true;
            caps.supportsDistanceMeasurement = false;
            caps.supportsDirectionDetection = false;
            caps.requiresWarmup = true;
            caps.supportsDeepSleepWake = true;
            caps.minDetectionDistance = 0;
            caps.maxDetectionDistance = 7000;  // ~7m typical
            caps.detectionAngleDegrees = 120;  // ~120 degree FOV
            caps.typicalWarmupMs = 60000;      // 60 seconds
            caps.typicalCurrentMa = 1;         // ~65uA typical, round up
            caps.sensorTypeName = "PIR Motion Sensor";
            break;

        case SENSOR_TYPE_IR:
            caps.supportsBinaryDetection = true;
            caps.supportsDistanceMeasurement = false;
            caps.supportsDirectionDetection = false;
            caps.requiresWarmup = false;
            caps.supportsDeepSleepWake = true;
            caps.minDetectionDistance = 0;
            caps.maxDetectionDistance = 500;   // ~50cm typical
            caps.detectionAngleDegrees = 35;   // Narrow beam
            caps.typicalWarmupMs = 0;
            caps.typicalCurrentMa = 5;         // ~5mA active
            caps.sensorTypeName = "IR Beam Sensor";
            break;

        case SENSOR_TYPE_ULTRASONIC:
            caps.supportsBinaryDetection = true;
            caps.supportsDistanceMeasurement = true;
            caps.supportsDirectionDetection = true;
            caps.requiresWarmup = false;
            caps.supportsDeepSleepWake = false; // Requires active measurement
            caps.minDetectionDistance = 20;     // 2cm minimum
            caps.maxDetectionDistance = 4000;   // 4m maximum
            caps.detectionAngleDegrees = 15;    // Narrow cone
            caps.typicalWarmupMs = 0;
            caps.typicalCurrentMa = 15;         // ~15mA during measurement
            caps.sensorTypeName = "Ultrasonic Distance Sensor";
            break;

        case SENSOR_TYPE_PASSIVE_IR:
            caps.supportsBinaryDetection = true;
            caps.supportsDistanceMeasurement = false;
            caps.supportsDirectionDetection = false;
            caps.requiresWarmup = true;
            caps.supportsDeepSleepWake = true;
            caps.minDetectionDistance = 0;
            caps.maxDetectionDistance = 5000;   // ~5m typical
            caps.detectionAngleDegrees = 100;
            caps.typicalWarmupMs = 30000;       // 30 seconds
            caps.typicalCurrentMa = 1;
            caps.sensorTypeName = "Passive IR Sensor";
            break;

        default:
            caps.sensorTypeName = "Unknown Sensor";
            break;
    }

    return caps;
}

/**
 * @brief Get sensor type name as string
 *
 * @param type Sensor type
 * @return Human-readable name
 */
inline const char* getSensorTypeName(SensorType type) {
    switch (type) {
        case SENSOR_TYPE_PIR: return "PIR";
        case SENSOR_TYPE_IR: return "IR";
        case SENSOR_TYPE_ULTRASONIC: return "Ultrasonic";
        case SENSOR_TYPE_PASSIVE_IR: return "Passive IR";
        default: return "Unknown";
    }
}

#endif // STEPAWARE_SENSOR_TYPES_H
