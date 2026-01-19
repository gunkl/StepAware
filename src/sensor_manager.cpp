#include "sensor_manager.h"
#include "sensor_factory.h"
#include "logger.h"
#include <Arduino.h>

SensorManager::SensorManager()
    : m_fusionMode(FUSION_MODE_ANY)
    , m_activeSensorCount(0)
    , m_primarySlotIndex(0xFF)
    , m_initialized(false)
{
    // Initialize all slots
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        m_slots[i].sensor = nullptr;
        m_slots[i].enabled = false;
        m_slots[i].isPrimary = false;
        m_slots[i].slotIndex = i;
        strncpy(m_slots[i].name, "", sizeof(m_slots[i].name));
        memset(&m_slots[i].config, 0, sizeof(SensorConfig));
    }

    memset(m_lastError, 0, sizeof(m_lastError));
}

SensorManager::~SensorManager() {
    // Clean up all sensors
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor) {
            SensorFactory::destroy(m_slots[i].sensor);
            m_slots[i].sensor = nullptr;
        }
    }
}

bool SensorManager::begin() {
    if (m_initialized) {
        LOG_WARN("SensorManager: Already initialized");
        return true;
    }

    LOG_INFO("SensorManager: Initializing");

    // Initialize any pre-configured sensors
    bool allSuccess = true;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            if (!m_slots[i].sensor->begin()) {
                LOG_ERROR("SensorManager: Failed to init sensor %u (%s)",
                         i, m_slots[i].name);
                allSuccess = false;
            } else {
                LOG_INFO("SensorManager: Initialized sensor %u (%s)",
                        i, m_slots[i].name);
            }
        }
    }

    updateActiveSensorCount();
    m_primarySlotIndex = findPrimarySlot();

    m_initialized = true;
    LOG_INFO("SensorManager: Initialized with %u active sensors", m_activeSensorCount);

    return allSuccess;
}

void SensorManager::update() {
    if (!m_initialized) {
        return;
    }

    // Update all enabled sensors
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            m_slots[i].sensor->update();
        }
    }
}

bool SensorManager::addSensor(uint8_t slotIndex, const SensorConfig& config,
                              const char* name, bool isPrimary, bool mockMode) {
    if (slotIndex >= MAX_SENSORS) {
        setError("Invalid slot index");
        return false;
    }

    // Remove existing sensor in this slot
    if (m_slots[slotIndex].sensor) {
        LOG_WARN("SensorManager: Replacing sensor in slot %u", slotIndex);
        removeSensor(slotIndex);
    }

    // Create sensor via factory
    HAL_MotionSensor* sensor = SensorFactory::create(config, mockMode);
    if (!sensor) {
        setError("Failed to create sensor");
        return false;
    }

    // Configure slot
    m_slots[slotIndex].sensor = sensor;
    m_slots[slotIndex].config = config;
    m_slots[slotIndex].enabled = true;
    m_slots[slotIndex].isPrimary = isPrimary;

    if (name && strlen(name) > 0) {
        strncpy(m_slots[slotIndex].name, name, sizeof(m_slots[slotIndex].name) - 1);
        m_slots[slotIndex].name[sizeof(m_slots[slotIndex].name) - 1] = '\0';
    } else {
        snprintf(m_slots[slotIndex].name, sizeof(m_slots[slotIndex].name),
                "Sensor %u", slotIndex);
    }

    // If primary, clear primary flag from other sensors
    if (isPrimary) {
        for (uint8_t i = 0; i < MAX_SENSORS; i++) {
            if (i != slotIndex && m_slots[i].sensor) {
                m_slots[i].isPrimary = false;
            }
        }
        m_primarySlotIndex = slotIndex;
    }

    updateActiveSensorCount();

    // Initialize sensor if manager is already initialized
    if (m_initialized) {
        if (!sensor->begin()) {
            LOG_ERROR("SensorManager: Failed to initialize sensor %u (%s)",
                     slotIndex, m_slots[slotIndex].name);
            setError("Failed to initialize sensor");
            return false;
        }
    }

    LOG_INFO("SensorManager: Added sensor %u (%s) - %s",
            slotIndex, m_slots[slotIndex].name,
            sensor->getCapabilities().sensorTypeName);

    return true;
}

bool SensorManager::removeSensor(uint8_t slotIndex) {
    if (slotIndex >= MAX_SENSORS) {
        setError("Invalid slot index");
        return false;
    }

    if (!m_slots[slotIndex].sensor) {
        // Already empty
        return true;
    }

    LOG_INFO("SensorManager: Removing sensor %u (%s)",
            slotIndex, m_slots[slotIndex].name);

    // Destroy sensor
    SensorFactory::destroy(m_slots[slotIndex].sensor);
    m_slots[slotIndex].sensor = nullptr;
    m_slots[slotIndex].enabled = false;
    m_slots[slotIndex].isPrimary = false;
    strncpy(m_slots[slotIndex].name, "", sizeof(m_slots[slotIndex].name));

    updateActiveSensorCount();

    // Update primary slot index if we removed the primary
    if (m_primarySlotIndex == slotIndex) {
        m_primarySlotIndex = findPrimarySlot();
    }

    return true;
}

