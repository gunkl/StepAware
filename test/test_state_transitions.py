#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test State Machine Transitions - Validates mode transitions and edge cases

This test suite ensures the state machine correctly handles all mode transitions,
including edge cases like mode changes during warnings, rapid mode cycling, etc.
"""

import unittest
import time


class MockStateMachine:
    """Mock state machine matching C++ implementation"""

    # Operating modes
    MODE_OFF = 0
    MODE_CONTINUOUS_ON = 1
    MODE_MOTION_DETECT = 2

    def __init__(self):
        self.mode = self.MODE_OFF
        self.mode_changes = 0
        self.motion_events = 0
        self.warning_active = False
        self.warning_start_time = 0
        self.warning_duration = 15000  # 15 seconds
        self.current_time_ms = 0
        self.led_on = False
        self.led_blinking = False

    def set_time_ms(self, time_ms):
        self.current_time_ms = time_ms

    def advance_time(self, delta_ms):
        self.current_time_ms += delta_ms

    def set_mode(self, mode):
        if mode == self.mode:
            return  # Already in this mode

        # Exit current mode
        self.exit_mode(self.mode)

        # Change mode
        self.mode = mode
        self.mode_changes += 1

        # Enter new mode
        self.enter_mode(mode)

    def enter_mode(self, mode):
        if mode == self.MODE_OFF:
            self.led_on = False
            self.led_blinking = False
        elif mode == self.MODE_CONTINUOUS_ON:
            self.led_on = True
            self.led_blinking = True
        elif mode == self.MODE_MOTION_DETECT:
            self.led_on = False
            self.led_blinking = False

    def exit_mode(self, mode):
        # Stop any active warnings
        if self.warning_active:
            self.stop_warning()

        if mode == self.MODE_CONTINUOUS_ON:
            self.led_blinking = False

    def trigger_warning(self):
        if self.mode == self.MODE_MOTION_DETECT:
            self.warning_active = True
            self.warning_start_time = self.current_time_ms
            self.led_on = True
            self.led_blinking = True
            self.motion_events += 1

    def stop_warning(self):
        self.warning_active = False
        self.led_on = False
        self.led_blinking = False

    def update(self):
        # Check if warning expired
        if self.warning_active:
            if self.current_time_ms - self.warning_start_time >= self.warning_duration:
                self.stop_warning()

    def cycle_mode(self):
        if self.mode == self.MODE_OFF:
            self.set_mode(self.MODE_CONTINUOUS_ON)
        elif self.mode == self.MODE_CONTINUOUS_ON:
            self.set_mode(self.MODE_MOTION_DETECT)
        elif self.mode == self.MODE_MOTION_DETECT:
            self.set_mode(self.MODE_OFF)


class TestStateTransitions(unittest.TestCase):
    """Test suite for state machine transitions"""

    def setUp(self):
        self.sm = MockStateMachine()
        self.sm.set_time_ms(0)

    def test_initial_state(self):
        """Test system starts in OFF mode"""
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_OFF)
        self.assertFalse(self.sm.led_on)
        self.assertFalse(self.sm.led_blinking)
        self.assertEqual(self.sm.mode_changes, 0)

    def test_mode_cycle_sequence(self):
        """Test mode cycling: OFF -> CONTINUOUS_ON -> MOTION_DETECT -> OFF"""
        # Start in OFF
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_OFF)

        # Cycle to CONTINUOUS_ON
        self.sm.cycle_mode()
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertTrue(self.sm.led_blinking)
        self.assertEqual(self.sm.mode_changes, 1)

        # Cycle to MOTION_DETECT
        self.sm.cycle_mode()
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_MOTION_DETECT)
        self.assertFalse(self.sm.led_on)  # No motion yet
        self.assertEqual(self.sm.mode_changes, 2)

        # Cycle back to OFF
        self.sm.cycle_mode()
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_OFF)
        self.assertFalse(self.sm.led_on)
        self.assertEqual(self.sm.mode_changes, 3)

    def test_set_mode_to_same_mode(self):
        """Test setting mode to current mode does nothing"""
        self.sm.set_mode(MockStateMachine.MODE_OFF)
        self.assertEqual(self.sm.mode_changes, 0)  # No change

        self.sm.set_mode(MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertEqual(self.sm.mode_changes, 1)

        self.sm.set_mode(MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertEqual(self.sm.mode_changes, 1)  # Still 1, no change

    def test_continuous_on_led_behavior(self):
        """Test LED behavior in CONTINUOUS_ON mode"""
        self.sm.set_mode(MockStateMachine.MODE_CONTINUOUS_ON)

        # LED should be blinking
        self.assertTrue(self.sm.led_on)
        self.assertTrue(self.sm.led_blinking)

        # Should continue blinking indefinitely
        self.sm.advance_time(60000)  # 60 seconds
        self.sm.update()
        self.assertTrue(self.sm.led_blinking)

    def test_motion_detect_warning_trigger(self):
        """Test warning triggers in MOTION_DETECT mode"""
        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)

        # No warning initially
        self.assertFalse(self.sm.warning_active)
        self.assertFalse(self.sm.led_on)

        # Trigger motion
        self.sm.trigger_warning()

        # Warning should be active
        self.assertTrue(self.sm.warning_active)
        self.assertTrue(self.sm.led_on)
        self.assertEqual(self.sm.motion_events, 1)

        # After 14 seconds, still active
        self.sm.advance_time(14000)
        self.sm.update()
        self.assertTrue(self.sm.warning_active)

        # After 15 seconds, should stop
        self.sm.advance_time(1000)
        self.sm.update()
        self.assertFalse(self.sm.warning_active)
        self.assertFalse(self.sm.led_on)

    def test_mode_change_during_warning(self):
        """Test changing mode while warning is active"""
        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)
        self.sm.trigger_warning()

        # Warning is active
        self.assertTrue(self.sm.warning_active)
        self.assertTrue(self.sm.led_on)

        # Change mode
        self.sm.advance_time(5000)
        self.sm.set_mode(MockStateMachine.MODE_OFF)

        # Warning should be stopped
        self.assertFalse(self.sm.warning_active)
        self.assertFalse(self.sm.led_on)

    def test_rapid_mode_cycling(self):
        """Test rapid mode changes don't cause issues"""
        for i in range(10):
            self.sm.cycle_mode()

        # After 10 cycles, should be back at starting mode (10 % 3 = 1)
        self.assertEqual(self.sm.mode, MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertEqual(self.sm.mode_changes, 10)

    def test_continuous_on_to_motion_detect_stops_led(self):
        """Test LED stops when switching CONTINUOUS_ON -> MOTION_DETECT"""
        # This is the bug we fixed!
        self.sm.set_mode(MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertTrue(self.sm.led_blinking)

        # Switch to MOTION_DETECT
        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)

        # LED should be OFF (no motion detected yet)
        self.assertFalse(self.sm.led_on)
        self.assertFalse(self.sm.led_blinking)

    def test_off_mode_ignores_motion(self):
        """Test motion is ignored in OFF mode"""
        self.sm.set_mode(MockStateMachine.MODE_OFF)

        # Trigger warning only works in MOTION_DETECT mode
        # This test verifies that being in OFF mode, trigger_warning has no effect
        initial_events = self.sm.motion_events
        self.sm.trigger_warning()  # Should be ignored

        # Motion events should not change (trigger_warning checks mode)
        self.assertEqual(self.sm.motion_events, initial_events)

        # LED should still be off
        self.assertFalse(self.sm.led_on)

    def test_motion_event_counting(self):
        """Test motion events are counted correctly"""
        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)

        # Trigger multiple motion events
        for i in range(5):
            self.sm.trigger_warning()
            self.sm.advance_time(20000)  # Wait for warning to expire
            self.sm.update()

        self.assertEqual(self.sm.motion_events, 5)

    def test_mode_change_counter(self):
        """Test mode change counter increments correctly"""
        self.assertEqual(self.sm.mode_changes, 0)

        self.sm.set_mode(MockStateMachine.MODE_CONTINUOUS_ON)
        self.assertEqual(self.sm.mode_changes, 1)

        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)
        self.assertEqual(self.sm.mode_changes, 2)

        self.sm.set_mode(MockStateMachine.MODE_OFF)
        self.assertEqual(self.sm.mode_changes, 3)

    def test_warning_timeout_exact(self):
        """Test warning expires at exactly 15 seconds"""
        self.sm.set_mode(MockStateMachine.MODE_MOTION_DETECT)
        self.sm.trigger_warning()

        # At 14999ms, should still be active
        self.sm.advance_time(14999)
        self.sm.update()
        self.assertTrue(self.sm.warning_active)

        # At 15000ms, should stop
        self.sm.advance_time(1)
        self.sm.update()
        self.assertFalse(self.sm.warning_active)


def run_tests():
    """Run all state transition tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestStateTransitions)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
