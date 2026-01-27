#include "hal_ultrasonic_grove.h"
#include "logger.h"

// Static capabilities definition
const SensorCapabilities HAL_Ultrasonic_Grove::s_capabilities = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE);

HAL_Ultrasonic_Grove::HAL_Ultrasonic_Grove(uint8_t sigPin, bool mock_mode)
    : DistanceSensorBase(
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE).minDetectionDistance,
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE).maxDetectionDistance,
        3  // Window size = 3 for fast pedestrian detection (200ms response)
      ),
      m_sigPin(sigPin),
      m_mockMode(mock_mode),
      m_initialized(false),
      m_lastMeasurementTime(0),
      m_measurementInterval(MIN_MEASUREMENT_INTERVAL_MS),
      m_mockDistance(1000)  // Default mock distance: 1000mm (1m) - above threshold, won't trigger
{
}

HAL_Ultrasonic_Grove::~HAL_Ultrasonic_Grove()
{
}

bool HAL_Ultrasonic_Grove::begin()
{
    if (m_initialized) {
        return true;
    }

    if (m_mockMode) {
        m_initialized = true;
        LOG_INFO("Ultrasonic Grove: Initialized in MOCK mode");
        return true;
    }

    // Configure pin (will switch between OUTPUT and INPUT during measurement)
    pinMode(m_sigPin, OUTPUT);
    digitalWrite(m_sigPin, LOW);

    m_initialized = true;
    LOG_INFO("Ultrasonic Grove: Initialized (sig=GPIO%d)", m_sigPin);
    return true;
}

void HAL_Ultrasonic_Grove::update()
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

const SensorCapabilities& HAL_Ultrasonic_Grove::getCapabilities() const
{
    return s_capabilities;
}

void HAL_Ultrasonic_Grove::setMeasurementInterval(uint32_t interval_ms)
{
    m_measurementInterval = (interval_ms < MIN_MEASUREMENT_INTERVAL_MS)
                            ? MIN_MEASUREMENT_INTERVAL_MS
                            : interval_ms;
    DEBUG_PRINTF("[HAL_Ultrasonic_Grove] Measurement interval set to %u ms\n", m_measurementInterval);
}

void HAL_Ultrasonic_Grove::mockSetMotion(bool detected)
{
    if (!m_mockMode) {
        return;
    }
    // In mock mode, setting motion directly isn't supported in new architecture
    // Use mockSetDistance() instead
}

void HAL_Ultrasonic_Grove::mockSetDistance(uint32_t distance_mm)
{
    if (!m_mockMode) {
        return;
    }

    m_mockDistance = distance_mm;
    DEBUG_PRINTF("[HAL_Ultrasonic_Grove] Mock distance set to %u mm\n", distance_mm);
}

// =========================================================================
// Protected: Hardware Interface Implementation
// =========================================================================

uint32_t HAL_Ultrasonic_Grove::getDistanceReading()
{
    if (m_mockMode) {
        return m_mockDistance;
    }

#if !MOCK_HARDWARE
    // Grove Ultrasonic Ranger protocol:
    // 1. Send 10µs trigger pulse on SIG pin
    // 2. Switch SIG pin to INPUT
    // 3. Wait for echo HIGH pulse
    // 4. Measure pulse width

    // Step 1: Send trigger pulse (SIG as OUTPUT)
    pinMode(m_sigPin, OUTPUT);
    digitalWrite(m_sigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(m_sigPin, HIGH);
    delayMicroseconds(10);  // 10µs trigger pulse
    digitalWrite(m_sigPin, LOW);

    // Step 2: Switch to INPUT for echo
    pinMode(m_sigPin, INPUT);

    // Step 3 & 4: Wait for echo and measure pulse width
    unsigned long duration = pulseIn(m_sigPin, HIGH, MEASUREMENT_TIMEOUT_US);

    // Check for timeout
    if (duration == 0) {
        static uint32_t lastWarnTime = 0;
        uint32_t now = millis();
        if (now - lastWarnTime > 5000) {  // Warn once every 5 seconds
            LOG_WARN("Ultrasonic Grove: Timeout - no echo received (check wiring/object distance)");
            lastWarnTime = now;
        }
        return 0;  // Out of range or error
    }

    // Calculate distance in mm
    // Formula: distance = (duration / 2) / 2.91 (for mm)
    // Simplified: distance = duration / 5.82
    uint32_t distance_mm = (uint32_t)((float)duration / 5.82f);

    DEBUG_PRINTF("[HAL_Ultrasonic_Grove] Raw: duration=%lu us, distance=%u mm (range: %u-%u mm)\n",
                 duration, distance_mm, s_capabilities.minDetectionDistance, s_capabilities.maxDetectionDistance);

    // Validate range
    if (distance_mm < s_capabilities.minDetectionDistance ||
        distance_mm > s_capabilities.maxDetectionDistance) {
        return 0;  // Out of valid range
    }

    return distance_mm;
#else
    // Mock hardware build
    return m_mockDistance;
#endif
}