bool SensorManager::setSensorEnabled(uint8_t slotIndex, bool enabled) {
    if (slotIndex >= MAX_SENSORS) {
        setError("Invalid slot index");
        return false;
    }

    if (!m_slots[slotIndex].sensor) {
        setError("No sensor in slot");
        return false;
    }

    m_slots[slotIndex].enabled = enabled;
    updateActiveSensorCount();

    LOG_INFO("SensorManager: Sensor %u (%s) %s",
            slotIndex, m_slots[slotIndex].name,
            enabled ? "enabled" : "disabled");

    return true;
}

HAL_MotionSensor* SensorManager::getSensor(uint8_t slotIndex) {
    if (slotIndex >= MAX_SENSORS) {
        return nullptr;
    }
    return m_slots[slotIndex].sensor;
}

const SensorSlot* SensorManager::getSensorSlot(uint8_t slotIndex) const {
    if (slotIndex >= MAX_SENSORS) {
        return nullptr;
    }
    return &m_slots[slotIndex];
}

HAL_MotionSensor* SensorManager::getPrimarySensor() {
    if (m_primarySlotIndex == 0xFF || m_primarySlotIndex >= MAX_SENSORS) {
        // No primary sensor set, return first active sensor
        for (uint8_t i = 0; i < MAX_SENSORS; i++) {
            if (m_slots[i].sensor && m_slots[i].enabled) {
                return m_slots[i].sensor;
            }
        }
        return nullptr;
    }
    return m_slots[m_primarySlotIndex].sensor;
}

void SensorManager::setFusionMode(SensorFusionMode mode) {
    m_fusionMode = mode;
    LOG_INFO("SensorManager: Fusion mode set to %u", mode);
}

SensorFusionMode SensorManager::getFusionMode() const {
    return m_fusionMode;
}

bool SensorManager::isMotionDetected() {
    if (!m_initialized || m_activeSensorCount == 0) {
        return false;
    }

    switch (m_fusionMode) {
        case FUSION_MODE_ANY: {
            // Any sensor detecting = motion
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (m_slots[i].sensor && m_slots[i].enabled) {
                    if (m_slots[i].sensor->motionDetected()) {
                        return true;
                    }
                }
            }
            return false;
        }

        case FUSION_MODE_ALL: {
            // All sensors must detect
            bool anyActive = false;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (m_slots[i].sensor && m_slots[i].enabled) {
                    anyActive = true;
                    if (!m_slots[i].sensor->motionDetected()) {
                        return false;  // At least one not detecting
                    }
                }
            }
            return anyActive;  // All detecting (if any active)
        }

        case FUSION_MODE_TRIGGER_MEASURE: {
            // Primary/trigger sensor must detect
            HAL_MotionSensor* primary = getPrimarySensor();
            if (primary) {
                return primary->motionDetected();
            }
            return false;
        }

        case FUSION_MODE_INDEPENDENT:
        default: {
            // Independent mode - check primary sensor
            HAL_MotionSensor* primary = getPrimarySensor();
            if (primary) {
                return primary->motionDetected();
            }
            return false;
        }
    }
}

CombinedSensorStatus SensorManager::getStatus() {
    CombinedSensorStatus status = {};
    status.anyMotionDetected = false;
    status.allMotionDetected = true;
    status.activeSensorCount = 0;
    status.detectingSensorCount = 0;
    status.nearestDistance = 0xFFFFFFFF;  // Max value
    status.primaryDirection = DIRECTION_UNKNOWN;
    status.combinedEventCount = 0;

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            status.activeSensorCount++;

            if (m_slots[i].sensor->motionDetected()) {
                status.anyMotionDetected = true;
                status.detectingSensorCount++;
            } else {
                status.allMotionDetected = false;
            }

            // Get distance from distance sensors
            if (m_slots[i].sensor->getCapabilities().supportsDistanceMeasurement) {
                uint32_t dist = m_slots[i].sensor->getDistance();
                if (dist < status.nearestDistance) {
                    status.nearestDistance = dist;
                }
            }

            // Get direction from primary sensor
            if (m_slots[i].isPrimary &&
                m_slots[i].sensor->getCapabilities().supportsDirectionDetection) {
                status.primaryDirection = m_slots[i].sensor->getDirection();
            }

            // Sum event counts
            status.combinedEventCount += m_slots[i].sensor->getEventCount();
        }
    }

    // If no active sensors, allMotionDetected should be false
    if (status.activeSensorCount == 0) {
        status.allMotionDetected = false;
    }

    // If no distance sensor, set to 0
    if (status.nearestDistance == 0xFFFFFFFF) {
        status.nearestDistance = 0;
    }

    return status;
}

uint8_t SensorManager::getActiveSensorCount() const {
    return m_activeSensorCount;
}

bool SensorManager::allSensorsReady() {
    if (!m_initialized || m_activeSensorCount == 0) {
        return false;
    }

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            if (!m_slots[i].sensor->isReady()) {
                return false;
            }
        }
    }

    return true;
}

