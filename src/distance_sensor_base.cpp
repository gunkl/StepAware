#include "distance_sensor_base.h"
#include "debug_logger.h"

DistanceSensorBase::DistanceSensorBase(uint32_t minDistance, uint32_t maxDistance, uint8_t windowSize)
    : m_currentDistance(0),
      m_detectionThreshold(DEFAULT_THRESHOLD_MM),
      m_minDistance(minDistance),
      m_maxDistance(maxDistance),
      m_objectDetected(false),
      m_directionEnabled(false),
      m_direction(DIRECTION_UNKNOWN),
      m_lastLoggedDirection(DIRECTION_UNKNOWN),
      m_directionSensitivity(DEFAULT_SENSITIVITY_MM),
      m_directionTriggerMode(0),  // Default: approaching only
      m_candidateDirection(DIRECTION_UNKNOWN),
      m_directionStabilityCount(0),
      m_seenApproachingFromOutside(false),
      m_suddenAppearance(false),
      m_awaitingDirectionConfirmation(false),
      m_confirmationCyclesRemaining(0),
      m_consecutiveInRangeCount(0),
      m_lastRawDistance(0),
      m_lastMotionDetectedTime(0),
      m_rawReadingHistoryIndex(0),
      m_skipDirectionUpdateCount(0),
      m_sampleWindowSize(windowSize),
      m_sampleWindowIndex(0),
      m_sampleWindowCount(0),
      m_windowAverage(0),
      m_lastWindowAverage(0),
      m_windowFilled(false),
      m_deltaHistoryIndex(0),
      m_deltaHistoryCount(0),
      m_lastEvent(MOTION_EVENT_NONE),
      m_eventCount(0),
      m_lastEventTime(0),
      m_sampleIntervalMs(75)  // Default 75ms sample interval
{
    // Clamp window size to valid range
    if (m_sampleWindowSize < MIN_SAMPLE_WINDOW_SIZE) m_sampleWindowSize = MIN_SAMPLE_WINDOW_SIZE;
    if (m_sampleWindowSize > MAX_SAMPLE_WINDOW_SIZE) m_sampleWindowSize = MAX_SAMPLE_WINDOW_SIZE;

    // Initialize sample window to zeros
    for (uint8_t i = 0; i < MAX_SAMPLE_WINDOW_SIZE; i++) {
        m_sampleWindow[i] = 0;
    }

    // Initialize sudden appearance buffer
    for (uint8_t i = 0; i < SUDDEN_APPEARANCE_READING_COUNT; i++) {
        m_suddenAppearanceBuffer[i] = 0;
    }

    // Initialize raw reading history
    for (uint8_t i = 0; i < RAW_READING_HISTORY_SIZE; i++) {
        m_rawReadingHistory[i] = 0;
    }

    // Initialize delta history
    for (uint8_t i = 0; i < DELTA_HISTORY_SIZE; i++) {
        m_deltaHistory[i] = 0;
    }
}

void DistanceSensorBase::setSampleWindowSize(uint8_t size)
{
    // Clamp to valid range
    if (size < MIN_SAMPLE_WINDOW_SIZE) size = MIN_SAMPLE_WINDOW_SIZE;
    if (size > MAX_SAMPLE_WINDOW_SIZE) size = MAX_SAMPLE_WINDOW_SIZE;

    // Only update if changed
    if (size == m_sampleWindowSize) {
        return;
    }

    m_sampleWindowSize = size;

    // Reset window state when size changes
    m_sampleWindowIndex = 0;
    m_sampleWindowCount = 0;
    m_windowFilled = false;
    m_windowAverage = 0;
    m_lastWindowAverage = 0;
    m_lastLoggedDirection = DIRECTION_UNKNOWN;  // Reset logged direction
    m_candidateDirection = DIRECTION_UNKNOWN;   // Reset direction stability tracking
    m_directionStabilityCount = 0;

    // Clear samples
    for (uint8_t i = 0; i < MAX_SAMPLE_WINDOW_SIZE; i++) {
        m_sampleWindow[i] = 0;
    }
}

