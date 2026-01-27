#include "hal_ultrasonic.h"
#include "logger.h"

// Static capabilities definition
const SensorCapabilities HAL_Ultrasonic::s_capabilities = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC);

HAL_Ultrasonic::HAL_Ultrasonic(uint8_t triggerPin, uint8_t echoPin, bool mock_mode)
    : DistanceSensorBase(
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).minDetectionDistance,
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).maxDetectionDistance,
        5  // Window size = 5 for fast pedestrian detection (300ms response)
      ),
      m_triggerPin(triggerPin),
      m_echoPin(echoPin),
      m_mockMode(mock_mode),
      m_initialized(false),
      m_lastMeasurementTime(0),
      m_measurementInterval(MIN_MEASUREMENT_INTERVAL_MS),
      m_mockDistance(1000)  // Default mock distance: 1000mm (1m) - above threshold, won't trigger
{
}

HAL_Ultrasonic::~HAL_Ultrasonic()
{
}

bool HAL_Ultrasonic::begin()
{
    if (m_initialized) {
        return true;
    }

    if (m_mockMode) {
        m_initialized = true;
        LOG_INFO("Ultrasonic HC-SR04: Initialized in MOCK mode");
        return true;
    }

    // Configure GPIO pins
    pinMode(m_triggerPin, OUTPUT);
    pinMode(m_echoPin, INPUT);
    digitalWrite(m_triggerPin, LOW);

    m_initialized = true;
    LOG_INFO("Ultrasonic HC-SR04: Initialized (trigger=GPIO%d, echo=GPIO%d)",
             m_triggerPin, m_echoPin);
    return true;
}

void HAL_Ultrasonic::update()
{
    if (!m_initialized) {
        return;
    }

    // Rate limit measurements
    uint32_t now = millis();
    if (now - m_lastMeasurementTime < m_measurementInterval) {
        return;
    }
    m_lastMeasurementTime = now;

    // Call base class update (will call our getDistanceReading())
    updateDistanceSensor();
}

const SensorCapabilities& HAL_Ultrasonic::getCapabilities() const
{
    return s_capabilities;
}

void HAL_Ultrasonic::setMeasurementInterval(uint32_t interval_ms)
{
    m_measurementInterval = (interval_ms < MIN_MEASUREMENT_INTERVAL_MS)
                            ? MIN_MEASUREMENT_INTERVAL_MS
                            : interval_ms;
    DEBUG_PRINTF("[HAL_Ultrasonic] Measurement interval set to %u ms\n", m_measurementInterval);
}

void HAL_Ultrasonic::mockSetMotion(bool detected)
{
    if (!m_mockMode) {
        return;
    }
    // In mock mode, setting motion directly isn't supported in new architecture
    // Use mockSetDistance() instead
}

void HAL_Ultrasonic::mockSetDistance(uint32_t distance_mm)
{
    if (!m_mockMode) {
        return;
    }

    m_mockDistance = distance_mm;
    DEBUG_PRINTF("[HAL_Ultrasonic] Mock distance set to %u mm\n", distance_mm);
}

// =========================================================================
// Protected: Hardware Interface Implementation
// =========================================================================

uint32_t HAL_Ultrasonic::getDistanceReading()
{
    if (m_mockMode) {
        // Mock mode active - return mock distance without logging every call
        return m_mockDistance;
    }

#if !MOCK_HARDWARE
    // HC-SR04 protocol:
    // 1. Send 10µs trigger pulse
    // 2. Wait for echo HIGH pulse
    // 3. Measure pulse width
    // 4. Calculate distance

    // Send trigger pulse
    digitalWrite(m_triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(m_triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(m_triggerPin, LOW);

    // Wait for echo and measure pulse width
    unsigned long duration = pulseIn(m_echoPin, HIGH, MEASUREMENT_TIMEOUT_US);

    // Check for timeout
    if (duration == 0) {
        LOG_WARN("Ultrasonic pulseIn timeout - no echo on pin %d (trigger on pin %d)",
                 m_echoPin, m_triggerPin);
        return 0;  // Out of range or error
    }

    // Calculate distance in mm
    // Formula: distance = (duration / 2) / 2.91 (for mm)
    // Simplified: distance = duration / 5.82
    uint32_t distance_mm = (uint32_t)((float)duration / 5.82f);

    DEBUG_PRINTF("[HAL_Ultrasonic] Raw: duration=%lu us, distance=%u mm (range: %u-%u mm)\n",
                 duration, distance_mm, s_capabilities.minDetectionDistance, s_capabilities.maxDetectionDistance);

    // Validate range
    if (distance_mm < s_capabilities.minDetectionDistance ||
        distance_mm > s_capabilities.maxDetectionDistance) {
        LOG_INFO("Ultrasonic distance %u mm out of range (%u-%u mm), ignoring",
                 distance_mm, s_capabilities.minDetectionDistance, s_capabilities.maxDetectionDistance);
        return 0;  // Out of valid range
    }

    // Log successful reading at INFO level (but rate-limit to avoid flooding)
    static uint32_t lastLogTime = 0;
    uint32_t now = millis();
    if (now - lastLogTime > 5000) {  // Log every 5 seconds max
        LOG_INFO("Ultrasonic: %u mm (duration: %lu us)", distance_mm, duration);
        lastLogTime = now;
    }

    return distance_mm;
#else
    // Mock hardware build - return mock distance without logging every call
    return m_mockDistance;
#endif
}
