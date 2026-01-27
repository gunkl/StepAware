#ifndef STEPAWARE_SENSOR_FACTORY_H
#define STEPAWARE_SENSOR_FACTORY_H

#include "sensor_types.h"
#include "hal_motion_sensor.h"
#include "hal_pir.h"
#include "hal_ultrasonic.h"
#include "hal_ultrasonic_grove.h"

/**
 * @file sensor_factory.h
 * @brief Factory for creating motion sensor instances
 *
 * Provides a centralized way to create sensor instances based on
 * configuration, enabling runtime sensor selection.
 *
 * Usage:
 * ```cpp
 * // Create from config
 * SensorConfig config;
 * config.type = SENSOR_TYPE_PIR;
 * config.primaryPin = 5;
 * HAL_MotionSensor* sensor = SensorFactory::create(config);
 *
 * // Or use convenience methods
 * HAL_MotionSensor* pir = SensorFactory::createPIR(5);
 * HAL_MotionSensor* ultrasonic = SensorFactory::createUltrasonic(12, 14);
 * ```
 */
class SensorFactory {
public:
    /**
     * @brief Create a sensor from configuration
     *
     * @param config Sensor configuration
     * @param mockMode Enable mock mode for testing
     * @return Pointer to created sensor, nullptr on failure
     */
    static HAL_MotionSensor* create(const SensorConfig& config, bool mockMode = false);

    /**
     * @brief Create a PIR sensor
     *
     * @param pin GPIO pin for PIR output
     * @param mockMode Enable mock mode for testing
     * @return Pointer to HAL_PIR instance
     */
    static HAL_MotionSensor* createPIR(uint8_t pin, bool mockMode = false);

    /**
     * @brief Create an ultrasonic sensor (HC-SR04, 4-pin)
     *
     * @param triggerPin GPIO pin for trigger
     * @param echoPin GPIO pin for echo
     * @param mockMode Enable mock mode for testing
     * @return Pointer to HAL_Ultrasonic instance
     */
    static HAL_MotionSensor* createUltrasonic(uint8_t triggerPin, uint8_t echoPin,
                                               bool mockMode = false);

    /**
     * @brief Create a Grove ultrasonic sensor (v2.0, 3-pin)
     *
     * @param sigPin GPIO pin for combined trigger/echo signal
     * @param mockMode Enable mock mode for testing
     * @return Pointer to HAL_Ultrasonic_Grove instance
     */
    static HAL_MotionSensor* createUltrasonicGrove(uint8_t sigPin,
                                                    bool mockMode = false);

    /**
     * @brief Create a sensor from type enum
     *
     * Uses default pin configuration from config.h
     *
     * @param type Sensor type to create
     * @param mockMode Enable mock mode for testing
     * @return Pointer to created sensor, nullptr if type not supported
     */
    static HAL_MotionSensor* createFromType(SensorType type, bool mockMode = false);

    /**
     * @brief Get default configuration for a sensor type
     *
     * @param type Sensor type
     * @return Default configuration for the sensor type
     */
    static SensorConfig getDefaultConfig(SensorType type);

    /**
     * @brief Check if a sensor type is supported
     *
     * @param type Sensor type to check
     * @return true if the sensor type can be created
     */
    static bool isSupported(SensorType type);

    /**
     * @brief Get list of supported sensor types
     *
     * @param types Array to fill with supported types
     * @param maxTypes Maximum number of types to return
     * @return Number of supported types
     */
    static uint8_t getSupportedTypes(SensorType* types, uint8_t maxTypes);

    /**
     * @brief Destroy a sensor created by the factory
     *
     * @param sensor Pointer to sensor to destroy
     */
    static void destroy(HAL_MotionSensor* sensor);

private:
    // Prevent instantiation
    SensorFactory() = delete;
    ~SensorFactory() = delete;
};

#endif // STEPAWARE_SENSOR_FACTORY_H
