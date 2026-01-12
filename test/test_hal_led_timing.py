#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test HAL LED Timing - Validates LED pattern timing and brightness levels

This test suite ensures LED patterns execute with correct timing intervals,
brightness levels, and duration limits.
"""

import unittest
import time
from pathlib import Path


class MockLED:
    """Mock LED implementation matching C++ HAL_LED behavior"""

    # Pattern constants
    PATTERN_OFF = 0
    PATTERN_ON = 1
    PATTERN_BLINK_FAST = 2
    PATTERN_BLINK_SLOW = 3
    PATTERN_BLINK_WARNING = 4
    PATTERN_PULSE = 5
    PATTERN_CUSTOM = 6

    # Timing constants (from config.h)
    LED_BLINK_FAST_MS = 250
    LED_BLINK_SLOW_MS = 1000
    LED_BLINK_WARNING_MS = 500

    # Brightness levels
    LED_BRIGHTNESS_OFF = 0
    LED_BRIGHTNESS_DIM = 20
    LED_BRIGHTNESS_MEDIUM = 128
    LED_BRIGHTNESS_FULL = 255

    def __init__(self, pin=3, pwm_channel=0):
        self.pin = pin
        self.pwm_channel = pwm_channel
        self.initialized = False
        self.current_pattern = self.PATTERN_OFF
        self.brightness = 0
        self.is_on = False
        self.pattern_start_time = 0
        self.pattern_duration = 0
        self.last_toggle_time = 0
        self.pattern_state = False
        self.custom_on_ms = 500
        self.custom_off_ms = 500
        self.current_time_ms = 0  # Simulated time

    def begin(self):
        self.initialized = True
        return True

    def set_time_ms(self, time_ms):
        """Set simulated time for testing"""
        self.current_time_ms = time_ms

    def advance_time(self, delta_ms):
        """Advance simulated time"""
        self.current_time_ms += delta_ms

    def get_pattern_interval(self):
        """Get interval for current pattern"""
        if self.current_pattern == self.PATTERN_BLINK_FAST:
            return self.LED_BLINK_FAST_MS
        elif self.current_pattern == self.PATTERN_BLINK_SLOW:
            return self.LED_BLINK_SLOW_MS
        elif self.current_pattern == self.PATTERN_BLINK_WARNING:
            return self.LED_BLINK_WARNING_MS
        elif self.current_pattern == self.PATTERN_CUSTOM:
            return self.custom_on_ms if self.pattern_state else self.custom_off_ms
        return 0

    def update(self):
        """Update LED state (matches C++ implementation)"""
        if not self.initialized:
            return

        # Check if pattern duration has expired
        if self.pattern_duration > 0:
            if self.current_time_ms - self.pattern_start_time >= self.pattern_duration:
                self.stop_pattern()
                return

        # Process pattern logic
        if self.current_pattern == self.PATTERN_OFF:
            pass
        elif self.current_pattern == self.PATTERN_ON:
            pass
        elif self.current_pattern in [self.PATTERN_BLINK_FAST, self.PATTERN_BLINK_SLOW,
                                       self.PATTERN_BLINK_WARNING, self.PATTERN_CUSTOM]:
            interval = self.get_pattern_interval()
            if self.current_time_ms - self.last_toggle_time >= interval:
                self.pattern_state = not self.pattern_state
                self.last_toggle_time = self.current_time_ms

                if self.pattern_state:
                    self.brightness = self.LED_BRIGHTNESS_FULL
                    self.is_on = True
                else:
                    self.brightness = 0
                    self.is_on = False

    def on(self, brightness=LED_BRIGHTNESS_FULL):
        self.brightness = brightness
        self.is_on = True
        self.current_pattern = self.PATTERN_ON

    def off(self):
        self.brightness = 0
        self.is_on = False
        self.current_pattern = self.PATTERN_OFF

    def set_brightness(self, brightness):
        self.brightness = brightness

    def get_brightness(self):
        return self.brightness

    def start_pattern(self, pattern, duration_ms=0):
        self.current_pattern = pattern
        self.pattern_start_time = self.current_time_ms
        self.pattern_duration = duration_ms
        self.last_toggle_time = self.current_time_ms
        self.pattern_state = False

        if pattern == self.PATTERN_OFF:
            self.off()
        elif pattern == self.PATTERN_ON:
            self.on(self.LED_BRIGHTNESS_FULL)

    def stop_pattern(self):
        self.current_pattern = self.PATTERN_OFF
        self.pattern_duration = 0
        self.off()

    def get_pattern(self):
        return self.current_pattern

    def is_pattern_active(self):
        return self.current_pattern != self.PATTERN_OFF

    def set_custom_pattern(self, on_ms, off_ms):
        self.custom_on_ms = on_ms
        self.custom_off_ms = off_ms


class TestLEDTiming(unittest.TestCase):
    """Test suite for LED pattern timing"""

    def setUp(self):
        self.led = MockLED()
        self.led.begin()
        self.led.set_time_ms(0)

    def test_blink_fast_timing(self):
        """Test fast blink pattern timing (250ms intervals)"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_FAST)

        # Should start in OFF state
        self.assertFalse(self.led.is_on)

        # After 250ms, should be ON
        self.led.advance_time(250)
        self.led.update()
        self.assertTrue(self.led.is_on)
        self.assertEqual(self.led.get_brightness(), MockLED.LED_BRIGHTNESS_FULL)

        # After another 250ms, should be OFF
        self.led.advance_time(250)
        self.led.update()
        self.assertFalse(self.led.is_on)
        self.assertEqual(self.led.get_brightness(), 0)

        # After another 250ms, should be ON again
        self.led.advance_time(250)
        self.led.update()
        self.assertTrue(self.led.is_on)

    def test_blink_slow_timing(self):
        """Test slow blink pattern timing (1000ms intervals)"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_SLOW)

        # Should start in OFF state
        self.assertFalse(self.led.is_on)

        # After 1000ms, should be ON
        self.led.advance_time(1000)
        self.led.update()
        self.assertTrue(self.led.is_on)

        # After another 1000ms, should be OFF
        self.led.advance_time(1000)
        self.led.update()
        self.assertFalse(self.led.is_on)

    def test_blink_warning_timing(self):
        """Test warning blink pattern timing (500ms intervals)"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_WARNING)

        # Should start in OFF state
        self.assertFalse(self.led.is_on)

        # After 500ms, should be ON
        self.led.advance_time(500)
        self.led.update()
        self.assertTrue(self.led.is_on)

        # After another 500ms, should be OFF
        self.led.advance_time(500)
        self.led.update()
        self.assertFalse(self.led.is_on)

    def test_pattern_duration_finite(self):
        """Test pattern stops after finite duration"""
        # Start 15-second warning pattern
        self.led.start_pattern(MockLED.PATTERN_BLINK_WARNING, 15000)

        # Should be active
        self.assertTrue(self.led.is_pattern_active())

        # After 14 seconds, still active
        self.led.advance_time(14000)
        self.led.update()
        self.assertTrue(self.led.is_pattern_active())

        # After 15 seconds, should stop
        self.led.advance_time(1000)
        self.led.update()
        self.assertFalse(self.led.is_pattern_active())
        self.assertEqual(self.led.get_pattern(), MockLED.PATTERN_OFF)

    def test_pattern_duration_infinite(self):
        """Test pattern runs indefinitely when duration=0"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_WARNING, 0)

        # Should be active
        self.assertTrue(self.led.is_pattern_active())

        # After 60 seconds, still active
        self.led.advance_time(60000)
        self.led.update()
        self.assertTrue(self.led.is_pattern_active())

        # After 120 seconds, still active
        self.led.advance_time(60000)
        self.led.update()
        self.assertTrue(self.led.is_pattern_active())

    def test_brightness_levels(self):
        """Test different brightness levels"""
        # Full brightness
        self.led.on(MockLED.LED_BRIGHTNESS_FULL)
        self.assertEqual(self.led.get_brightness(), 255)

        # Medium brightness
        self.led.set_brightness(MockLED.LED_BRIGHTNESS_MEDIUM)
        self.assertEqual(self.led.get_brightness(), 128)

        # Dim brightness
        self.led.set_brightness(MockLED.LED_BRIGHTNESS_DIM)
        self.assertEqual(self.led.get_brightness(), 20)

        # Off
        self.led.set_brightness(MockLED.LED_BRIGHTNESS_OFF)
        self.assertEqual(self.led.get_brightness(), 0)

    def test_custom_pattern_timing(self):
        """Test custom pattern with asymmetric timing"""
        # Set custom pattern: 300ms ON, 700ms OFF
        self.led.set_custom_pattern(300, 700)
        self.led.start_pattern(MockLED.PATTERN_CUSTOM)

        # Should start OFF
        self.assertFalse(self.led.is_on)

        # After 300ms (first interval is OFF time), should toggle
        self.led.advance_time(700)
        self.led.update()
        self.assertTrue(self.led.is_on)

        # After 300ms more (ON time), should be OFF
        self.led.advance_time(300)
        self.led.update()
        self.assertFalse(self.led.is_on)

    def test_pattern_interruption(self):
        """Test stopping pattern before duration expires"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_FAST, 10000)

        # Should be active
        self.assertTrue(self.led.is_pattern_active())

        # Stop pattern early
        self.led.advance_time(2000)
        self.led.stop_pattern()

        # Should be OFF
        self.assertFalse(self.led.is_pattern_active())
        self.assertEqual(self.led.get_pattern(), MockLED.PATTERN_OFF)
        self.assertFalse(self.led.is_on)

    def test_pattern_state_after_expiration(self):
        """Test LED state after pattern expires"""
        self.led.start_pattern(MockLED.PATTERN_BLINK_WARNING, 1000)

        # Advance past expiration
        self.led.advance_time(1500)
        self.led.update()

        # Should be off and inactive
        self.assertFalse(self.led.is_on)
        self.assertFalse(self.led.is_pattern_active())
        self.assertEqual(self.led.get_brightness(), 0)


def run_tests():
    """Run all LED timing tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestLEDTiming)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