void DistanceSensorBase::updateDistanceSensor()
{
    // Get raw distance from subclass implementation
    uint32_t rawDistance = getDistanceReading();

    // Store previous average for direction detection (before updating)
    m_lastWindowAverage = m_windowAverage;

    if (rawDistance > 0 && rawDistance <= m_maxDistance) {
        // ALWAYS maintain a rolling buffer of last 3 RAW readings
        // Shift buffer: move readings down, add new reading at end
        m_suddenAppearanceBuffer[0] = m_suddenAppearanceBuffer[1];
        m_suddenAppearanceBuffer[1] = m_suddenAppearanceBuffer[2];
        m_suddenAppearanceBuffer[2] = rawDistance;

        // Track how many valid readings we have (0-3)
        if (m_consecutiveInRangeCount < SUDDEN_APPEARANCE_READING_COUNT) {
            m_consecutiveInRangeCount++;
        }

        // CRITICAL: WIPE CHECK HAPPENS HERE - BEFORE adding to window, BEFORE any other logic
        // Check if CURRENT raw reading is within warning range but window is outside warning range

        bool rawWithinWarning = (rawDistance >= m_minDistance && rawDistance <= m_detectionThreshold);
        bool windowOutsideWarning = (m_windowAverage == 0 || m_windowAverage > m_detectionThreshold);

        // Only log when state changes
        static bool lastRawWithinWarning = false;
        static bool lastWindowOutsideWarning = false;

        if (rawWithinWarning != lastRawWithinWarning || windowOutsideWarning != lastWindowOutsideWarning) {
            DEBUG_LOG_SENSOR("WIPE CHECK: raw=%u, rawInRange=%s, winAvg=%u, windowOutRange=%s",
                           rawDistance,
                           rawWithinWarning ? "YES" : "NO",
                           m_windowAverage,
                           windowOutsideWarning ? "YES" : "NO");
            lastRawWithinWarning = rawWithinWarning;
            lastWindowOutsideWarning = windowOutsideWarning;
        }

        // If raw reading is IN warning range but window is OUT of warning range, wipe everything NOW
        // BUT only if the delta is significant (> 200mm) to avoid boundary cases
        uint32_t wipeDelta = (m_windowAverage > rawDistance) ? (m_windowAverage - rawDistance) : 0;
        if (rawWithinWarning && windowOutsideWarning && wipeDelta > 200) {
            DEBUG_LOG_SENSOR(">>> WIPING: raw=%u is IN range, window=%u is OUT of range (delta=%u mm)",
                           rawDistance, m_windowAverage, wipeDelta);

            // Wipe the 3-reading buffer to contain only this new close reading
            for (uint8_t i = 0; i < SUDDEN_APPEARANCE_READING_COUNT; i++) {
                m_suddenAppearanceBuffer[i] = rawDistance;
            }
            DEBUG_LOG_SENSOR("  3-reading buffer wiped to %u mm (all 3 values)", rawDistance);

            // Capture current window average before wiping
            uint16_t previousAverage = m_windowAverage;

            // Wipe the window to this reading, passing previous average for direction candidate
            resetWindowWithDistance(rawDistance, previousAverage);
            m_windowAverage = calculateWindowAverage();
            m_currentDistance = m_windowAverage;
            DEBUG_LOG_SENSOR("  Window wiped to %u mm (all %u samples)", rawDistance, m_sampleWindowSize);

            // With median filtering, no need to wait - median is valid immediately
            // Continue with normal processing
        }

        // Now add raw reading to rolling window
        addSampleToWindow(rawDistance);

        // Update dual-mode detection state
        updateDualModeDetectionState(rawDistance);

        // Track raw distance for dual-mode detection (AFTER updateDualModeDetectionState uses previous value)
        m_lastRawDistance = rawDistance;

        // Update raw reading history for diagnostics
        m_rawReadingHistory[m_rawReadingHistoryIndex] = rawDistance;
        m_rawReadingHistoryIndex = (m_rawReadingHistoryIndex + 1) % RAW_READING_HISTORY_SIZE;
    } else {
        // Timeout/no echo/out of range - don't pollute window with max distance
        // Instead, repeat the last valid median to maintain stability
        if (m_windowAverage > 0) {
            addSampleToWindow(m_windowAverage);
        } else {
            // No valid history yet, use max distance as fallback
            addSampleToWindow(m_maxDistance);
        }

        // Reset dual-mode detection on invalid readings
        m_consecutiveInRangeCount = 0;
    }

    // Calculate new average from window
    m_windowAverage = calculateWindowAverage();
    m_currentDistance = m_windowAverage;

    // Update direction if enabled
    if (m_directionEnabled && m_windowFilled) {
        updateDirection();

        // With median filtering, no waiting period needed - direction is valid immediately
    }

    // Check for threshold crossing events
    checkThresholdEvents();
}

