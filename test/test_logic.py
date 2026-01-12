#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
StepAware Logic Tests (Python)
Tests the core state machine and button logic without needing C++ compilation.
"""

import sys
import os

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

from enum import Enum

class TestResult:
    def __init__(self):
        self.tests_run = 0
        self.tests_passed = 0
        self.tests_failed = 0
        self.failures = []

    def run_test(self, name, test_func):
        """Run a single test"""
        self.tests_run += 1
        try:
            test_func()
            self.tests_passed += 1
            print(f"✓ {name}")
            return True
        except AssertionError as e:
            self.tests_failed += 1
            self.failures.append((name, str(e)))
            print(f"✗ {name}: {e}")
            return False
        except Exception as e:
            self.tests_failed += 1
            self.failures.append((name, f"Exception: {e}"))
            print(f"✗ {name}: Exception: {e}")
            return False

    def print_summary(self):
        """Print test summary"""
        print(f"\n{'='*60}")
        print(f"Test Summary")
        print(f"{'='*60}")
        print(f"Total Tests: {self.tests_run}")
        print(f"Passed: {self.tests_passed} ({100*self.tests_passed//self.tests_run if self.tests_run > 0 else 0}%)")
        print(f"Failed: {self.tests_failed}")
        print(f"{'='*60}\n")

        if self.failures:
            print("Failed Tests:")
            for name, error in self.failures:
                print(f"  - {name}: {error}")
            print()

        return self.tests_failed == 0


# State Machine Implementation (matches C++ logic)
class OperatingMode(Enum):
    OFF = 0
    CONTINUOUS_ON = 1
    MOTION_DETECT = 2

class StateMachine:
    def __init__(self):
        self.mode = OperatingMode.OFF
        self.motion_events = 0
        self.mode_changes = 0
        self.led_on = False
        self.warning_active = False
        self.warning_end_time = 0
        self.time = 0  # Mock time in ms

    def advance_time(self, ms):
        self.time += ms

    def get_time(self):
        return self.time

    def cycle_mode(self):
        """Cycle through modes: OFF -> CONTINUOUS_ON -> MOTION_DETECT -> OFF"""
        mode_sequence = [OperatingMode.OFF, OperatingMode.CONTINUOUS_ON, OperatingMode.MOTION_DETECT]
        current_index = mode_sequence.index(self.mode)
        next_index = (current_index + 1) % len(mode_sequence)
        self.mode = mode_sequence[next_index]
        self.mode_changes += 1

        # Update LED based on mode
        if self.mode == OperatingMode.CONTINUOUS_ON:
            self.led_on = True
        else:
            self.led_on = False

    def set_mode(self, new_mode):
        """Set mode directly"""
        if self.mode != new_mode:
            self.mode = new_mode
            self.mode_changes += 1

            if self.mode == OperatingMode.CONTINUOUS_ON:
                self.led_on = True
            else:
                self.led_on = False

    def handle_motion(self):
        """Handle motion detection event"""
        if self.mode == OperatingMode.MOTION_DETECT:
            self.motion_events += 1
            self.warning_active = True
            self.warning_end_time = self.time + 15000  # 15 seconds

    def update(self):
        """Update state machine (call in loop)"""
        # Check if warning expired
        if self.warning_active and self.time >= self.warning_end_time:
            self.warning_active = False

    def is_led_on(self):
        """Check if LED should be on"""
        return self.led_on or self.warning_active

    def reset_stats(self):
        """Reset statistics"""
        self.motion_events = 0
        self.mode_changes = 0


# Button Implementation
class Button:
    def __init__(self, debounce_ms=50, long_press_ms=1000):
        self.debounce_ms = debounce_ms
        self.long_press_ms = long_press_ms
        self.pressed = False
        self.press_time = 0
        self.click_count = 0
        self.time = 0

    def advance_time(self, ms):
        self.time += ms

    def mock_press(self):
        """Simulate button press"""
        self.pressed = True
        self.press_time = self.time

    def mock_release(self):
        """Simulate button release"""
        if self.pressed:
            press_duration = self.time - self.press_time
            self.pressed = False

            if press_duration < self.long_press_ms:
                self.click_count += 1
                return 'click'
            else:
                return 'long_press'
        return None

    def is_pressed(self):
        return self.pressed


# Test Functions
def test_state_machine_initialization():
    sm = StateMachine()
    assert sm.mode == OperatingMode.OFF, "Initial mode should be OFF"
    assert sm.motion_events == 0, "Motion events should start at 0"
    assert sm.mode_changes == 0, "Mode changes should start at 0"
    assert not sm.is_led_on(), "LED should be off initially"

def test_mode_cycling():
    sm = StateMachine()

    # OFF -> CONTINUOUS_ON
    sm.cycle_mode()
    assert sm.mode == OperatingMode.CONTINUOUS_ON, "Should cycle to CONTINUOUS_ON"
    assert sm.is_led_on(), "LED should be on in CONTINUOUS_ON mode"
    assert sm.mode_changes == 1, "Mode changes should be 1"

    # CONTINUOUS_ON -> MOTION_DETECT
    sm.cycle_mode()
    assert sm.mode == OperatingMode.MOTION_DETECT, "Should cycle to MOTION_DETECT"
    assert not sm.is_led_on(), "LED should be off in MOTION_DETECT (no motion yet)"
    assert sm.mode_changes == 2, "Mode changes should be 2"

    # MOTION_DETECT -> OFF
    sm.cycle_mode()
    assert sm.mode == OperatingMode.OFF, "Should cycle back to OFF"
    assert not sm.is_led_on(), "LED should be off in OFF mode"
    assert sm.mode_changes == 3, "Mode changes should be 3"

def test_motion_detection():
    sm = StateMachine()
    sm.set_mode(OperatingMode.MOTION_DETECT)

    # No motion initially
    assert sm.motion_events == 0, "No motion events initially"
    assert not sm.warning_active, "Warning not active"

    # Trigger motion
    sm.handle_motion()
    assert sm.motion_events == 1, "Motion event should be registered"
    assert sm.warning_active, "Warning should be active"
    assert sm.is_led_on(), "LED should be on during warning"

def test_motion_ignored_in_off_mode():
    sm = StateMachine()
    sm.set_mode(OperatingMode.OFF)

    sm.handle_motion()
    assert sm.motion_events == 0, "Motion should be ignored in OFF mode"
    assert not sm.warning_active, "Warning should not activate"

def test_motion_ignored_in_continuous_mode():
    sm = StateMachine()
    sm.set_mode(OperatingMode.CONTINUOUS_ON)

    sm.handle_motion()
    assert sm.motion_events == 0, "Motion should be ignored in CONTINUOUS_ON mode"
    assert not sm.warning_active, "Warning should not activate (LED already on)"

def test_warning_timeout():
    sm = StateMachine()
    sm.set_mode(OperatingMode.MOTION_DETECT)

    # Trigger motion
    sm.handle_motion()
    assert sm.warning_active, "Warning should be active"

    # Advance time but not past timeout
    sm.advance_time(10000)  # 10 seconds
    sm.update()
    assert sm.warning_active, "Warning should still be active"

    # Advance past timeout
    sm.advance_time(6000)  # Total 16 seconds (> 15)
    sm.update()
    assert not sm.warning_active, "Warning should have expired"
    assert not sm.is_led_on(), "LED should be off after warning expires"

def test_multiple_motion_events():
    sm = StateMachine()
    sm.set_mode(OperatingMode.MOTION_DETECT)

    # First motion
    sm.handle_motion()
    assert sm.motion_events == 1, "First motion event"

    # Second motion before warning expires
    sm.advance_time(5000)
    sm.handle_motion()
    assert sm.motion_events == 2, "Second motion event"

    # Third motion after warning expires
    sm.advance_time(20000)
    sm.update()
    sm.handle_motion()
    assert sm.motion_events == 3, "Third motion event"

def test_button_click():
    btn = Button()

    # Press and release quickly
    btn.mock_press()
    assert btn.is_pressed(), "Button should be pressed"

    btn.advance_time(100)  # Short press (< 1000ms)
    result = btn.mock_release()

    assert result == 'click', "Should register as click"
    assert btn.click_count == 1, "Click count should be 1"
    assert not btn.is_pressed(), "Button should not be pressed after release"

def test_button_long_press():
    btn = Button()

    # Press and hold
    btn.mock_press()
    btn.advance_time(1500)  # Long press (>= 1000ms)
    result = btn.mock_release()

    assert result == 'long_press', "Should register as long press"
    assert btn.click_count == 0, "Long press should not increment click count"

def test_button_multiple_clicks():
    btn = Button()

    # First click
    btn.mock_press()
    btn.advance_time(100)
    btn.mock_release()

    # Second click
    btn.advance_time(100)
    btn.mock_press()
    btn.advance_time(100)
    btn.mock_release()

    # Third click
    btn.advance_time(100)
    btn.mock_press()
    btn.advance_time(100)
    btn.mock_release()

    assert btn.click_count == 3, "Should have 3 clicks"

def test_set_mode_same_mode():
    """BUG TEST: Setting mode to same mode should not increment counter"""
    sm = StateMachine()
    sm.set_mode(OperatingMode.OFF)

    initial_changes = sm.mode_changes
    sm.set_mode(OperatingMode.OFF)  # Set to same mode

    assert sm.mode_changes == initial_changes, "Mode changes should not increment when setting to same mode"

def test_mode_change_during_warning():
    """BUG TEST: What happens when mode changes during active warning?"""
    sm = StateMachine()
    sm.set_mode(OperatingMode.MOTION_DETECT)
    sm.handle_motion()

    assert sm.warning_active, "Warning should be active"

    # Change mode while warning active
    sm.set_mode(OperatingMode.OFF)

    # Current behavior: warning stays active (potential bug?)
    # Should warning clear when leaving MOTION_DETECT mode?
    # This test documents current behavior
    assert sm.mode == OperatingMode.OFF, "Mode should change to OFF"


def main():
    print("\n╔════════════════════════════════════════╗")
    print("║                                        ║")
    print("║     StepAware Logic Test Suite        ║")
    print("║                                        ║")
    print("╚════════════════════════════════════════╝\n")

    results = TestResult()

    print("State Machine Tests:")
    print("-" * 40)
    results.run_test("State machine initialization", test_state_machine_initialization)
    results.run_test("Mode cycling", test_mode_cycling)
    results.run_test("Motion detection in MOTION_DETECT mode", test_motion_detection)
    results.run_test("Motion ignored in OFF mode", test_motion_ignored_in_off_mode)
    results.run_test("Motion ignored in CONTINUOUS_ON mode", test_motion_ignored_in_continuous_mode)
    results.run_test("Warning timeout", test_warning_timeout)
    results.run_test("Multiple motion events", test_multiple_motion_events)
    results.run_test("Set mode to same mode", test_set_mode_same_mode)
    results.run_test("Mode change during warning", test_mode_change_during_warning)

    print("\nButton Tests:")
    print("-" * 40)
    results.run_test("Button click", test_button_click)
    results.run_test("Button long press", test_button_long_press)
    results.run_test("Multiple button clicks", test_button_multiple_clicks)

    success = results.print_summary()
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
