/**
 * @file test_ultrasonic.cpp
 * @brief Unit tests for HAL_Ultrasonic class and SensorFactory
 */

#include <unity.h>
#include <stdint.h>

// Mock Arduino functions for native testing
#ifdef UNIT_TEST

unsigned long mock_millis_value = 0;
uint8_t mock_trigger_state = 0;
unsigned long mock_echo_duration = 0;

unsigned long millis() {
    return mock_millis_value;
}

void pinMode(uint8_t pin, uint8_t mode) {
    // Mock - do nothing
}

void digitalWrite(uint8_t pin, uint8_t value) {
    mock_trigger_state = value;
}

int digitalRead(uint8_t pin) {
    return 0;
}

void delayMicroseconds(unsigned int us) {
    // Mock - do nothing
}

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout) {
    return mock_echo_duration;
}

void advance_time(unsigned long ms) {
    mock_millis_value += ms;
}

void reset_time() {
    mock_millis_value = 0;
}

void set_echo_duration(unsigned long duration_us) {
    mock_echo_duration = duration_us;
}

// Mock Serial
struct {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char*, ...) {}
} Serial;

#endif

// Include sensor types and factory
#include "sensor_types.h"

// Mock Ultrasonic sensor for testing
class MockUltrasonic {
private:
    bool m_mockMode;
    bool m_initialized;
    uint32_t m_currentDistance;
    uint32_t m_lastDistance;
    uint32_t m_detectionThreshold;
    bool m_objectDetected;
    bool m_directionEnabled;
    MotionDirection m_direction;
    uint32_t m_directionSensitivity;
    MotionEvent m_lastEvent;
    uint32_t m_eventCount;
    uint32_t m_lastEventTime;
    uint32_t m_lastMeasurementTime;
    uint32_t m_measurementInterval;
    SensorCapabilities m_capabilities;

    static constexpr uint32_t MIN_MEASUREMENT_INTERVAL_MS = 60;
    static constexpr uint32_t DEFAULT_THRESHOLD_MM = 500;
    static constexpr uint32_t DEFAULT_SENSITIVITY_MM = 20;

public:
    MockUltrasonic(uint8_t triggerPin = 0, uint8_t echoPin = 0, bool mockMode = true)
        : m_mockMode(mockMode)
        , m_initialized(false)
        , m_currentDistance(0)
        , m_lastDistance(0)
        , m_detectionThreshold(DEFAULT_THRESHOLD_MM)
        , m_objectDetected(false)
        , m_directionEnabled(true)
        , m_direction(DIRECTION_UNKNOWN)
        , m_directionSensitivity(DEFAULT_SENSITIVITY_MM)
        , m_lastEvent(MOTION_EVENT_NONE)
        , m_eventCount(0)
        , m_lastEventTime(0)
        , m_lastMeasurementTime(0)
        , m_measurementInterval(MIN_MEASUREMENT_INTERVAL_MS)
    {
        m_capabilities = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC);
    }

    bool begin() {
        m_initialized = true;
        return true;
    }

    void update() {
        if (!m_initialized) return;

        uint32_t now = millis();
        if (now - m_lastMeasurementTime < m_measurementInterval) {
            return;
        }
        m_lastMeasurementTime = now;

        m_lastDistance = m_currentDistance;

        // Update direction
        if (m_directionEnabled) {
            updateDirection();
        }

        // Check threshold events
        checkThresholdEvents();
    }

    void mockSetDistance(uint32_t distance_mm) {
        m_currentDistance = distance_mm;
    }

    bool motionDetected() const { return m_objectDetected; }
    bool isReady() const { return m_initialized; }
    SensorType getSensorType() const { return SENSOR_TYPE_ULTRASONIC; }
    const SensorCapabilities& getCapabilities() const { return m_capabilities; }
    uint32_t getDistance() const { return m_currentDistance; }
    MotionDirection getDirection() const { return m_direction; }
    MotionEvent getLastEvent() const { return m_lastEvent; }
    uint32_t getEventCount() const { return m_eventCount; }
    uint32_t getLastEventTime() const { return m_lastEventTime; }
    bool isMockMode() const { return m_mockMode; }

    void setDetectionThreshold(uint32_t threshold_mm) {
        m_detectionThreshold = threshold_mm;
    }

    uint32_t getDetectionThreshold() const { return m_detectionThreshold; }

    void setDirectionDetection(bool enable) {
        m_directionEnabled = enable;
        if (!enable) m_direction = DIRECTION_UNKNOWN;
    }

    bool isDirectionDetectionEnabled() const { return m_directionEnabled; }

    void setDirectionSensitivity(uint32_t sensitivity_mm) {
        m_directionSensitivity = sensitivity_mm;
    }

    void setMeasurementInterval(uint32_t interval_ms) {
        m_measurementInterval = (interval_ms < MIN_MEASUREMENT_INTERVAL_MS)
                                ? MIN_MEASUREMENT_INTERVAL_MS
                                : interval_ms;
    }

    void resetEventCount() {
        m_eventCount = 0;
    }