// =========================================================================
// Private Methods - Distance Processing
// =========================================================================

void DistanceSensorBase::addSampleToWindow(uint32_t distance_mm)
{
    // Add sample to circular buffer
    m_sampleWindow[m_sampleWindowIndex] = distance_mm;

    // Advance index (circular)
    m_sampleWindowIndex = (m_sampleWindowIndex + 1) % m_sampleWindowSize;

    // Track number of valid samples
    if (m_sampleWindowCount < m_sampleWindowSize) {
        m_sampleWindowCount++;
    }

    // Mark window as filled once we have enough samples
    if (m_sampleWindowCount >= m_sampleWindowSize) {
        m_windowFilled = true;
    }
}

uint32_t DistanceSensorBase::calculateWindowAverage() const
{
    if (m_sampleWindowCount == 0) {
        return 0;
    }

    // Use median instead of average for better outlier rejection
    // Copy window values to temporary array for sorting
    uint32_t temp[MAX_SAMPLE_WINDOW_SIZE];
    for (uint8_t i = 0; i < m_sampleWindowCount; i++) {
        temp[i] = m_sampleWindow[i];
    }

    // Simple insertion sort (efficient for small arrays)
    for (uint8_t i = 1; i < m_sampleWindowCount; i++) {
        uint32_t key = temp[i];
        int8_t j = i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j--;
        }
        temp[j + 1] = key;
    }

    // Return median value
    uint8_t midIndex = m_sampleWindowCount / 2;
    if (m_sampleWindowCount % 2 == 1) {
        // Odd number of samples: return middle value
        return temp[midIndex];
    } else {
        // Even number of samples: return average of two middle values
        return (temp[midIndex - 1] + temp[midIndex]) / 2;
    }
}

