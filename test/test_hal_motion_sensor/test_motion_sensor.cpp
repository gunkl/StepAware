/**
 * @file test_motion_sensor.cpp
 * @brief Unit tests for HAL_MotionSensor abstraction and HAL_PIR implementation
 */

#include <unity.h>
#include <stdint.h>

// Mock Arduino functions for native testing
#ifdef UNIT_TEST

unsigned long mock_millis_value = 0;
uint8_t mock_pin_state = 0;  // LOW = no motion

unsigned long millis() {
    return mock_millis_value;
}

void pinMode(uint8_t pin, uint8_t mode) {
    // Mock - do nothing
}

int digitalRead(uint8_t pin) {
    return mock_pin_state;
}

void advance_time(unsigned long ms) {
    mock_millis_value += ms;
}

void reset_time() {
    mock_millis_value = 0;
}

void mock_set_pin_high() {
    mock_pin_state = 1;
}

void mock_set_pin_low() {
    mock_pin_state = 0;
}

// Mock Serial
struct {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char*, ...) {}
} Serial;

#endif

// Include sensor types
#include "sensor_types.h"

// Test sensor capabilities structure
void test_sensor_capabilities_default_pir(void) {
    SensorCapabilities caps = getDefaultCapabilities(SENSOR_TYPE_PIR);

    TEST_ASSERT_TRUE(caps.supportsBinaryDetection);
    TEST_ASSERT_FALSE(caps.supportsDistanceMeasurement);
    TEST_ASSERT_FALSE(caps.supportsDirectionDetection);
    TEST_ASSERT_TRUE(caps.requiresWarmup);
    TEST_ASSERT_TRUE(caps.supportsDeepSleepWake);
    TEST_ASSERT_EQUAL_STRING("PIR Motion Sensor", caps.sensorTypeName);
}

void test_sensor_capabilities_default_ultrasonic(void) {
    SensorCapabilities caps = getDefaultCapabilities(SENSOR_TYPE_ULTRASONIC);

    TEST_ASSERT_TRUE(caps.supportsBinaryDetection);
    TEST_ASSERT_TRUE(caps.supportsDistanceMeasurement);
    TEST_ASSERT_TRUE(caps.supportsDirectionDetection);
    TEST_ASSERT_FALSE(caps.requiresWarmup);
    TEST_ASSERT_FALSE(caps.supportsDeepSleepWake);
    TEST_ASSERT_EQUAL_STRING("Ultrasonic Distance Sensor", caps.sensorTypeName);
}

void test_sensor_type_name(void) {
    TEST_ASSERT_EQUAL_STRING("PIR", getSensorTypeName(SENSOR_TYPE_PIR));
    TEST_ASSERT_EQUAL_STRING("IR", getSensorTypeName(SENSOR_TYPE_IR));
    TEST_ASSERT_EQUAL_STRING("Ultrasonic", getSensorTypeName(SENSOR_TYPE_ULTRASONIC));
    TEST_ASSERT_EQUAL_STRING("Passive IR", getSensorTypeName(SENSOR_TYPE_PASSIVE_IR));
    TEST_ASSERT_EQUAL_STRING("Unknown", getSensorTypeName((SensorType)99));
}

void test_motion_event_enum(void) {
    TEST_ASSERT_EQUAL(0, MOTION_EVENT_NONE);
    TEST_ASSERT_EQUAL(1, MOTION_EVENT_DETECTED);
    TEST_ASSERT_EQUAL(2, MOTION_EVENT_CLEARED);
    TEST_ASSERT_NOT_EQUAL(MOTION_EVENT_NONE, MOTION_EVENT_DETECTED);
}

void test_motion_direction_enum(void) {
    TEST_ASSERT_EQUAL(0, DIRECTION_UNKNOWN);
    TEST_ASSERT_EQUAL(1, DIRECTION_STATIONARY);
    TEST_ASSERT_EQUAL(2, DIRECTION_APPROACHING);
    TEST_ASSERT_EQUAL(3, DIRECTION_RECEDING);
}