uint32_t SensorManager::getNearestDistance() {
    uint32_t nearest = 0;

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            if (m_slots[i].sensor->getCapabilities().supportsDistanceMeasurement) {
                uint32_t dist = m_slots[i].sensor->getDistance();
                if (nearest == 0 || dist < nearest) {
                    nearest = dist;
                }
            }
        }
    }

    return nearest;
}

MotionDirection SensorManager::getPrimaryDirection() {
    HAL_MotionSensor* primary = getPrimarySensor();
    if (primary && primary->getCapabilities().supportsDirectionDetection) {
        return primary->getDirection();
    }
    return DIRECTION_UNKNOWN;
}

void SensorManager::resetEventCounts() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor) {
            m_slots[i].sensor->resetEventCount();
        }
    }
    LOG_INFO("SensorManager: Reset all event counts");
}

void SensorManager::printStatus() {
    Serial.println("\n========== Sensor Manager Status ==========");
    Serial.printf("Active Sensors: %u / %u\n", m_activeSensorCount, MAX_SENSORS);
    Serial.printf("Fusion Mode: ");
    switch (m_fusionMode) {
        case FUSION_MODE_ANY: Serial.println("ANY"); break;
        case FUSION_MODE_ALL: Serial.println("ALL"); break;
        case FUSION_MODE_TRIGGER_MEASURE: Serial.println("TRIGGER_MEASURE"); break;
        case FUSION_MODE_INDEPENDENT: Serial.println("INDEPENDENT"); break;
        default: Serial.println("UNKNOWN"); break;
    }
    Serial.println();

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor) {
            const SensorCapabilities& caps = m_slots[i].sensor->getCapabilities();

            Serial.printf("Slot %u: %s %s\n", i,
                         m_slots[i].name,
                         m_slots[i].isPrimary ? "[PRIMARY]" : "");
            Serial.printf("  Type: %s\n", caps.sensorTypeName);
            Serial.printf("  Enabled: %s\n", m_slots[i].enabled ? "YES" : "NO");
            Serial.printf("  Ready: %s\n", m_slots[i].sensor->isReady() ? "YES" : "NO");
            Serial.printf("  Motion: %s\n", m_slots[i].sensor->motionDetected() ? "YES" : "NO");

            if (caps.supportsDistanceMeasurement) {
                Serial.printf("  Distance: %u mm\n", m_slots[i].sensor->getDistance());
            }

            if (caps.supportsDirectionDetection) {
                const char* dirName = "Unknown";
                switch (m_slots[i].sensor->getDirection()) {
                    case DIRECTION_STATIONARY: dirName = "Stationary"; break;
                    case DIRECTION_APPROACHING: dirName = "Approaching"; break;
                    case DIRECTION_RECEDING: dirName = "Receding"; break;
                    default: break;
                }
                Serial.printf("  Direction: %s\n", dirName);
            }

            Serial.printf("  Events: %u\n", m_slots[i].sensor->getEventCount());
            Serial.println();
        }
    }

    CombinedSensorStatus status = getStatus();
    Serial.println("Combined Status:");
    Serial.printf("  Any Motion: %s\n", status.anyMotionDetected ? "YES" : "NO");
    Serial.printf("  All Motion: %s\n", status.allMotionDetected ? "YES" : "NO");
    Serial.printf("  Detecting: %u / %u\n",
                 status.detectingSensorCount, status.activeSensorCount);
    if (status.nearestDistance > 0) {
        Serial.printf("  Nearest: %u mm\n", status.nearestDistance);
    }
    Serial.printf("  Total Events: %u\n", status.combinedEventCount);

    Serial.println("===========================================\n");
}

bool SensorManager::validateConfiguration() {
    // Check for conflicting configurations
    uint8_t primaryCount = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].isPrimary) {
            primaryCount++;
        }
    }

    if (primaryCount > 1) {
        setError("Multiple primary sensors configured");
        return false;
    }

    // Validate fusion mode
    if (m_fusionMode == FUSION_MODE_TRIGGER_MEASURE) {
        if (m_activeSensorCount < 2) {
            setError("TRIGGER_MEASURE mode requires at least 2 sensors");
            return false;
        }
        if (primaryCount == 0) {
            setError("TRIGGER_MEASURE mode requires a primary sensor");
            return false;
        }
    }

    return true;
}

const char* SensorManager::getLastError() const {
    return m_lastError;
}

void SensorManager::updateActiveSensorCount() {
    m_activeSensorCount = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].enabled) {
            m_activeSensorCount++;
        }
    }
}

uint8_t SensorManager::findPrimarySlot() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (m_slots[i].sensor && m_slots[i].isPrimary) {
            return i;
        }
    }
    return 0xFF;  // No primary found
}

void SensorManager::setError(const char* error) {
    strncpy(m_lastError, error, sizeof(m_lastError) - 1);
    m_lastError[sizeof(m_lastError) - 1] = '\0';
}