void DistanceSensorBase::resetWindowWithDistance(uint32_t distance_mm, uint16_t previousAverage)
{
    // Fill entire window buffer with the specified distance
    for (uint8_t i = 0; i < m_sampleWindowSize; i++) {
        m_sampleWindow[i] = distance_mm;
    }

    // Mark window as fully filled
    m_sampleWindowCount = m_sampleWindowSize;
    m_windowFilled = true;

    // Reset index to start
    m_sampleWindowIndex = 0;

    // Set both current and last average to this distance
    m_windowAverage = distance_mm;
    m_lastWindowAverage = distance_mm;

    // Reset delta history (no valid deltas after wipe)
    m_deltaHistoryIndex = 0;
    m_deltaHistoryCount = 0;
    for (uint8_t i = 0; i < DELTA_HISTORY_SIZE; i++) {
        m_deltaHistory[i] = 0;
    }

    // Set direction immediately based on wipe delta
    // The wipe provides high-quality direction info (large distance change)
    // Confirm it immediately rather than waiting for more samples
    if (previousAverage > 0) {
        int32_t delta = (int32_t)distance_mm - (int32_t)previousAverage;

        if (abs(delta) >= (int32_t)m_directionSensitivity) {
            if (delta < 0) {
                // New distance is smaller → moving closer → APPROACHING
                m_direction = DIRECTION_APPROACHING;
                m_candidateDirection = DIRECTION_APPROACHING;
                m_directionStabilityCount = 3;  // Already confirmed
                DEBUG_LOG_SENSOR("  Direction CONFIRMED as APPROACHING by wipe (new=%u < prev=%u, delta=%d)",
                               distance_mm, previousAverage, delta);
            } else {
                // New distance is larger → moving away → RECEDING
                m_direction = DIRECTION_RECEDING;
                m_candidateDirection = DIRECTION_RECEDING;
                m_directionStabilityCount = 3;  // Already confirmed
                DEBUG_LOG_SENSOR("  Direction CONFIRMED as RECEDING by wipe (new=%u > prev=%u, delta=%d)",
                               distance_mm, previousAverage, delta);
            }
        } else {
            // Change is within sensitivity threshold → STATIONARY
            m_direction = DIRECTION_STATIONARY;
            m_candidateDirection = DIRECTION_STATIONARY;
            m_directionStabilityCount = 3;  // Already confirmed
            DEBUG_LOG_SENSOR("  Direction CONFIRMED as STATIONARY by wipe (delta=%d < sensitivity=%u)",
                           delta, m_directionSensitivity);
        }

        m_lastLoggedDirection = m_direction;  // Mark as logged to avoid duplicate logs

        // Skip direction updates to prevent the confirmed direction from being overwritten
        // After wipe, window is filled with same value, and timeouts repeat the median
        // Skip 2x window size to ensure enough fresh samples arrive
        m_skipDirectionUpdateCount = m_sampleWindowSize * 2;
        DEBUG_LOG_SENSOR("  Will skip %u direction updates to preserve wipe-confirmed direction", m_skipDirectionUpdateCount);
    } else {
        // No previous average available - reset to unknown
        m_direction = DIRECTION_UNKNOWN;
        m_lastLoggedDirection = DIRECTION_UNKNOWN;
        m_candidateDirection = DIRECTION_UNKNOWN;
        m_directionStabilityCount = 0;
    }

    DEBUG_LOG_SENSOR("Window reset to distance=%u mm (all %u samples set to this value)",
                   distance_mm, m_sampleWindowSize);
}

bool DistanceSensorBase::isMovementDetected() const
{
    // Need filled window to detect movement
    if (!m_windowFilled || m_lastWindowAverage == 0) {
        return false;
    }

    // Calculate change between current and previous window averages
    int32_t change = abs((int32_t)m_windowAverage - (int32_t)m_lastWindowAverage);

    // Find min/max in current window to filter out noise
    uint32_t minDist = m_sampleWindow[0];
    uint32_t maxDist = m_sampleWindow[0];
    for (uint8_t i = 1; i < m_sampleWindowSize; i++) {
        if (m_sampleWindow[i] < minDist) minDist = m_sampleWindow[i];
        if (m_sampleWindow[i] > maxDist) maxDist = m_sampleWindow[i];
    }

    // Range within window (spread of readings)
    uint32_t windowRange = maxDist - minDist;

    // Adaptive threshold based on sample rate:
    // threshold = sampleInterval * VELOCITY_THRESHOLD (25 mm/ms ≈ 90 km/h)
    // - 75ms interval → 1875mm threshold (reasonable for fast detection)
    // - Faster sampling (50ms) → 1250mm threshold (more sensitive)
    // - Slower sampling (100ms) → 2500mm threshold (less noisy)
    uint32_t adaptiveThreshold = m_sampleIntervalMs * VELOCITY_THRESHOLD_MM_PER_MS;

    // Movement detected if:
    // 1. Average changed significantly (≥ adaptive threshold)
    // 2. AND window shows consistent change (low spread OR large overall change)
    bool significantChange = (change >= adaptiveThreshold);
    bool consistentReading = (windowRange < 100);  // Stable readings (not noisy)

    // Detect movement if significant change with consistent readings
    // OR if change is very large (≥1.5x threshold) regardless of consistency
    return (significantChange && consistentReading) || (change >= (adaptiveThreshold * 3 / 2));
}