void test_sensor_status_struct(void) {
    SensorStatus status = {};

    TEST_ASSERT_FALSE(status.ready);
    TEST_ASSERT_FALSE(status.motionDetected);
    TEST_ASSERT_EQUAL_UINT32(0, status.lastEventTime);
    TEST_ASSERT_EQUAL_UINT32(0, status.eventCount);
    TEST_ASSERT_EQUAL_UINT32(0, status.distance);
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, status.direction);
    TEST_ASSERT_EQUAL(MOTION_EVENT_NONE, status.lastEvent);
}

void test_sensor_config_struct(void) {
    SensorConfig config = {};
    config.type = SENSOR_TYPE_PIR;
    config.primaryPin = 5;
    config.detectionThreshold = 1000;
    config.debounceMs = 50;

    TEST_ASSERT_EQUAL(SENSOR_TYPE_PIR, config.type);
    TEST_ASSERT_EQUAL_UINT8(5, config.primaryPin);
    TEST_ASSERT_EQUAL_UINT32(1000, config.detectionThreshold);
    TEST_ASSERT_EQUAL_UINT32(50, config.debounceMs);
}

// Mock PIR sensor implementation for testing polymorphism
class MockMotionSensor {
private:
    bool m_mockMode;
    bool m_ready;
    bool m_motionDetected;
    uint32_t m_eventCount;
    uint32_t m_lastEventTime;
    MotionEvent m_lastEvent;
    SensorCapabilities m_capabilities;

public:
    MockMotionSensor(bool mockMode = true)
        : m_mockMode(mockMode)
        , m_ready(false)
        , m_motionDetected(false)
        , m_eventCount(0)
        , m_lastEventTime(0)
        , m_lastEvent(MOTION_EVENT_NONE)
    {
        m_capabilities = getDefaultCapabilities(SENSOR_TYPE_PIR);
    }

    bool begin() {
        return true;
    }

    void update() {
        // Simulate warmup after 60 seconds
        if (!m_ready && millis() >= 60000) {
            m_ready = true;
        }
    }

    bool motionDetected() const { return m_motionDetected; }
    bool isReady() const { return m_ready; }
    SensorType getSensorType() const { return SENSOR_TYPE_PIR; }
    const SensorCapabilities& getCapabilities() const { return m_capabilities; }
    uint32_t getWarmupTimeRemaining() const {
        if (m_ready) return 0;
        uint32_t elapsed = millis();
        if (elapsed >= 60000) return 0;
        return 60000 - elapsed;
    }
    MotionEvent getLastEvent() const { return m_lastEvent; }
    uint32_t getEventCount() const { return m_eventCount; }
    void resetEventCount() { m_eventCount = 0; }
    uint32_t getLastEventTime() const { return m_lastEventTime; }
    bool isMockMode() const { return m_mockMode; }

    void mockSetMotion(bool detected) {
        if (m_mockMode) {
            bool wasDetected = m_motionDetected;
            m_motionDetected = detected;

            if (detected && !wasDetected) {
                m_eventCount++;
                m_lastEventTime = millis();
                m_lastEvent = MOTION_EVENT_DETECTED;
            } else if (!detected && wasDetected) {
                m_lastEventTime = millis();
                m_lastEvent = MOTION_EVENT_CLEARED;
            }
        }
    }

    void mockSetReady() {
        if (m_mockMode) {
            m_ready = true;
        }
    }
};

MockMotionSensor* testSensor = nullptr;

void setUp(void) {
    reset_time();
    mock_set_pin_low();
    testSensor = new MockMotionSensor(true);
}

void tearDown(void) {
    delete testSensor;
    testSensor = nullptr;
}

void test_mock_sensor_initialization(void) {
    TEST_ASSERT_NOT_NULL(testSensor);
    TEST_ASSERT_TRUE(testSensor->begin());
    TEST_ASSERT_FALSE(testSensor->isReady());  // Not ready before warmup
    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getEventCount());
}