private:
    void updateDirection() {
        if (m_currentDistance == 0 || m_lastDistance == 0) {
            m_direction = DIRECTION_UNKNOWN;
            return;
        }

        int32_t delta = (int32_t)m_currentDistance - (int32_t)m_lastDistance;

        if (abs(delta) < (int32_t)m_directionSensitivity) {
            m_direction = DIRECTION_STATIONARY;
        } else if (delta < 0) {
            m_direction = DIRECTION_APPROACHING;
        } else {
            m_direction = DIRECTION_RECEDING;
        }
    }

    void checkThresholdEvents() {
        bool wasDetected = m_objectDetected;
        m_objectDetected = (m_currentDistance > 0 &&
                            m_currentDistance <= m_detectionThreshold);

        if (m_objectDetected && !wasDetected) {
            m_eventCount++;
            m_lastEventTime = millis();
            m_lastEvent = MOTION_EVENT_THRESHOLD_CROSSED;
        }

        if (!m_objectDetected && wasDetected) {
            m_lastEventTime = millis();
            m_lastEvent = MOTION_EVENT_CLEARED;
        }
    }
};

MockUltrasonic* testSensor = nullptr;

void setUp(void) {
    reset_time();
    testSensor = new MockUltrasonic(8, 9, true);
}

void tearDown(void) {
    delete testSensor;
    testSensor = nullptr;
}

// =========================================================================
// Ultrasonic Sensor Tests
// =========================================================================

void test_ultrasonic_initialization(void) {
    TEST_ASSERT_NOT_NULL(testSensor);
    TEST_ASSERT_TRUE(testSensor->begin());
    TEST_ASSERT_TRUE(testSensor->isReady());
    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getDistance());
}

void test_ultrasonic_no_warmup_required(void) {
    testSensor->begin();
    // Ultrasonic sensors don't need warmup
    TEST_ASSERT_TRUE(testSensor->isReady());
}

void test_ultrasonic_sensor_type(void) {
    TEST_ASSERT_EQUAL(SENSOR_TYPE_ULTRASONIC, testSensor->getSensorType());
}

void test_ultrasonic_capabilities(void) {
    const SensorCapabilities& caps = testSensor->getCapabilities();

    TEST_ASSERT_TRUE(caps.supportsBinaryDetection);
    TEST_ASSERT_TRUE(caps.supportsDistanceMeasurement);
    TEST_ASSERT_TRUE(caps.supportsDirectionDetection);
    TEST_ASSERT_FALSE(caps.requiresWarmup);
    TEST_ASSERT_FALSE(caps.supportsDeepSleepWake);
    TEST_ASSERT_EQUAL_UINT32(20, caps.minDetectionDistance);
    TEST_ASSERT_EQUAL_UINT32(4000, caps.maxDetectionDistance);
}

void test_ultrasonic_distance_detection(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(500);  // 50cm

    // Object at 30cm (within threshold)
    testSensor->mockSetDistance(300);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_TRUE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(300, testSensor->getDistance());
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());
}

void test_ultrasonic_object_outside_threshold(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(500);

    // Object at 80cm (outside threshold)
    testSensor->mockSetDistance(800);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(800, testSensor->getDistance());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getEventCount());
}

void test_ultrasonic_threshold_change(void) {
    testSensor->begin();

    // Default threshold is 500mm
    TEST_ASSERT_EQUAL_UINT32(500, testSensor->getDetectionThreshold());

    // Change threshold
    testSensor->setDetectionThreshold(1000);
    TEST_ASSERT_EQUAL_UINT32(1000, testSensor->getDetectionThreshold());

    // Object at 80cm should now be detected
    testSensor->mockSetDistance(800);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_TRUE(testSensor->motionDetected());
}

void test_ultrasonic_direction_approaching(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(1000);
    testSensor->setDirectionSensitivity(20);

    // Initial distance 80cm
    testSensor->mockSetDistance(800);
    advance_time(100);
    testSensor->update();

    // Move closer to 60cm
    testSensor->mockSetDistance(600);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_EQUAL(DIRECTION_APPROACHING, testSensor->getDirection());
}

void test_ultrasonic_direction_receding(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(1000);
    testSensor->setDirectionSensitivity(20);

    // Initial distance 40cm
    testSensor->mockSetDistance(400);
    advance_time(100);
    testSensor->update();

    // Move away to 60cm
    testSensor->mockSetDistance(600);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_EQUAL(DIRECTION_RECEDING, testSensor->getDirection());
}

void test_ultrasonic_direction_stationary(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(1000);
    testSensor->setDirectionSensitivity(50);

    // Initial distance 50cm
    testSensor->mockSetDistance(500);
    advance_time(100);
    testSensor->update();

    // Slight movement (less than sensitivity)
    testSensor->mockSetDistance(510);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_EQUAL(DIRECTION_STATIONARY, testSensor->getDirection());
}

