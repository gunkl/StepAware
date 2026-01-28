#include "hal_ultrasonic_grove.h"
#include "logger.h"
#include "debug_logger.h"
#include <esp_task_wdt.h>
#include <cmath>

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
    // For Grove sensor: trigger and echo on same pin, max distance in cm
    uint16_t maxDistanceCm = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC_GROVE).maxDetectionDistance / 10;
    m_sonar = new NewPing(m_sigPin, m_sigPin, maxDistanceCm);
#endif
}

HAL_Ultrasonic_Grove::~HAL_Ultrasonic_Grove()
{
#if !MOCK_HARDWARE
    if (m_sonar) {
        delete m_sonar;
        m_sonar = nullptr;
    }
#endif
}

bool HAL_Ultrasonic_Grove::begin()
{
    if (m_initialized) {
        return true;
    }

    if (m_mockMode) {
        m_initialized = true;
        DEBUG_LOG_SENSOR("Ultrasonic Grove: Initialized in MOCK mode");
        return true;
    }

    // Configure pin (will switch between OUTPUT and INPUT during measurement)
    pinMode(m_sigPin, OUTPUT);
    digitalWrite(m_sigPin, LOW);

    m_initialized = true;
    DEBUG_LOG_SENSOR("Ultrasonic Grove: Initialized (sig=GPIO%d)", m_sigPin);
    return true;
}

void HAL_Ultrasonic_Grove::update()
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

const SensorCapabilities& HAL_Ultrasonic_Grove::getCapabilities() const
{
    return s_capabilities;
}

