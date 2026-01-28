#include "hal_ultrasonic.h"
#include "logger.h"
#include "debug_logger.h"
#include <esp_task_wdt.h>
#include <cmath>

// Static capabilities definition
const SensorCapabilities HAL_Ultrasonic::s_capabilities = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC);

HAL_Ultrasonic::HAL_Ultrasonic(uint8_t triggerPin, uint8_t echoPin, bool mock_mode)
    : DistanceSensorBase(
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).minDetectionDistance,
        getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).maxDetectionDistance,
        3  // Window size = 3 for fast pedestrian detection
      ),
      m_triggerPin(triggerPin),
      m_echoPin(echoPin),
      m_mockMode(mock_mode),
      m_initialized(false),
      m_lastMeasurementTime(0),
      m_measurementInterval(75),  // Default 75ms for adaptive threshold
      m_mockDistance(1000),  // Default mock distance: 1000mm (1m) - above threshold, won't trigger
      m_successCounter(0),  // Start at 0 (will build up to 100)
      m_totalSamplesCollected(0),
      m_errorRateValid(false)  // Not valid until 100 samples collected
{
    // Set sample interval in base class for adaptive threshold
    setSampleInterval(m_measurementInterval);

#if !MOCK_HARDWARE
    // Create NewPing instance
    // For HC-SR04: separate trigger and echo pins, max distance in cm
    uint16_t maxDistanceCm = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC).maxDetectionDistance / 10;
    m_sonar = new NewPing(m_triggerPin, m_echoPin, maxDistanceCm);
#endif
}

HAL_Ultrasonic::~HAL_Ultrasonic()
{
#if !MOCK_HARDWARE
    if (m_sonar) {
        delete m_sonar;
        m_sonar = nullptr;
    }
#endif
}

bool HAL_Ultrasonic::begin()
{
    if (m_initialized) {
        return true;
    }

    if (m_mockMode) {
        m_initialized = true;
        DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Initialized in MOCK mode");
        return true;
    }

    // Configure GPIO pins
    pinMode(m_triggerPin, OUTPUT);
    pinMode(m_echoPin, INPUT);
    digitalWrite(m_triggerPin, LOW);

    m_initialized = true;
    DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Initialized (trigger=GPIO%d, echo=GPIO%d)",
             m_triggerPin, m_echoPin);
    return true;
}

void HAL_Ultrasonic::update()
{
    if (!m_initialized) {
        return;
    }

    uint32_t now = millis();

    // Rate limit measurements
    if (now - m_lastMeasurementTime < m_measurementInterval) {
        return;
    }
    m_lastMeasurementTime = now;

    // Call base class update (will call our getDistanceReading())
    // Error rate is automatically tracked in getDistanceReading() via rolling buffer
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
    // Update base class sample interval for adaptive threshold calculation
    setSampleInterval(m_measurementInterval);
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
        // In mock mode, simulate perfect sensor (always success)
        if (m_totalSamplesCollected < ERROR_RATE_SAMPLE_COUNT) {
            m_totalSamplesCollected++;
            m_successCounter++;
            if (m_totalSamplesCollected >= ERROR_RATE_SAMPLE_COUNT) {
                m_errorRateValid = true;
                DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Mock mode - 100 samples collected, error rate = 0.0%%");
            }
        }
        return m_mockDistance;
    }