void DistanceSensorBase::updateDualModeDetectionState(uint32_t rawDistance)
{
    // Check if reading is within detection range
    bool withinRange = (rawDistance >= m_minDistance && rawDistance <= m_detectionThreshold);
    bool outsideRange = (rawDistance > m_detectionThreshold);

    if (outsideRange) {
        // Reading is outside detection range
        // Check if it's approaching (distance decreasing from previous reading)
        if (m_lastRawDistance > 0 && rawDistance < m_lastRawDistance) {
            // Object is approaching from outside range - flag as gradual approach
            // Only log when transitioning from non-approach to approach mode
            if (!m_seenApproachingFromOutside) {
                DEBUG_LOG_SENSOR("Gradual approach detected: dist=%u (prev=%u), outside range, approaching",
                               rawDistance, m_lastRawDistance);
            }

            m_seenApproachingFromOutside = true;
            m_suddenAppearance = false;
            m_consecutiveInRangeCount = 0;
        }
    } else if (withinRange) {
        // Reading is within detection range
        m_consecutiveInRangeCount++;

        if (m_consecutiveInRangeCount >= SUDDEN_APPEARANCE_READING_COUNT) {
            // Have required consecutive readings within range
            if (!m_seenApproachingFromOutside && !m_suddenAppearance) {
                // Object appeared within range without prior approach from outside
                // Flag as sudden appearance - median is valid immediately, no waiting needed
                m_suddenAppearance = true;
                m_awaitingDirectionConfirmation = false;  // No confirmation wait needed with median filtering

                DEBUG_LOG_SENSOR("Sudden appearance: %u consecutive readings within range (median filtering active)",
                               m_consecutiveInRangeCount);
            }
        }
    } else {
        // Reading is below minimum range - reset state
        m_consecutiveInRangeCount = 0;
    }
}

