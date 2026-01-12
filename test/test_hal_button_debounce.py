#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test HAL Button Debouncing - Validates button debouncing and event detection

This test suite ensures button debouncing works correctly, rejects bounces,
and properly detects clicks and long presses.
"""

import unittest


class MockButton:
    """Mock button implementation matching C++ HAL_Button behavior"""

    # Event constants
    EVENT_NONE = 0
    EVENT_PRESSED = 1
    EVENT_RELEASED = 2
    EVENT_CLICK = 3
    EVENT_LONG_PRESS = 4

    # State constants
    STATE_RELEASED = 0
    STATE_DEBOUNCING_PRESS = 1
    STATE_PRESSED = 2
    STATE_LONG_PRESS = 3
    STATE_DEBOUNCING_RELEASE = 4

    def __init__(self, pin=0, debounce_ms=50, long_press_ms=1000):
        self.pin = pin
        self.debounce_ms = debounce_ms
        self.long_press_ms = long_press_ms
        self.initialized = False
        self.state = self.STATE_RELEASED
        self.is_pressed = False
        self.raw_state = False  # False = released (HIGH), True = pressed (LOW)
        self.press_time = 0
        self.release_time = 0
        self.last_state_change = 0
        self.long_press_triggered = False
        self.click_count = 0
        self.event_queue = []
        self.current_time_ms = 0

    def begin(self):
        self.initialized = True
        return True

    def set_time_ms(self, time_ms):
        """Set simulated time"""
        self.current_time_ms = time_ms

    def advance_time(self, delta_ms):
        """Advance simulated time"""
        self.current_time_ms += delta_ms

    def mock_press(self):
        """Simulate button press (pin goes LOW)"""
        self.raw_state = True

    def mock_release(self):
        """Simulate button release (pin goes HIGH)"""
        self.raw_state = False

    def push_event(self, event):
        """Add event to queue"""
        self.event_queue.append(event)

    def has_event(self, event):
        """Check if event is in queue and remove it"""
        if event in self.event_queue:
            self.event_queue.remove(event)
            return True
        return False

    def get_next_event(self):
        """Get next event from queue"""
        if self.event_queue:
            return self.event_queue.pop(0)
        return self.EVENT_NONE

    def clear_events(self):
        """Clear all pending events"""
        self.event_queue = []

    def get_click_count(self):
        return self.click_count

    def reset_click_count(self):
        self.click_count = 0

    def update(self):
        """Update button state (matches C++ implementation)"""
        if not self.initialized:
            return

        current_raw_state = self.raw_state
        now = self.current_time_ms

        # State machine for debouncing
        if self.state == self.STATE_RELEASED:
            if current_raw_state:
                self.state = self.STATE_DEBOUNCING_PRESS
                self.last_state_change = now

        elif self.state == self.STATE_DEBOUNCING_PRESS:
            if not current_raw_state:
                # False press, back to released
                self.state = self.STATE_RELEASED
            elif now - self.last_state_change >= self.debounce_ms:
                # Debounce time elapsed, press confirmed
                self.state = self.STATE_PRESSED
                self.is_pressed = True
                self.press_time = now
                self.long_press_triggered = False
                self.push_event(self.EVENT_PRESSED)

        elif self.state == self.STATE_PRESSED:
            if not current_raw_state:
                # Button released
                self.state = self.STATE_DEBOUNCING_RELEASE
                self.last_state_change = now
            elif (not self.long_press_triggered and
                  now - self.press_time >= self.long_press_ms):
                # Long press detected
                self.state = self.STATE_LONG_PRESS
                self.long_press_triggered = True
                self.push_event(self.EVENT_LONG_PRESS)

        elif self.state == self.STATE_LONG_PRESS:
            if not current_raw_state:
                # Button released after long press
                self.state = self.STATE_DEBOUNCING_RELEASE
                self.last_state_change = now

        elif self.state == self.STATE_DEBOUNCING_RELEASE:
            if current_raw_state:
                # False release, back to pressed
                if self.long_press_triggered:
                    self.state = self.STATE_LONG_PRESS
                else:
                    self.state = self.STATE_PRESSED
            elif now - self.last_state_change >= self.debounce_ms:
                # Debounce time elapsed, release confirmed
                self.state = self.STATE_RELEASED
                self.is_pressed = False
                self.release_time = now
                self.push_event(self.EVENT_RELEASED)

                # If it wasn't a long press, it's a click
                if not self.long_press_triggered:
                    self.push_event(self.EVENT_CLICK)
                    self.click_count += 1


class TestButtonDebouncing(unittest.TestCase):
    """Test suite for button debouncing"""

    def setUp(self):
        self.button = MockButton(pin=0, debounce_ms=50, long_press_ms=1000)
        self.button.begin()
        self.button.set_time_ms(0)

    def test_simple_click(self):
        """Test simple button click without bounces"""
        # Press button
        self.button.mock_press()
        self.button.update()

        # Should be debouncing
        self.assertFalse(self.button.is_pressed)

        # After debounce time, should be pressed
        self.button.advance_time(50)
        self.button.update()
        self.assertTrue(self.button.is_pressed)
        self.assertTrue(self.button.has_event(MockButton.EVENT_PRESSED))

        # Release button
        self.button.advance_time(100)
        self.button.mock_release()
        self.button.update()

        # After debounce time, should be released
        self.button.advance_time(50)
        self.button.update()
        self.assertFalse(self.button.is_pressed)
        self.assertTrue(self.button.has_event(MockButton.EVENT_RELEASED))
        self.assertTrue(self.button.has_event(MockButton.EVENT_CLICK))
        self.assertEqual(self.button.get_click_count(), 1)

    def test_bounce_rejection_on_press(self):
        """Test that bounces during press are rejected"""
        # Press button
        self.button.mock_press()
        self.button.update()

        # Bounce: release within debounce time
        self.button.advance_time(20)
        self.button.mock_release()
        self.button.update()

        # Still in debouncing state, should go back to released
        self.assertEqual(self.button.state, MockButton.STATE_RELEASED)
        self.assertFalse(self.button.is_pressed)

        # Press again (second attempt)
        self.button.advance_time(10)
        self.button.mock_press()
        self.button.update()

        # Now debouncing this new press
        self.assertEqual(self.button.state, MockButton.STATE_DEBOUNCING_PRESS)

        # After full debounce from this press, should be confirmed
        self.button.advance_time(50)
        self.button.update()
        self.assertTrue(self.button.is_pressed)
        self.assertEqual(self.button.state, MockButton.STATE_PRESSED)

    def test_bounce_rejection_on_release(self):
        """Test that bounces during release are rejected"""
        # Press and confirm
        self.button.mock_press()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()
        self.assertTrue(self.button.is_pressed)
        self.button.clear_events()

        # Release button
        self.button.advance_time(100)
        self.button.mock_release()
        self.button.update()

        # Bounce: press again within debounce time
        self.button.advance_time(20)
        self.button.mock_press()
        self.button.update()

        # Release again
        self.button.advance_time(10)
        self.button.mock_release()
        self.button.update()

        # Should not be released yet (bouncing)
        self.assertTrue(self.button.is_pressed)

        # After full debounce time, should be released
        self.button.advance_time(50)
        self.button.update()
        self.assertFalse(self.button.is_pressed)
        self.assertTrue(self.button.has_event(MockButton.EVENT_CLICK))

    def test_long_press_detection(self):
        """Test long press detection"""
        # Press button
        self.button.mock_press()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()
        self.assertTrue(self.button.is_pressed)
        self.button.clear_events()

        # Hold for 500ms - should not be long press yet
        self.button.advance_time(500)
        self.button.update()
        self.assertFalse(self.button.has_event(MockButton.EVENT_LONG_PRESS))

        # Hold for 1000ms total - should trigger long press
        self.button.advance_time(500)
        self.button.update()
        self.assertTrue(self.button.has_event(MockButton.EVENT_LONG_PRESS))

        # Release
        self.button.advance_time(100)
        self.button.mock_release()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()

        # Should have release event but NOT click event (was long press)
        self.assertTrue(self.button.has_event(MockButton.EVENT_RELEASED))
        self.assertFalse(self.button.has_event(MockButton.EVENT_CLICK))
        self.assertEqual(self.button.get_click_count(), 0)

    def test_short_press_not_long_press(self):
        """Test that short press doesn't trigger long press"""
        # Press and hold for 900ms (just under threshold)
        self.button.mock_press()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()
        self.button.advance_time(850)
        self.button.update()

        # Should not be long press
        self.assertFalse(self.button.has_event(MockButton.EVENT_LONG_PRESS))

        # Release
        self.button.mock_release()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()

        # Should be click
        self.assertTrue(self.button.has_event(MockButton.EVENT_CLICK))
        self.assertEqual(self.button.get_click_count(), 1)

    def test_multiple_rapid_clicks(self):
        """Test multiple rapid button clicks"""
        for i in range(5):
            # Press
            self.button.mock_press()
            self.button.update()
            self.button.advance_time(50)
            self.button.update()

            # Release
            self.button.advance_time(100)
            self.button.mock_release()
            self.button.update()
            self.button.advance_time(50)
            self.button.update()

            # Check click detected
            self.assertTrue(self.button.has_event(MockButton.EVENT_CLICK))

        # Should have 5 clicks
        self.assertEqual(self.button.get_click_count(), 5)

    def test_debounce_time_exactly(self):
        """Test behavior at exact debounce time boundary"""
        # Press button
        self.button.mock_press()
        self.button.update()

        # Advance exactly debounce time
        self.button.advance_time(50)
        self.button.update()

        # Should be pressed
        self.assertTrue(self.button.is_pressed)
        self.assertTrue(self.button.has_event(MockButton.EVENT_PRESSED))

    def test_event_queue_ordering(self):
        """Test that events are queued in correct order"""
        # Press button
        self.button.mock_press()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()

        # Release button
        self.button.advance_time(100)
        self.button.mock_release()
        self.button.update()
        self.button.advance_time(50)
        self.button.update()

        # Events should be: PRESSED, RELEASED, CLICK
        self.assertEqual(self.button.get_next_event(), MockButton.EVENT_PRESSED)
        self.assertEqual(self.button.get_next_event(), MockButton.EVENT_RELEASED)
        self.assertEqual(self.button.get_next_event(), MockButton.EVENT_CLICK)
        self.assertEqual(self.button.get_next_event(), MockButton.EVENT_NONE)


def run_tests():
    """Run all button debouncing tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestButtonDebouncing)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