void test_ultrasonic_direction_detection_disabled(void) {
    testSensor->begin();
    testSensor->setDirectionDetection(false);

    TEST_ASSERT_FALSE(testSensor->isDirectionDetectionEnabled());
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, testSensor->getDirection());

    // Re-enable
    testSensor->setDirectionDetection(true);
    TEST_ASSERT_TRUE(testSensor->isDirectionDetectionEnabled());
}

void test_ultrasonic_multiple_threshold_crossings(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(500);

    // Enter detection zone
    testSensor->mockSetDistance(300);
    advance_time(100);
    testSensor->update();
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());

    // Leave detection zone
    testSensor->mockSetDistance(700);
    advance_time(100);
    testSensor->update();
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());
    TEST_ASSERT_EQUAL(MOTION_EVENT_CLEARED, testSensor->getLastEvent());

    // Enter again
    testSensor->mockSetDistance(200);
    advance_time(100);
    testSensor->update();
    TEST_ASSERT_EQUAL_UINT32(2, testSensor->getEventCount());
}

void test_ultrasonic_event_count_reset(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(500);

    testSensor->mockSetDistance(300);
    advance_time(100);
    testSensor->update();
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());

    testSensor->resetEventCount();
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getEventCount());
}

void test_ultrasonic_measurement_interval(void) {
    testSensor->begin();
    testSensor->setMeasurementInterval(100);

    testSensor->mockSetDistance(300);

    // First update
    advance_time(50);
    testSensor->update();
    // Not enough time passed, should not detect
    TEST_ASSERT_FALSE(testSensor->motionDetected());

    // Wait for interval
    advance_time(60);
    testSensor->update();
    // Now should detect
    TEST_ASSERT_TRUE(testSensor->motionDetected());
}

void test_ultrasonic_zero_distance_handling(void) {
    testSensor->begin();
    testSensor->setDetectionThreshold(500);

    // Zero distance (out of range / timeout)
    testSensor->mockSetDistance(0);
    advance_time(100);
    testSensor->update();

    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getDistance());
}

// =========================================================================
// Sensor Factory Tests
// =========================================================================

void test_sensor_type_supported_pir(void) {
    // PIR is supported
    SensorCapabilities caps = getDefaultCapabilities(SENSOR_TYPE_PIR);
    TEST_ASSERT_TRUE(caps.supportsBinaryDetection);
    TEST_ASSERT_TRUE(caps.requiresWarmup);
}

void test_sensor_type_supported_ultrasonic(void) {
    // Ultrasonic is supported
    SensorCapabilities caps = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC);
    TEST_ASSERT_TRUE(caps.supportsDistanceMeasurement);
    TEST_ASSERT_TRUE(caps.supportsDirectionDetection);
}

void test_default_config_pir(void) {
    SensorConfig config = {};
    config.type = SENSOR_TYPE_PIR;

    TEST_ASSERT_EQUAL(SENSOR_TYPE_PIR, config.type);
}

void test_default_config_ultrasonic(void) {
    SensorConfig config = {};
    config.type = SENSOR_TYPE_ULTRASONIC;
    config.detectionThreshold = 500;
    config.enableDirectionDetection = true;

    TEST_ASSERT_EQUAL(SENSOR_TYPE_ULTRASONIC, config.type);
    TEST_ASSERT_EQUAL_UINT32(500, config.detectionThreshold);
    TEST_ASSERT_TRUE(config.enableDirectionDetection);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Ultrasonic sensor tests
    RUN_TEST(test_ultrasonic_initialization);
    RUN_TEST(test_ultrasonic_no_warmup_required);
    RUN_TEST(test_ultrasonic_sensor_type);
    RUN_TEST(test_ultrasonic_capabilities);
    RUN_TEST(test_ultrasonic_distance_detection);
    RUN_TEST(test_ultrasonic_object_outside_threshold);
    RUN_TEST(test_ultrasonic_threshold_change);
    RUN_TEST(test_ultrasonic_direction_approaching);
    RUN_TEST(test_ultrasonic_direction_receding);
    RUN_TEST(test_ultrasonic_direction_stationary);
    RUN_TEST(test_ultrasonic_direction_detection_disabled);
    RUN_TEST(test_ultrasonic_multiple_threshold_crossings);
    RUN_TEST(test_ultrasonic_event_count_reset);
    RUN_TEST(test_ultrasonic_measurement_interval);
    RUN_TEST(test_ultrasonic_zero_distance_handling);

    // Sensor factory tests
    RUN_TEST(test_sensor_type_supported_pir);
    RUN_TEST(test_sensor_type_supported_ultrasonic);
    RUN_TEST(test_default_config_pir);
    RUN_TEST(test_default_config_ultrasonic);

    return UNITY_END();
}