void test_mock_sensor_warmup(void) {
    testSensor->begin();

    // Before warmup
    TEST_ASSERT_FALSE(testSensor->isReady());
    TEST_ASSERT_GREATER_THAN(0, testSensor->getWarmupTimeRemaining());

    // Partial warmup
    advance_time(30000);  // 30 seconds
    testSensor->update();
    TEST_ASSERT_FALSE(testSensor->isReady());
    TEST_ASSERT_EQUAL_UINT32(30000, testSensor->getWarmupTimeRemaining());

    // Complete warmup
    advance_time(30000);  // Total 60 seconds
    testSensor->update();
    TEST_ASSERT_TRUE(testSensor->isReady());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getWarmupTimeRemaining());
}

void test_mock_sensor_skip_warmup(void) {
    testSensor->begin();
    TEST_ASSERT_FALSE(testSensor->isReady());

    // Use mock to skip warmup
    testSensor->mockSetReady();
    TEST_ASSERT_TRUE(testSensor->isReady());
}

void test_mock_sensor_motion_detection(void) {
    testSensor->begin();
    testSensor->mockSetReady();

    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getEventCount());

    // Trigger motion
    testSensor->mockSetMotion(true);
    TEST_ASSERT_TRUE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());
    TEST_ASSERT_EQUAL(MOTION_EVENT_DETECTED, testSensor->getLastEvent());

    // Clear motion
    advance_time(1000);
    testSensor->mockSetMotion(false);
    TEST_ASSERT_FALSE(testSensor->motionDetected());
    TEST_ASSERT_EQUAL(MOTION_EVENT_CLEARED, testSensor->getLastEvent());
    TEST_ASSERT_EQUAL_UINT32(1000, testSensor->getLastEventTime());
}

void test_mock_sensor_multiple_events(void) {
    testSensor->begin();
    testSensor->mockSetReady();

    // Multiple motion events
    testSensor->mockSetMotion(true);
    testSensor->mockSetMotion(false);
    testSensor->mockSetMotion(true);
    testSensor->mockSetMotion(false);
    testSensor->mockSetMotion(true);

    TEST_ASSERT_EQUAL_UINT32(3, testSensor->getEventCount());
}

void test_mock_sensor_event_count_reset(void) {
    testSensor->begin();
    testSensor->mockSetReady();

    testSensor->mockSetMotion(true);
    testSensor->mockSetMotion(false);
    TEST_ASSERT_EQUAL_UINT32(1, testSensor->getEventCount());

    testSensor->resetEventCount();
    TEST_ASSERT_EQUAL_UINT32(0, testSensor->getEventCount());
}

void test_mock_sensor_capabilities(void) {
    const SensorCapabilities& caps = testSensor->getCapabilities();

    TEST_ASSERT_TRUE(caps.supportsBinaryDetection);
    TEST_ASSERT_FALSE(caps.supportsDistanceMeasurement);
    TEST_ASSERT_TRUE(caps.requiresWarmup);
    TEST_ASSERT_EQUAL(SENSOR_TYPE_PIR, testSensor->getSensorType());
}

void test_mock_sensor_mock_mode(void) {
    TEST_ASSERT_TRUE(testSensor->isMockMode());

    MockMotionSensor realSensor(false);  // Not mock mode
    TEST_ASSERT_FALSE(realSensor.isMockMode());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Sensor types tests
    RUN_TEST(test_sensor_capabilities_default_pir);
    RUN_TEST(test_sensor_capabilities_default_ultrasonic);
    RUN_TEST(test_sensor_type_name);
    RUN_TEST(test_motion_event_enum);
    RUN_TEST(test_motion_direction_enum);
    RUN_TEST(test_sensor_status_struct);
    RUN_TEST(test_sensor_config_struct);

    // Mock sensor tests
    RUN_TEST(test_mock_sensor_initialization);
    RUN_TEST(test_mock_sensor_warmup);
    RUN_TEST(test_mock_sensor_skip_warmup);
    RUN_TEST(test_mock_sensor_motion_detection);
    RUN_TEST(test_mock_sensor_multiple_events);
    RUN_TEST(test_mock_sensor_event_count_reset);
    RUN_TEST(test_mock_sensor_capabilities);
    RUN_TEST(test_mock_sensor_mock_mode);

    return UNITY_END();
}
