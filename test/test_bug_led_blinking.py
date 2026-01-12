#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test for bug: LED keeps blinking when switching from CONTINUOUS_ON to MOTION_DETECT
"""

import sys
import os

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

from enum import Enum

class LEDPattern(Enum):
    PATTERN_OFF = 0
    PATTERN_ON = 1
    PATTERN_BLINK_WARNING = 2

class LED:
    def __init__(self):
        self.pattern = LEDPattern.PATTERN_OFF
        self.duration = 0
        self.start_time = 0
        self.time = 0
        self.brightness = 0

    def advance_time(self, ms):
        self.time += ms

    def start_pattern(self, pattern, duration_ms=0):
        """Start a pattern with optional duration (0 = infinite)"""
        self.pattern = pattern
        self.duration = duration_ms
        self.start_time = self.time

        if pattern == LEDPattern.PATTERN_OFF:
            self.brightness = 0
        elif pattern == LEDPattern.PATTERN_ON or pattern == LEDPattern.PATTERN_BLINK_WARNING:
            self.brightness = 255  # Simplified - starts ON

        print(f"[LED] Pattern started: {pattern.name}, duration: {duration_ms} ms")

    def stop_pattern(self):
        """Stop current pattern"""
        self.pattern = LEDPattern.PATTERN_OFF
        self.duration = 0
        self.brightness = 0
        print(f"[LED] Pattern stopped")

    def update(self):
        """Update LED state (called in loop)"""
        # Check if timed pattern expired
        if self.duration > 0:
            if self.time - self.start_time >= self.duration:
                self.stop_pattern()
                return

        # Update pattern (simplified - just show if active)
        if self.pattern == LEDPattern.PATTERN_BLINK_WARNING:
            # Blink logic would go here
            pass

    def is_on(self):
        return self.brightness > 0

    def is_pattern_active(self):
        if self.duration == 0:
            return self.pattern != LEDPattern.PATTERN_OFF
        return (self.time - self.start_time) < self.duration


class OperatingMode(Enum):
    OFF = 0
    CONTINUOUS_ON = 1
    MOTION_DETECT = 2


class StateMachine:
    def __init__(self):
        self.mode = OperatingMode.OFF
        self.led = LED()

    def advance_time(self, ms):
        self.led.advance_time(ms)

    def enter_mode(self, mode):
        """Enter a new mode"""
        if mode == OperatingMode.OFF:
            self.led.stop_pattern()

        elif mode == OperatingMode.CONTINUOUS_ON:
            # Start infinite warning pattern
            self.led.start_pattern(LEDPattern.PATTERN_BLINK_WARNING, 0)  # 0 = infinite

        elif mode == OperatingMode.MOTION_DETECT:
            # Stop any active pattern
            self.led.stop_pattern()

    def exit_mode(self, mode):
        """Exit current mode (cleanup)"""
        # Currently empty - this might be the bug!
        pass

    def set_mode(self, new_mode):
        """Change operating mode"""
        if new_mode == self.mode:
            return

        print(f"\n[StateMachine] Mode change: {self.mode.name} -> {new_mode.name}")

        # Exit current mode
        self.exit_mode(self.mode)

        # Update mode
        self.mode = new_mode

        # Enter new mode
        self.enter_mode(new_mode)

    def update(self):
        """Update state machine"""
        self.led.update()


def test_bug_continuous_to_motion_detect():
    """
    BUG TEST: LED should stop blinking when switching from CONTINUOUS_ON to MOTION_DETECT
    """
    print("\n" + "="*60)
    print("BUG TEST: CONTINUOUS_ON -> MOTION_DETECT LED Blinking")
    print("="*60)

    sm = StateMachine()

    # Start in OFF mode
    print("\n1. Starting in OFF mode")
    sm.set_mode(OperatingMode.OFF)
    assert not sm.led.is_on(), "LED should be OFF"
    assert not sm.led.is_pattern_active(), "No pattern should be active"
    print("   ✓ LED is OFF")

    # Switch to CONTINUOUS_ON
    print("\n2. Switching to CONTINUOUS_ON")
    sm.set_mode(OperatingMode.CONTINUOUS_ON)
    assert sm.led.is_pattern_active(), "Pattern should be active"
    assert sm.led.pattern == LEDPattern.PATTERN_BLINK_WARNING, "Should be warning pattern"
    assert sm.led.duration == 0, "Should be infinite duration"
    print(f"   ✓ LED pattern active: {sm.led.pattern.name}")
    print(f"   ✓ Duration: {sm.led.duration} (infinite)")

    # Let it run for a bit
    print("\n3. Running for 5 seconds in CONTINUOUS_ON mode")
    sm.advance_time(5000)
    sm.update()
    assert sm.led.is_pattern_active(), "Pattern should still be active"
    print("   ✓ LED still blinking (expected)")

    # Switch to MOTION_DETECT - THIS IS WHERE THE BUG OCCURS
    print("\n4. Switching to MOTION_DETECT")
    print("   Expected: LED should STOP (no motion detected yet)")
    sm.set_mode(OperatingMode.MOTION_DETECT)

    # Check LED state
    print(f"\n5. Checking LED state after mode change:")
    print(f"   Pattern: {sm.led.pattern.name}")
    print(f"   Is ON: {sm.led.is_on()}")
    print(f"   Is pattern active: {sm.led.is_pattern_active()}")
    print(f"   Brightness: {sm.led.brightness}")

    # THE BUG: If LED is still blinking here, that's the bug!
    if sm.led.is_pattern_active():
        print("\n   ✗ BUG FOUND: LED pattern still active!")
        print("   LED should have stopped when entering MOTION_DETECT mode")
        return False
    elif sm.led.is_on():
        print("\n   ✗ BUG FOUND: LED is still ON!")
        print("   LED should be OFF in MOTION_DETECT (no motion yet)")
        return False
    else:
        print("\n   ✓ PASS: LED correctly stopped")
        return True


def test_possible_fix():
    """
    Test the proposed fix: ensure exitMode() stops patterns
    """
    print("\n" + "="*60)
    print("TESTING PROPOSED FIX")
    print("="*60)

    class FixedStateMachine(StateMachine):
        def exit_mode(self, mode):
            """Exit current mode - FIXED VERSION"""
            # Always stop LED pattern when exiting a mode
            if mode == OperatingMode.CONTINUOUS_ON:
                print(f"   [FIX] Explicitly stopping LED pattern when exiting {mode.name}")
                self.led.stop_pattern()

    sm = FixedStateMachine()

    # Repeat the test with fixed version
    print("\n1. Set to CONTINUOUS_ON")
    sm.set_mode(OperatingMode.CONTINUOUS_ON)
    assert sm.led.is_pattern_active()

    print("\n2. Switch to MOTION_DETECT (with fix)")
    sm.set_mode(OperatingMode.MOTION_DETECT)

    print(f"\n3. LED state:")
    print(f"   Pattern: {sm.led.pattern.name}")
    print(f"   Is pattern active: {sm.led.is_pattern_active()}")

    if not sm.led.is_pattern_active() and not sm.led.is_on():
        print("\n   ✓ FIX WORKS: LED correctly stopped")
        return True
    else:
        print("\n   ✗ FIX FAILED: LED still active")
        return False


def main():
    print("\n╔════════════════════════════════════════╗")
    print("║                                        ║")
    print("║   Bug Reproduction: LED Blinking      ║")
    print("║                                        ║")
    print("╚════════════════════════════════════════╝")

    # Test 1: Reproduce the bug
    bug_reproduced = not test_bug_continuous_to_motion_detect()

    # Test 2: Test the fix
    fix_works = test_possible_fix()

    # Summary
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)
    print(f"Bug reproduced: {bug_reproduced}")
    print(f"Fix works: {fix_works}")

    if bug_reproduced:
        print("\n✓ Successfully reproduced the bug")
        print("\nRoot cause:")
        print("  When switching from CONTINUOUS_ON to MOTION_DETECT,")
        print("  exitMode() doesn't stop the infinite LED pattern.")
        print("  enterMode(MOTION_DETECT) calls stopPattern(), but")
        print("  there might be a timing issue or the pattern state")
        print("  isn't fully cleared.")
        print("\nSuggested fix:")
        print("  In exitMode(), explicitly stop LED patterns when")
        print("  exiting CONTINUOUS_ON mode.")
    else:
        print("\n? Could not reproduce bug in Python simulation")
        print("  Bug may be C++ specific (timing, state, etc.)")

    return 0

if __name__ == '__main__':
    sys.exit(main())