#if !MOCK_HARDWARE
    // Use NewPing library for reliable readings
    // ping_cm() returns distance in cm, 0 if no echo/timeout
    // We convert to mm for consistency with our system

    unsigned int distance_cm = m_sonar->ping_cm();
    uint32_t distance_mm = distance_cm * 10;  // Convert cm to mm

    bool gotEcho = (distance_cm != 0);
    bool inValidRange = false;

    if (gotEcho) {
        // Check against configured range limits (not just hardware capability)
        inValidRange = (distance_mm >= m_minDistance && distance_mm <= m_maxDistance);
    } else {
        // No echo - could be timeout or out of range
        // NewPing returns 0 for both timeout and max range exceeded
        distance_mm = 0;
    }

    // Track if we recently had valid in-range readings (to know if timeouts are errors)
    static uint32_t consecutiveInRangeReadings = 0;
    static uint32_t lastValidReadingTime = 0;
    uint32_t now = millis();

    if (gotEcho && inValidRange) {
        consecutiveInRangeReadings++;
        lastValidReadingTime = now;
    } else if (gotEcho && !inValidRange) {
        // Out of range - gradually forget we had an object
        if (consecutiveInRangeReadings > 0) {
            consecutiveInRangeReadings--;
        }
    }

    // Only track error rate when we're measuring objects in valid range
    // SUCCESS: Got echo and distance is in valid range
    // FAILURE: Timeout when we recently had valid readings (object disappeared unexpectedly)
    // IGNORE: Out-of-range readings and timeouts when no object expected
    bool shouldTrackReading = false;
    bool readSuccess = false;

    if (gotEcho && inValidRange) {
        // Valid in-range reading
        shouldTrackReading = true;
        readSuccess = true;
    } else if (!gotEcho && consecutiveInRangeReadings >= 2 && (now - lastValidReadingTime) < 1000) {
        // Timeout when we had recent valid readings (likely a real error)
        shouldTrackReading = true;
        readSuccess = false;
    }
    // else: ignore out-of-range readings and random timeouts

    if (shouldTrackReading && m_totalSamplesCollected < ERROR_RATE_SAMPLE_COUNT) {
        // Warmup phase: counting successes and failures
        m_totalSamplesCollected++;
        if (readSuccess) {
            m_successCounter++;
        }

        // Detailed logging for first 10 samples
        if (m_totalSamplesCollected <= 10) {
            if (readSuccess) {
                DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Sample #%d: SUCCESS (distance=%u mm, IN-RANGE, successCounter=%d)",
                         m_totalSamplesCollected, distance_mm, m_successCounter);
            } else {
                DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Sample #%d: FAILURE - NO ECHO when object expected (successCounter=%d)",
                         m_totalSamplesCollected, m_successCounter);
            }
        }

        // Check if we've reached 100 samples
        if (m_totalSamplesCollected >= ERROR_RATE_SAMPLE_COUNT) {
            m_errorRateValid = true;
            float errorRate = 100.0f - (float)m_successCounter;
            DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Rolling buffer filled - error rate = %.1f%% (%d successes, %d failures out of 100 in-range samples)",
                     errorRate, m_successCounter, ERROR_RATE_SAMPLE_COUNT - m_successCounter);
        } else if (m_totalSamplesCollected % 25 == 0) {
            DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Warmup progress: %d/100 in-range samples (%d successes, %d failures so far)",
                     m_totalSamplesCollected, m_successCounter, m_totalSamplesCollected - m_successCounter);
        }
    } else if (shouldTrackReading) {
        // Rolling buffer active: increment on success, decrement on failure
        if (readSuccess) {
            if (m_successCounter < ERROR_RATE_SAMPLE_COUNT) {
                m_successCounter++;
            }
        } else {
            if (m_successCounter > 0) {
                m_successCounter--;
            }
        }
    }

    // Check for timeout
    if (!gotEcho) {
        static uint32_t lastWarnTime = 0;
        if (now - lastWarnTime > 5000) {  // Warn once every 5 seconds
            DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Timeout - no echo received. Error rate: %.1f%% (tracking in-range objects only)",
                     getErrorRate());
            lastWarnTime = now;
        }
        return 0;  // No echo received
    }

    // Log when range state changes (in-range <-> out-of-range transitions)
    static bool lastInValidRange = false;
    static bool firstReading = true;

    if (firstReading || (inValidRange != lastInValidRange)) {
        if (gotEcho) {
            if (inValidRange) {
                DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Range state changed - distance=%u mm (IN valid range %u-%u mm)",
                         distance_mm, m_minDistance, m_maxDistance);
            } else {
                DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: Range state changed - distance=%u mm (OUT of range %u-%u mm)",
                         distance_mm, m_minDistance, m_maxDistance);
            }
        }
        lastInValidRange = inValidRange;
        firstReading = false;
    }

    DEBUG_PRINTF("[HAL_Ultrasonic] Raw: distance=%u mm (configured range: %u-%u mm)\n",
                 distance_mm, m_minDistance, m_maxDistance);

    // Return actual distance regardless of whether we tracked it for error rate
    // The sensor worked (got an echo)
    return distance_mm;
#else
    // Mock hardware build
    if (m_totalSamplesCollected < ERROR_RATE_SAMPLE_COUNT) {
        m_totalSamplesCollected++;
        m_successCounter++;
        if (m_totalSamplesCollected >= ERROR_RATE_SAMPLE_COUNT) {
            m_errorRateValid = true;
        }
    }
    return m_mockDistance;
#endif
}

// =========================================================================
// Error Rate Monitoring (Rolling Buffer)
// =========================================================================

float HAL_Ultrasonic::getErrorRate() const
{
    if (!m_errorRateValid) {
        DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: getErrorRate() called, returning -1.0 (%d/100 samples collected)",
                  m_totalSamplesCollected);
        return -1.0f;  // No data until first 100 samples collected
    }
    // Error rate = 100 - success counter
    float errorRate = 100.0f - (float)m_successCounter;
    DEBUG_LOG_SENSOR("Ultrasonic HC-SR04: getErrorRate() called, returning %.1f%% (successCounter=%d)",
              errorRate, m_successCounter);
    return errorRate;
}
