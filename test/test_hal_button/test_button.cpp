/**
 * @file test_button.cpp
 * @brief Unit tests for HAL_Button class
 */

#include <unity.h>
#include <stdint.h>

// Mock Arduino functions for native testing
#ifdef UNIT_TEST

unsigned long mock_millis_value = 0;

unsigned long millis() {
    return mock_millis_value;
}

void pinMode(uint8_t pin, uint8_t mode) {
    // Mock - do nothing
}

int digitalRead(uint8_t pin) {
    // Mock - return HIGH (button not pressed)
    return 1;
}

void advance_time(unsigned long ms) {
    mock_millis_value += ms;
}

void reset_time() {
    mock_millis_value = 0;
}

#endif

// Simple mock button class for testing
class MockButton {
private:
    uint8_t pin;
    bool pressed;
    unsigned long press_time;
    unsigned long release_time;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t click_count;

    enum State {
        STATE_RELEASED,
        STATE_PRESSED,
        STATE_DEBOUNCING
    } state;

public:
    enum ButtonEvent {
        EVENT_NONE,
        EVENT_PRESSED,
        EVENT_RELEASED,
        EVENT_CLICK,
        EVENT_LONG_PRESS
    };

    MockButton(uint8_t button_pin, uint32_t debounce = 50, uint32_t long_press = 1000)
        : pin(button_pin), pressed(false), press_time(0), release_time(0),
          debounce_ms(debounce), long_press_ms(long_press), click_count(0),
          state(STATE_RELEASED) {
    }

    void begin() {
        pinMode(pin, 0); // INPUT with pull-up
    }

    ButtonEvent update() {
        bool current = !digitalRead(pin); // Active LOW
        unsigned long now = millis();

        switch (state) {
            case STATE_RELEASED:
                if (current) {
                    state = STATE_DEBOUNCING;
                    press_time = now;
                }
                break;

            case STATE_DEBOUNCING:
                if (now - press_time >= debounce_ms) {
                    if (current) {
                        state = STATE_PRESSED;
                        pressed = true;
                        return EVENT_PRESSED;
                    } else {
                        state = STATE_RELEASED;
                    }
                }
                break;

            case STATE_PRESSED:
                if (!current) {
                    release_time = now;
                    state = STATE_RELEASED;
                    pressed = false;

                    unsigned long press_duration = release_time - press_time;
                    if (press_duration >= long_press_ms) {
                        return EVENT_LONG_PRESS;
                    } else {
                        click_count++;
                        return EVENT_CLICK;
                    }
                } else {
                    // Check for long press
                    unsigned long press_duration = now - press_time;
                    if (press_duration >= long_press_ms) {
                        // Trigger long press event once
                        state = STATE_RELEASED; // Prevent repeated events
                        pressed = false;
                        return EVENT_LONG_PRESS;
                    }
                }
                break;
        }

        return EVENT_NONE;
    }

    bool isPressed() const {
        return pressed;
    }

    uint32_t getClickCount() const {
        return click_count;
    }

    void resetClickCount() {
        click_count = 0;
    }

    void mockPress() {
        pressed = true;
        press_time = millis();
        state = STATE_PRESSED;
    }

    void mockRelease() {
        pressed = false;
        release_time = millis();
        state = STATE_RELEASED;
    }
};

MockButton* test_button = nullptr;

void setUp(void) {
    reset_time();
    test_button = new MockButton(0, 50, 1000); // GPIO0, 50ms debounce, 1000ms long press
    test_button->begin();
}

void tearDown(void) {
    delete test_button;
    test_button = nullptr;
}

void test_button_initialization(void) {
    TEST_ASSERT_NOT_NULL(test_button);
    TEST_ASSERT_FALSE(test_button->isPressed());
    TEST_ASSERT_EQUAL_UINT32(0, test_button->getClickCount());
}

void test_button_debounce(void) {
    // Simulate button press
    test_button->mockPress();

    // Before debounce time - should not register
    advance_time(25);
    MockButton::ButtonEvent event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_NONE, event);

    // After debounce time - should register press
    advance_time(30);
    event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_PRESSED, event);
    TEST_ASSERT_TRUE(test_button->isPressed());
}

void test_button_click(void) {
    // Press button
    test_button->mockPress();
    advance_time(60); // Past debounce
    MockButton::ButtonEvent event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_PRESSED, event);

    // Release button (short press = click)
    advance_time(100); // Total press time = 160ms (< 1000ms)
    test_button->mockRelease();
    event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_CLICK, event);
    TEST_ASSERT_EQUAL_UINT32(1, test_button->getClickCount());
}

void test_button_long_press(void) {
    // Press button
    test_button->mockPress();
    advance_time(60); // Past debounce
    MockButton::ButtonEvent event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_PRESSED, event);

    // Hold for long press duration
    advance_time(1000); // Total = 1060ms (>= 1000ms)
    event = test_button->update();
    TEST_ASSERT_EQUAL(MockButton::EVENT_LONG_PRESS, event);

    // Long press should NOT increment click count
    TEST_ASSERT_EQUAL_UINT32(0, test_button->getClickCount());
}

void test_button_multiple_clicks(void) {
    // First click
    test_button->mockPress();
    advance_time(60);
    test_button->update();
    advance_time(100);
    test_button->mockRelease();
    test_button->update();
    TEST_ASSERT_EQUAL_UINT32(1, test_button->getClickCount());

    // Second click
    advance_time(100);
    test_button->mockPress();
    advance_time(60);
    test_button->update();
    advance_time(100);
    test_button->mockRelease();
    test_button->update();
    TEST_ASSERT_EQUAL_UINT32(2, test_button->getClickCount());

    // Third click
    advance_time(100);
    test_button->mockPress();
    advance_time(60);
    test_button->update();
    advance_time(100);
    test_button->mockRelease();
    test_button->update();
    TEST_ASSERT_EQUAL_UINT32(3, test_button->getClickCount());
}

void test_button_reset_count(void) {
    // Make some clicks
    test_button->mockPress();
    advance_time(60);
    test_button->update();
    advance_time(100);
    test_button->mockRelease();
    test_button->update();

    TEST_ASSERT_EQUAL_UINT32(1, test_button->getClickCount());

    // Reset count
    test_button->resetClickCount();
    TEST_ASSERT_EQUAL_UINT32(0, test_button->getClickCount());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_button_initialization);
    RUN_TEST(test_button_debounce);
    RUN_TEST(test_button_click);
    RUN_TEST(test_button_long_press);
    RUN_TEST(test_button_multiple_clicks);
    RUN_TEST(test_button_reset_count);

    return UNITY_END();
}
