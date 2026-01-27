#include "distance_sensor_base.h"

DistanceSensorBase::DistanceSensorBase(uint32_t minDistance, uint32_t maxDistance, uint8_t windowSize)
    : m_currentDistance(0),
      m_detectionThreshold(DEFAULT_THRESHOLD_MM),
      m_minDistance(minDistance),
      m_maxDistance(maxDistance),
      m_objectDetected(false),
      m_directionEnabled(false),
      m_direction(DIRECTION_UNKNOWN),
      m_directionSensitivity(DEFAULT_SENSITIVITY_MM),
      m_directionTriggerMode(0),  // Default: approaching only
      m_sampleWindowSize(windowSize),
      m_sampleWindowIndex(0),
      m_sampleWindowCount(0),
      m_windowAverage(0),
      m_lastWindowAverage(0),
      m_windowFilled(false),
      m_lastEvent(MOTION_EVENT_NONE),
      m_eventCount(0),
      m_lastEventTime(0)
{
    // Clamp window size to valid range
    if (m_sampleWindowSize < MIN_SAMPLE_WINDOW_SIZE) m_sampleWindowSize = MIN_SAMPLE_WINDOW_SIZE;
    if (m_sampleWindowSize > MAX_SAMPLE_WINDOW_SIZE) m_sampleWindowSize = MAX_SAMPLE_WINDOW_SIZE;

    // Initialize sample window to zeros
    for (uint8_t i = 0; i < MAX_SAMPLE_WINDOW_SIZE; i++) {
        m_sampleWindow[i] = 0;
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

    if (rawDistance > 0) {
        // Valid reading - add to rolling window
        addSampleToWindow(rawDistance);
    } else {
        // Timeout/no echo - treat as "no object detected"
        // This prevents distance from getting stuck at old values
        // We add a max-range reading to indicate object is gone
        addSampleToWindow(m_maxDistance);
    }

    // Calculate new average from window
    m_windowAverage = calculateWindowAverage();
    m_currentDistance = m_windowAverage;

    // Update direction if enabled
    if (m_directionEnabled && m_windowFilled) {
        updateDirection();
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

    uint32_t sum = 0;
    for (uint8_t i = 0; i < m_sampleWindowCount; i++) {
        sum += m_sampleWindow[i];
    }

    return sum / m_sampleWindowCount;
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

    // Movement detected if:
    // 1. Average changed significantly (≥200mm)
    // 2. AND window shows consistent change (low spread OR large overall change)
    bool significantChange = (change >= MOVEMENT_THRESHOLD_MM);
    bool consistentReading = (windowRange < 100);  // Stable readings (not noisy)

    // Detect movement if significant change with consistent readings
    // OR if change is very large (≥300mm) regardless of consistency
    return (significantChange && consistentReading) || (change >= 300);
}

void DistanceSensorBase::updateDirection()
{
    if (m_windowAverage == 0 || m_lastWindowAverage == 0) {
        m_direction = DIRECTION_UNKNOWN;
        return;
    }

    // Direction detection based on window average change
    int32_t delta = (int32_t)m_windowAverage - (int32_t)m_lastWindowAverage;

    if (abs(delta) < (int32_t)m_directionSensitivity) {
        m_direction = DIRECTION_STATIONARY;
    } else if (delta < 0) {
        // Distance decreasing = object approaching
        m_direction = DIRECTION_APPROACHING;
        DEBUG_PRINTF("[DistanceSensorBase] Direction: APPROACHING (change: %ld mm)\n", delta);
    } else {
        // Distance increasing = object receding
        m_direction = DIRECTION_RECEDING;
        DEBUG_PRINTF("[DistanceSensorBase] Direction: RECEDING (change: %ld mm)\n", delta);
    }
}

void DistanceSensorBase::checkThresholdEvents()
{
    bool wasDetected = m_objectDetected;

    // Check if object is in range
    bool inRange = (m_currentDistance > 0 &&
                   m_currentDistance >= m_minDistance &&
                   m_currentDistance <= m_maxDistance);

    // Check for movement
    bool movementDetected = isMovementDetected();

    // If direction detection disabled, just check range
    if (!m_directionEnabled) {
        m_objectDetected = inRange;
    } else {
        // Direction detection enabled - check movement AND direction

        // Check if direction matches configured trigger mode
        bool directionMatches = false;
        switch (m_directionTriggerMode) {
            case 0:  // Approaching only
                directionMatches = (m_direction == DIRECTION_APPROACHING);
                break;
            case 1:  // Receding only
                directionMatches = (m_direction == DIRECTION_RECEDING);
                break;
            case 2:  // Both directions
                directionMatches = (m_direction == DIRECTION_APPROACHING ||
                                   m_direction == DIRECTION_RECEDING);
                break;
            default:
                directionMatches = true;  // Unknown mode, accept any
                break;
        }

        // Object detected if: in range AND moving AND direction matches
        m_objectDetected = inRange && movementDetected && directionMatches;

        DEBUG_PRINTF("[DistanceSensorBase] Check: inRange=%d, movement=%d, dirMatch=%d, dir=%d, trigMode=%d\n",
                     inRange, movementDetected, directionMatches, (int)m_direction, m_directionTriggerMode);
    }

    // Rising edge - object entered detection zone with movement
    if (m_objectDetected && !wasDetected) {
        m_eventCount++;
        m_lastEventTime = millis();
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