void DistanceSensorBase::updateDirection()
{
    // Skip direction updates if requested (e.g., after wipe to preserve wipe-set candidate)
    // After wipe, window is filled with same value so we need fresh samples before direction is valid
    if (m_skipDirectionUpdateCount > 0) {
        m_skipDirectionUpdateCount--;
        DEBUG_LOG_SENSOR("  Direction update skipped (%u more to skip, preserving wipe-set candidate)",
                         m_skipDirectionUpdateCount);
        return;
    }

    if (m_windowAverage == 0 || m_lastWindowAverage == 0) {
        m_direction = DIRECTION_UNKNOWN;
        m_candidateDirection = DIRECTION_UNKNOWN;
        m_directionStabilityCount = 0;
        return;
    }

    // Calculate current delta and add to history
    int32_t currentDelta = (int32_t)m_windowAverage - (int32_t)m_lastWindowAverage;

    // Add current delta to history buffer
    m_deltaHistory[m_deltaHistoryIndex] = currentDelta;
    m_deltaHistoryIndex = (m_deltaHistoryIndex + 1) % DELTA_HISTORY_SIZE;
    if (m_deltaHistoryCount < DELTA_HISTORY_SIZE) {
        m_deltaHistoryCount++;
    }

    // Calculate median delta for robust direction detection
    int32_t medianDelta = currentDelta;  // Default to current if not enough history
    if (m_deltaHistoryCount >= 3) {
        // Copy delta history to temp array for sorting
        int32_t tempDeltas[DELTA_HISTORY_SIZE];
        for (uint8_t i = 0; i < m_deltaHistoryCount; i++) {
            tempDeltas[i] = m_deltaHistory[i];
        }

        // Simple insertion sort
        for (uint8_t i = 1; i < m_deltaHistoryCount; i++) {
            int32_t key = tempDeltas[i];
            int8_t j = i - 1;
            while (j >= 0 && tempDeltas[j] > key) {
                tempDeltas[j + 1] = tempDeltas[j];
                j--;
            }
            tempDeltas[j + 1] = key;
        }

        // Get median
        uint8_t midIndex = m_deltaHistoryCount / 2;
        if (m_deltaHistoryCount % 2 == 1) {
            medianDelta = tempDeltas[midIndex];
        } else {
            medianDelta = (tempDeltas[midIndex - 1] + tempDeltas[midIndex]) / 2;
        }
    }

    // Determine new direction candidate based on median delta
    MotionDirection newDirection;
    if (abs(medianDelta) < (int32_t)m_directionSensitivity) {
        newDirection = DIRECTION_STATIONARY;
    } else if (medianDelta < 0) {
        // Distance decreasing = object approaching
        newDirection = DIRECTION_APPROACHING;
    } else {
        // Distance increasing = object receding
        newDirection = DIRECTION_RECEDING;
    }

    // Direction stability: require consistent direction for DIRECTION_STABILITY_TIME_MS
    // Calculate required stable samples based on sample interval
    // Example: 400ms / 75ms = ~5 samples
    uint8_t requiredStableSamples = (uint8_t)((DIRECTION_STABILITY_TIME_MS + m_sampleIntervalMs - 1) / m_sampleIntervalMs);
    if (requiredStableSamples < 2) requiredStableSamples = 2;  // Minimum 2 samples

    if (newDirection == m_candidateDirection) {
        // Same direction as candidate - increment stability counter
        m_directionStabilityCount++;

        // Only confirm direction change after stability period
        if (m_directionStabilityCount >= requiredStableSamples) {
            // Direction has been stable for required time - confirm it
            if (newDirection != m_direction) {
                // Direction actually changed - log it
                const char* directionStr;
                switch (newDirection) {
                    case DIRECTION_STATIONARY:  directionStr = "STATIONARY"; break;
                    case DIRECTION_APPROACHING: directionStr = "APPROACHING"; break;
                    case DIRECTION_RECEDING:    directionStr = "RECEDING"; break;
                    default:                    directionStr = "UNKNOWN"; break;
                }

                DEBUG_LOG_SENSOR("Direction confirmed after %u samples (%u ms): %s (lastAvg=%u, currentAvg=%u, medianDelta=%ld, sensitivity=%u)",
                                 m_directionStabilityCount, m_directionStabilityCount * m_sampleIntervalMs,
                                 directionStr, m_lastWindowAverage, m_windowAverage, medianDelta, m_directionSensitivity);

                m_direction = newDirection;
                m_lastLoggedDirection = newDirection;
            }
            // Keep count at threshold (saturate) to avoid overflow
            m_directionStabilityCount = requiredStableSamples;
        }
    } else {
        // Direction changed - start new candidate
        m_candidateDirection = newDirection;
        m_directionStabilityCount = 1;  // First sample of new candidate

        // Log candidate change for debugging
        static MotionDirection lastLoggedCandidate = DIRECTION_UNKNOWN;
        if (newDirection != lastLoggedCandidate) {
            const char* candidateStr;
            switch (newDirection) {
                case DIRECTION_STATIONARY:  candidateStr = "STATIONARY"; break;
                case DIRECTION_APPROACHING: candidateStr = "APPROACHING"; break;
                case DIRECTION_RECEDING:    candidateStr = "RECEDING"; break;
                default:                    candidateStr = "UNKNOWN"; break;
            }
            DEBUG_LOG_SENSOR("Direction candidate: %s (need %u stable samples = %u ms)",
                             candidateStr, requiredStableSamples, requiredStableSamples * m_sampleIntervalMs);
            lastLoggedCandidate = newDirection;
        }
    }
}