void HAL_Ultrasonic_Grove::setMeasurementInterval(uint32_t interval_ms)
{
    m_measurementInterval = (interval_ms < MIN_MEASUREMENT_INTERVAL_MS)
                            ? MIN_MEASUREMENT_INTERVAL_MS
                            : interval_ms;
    // Update base class sample interval for adaptive threshold calculation
    setSampleInterval(m_measurementInterval);
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
        // In mock mode, simulate perfect sensor (always success)
        if (m_totalSamplesCollected < ERROR_RATE_SAMPLE_COUNT) {
            m_totalSamplesCollected++;
            m_successCounter++;
            if (m_totalSamplesCollected >= ERROR_RATE_SAMPLE_COUNT) {
                m_errorRateValid = true;
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Mock mode - 100 samples collected, error rate = 0.0%%");
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
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Sample #%d: SUCCESS (distance=%u mm, IN-RANGE, successCounter=%d)",
                         m_totalSamplesCollected, distance_mm, m_successCounter);
            } else {
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Sample #%d: FAILURE - NO ECHO when object expected (successCounter=%d)",
                         m_totalSamplesCollected, m_successCounter);
            }
        }

        // Check if we've reached 100 samples
        if (m_totalSamplesCollected >= ERROR_RATE_SAMPLE_COUNT) {
            m_errorRateValid = true;
            float errorRate = 100.0f - (float)m_successCounter;
            DEBUG_LOG_SENSOR("Ultrasonic Grove: Rolling buffer filled - error rate = %.1f%% (%d successes, %d failures out of 100 in-range samples)",
                     errorRate, m_successCounter, ERROR_RATE_SAMPLE_COUNT - m_successCounter);
        } else if (m_totalSamplesCollected % 25 == 0) {
            DEBUG_LOG_SENSOR("Ultrasonic Grove: Warmup progress: %d/100 in-range samples (%d successes, %d failures so far)",
                     m_totalSamplesCollected, m_successCounter, m_totalSamplesCollected - m_successCounter);
        }
    } else if (shouldTrackReading) {
        // Rolling buffer active: increment on success, decrement on failure
        // Only process readings we're tracking (in-range objects)
        static uint32_t rollingSuccessCount = 0;
        static uint32_t rollingFailureCount = 0;
        static uint32_t lastStatsLog = 0;
        static int16_t lastLoggedCounter = -1;  // Track last logged counter value

        if (readSuccess) {
            rollingSuccessCount++;
            if (m_successCounter < ERROR_RATE_SAMPLE_COUNT) {
                int16_t oldCounter = m_successCounter;
                m_successCounter++;
                // Only log if counter changed significantly (every 10 points) or crossed major thresholds
                if (lastLoggedCounter < 0 ||
                    (m_successCounter % 10 == 0 && oldCounter % 10 != 0) ||
                    (oldCounter < 50 && m_successCounter >= 50) ||
                    (oldCounter < 90 && m_successCounter >= 90)) {
                    DEBUG_LOG_SENSOR("Ultrasonic Grove: Counter improved to %d (error rate: %.1f%%)",
                             m_successCounter, 100.0f - (float)m_successCounter);
                    lastLoggedCounter = m_successCounter;
                }
            }
        } else {
            rollingFailureCount++;
            if (m_successCounter > 0) {
                int16_t oldCounter = m_successCounter;
                m_successCounter--;
                // Only log if counter changed significantly or crossed major thresholds
                if (lastLoggedCounter < 0 ||
                    m_successCounter == 0 ||
                    (m_successCounter % 10 == 0 && oldCounter % 10 != 0) ||
                    (oldCounter >= 50 && m_successCounter < 50) ||
                    (oldCounter >= 90 && m_successCounter < 90)) {
                    DEBUG_LOG_SENSOR("Ultrasonic Grove: FAILURE - counter at %d (error rate: %.1f%%)",
                            m_successCounter, 100.0f - (float)m_successCounter);
                    lastLoggedCounter = m_successCounter;
                }
            } else {
                // Only log "counter at 0" once when it first reaches 0
                if (lastLoggedCounter != 0) {
                    DEBUG_LOG_SENSOR("Ultrasonic Grove: FAILURE - counter at 0 (error rate: 100.0%%)");
                    lastLoggedCounter = 0;
                }
            }
        }

        // Log statistics periodically in rolling mode
        uint32_t totalRolling = rollingSuccessCount + rollingFailureCount;
        uint32_t now = millis();

        // Log every 100 samples OR every 10 seconds, whichever comes first
        bool shouldLog = (totalRolling % 100 == 0 && totalRolling > 0) ||
                        (now - lastStatsLog >= 10000);

        if (shouldLog && (now - lastStatsLog > 1000)) {
            float actualErrorRate = ((float)rollingFailureCount / (float)totalRolling) * 100.0f;
            float reportedError = 100.0f - (float)m_successCounter;

            DEBUG_LOG_SENSOR("Ultrasonic Grove: Rolling stats - %u in-range samples tracked (%u successes, %u failures, %.1f%% actual error rate)",
                     totalRolling, rollingSuccessCount, rollingFailureCount, actualErrorRate);
            DEBUG_LOG_SENSOR("Ultrasonic Grove: Counter=%d, Reported error rate=%.1f%% (last 100 in-range samples)",
                     m_successCounter, reportedError);

            // Show convergence progress if error rates differ significantly
            if (fabs(actualErrorRate - reportedError) > 5.0f) {
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Rolling buffer converging: actual=%.1f%%, reported=%.1f%% (diff=%.1f%%)",
                         actualErrorRate, reportedError, actualErrorRate - reportedError);
            }

            lastStatsLog = now;
        }
    }

    // Check for timeout
    if (!gotEcho) {
        static uint32_t lastWarnTime = 0;
        if (now - lastWarnTime > 5000) {  // Warn once every 5 seconds
            DEBUG_LOG_SENSOR("Ultrasonic Grove: Timeout - no echo received. Error rate: %.1f%% (tracking in-range objects only)",
                     getErrorRate());
            lastWarnTime = now;
        }
        return 0;  // No echo received
    }

    // We already calculated distance_mm above

    // Log when range state changes (in-range <-> out-of-range transitions)
    static bool lastInValidRange = false;
    static bool firstReading = true;

    if (firstReading || (inValidRange != lastInValidRange)) {
        if (gotEcho) {
            if (inValidRange) {
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Range state changed - distance=%u mm (IN valid range %u-%u mm)",
                         distance_mm, m_minDistance, m_maxDistance);
            } else {
                DEBUG_LOG_SENSOR("Ultrasonic Grove: Range state changed - distance=%u mm (OUT of range %u-%u mm)",
                         distance_mm, m_minDistance, m_maxDistance);
            }
        }
        lastInValidRange = inValidRange;
        firstReading = false;
    }

    // Still log initial warmup samples for diagnostics (only at specific milestones)
    static uint32_t lastLoggedWarmupSample = 0;
    if (shouldTrackReading && m_totalSamplesCollected <= 100) {
        bool shouldLogWarmup = (m_totalSamplesCollected <= 10) ||
                               (m_totalSamplesCollected == 25 && lastLoggedWarmupSample < 25) ||
                               (m_totalSamplesCollected == 50 && lastLoggedWarmupSample < 50) ||
                               (m_totalSamplesCollected == 75 && lastLoggedWarmupSample < 75) ||
                               (m_totalSamplesCollected == 100 && lastLoggedWarmupSample < 100);

        if (shouldLogWarmup) {
            DEBUG_LOG_SENSOR("Ultrasonic Grove: Warmup sample #%d - distance=%u mm",
                     m_totalSamplesCollected, distance_mm);
            lastLoggedWarmupSample = m_totalSamplesCollected;
        }
    }

    DEBUG_PRINTF("[HAL_Ultrasonic_Grove] Raw: distance=%u mm (configured range: %u-%u mm)\n",
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

float HAL_Ultrasonic_Grove::getErrorRate() const
{
    if (!m_errorRateValid) {
        DEBUG_LOG_SENSOR("Ultrasonic Grove: getErrorRate() called, returning -1.0 (%d/100 samples collected)",
                  m_totalSamplesCollected);
        return -1.0f;  // No data until first 100 samples collected
    }
    // Error rate = 100 - success counter
    float errorRate = 100.0f - (float)m_successCounter;
    DEBUG_LOG_SENSOR("Ultrasonic Grove: getErrorRate() called, returning %.1f%% (successCounter=%d)",
              errorRate, m_successCounter);
    return errorRate;
}
