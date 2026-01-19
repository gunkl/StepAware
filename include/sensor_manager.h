#ifndef STEPAWARE_SENSOR_MANAGER_H
#define STEPAWARE_SENSOR_MANAGER_H

#include "sensor_types.h"
#include "hal_motion_sensor.h"
#include <stdint.h>

/**
 * @file sensor_manager.h
 * @brief Multi-sensor management for StepAware
 *
 * Manages multiple sensors that can work independently or in combination:
 * - Multiple sensors facing different directions (e.g., hallway coverage)
 * - Trigger + measurement sensor combos (e.g., PIR trigger -> Ultrasonic distance)
 * - Redundant sensor configurations for reliability
 *
 * Phase 2 of Issue #4: Ability to use multiple sensors
 */

/**
 * @brief Maximum number of sensors supported
 */
#define MAX_SENSORS 4

/**
 * @brief Sensor fusion mode
 */
enum SensorFusionMode {
    FUSION_MODE_ANY = 0,        ///< Any sensor triggers detection
    FUSION_MODE_ALL,            ///< All sensors must agree
    FUSION_MODE_TRIGGER_MEASURE,///< First sensor triggers, second measures
    FUSION_MODE_INDEPENDENT     ///< Sensors report independently (no fusion)
};

/**
 * @brief Sensor slot configuration
 */
struct SensorSlot {
    HAL_MotionSensor* sensor;   ///< Pointer to sensor instance (nullptr if empty)
    SensorConfig config;        ///< Sensor configuration
    bool enabled;               ///< Sensor is active
    bool isPrimary;             ///< Primary sensor for fusion (trigger sensor)
    uint8_t slotIndex;          ///< Slot index (0-3)
    char name[32];              ///< User-defined sensor name
};

/**
 * @brief Combined sensor status from all active sensors
 */
struct CombinedSensorStatus {
    bool anyMotionDetected;         ///< At least one sensor detects motion
    bool allMotionDetected;         ///< All enabled sensors detect motion
    uint8_t activeSensorCount;      ///< Number of enabled sensors
    uint8_t detectingSensorCount;   ///< Number of sensors detecting motion
    uint32_t nearestDistance;       ///< Closest distance from all sensors (mm)
    MotionDirection primaryDirection;///< Direction from primary sensor
    uint32_t combinedEventCount;    ///< Total events from all sensors
};

/**
 * @brief Sensor Manager - handles multiple motion sensors
 *
 * Usage:
 * ```cpp
 * SensorManager sensorMgr;
 * sensorMgr.begin();
 *
 * // Add primary PIR sensor
 * SensorConfig pirConfig;
 * pirConfig.type = SENSOR_TYPE_PIR;
 * pirConfig.primaryPin = 5;
 * sensorMgr.addSensor(0, pirConfig, "Front PIR", true);
 *
 * // Add ultrasonic distance sensor
 * SensorConfig usConfig;
 * usConfig.type = SENSOR_TYPE_ULTRASONIC;
 * usConfig.primaryPin = 12;
 * usConfig.secondaryPin = 14;
 * sensorMgr.addSensor(1, usConfig, "Front Distance", false);
 *
 * // Set fusion mode
 * sensorMgr.setFusionMode(FUSION_MODE_TRIGGER_MEASURE);
 *
 * // In loop
 * sensorMgr.update();
 * if (sensorMgr.isMotionDetected()) {
 *     // Handle motion
 * }
 * ```
 */
class SensorManager {
public:
    /**
     * @brief Constructor
     */
    SensorManager();

    /**
     * @brief Destructor - cleans up sensors
     */
    ~SensorManager();

    /**
     * @brief Initialize sensor manager
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update all sensors
     *
     * Call this regularly in the main loop to update sensor states.
     */
    void update();

    /**
     * @brief Add a sensor to a slot
     *
     * @param slotIndex Slot index (0 to MAX_SENSORS-1)
     * @param config Sensor configuration
     * @param name User-defined sensor name
     * @param isPrimary True if this is the primary/trigger sensor
     * @param mockMode Enable mock mode for testing
     * @return true if sensor added successfully
     */
    bool addSensor(uint8_t slotIndex, const SensorConfig& config,
                   const char* name = nullptr, bool isPrimary = false,
                   bool mockMode = false);

