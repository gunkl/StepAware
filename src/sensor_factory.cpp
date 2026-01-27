#include "sensor_factory.h"
#include "config.h"

HAL_MotionSensor* SensorFactory::create(const SensorConfig& config, bool mockMode) {
    switch (config.type) {
        case SENSOR_TYPE_PIR:
        case SENSOR_TYPE_PASSIVE_IR:
            return createPIR(config.primaryPin, mockMode);

        case SENSOR_TYPE_ULTRASONIC: {
            HAL_Ultrasonic* sensor = new HAL_Ultrasonic(
                config.primaryPin,
                config.secondaryPin,
                mockMode
            );

            // Apply configuration
            if (config.detectionThreshold > 0) {
                sensor->setDetectionThreshold(config.detectionThreshold);
            }
            if (config.debounceMs > 0) {
                sensor->setMeasurementInterval(config.debounceMs);
            }
            if (config.sampleWindowSize > 0) {
                sensor->setSampleWindowSize(config.sampleWindowSize);
            }
            sensor->setDirectionDetection(config.enableDirectionDetection);
            sensor->setDirectionTriggerMode(config.directionTriggerMode);

            return sensor;
        }

        case SENSOR_TYPE_ULTRASONIC_GROVE: {
            HAL_Ultrasonic_Grove* sensor = new HAL_Ultrasonic_Grove(
                config.primaryPin,
                mockMode
            );

            // Apply configuration
            if (config.detectionThreshold > 0) {
                sensor->setDetectionThreshold(config.detectionThreshold);
            }
            if (config.debounceMs > 0) {
                sensor->setMeasurementInterval(config.debounceMs);
            }
            if (config.sampleWindowSize > 0) {
                sensor->setSampleWindowSize(config.sampleWindowSize);
            }
            sensor->setDirectionDetection(config.enableDirectionDetection);
            sensor->setDirectionTriggerMode(config.directionTriggerMode);

            return sensor;
        }

        case SENSOR_TYPE_IR:
            // IR sensor not yet implemented, fall through to default
            DEBUG_PRINTLN("[SensorFactory] IR sensor type not yet implemented");
            return nullptr;

        default:
            DEBUG_PRINTF("[SensorFactory] Unknown sensor type: %d\n", config.type);
            return nullptr;
    }
}

HAL_MotionSensor* SensorFactory::createPIR(uint8_t pin, bool mockMode) {
    DEBUG_PRINTF("[SensorFactory] Creating PIR sensor on pin %d (mock: %s)\n",
                 pin, mockMode ? "yes" : "no");
    return new HAL_PIR(pin, mockMode);
}

HAL_MotionSensor* SensorFactory::createUltrasonic(uint8_t triggerPin, uint8_t echoPin,
                                                   bool mockMode) {
    DEBUG_PRINTF("[SensorFactory] Creating HC-SR04 Ultrasonic sensor (trigger: %d, echo: %d, mock: %s)\n",
                 triggerPin, echoPin, mockMode ? "yes" : "no");
    return new HAL_Ultrasonic(triggerPin, echoPin, mockMode);
}

HAL_MotionSensor* SensorFactory::createUltrasonicGrove(uint8_t sigPin, bool mockMode) {
    DEBUG_PRINTF("[SensorFactory] Creating Grove Ultrasonic sensor (sig: %d, mock: %s)\n",
                 sigPin, mockMode ? "yes" : "no");
    return new HAL_Ultrasonic_Grove(sigPin, mockMode);
}

HAL_MotionSensor* SensorFactory::createFromType(SensorType type, bool mockMode) {
    SensorConfig config = getDefaultConfig(type);
    return create(config, mockMode);
}

SensorConfig SensorFactory::getDefaultConfig(SensorType type) {
    SensorConfig config = {};
    config.type = type;
    config.debounceMs = 50;
    config.warmupMs = 0;
    config.enableDirectionDetection = false;
    config.invertLogic = false;

    switch (type) {
        case SENSOR_TYPE_PIR:
        case SENSOR_TYPE_PASSIVE_IR:
            config.primaryPin = PIN_PIR_SENSOR;
            config.secondaryPin = 0;
            config.detectionThreshold = 0;  // Not applicable for PIR
            config.warmupMs = PIR_WARMUP_TIME_MS;
            break;

        case SENSOR_TYPE_ULTRASONIC:
            config.primaryPin = PIN_ULTRASONIC_TRIGGER;
            config.secondaryPin = PIN_ULTRASONIC_ECHO;
            config.detectionThreshold = 500;  // 50cm default
            config.enableDirectionDetection = true;
            config.debounceMs = 60;  // Min measurement interval
            break;

        case SENSOR_TYPE_ULTRASONIC_GROVE:
            config.primaryPin = PIN_ULTRASONIC_TRIGGER;  // Use same pin as trigger (single pin)
            config.secondaryPin = 0;  // Not used for Grove (single-pin sensor)
            config.detectionThreshold = 1200;  // 120cm default
            config.enableDirectionDetection = true;
            config.debounceMs = 60;  // Min measurement interval
            break;

        case SENSOR_TYPE_IR:
            config.primaryPin = PIN_PIR_SENSOR;  // Placeholder
            config.secondaryPin = 0;
            config.detectionThreshold = 0;
            break;

        default:
            break;
    }

    return config;
}

bool SensorFactory::isSupported(SensorType type) {
    switch (type) {
        case SENSOR_TYPE_PIR:
        case SENSOR_TYPE_PASSIVE_IR:
        case SENSOR_TYPE_ULTRASONIC:
        case SENSOR_TYPE_ULTRASONIC_GROVE:
            return true;

        case SENSOR_TYPE_IR:
            return false;  // Not yet implemented

        default:
            return false;
    }
}

uint8_t SensorFactory::getSupportedTypes(SensorType* types, uint8_t maxTypes) {
    uint8_t count = 0;

    if (count < maxTypes) types[count++] = SENSOR_TYPE_PIR;
    if (count < maxTypes) types[count++] = SENSOR_TYPE_ULTRASONIC;
    if (count < maxTypes) types[count++] = SENSOR_TYPE_ULTRASONIC_GROVE;
    // SENSOR_TYPE_IR not included until implemented

    return count;
}

void SensorFactory::destroy(HAL_MotionSensor* sensor) {
    if (sensor) {
        DEBUG_PRINTF("[SensorFactory] Destroying sensor: %s\n",
                     sensor->getCapabilities().sensorTypeName);
        delete sensor;
    }
}