void DistanceSensorBase::checkThresholdEvents()
{
    bool wasDetected = m_objectDetected;

    // Check if object is in detection range (not max sensor range)
    bool inRange = (m_currentDistance > 0 &&
                   m_currentDistance >= m_minDistance &&
                   m_currentDistance <= m_detectionThreshold);

    // Check for movement
    bool movementDetected = isMovementDetected();

    // If direction detection disabled, just check range
    if (!m_directionEnabled) {
        m_objectDetected = inRange;
    } else {
        // Direction detection enabled - apply dual-mode detection logic

        // Check if direction matches configured trigger mode
        bool directionMatches = false;
        static MotionDirection lastLoggedDirection = DIRECTION_UNKNOWN;
        static bool lastLoggedMatch = false;

        switch (m_directionTriggerMode) {
            case 0:  // Approaching only
                directionMatches = (m_direction == DIRECTION_APPROACHING);
                // Only log when direction or match result changes
                if (m_direction != lastLoggedDirection || directionMatches != lastLoggedMatch) {
                    DEBUG_LOG_SENSOR("Direction match check: mode=0 (APPROACHING), current=%d, matches=%d",
                                   m_direction, directionMatches);
                    lastLoggedDirection = m_direction;
                    lastLoggedMatch = directionMatches;
                }
                break;
            case 1:  // Receding only
                directionMatches = (m_direction == DIRECTION_RECEDING);
                if (m_direction != lastLoggedDirection || directionMatches != lastLoggedMatch) {
                    DEBUG_LOG_SENSOR("Direction match check: mode=1 (RECEDING), current=%d, matches=%d",
                                   m_direction, directionMatches);
                    lastLoggedDirection = m_direction;
                    lastLoggedMatch = directionMatches;
                }
                break;
            case 2:  // Both directions
                directionMatches = (m_direction == DIRECTION_APPROACHING ||
                                   m_direction == DIRECTION_RECEDING);
                if (m_direction != lastLoggedDirection || directionMatches != lastLoggedMatch) {
                    DEBUG_LOG_SENSOR("Direction match check: mode=2 (BOTH), current=%d, matches=%d",
                                   m_direction, directionMatches);
                    lastLoggedDirection = m_direction;
                    lastLoggedMatch = directionMatches;
                }
                break;
            default:
                directionMatches = true;  // Unknown mode, accept any
                if (m_direction != lastLoggedDirection || directionMatches != lastLoggedMatch) {
                    DEBUG_LOG_SENSOR("Direction match check: mode=%u (UNKNOWN), current=%d, matches=%d",
                                   m_directionTriggerMode, m_direction, directionMatches);
                    lastLoggedDirection = m_direction;
                    lastLoggedMatch = directionMatches;
                }
                break;
        }

        // Apply dual-mode detection logic
        bool shouldTrigger = false;

        if (m_seenApproachingFromOutside) {
            // Gradual approach mode - trigger immediately when in range with movement and direction match
            shouldTrigger = inRange && movementDetected && directionMatches;

            // Only log when state changes
            static bool lastGradualInRange = false;
            static bool lastGradualMovement = false;
            static bool lastGradualDirMatch = false;
            static bool lastGradualTrigger = false;

            if (inRange != lastGradualInRange ||
                movementDetected != lastGradualMovement ||
                directionMatches != lastGradualDirMatch ||
                shouldTrigger != lastGradualTrigger) {

                // Get last 5 raw readings in chronological order (oldest to newest)
                // The circular buffer index points to where NEXT write will occur, so it has the oldest data
                uint32_t raw[RAW_READING_HISTORY_SIZE];
                for (uint8_t i = 0; i < RAW_READING_HISTORY_SIZE; i++) {
                    uint8_t bufferIndex = (m_rawReadingHistoryIndex + i) % RAW_READING_HISTORY_SIZE;
                    raw[i] = m_rawReadingHistory[bufferIndex];
                }

                DEBUG_LOG_SENSOR("Gradual approach: inRange=%d, movement=%d, dirMatch=%d → trigger=%d (window avg=%u mm, last raw=[%u, %u, %u, %u, %u])",
                               inRange, movementDetected, directionMatches, shouldTrigger, m_windowAverage,
                               raw[0], raw[1], raw[2], raw[3], raw[4]);

                lastGradualInRange = inRange;
                lastGradualMovement = movementDetected;
                lastGradualDirMatch = directionMatches;
                lastGradualTrigger = shouldTrigger;
            }
        } else if (m_suddenAppearance) {
            // Sudden appearance mode - median filtering allows immediate direction detection
            // Sudden appearance ITSELF is movement, only require range and direction match
            shouldTrigger = inRange && directionMatches;

            // Only log when state changes
            static bool lastInRange = false;
            static bool lastMovementDetected = false;
            static MotionDirection lastDirection = DIRECTION_UNKNOWN;
            static bool lastDirectionMatches = false;
            static bool lastShouldTrigger = false;

            if (inRange != lastInRange ||
                movementDetected != lastMovementDetected ||
                m_direction != lastDirection ||
                directionMatches != lastDirectionMatches ||
                shouldTrigger != lastShouldTrigger) {

                const char* dirStr = "UNKNOWN";
                switch (m_direction) {
                    case DIRECTION_APPROACHING: dirStr = "APPROACHING"; break;
                    case DIRECTION_RECEDING: dirStr = "RECEDING"; break;
                    case DIRECTION_STATIONARY: dirStr = "STATIONARY"; break;
                    default: break;
                }
                const char* modeStr = (m_directionTriggerMode == 0) ? "APPROACHING" :
                                     (m_directionTriggerMode == 1) ? "RECEDING" : "BOTH";
                DEBUG_LOG_SENSOR("Sudden appearance confirmed: inRange=%d, movement=%d, dir=%s, mode=%s (raw=%u), dirMatch=%d → trigger=%d",
                               inRange, movementDetected, dirStr, modeStr, m_directionTriggerMode, directionMatches, shouldTrigger);

                lastInRange = inRange;
                lastMovementDetected = movementDetected;
                lastDirection = m_direction;
                lastDirectionMatches = directionMatches;
                lastShouldTrigger = shouldTrigger;
            }
        } else {
            // No detection mode set yet - use normal logic but require movement
            shouldTrigger = inRange && movementDetected && directionMatches;
        }

        m_objectDetected = shouldTrigger;

        // Reset detection state when object has LEFT the detection zone
        // Reset when beyond threshold AND (moving away OR stationary)
        // Only preserve state during active approach (direction=APPROACHING while outside range)
        bool beyondDetectionThreshold = (m_currentDistance > m_detectionThreshold);
        bool notApproaching = (m_direction != DIRECTION_APPROACHING);

        // Also reset if no motion detected for 5 seconds (prevents stuck state from hand waves)
        uint32_t now = millis();
        bool motionTimeout = (m_lastMotionDetectedTime > 0) &&
                            ((now - m_lastMotionDetectedTime) > 5000); // 5 second timeout

        if ((beyondDetectionThreshold && notApproaching) || motionTimeout) {
            if (m_suddenAppearance || m_seenApproachingFromOutside) {
                if (motionTimeout) {
                    DEBUG_LOG_SENSOR("No motion for 5s (last=%u ms ago), resetting dual-mode state",
                                   now - m_lastMotionDetectedTime);
                } else {
                    DEBUG_LOG_SENSOR("Object left detection zone (dist=%u > threshold=%u, dir=%d), resetting dual-mode state",
                                   m_currentDistance, m_detectionThreshold, m_direction);
                }
            }
            m_seenApproachingFromOutside = false;
            m_suddenAppearance = false;
            m_awaitingDirectionConfirmation = false;
            m_confirmationCyclesRemaining = 0;
            m_consecutiveInRangeCount = 0;
            m_lastMotionDetectedTime = 0;  // Reset timestamp
        }
    }

    // Rising edge - object entered detection zone with movement
    if (m_objectDetected && !wasDetected) {
        m_eventCount++;
        m_lastEventTime = millis();
        m_lastMotionDetectedTime = millis();  // Update timestamp for timeout tracking
        m_lastEvent = MOTION_EVENT_THRESHOLD_CROSSED;
        DEBUG_PRINTF("[DistanceSensorBase] Motion detected at %u mm (movement: %d mm, event #%u)\n",
                     m_currentDistance, abs((int32_t)m_currentDistance - (int32_t)m_lastWindowAverage), m_eventCount);
    }

    // Falling edge - object left detection zone or stopped moving
    if (!m_objectDetected && wasDetected) {
        m_lastEventTime = millis();
        m_lastEvent = MOTION_EVENT_CLEARED;
        DEBUG_PRINTLN("[DistanceSensorBase] Motion cleared (object left or stopped moving)");
    }
}