    /**
     * @brief Remove a sensor from a slot
     *
     * @param slotIndex Slot index to clear
     * @return true if sensor removed successfully
     */
    bool removeSensor(uint8_t slotIndex);

    /**
     * @brief Enable or disable a sensor slot
     *
     * @param slotIndex Slot index
     * @param enabled True to enable, false to disable
     * @return true if successful
     */
    bool setSensorEnabled(uint8_t slotIndex, bool enabled);

    /**
     * @brief Get sensor at specific slot
     *
     * @param slotIndex Slot index
     * @return Pointer to sensor, nullptr if empty
     */
    HAL_MotionSensor* getSensor(uint8_t slotIndex);

    /**
     * @brief Get sensor slot information
     *
     * @param slotIndex Slot index
     * @return Pointer to slot info, nullptr if invalid
     */
    const SensorSlot* getSensorSlot(uint8_t slotIndex) const;

    /**
     * @brief Get primary sensor
     *
     * @return Pointer to primary sensor, nullptr if none
     */
    HAL_MotionSensor* getPrimarySensor();

    /**
     * @brief Set sensor fusion mode
     *
     * @param mode Fusion mode to use
     */
    void setFusionMode(SensorFusionMode mode);

    /**
     * @brief Get current fusion mode
     *
     * @return Current fusion mode
     */
    SensorFusionMode getFusionMode() const;

    /**
     * @brief Check if any sensor detects motion
     *
     * Behavior depends on fusion mode:
     * - ANY: Returns true if any sensor detects
     * - ALL: Returns true if all sensors detect
     * - TRIGGER_MEASURE: Returns true if trigger sensor detects
     * - INDEPENDENT: Returns true if primary sensor detects
     *
     * @return true if motion detected based on fusion mode
     */
    bool isMotionDetected();

    /**
     * @brief Get combined sensor status
     *
     * @return Combined status from all sensors
     */
    CombinedSensorStatus getStatus();

    /**
     * @brief Get number of active sensors
     *
     * @return Count of enabled sensors
     */
    uint8_t getActiveSensorCount() const;

    /**
     * @brief Check if all sensors are ready
     *
     * @return true if all enabled sensors are ready
     */
    bool allSensorsReady();

    /**
     * @brief Get nearest distance from all distance sensors
     *
     * @return Nearest distance in mm, 0 if no distance sensors
     */
    uint32_t getNearestDistance();

    /**
     * @brief Get direction from primary sensor
     *
     * @return Direction if primary sensor supports it, UNKNOWN otherwise
     */
    MotionDirection getPrimaryDirection();

    /**
     * @brief Reset event counts for all sensors
     */
    void resetEventCounts();

    /**
     * @brief Print sensor manager status
     *
     * Prints configuration and status of all sensors to Serial.
     */
    void printStatus();

    /**
     * @brief Validate sensor configuration
     *
     * Checks for conflicts and invalid configurations.
     *
     * @return true if configuration is valid
     */
    bool validateConfiguration();

    /**
     * @brief Get last error message
     *
     * @return Error message string
     */
    const char* getLastError() const;

private:
    SensorSlot m_slots[MAX_SENSORS];    ///< Sensor slots
    SensorFusionMode m_fusionMode;      ///< Current fusion mode
    uint8_t m_activeSensorCount;        ///< Number of active sensors
    uint8_t m_primarySlotIndex;         ///< Index of primary sensor
    bool m_initialized;                 ///< Initialization complete
    char m_lastError[128];              ///< Last error message

    /**
     * @brief Update active sensor count
     */
    void updateActiveSensorCount();

    /**
     * @brief Find primary sensor slot
     *
     * @return Slot index of primary sensor, 0xFF if none
     */
    uint8_t findPrimarySlot();

    /**
     * @brief Set error message
     *
     * @param error Error message
     */
    void setError(const char* error);
};

#endif // STEPAWARE_SENSOR_MANAGER_H
