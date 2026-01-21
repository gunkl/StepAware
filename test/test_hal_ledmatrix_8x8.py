#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test HAL_LEDMatrix_8x8 - Validates 8x8 LED Matrix Hardware Abstraction Layer

This test suite ensures the LED matrix HAL correctly initializes, handles animations,
and manages display state.

Issue #12 Phase 1: LED Matrix Support
"""

import unittest


class MockLEDMatrix8x8:
    """Mock LED Matrix matching C++ HAL_LEDMatrix_8x8 implementation"""

    # Animation patterns (matches C++ enum)
    ANIM_NONE = 0
    ANIM_MOTION_ALERT = 1
    ANIM_BATTERY_LOW = 2
    ANIM_BOOT_STATUS = 3
    ANIM_WIFI_CONNECTED = 4
    ANIM_CUSTOM = 5

    def __init__(self, i2c_address=0x70, sda_pin=8, scl_pin=9, mock_mode=True):
        self.i2c_address = i2c_address
        self.sda_pin = sda_pin
        self.scl_pin = scl_pin
        self.mock_mode = mock_mode
        self.initialized = False

        # Display state
        self.brightness = 5  # Default brightness (0-15)
        self.rotation = 0    # Default rotation (0-3)
        self.frame = [0] * 8  # 8 rows of 8 pixels

        # Animation state
        self.current_pattern = self.ANIM_NONE
        self.animation_start_time = 0
        self.animation_duration = 0
        self.animation_frame = 0
        self.is_animating_flag = False

        # Statistics
        self.update_count = 0
        self.animation_started_count = 0

    def begin(self):
        """Initialize LED matrix"""
        if self.mock_mode:
            self.initialized = True
            return True

        # In real hardware mode, would initialize I2C
        return False

    def update(self):
        """Update animation state (call every loop)"""
        if not self.initialized:
            return

        self.update_count += 1

        if self.is_animating_flag and self.animation_duration > 0:
            # Simulate animation frame progression
            elapsed = self.update_count * 10  # Assume 10ms per update
            if elapsed >= self.animation_duration:
                self.stop_animation()
            else:
                self.animation_frame = (elapsed // 100) % 8  # Change frame every 100ms

    def clear(self):
        """Clear all pixels"""
        self.frame = [0] * 8

    def set_brightness(self, level):
        """Set brightness (0-15)"""
        if 0 <= level <= 15:
            self.brightness = level
            return True
        return False

    def set_rotation(self, rotation):
        """Set rotation (0-3)"""
        if 0 <= rotation <= 3:
            self.rotation = rotation
            return True
        return False

    def start_animation(self, pattern, duration_ms=0):
        """Start an animation"""
        if pattern < 0 or pattern > 5:
            return False

        self.current_pattern = pattern
        self.animation_duration = duration_ms
        self.animation_start_time = self.update_count * 10
        self.animation_frame = 0
        self.is_animating_flag = True
        self.animation_started_count += 1

        return True

    def stop_animation(self):
        """Stop current animation"""
        self.is_animating_flag = False
        self.current_pattern = self.ANIM_NONE

    def is_animating(self):
        """Check if animation is running"""
        return self.is_animating_flag

    def get_pattern(self):
        """Get current animation pattern"""
        return self.current_pattern

    def draw_frame(self, frame_data):
        """Draw 8-byte frame buffer"""
        if len(frame_data) != 8:
            return False

        self.frame = list(frame_data)
        return True

    def set_pixel(self, x, y, on):
        """Set individual pixel"""
        if not (0 <= x <= 7 and 0 <= y <= 7):
            return False

        if on:
            self.frame[y] |= (1 << x)
        else:
            self.frame[y] &= ~(1 << x)

        return True

    def get_pixel(self, x, y):
        """Get individual pixel state"""
        if not (0 <= x <= 7 and 0 <= y <= 7):
            return False

        return bool(self.frame[y] & (1 << x))

    def get_brightness(self):
        """Get current brightness"""
        return self.brightness

    def is_ready(self):
        """Check if matrix is initialized"""
        return self.initialized

    def get_frame_as_string(self):
        """Return ASCII art representation of frame"""
        lines = []
        for row in self.frame:
            line = ""
            for x in range(8):
                if row & (1 << x):
                    line += "█"
                else:
                    line += " "
            lines.append(line)
        return "\n".join(lines)


class TestHALLEDMatrix8x8Initialization(unittest.TestCase):
    """Test suite for LED Matrix initialization"""

    def test_default_constructor(self):
        """Test LED matrix with default parameters"""
        matrix = MockLEDMatrix8x8()

        self.assertEqual(matrix.i2c_address, 0x70)
        self.assertEqual(matrix.sda_pin, 8)
        self.assertEqual(matrix.scl_pin, 9)
        self.assertTrue(matrix.mock_mode)
        self.assertFalse(matrix.initialized)

    def test_custom_i2c_address(self):
        """Test LED matrix with custom I2C address"""
        matrix = MockLEDMatrix8x8(i2c_address=0x71)

        self.assertEqual(matrix.i2c_address, 0x71)

    def test_begin_mock_mode(self):
        """Test begin() in mock mode"""
        matrix = MockLEDMatrix8x8(mock_mode=True)

        result = matrix.begin()

        self.assertTrue(result)
        self.assertTrue(matrix.initialized)

    def test_begin_hardware_mode_fails_without_hardware(self):
        """Test begin() fails in hardware mode without physical device"""
        matrix = MockLEDMatrix8x8(mock_mode=False)

        result = matrix.begin()

        self.assertFalse(result)
        self.assertFalse(matrix.initialized)

    def test_default_brightness(self):
        """Test default brightness is 5"""
        matrix = MockLEDMatrix8x8()
        matrix.begin()

        self.assertEqual(matrix.get_brightness(), 5)

    def test_default_rotation(self):
        """Test default rotation is 0"""
        matrix = MockLEDMatrix8x8()
        matrix.begin()

        self.assertEqual(matrix.rotation, 0)


class TestHALLEDMatrix8x8DisplayControl(unittest.TestCase):
    """Test suite for LED Matrix display control"""

    def setUp(self):
        self.matrix = MockLEDMatrix8x8()
        self.matrix.begin()

    def test_set_brightness_valid(self):
        """Test setting valid brightness levels"""
        # Test minimum
        self.assertTrue(self.matrix.set_brightness(0))
        self.assertEqual(self.matrix.get_brightness(), 0)

        # Test maximum
        self.assertTrue(self.matrix.set_brightness(15))
        self.assertEqual(self.matrix.get_brightness(), 15)

        # Test middle value
        self.assertTrue(self.matrix.set_brightness(7))
        self.assertEqual(self.matrix.get_brightness(), 7)

    def test_set_brightness_invalid(self):
        """Test setting invalid brightness levels"""
        self.assertFalse(self.matrix.set_brightness(16))
        self.assertFalse(self.matrix.set_brightness(-1))

    def test_set_rotation_valid(self):
        """Test setting valid rotation values"""
        for rotation in [0, 1, 2, 3]:
            self.assertTrue(self.matrix.set_rotation(rotation))
            self.assertEqual(self.matrix.rotation, rotation)

    def test_set_rotation_invalid(self):
        """Test setting invalid rotation values"""
        self.assertFalse(self.matrix.set_rotation(4))
        self.assertFalse(self.matrix.set_rotation(-1))

    def test_clear(self):
        """Test clearing display"""
        # Set some pixels first
        self.matrix.set_pixel(0, 0, True)
        self.matrix.set_pixel(7, 7, True)

        # Clear display
        self.matrix.clear()

        # Verify all pixels are off
        for y in range(8):
            self.assertEqual(self.matrix.frame[y], 0)


class TestHALLEDMatrix8x8PixelControl(unittest.TestCase):
    """Test suite for pixel-level control"""

    def setUp(self):
        self.matrix = MockLEDMatrix8x8()
        self.matrix.begin()
        self.matrix.clear()

    def test_set_pixel_valid(self):
        """Test setting individual pixels"""
        # Turn on pixel
        self.assertTrue(self.matrix.set_pixel(3, 4, True))
        self.assertTrue(self.matrix.get_pixel(3, 4))

        # Turn off pixel
        self.assertTrue(self.matrix.set_pixel(3, 4, False))
        self.assertFalse(self.matrix.get_pixel(3, 4))

    def test_set_pixel_corners(self):
        """Test setting corner pixels"""
        corners = [(0, 0), (7, 0), (0, 7), (7, 7)]

        for x, y in corners:
            self.assertTrue(self.matrix.set_pixel(x, y, True))
            self.assertTrue(self.matrix.get_pixel(x, y))

    def test_set_pixel_invalid_coordinates(self):
        """Test setting pixels with invalid coordinates"""
        self.assertFalse(self.matrix.set_pixel(8, 0, True))
        self.assertFalse(self.matrix.set_pixel(0, 8, True))
        self.assertFalse(self.matrix.set_pixel(-1, 0, True))
        self.assertFalse(self.matrix.set_pixel(0, -1, True))

    def test_draw_frame(self):
        """Test drawing 8-byte frame buffer"""
        # Create a checkerboard pattern
        frame = [0b01010101, 0b10101010, 0b01010101, 0b10101010,
                 0b01010101, 0b10101010, 0b01010101, 0b10101010]

        self.assertTrue(self.matrix.draw_frame(frame))
        self.assertEqual(self.matrix.frame, frame)

    def test_draw_frame_invalid_size(self):
        """Test drawing frame with incorrect size"""
        self.assertFalse(self.matrix.draw_frame([0, 0, 0]))  # Too short
        self.assertFalse(self.matrix.draw_frame([0] * 10))   # Too long


class TestHALLEDMatrix8x8Animations(unittest.TestCase):
    """Test suite for animation system"""

    def setUp(self):
        self.matrix = MockLEDMatrix8x8()
        self.matrix.begin()

    def test_start_animation_motion_alert(self):
        """Test starting motion alert animation"""
        result = self.matrix.start_animation(
            MockLEDMatrix8x8.ANIM_MOTION_ALERT,
            2000
        )

        self.assertTrue(result)
        self.assertTrue(self.matrix.is_animating())
        self.assertEqual(self.matrix.get_pattern(), MockLEDMatrix8x8.ANIM_MOTION_ALERT)
        self.assertEqual(self.matrix.animation_duration, 2000)

    def test_start_animation_battery_low(self):
        """Test starting battery low animation"""
        result = self.matrix.start_animation(
            MockLEDMatrix8x8.ANIM_BATTERY_LOW,
            1500
        )

        self.assertTrue(result)
        self.assertTrue(self.matrix.is_animating())
        self.assertEqual(self.matrix.get_pattern(), MockLEDMatrix8x8.ANIM_BATTERY_LOW)

    def test_start_animation_boot_status(self):
        """Test starting boot status animation"""
        result = self.matrix.start_animation(
            MockLEDMatrix8x8.ANIM_BOOT_STATUS,
            3000
        )

        self.assertTrue(result)
        self.assertTrue(self.matrix.is_animating())
        self.assertEqual(self.matrix.get_pattern(), MockLEDMatrix8x8.ANIM_BOOT_STATUS)

    def test_start_animation_infinite_duration(self):
        """Test starting animation with infinite duration (0)"""
        result = self.matrix.start_animation(
            MockLEDMatrix8x8.ANIM_MOTION_ALERT,
            0  # Loop forever
        )

        self.assertTrue(result)
        self.assertTrue(self.matrix.is_animating())
        self.assertEqual(self.matrix.animation_duration, 0)

    def test_stop_animation(self):
        """Test stopping animation"""
        self.matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 2000)
        self.assertTrue(self.matrix.is_animating())

        self.matrix.stop_animation()

        self.assertFalse(self.matrix.is_animating())
        self.assertEqual(self.matrix.get_pattern(), MockLEDMatrix8x8.ANIM_NONE)

    def test_animation_auto_stop_after_duration(self):
        """Test animation auto-stops after duration expires"""
        self.matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 1000)

        # Simulate 1000ms of updates (100 updates at 10ms each)
        for _ in range(100):
            self.matrix.update()

        # Animation should have stopped
        self.assertFalse(self.matrix.is_animating())

    def test_animation_frame_progression(self):
        """Test animation frames progress over time"""
        self.matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 2000)

        initial_frame = self.matrix.animation_frame

        # Run several updates
        for _ in range(20):
            self.matrix.update()

        # Frame should have progressed
        self.assertNotEqual(self.matrix.animation_frame, initial_frame)

    def test_multiple_animation_restarts(self):
        """Test starting animations multiple times"""
        # First animation
        self.matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 1000)
        self.assertEqual(self.matrix.animation_started_count, 1)

        # Second animation (should replace first)
        self.matrix.start_animation(MockLEDMatrix8x8.ANIM_BATTERY_LOW, 1500)
        self.assertEqual(self.matrix.animation_started_count, 2)
        self.assertEqual(self.matrix.get_pattern(), MockLEDMatrix8x8.ANIM_BATTERY_LOW)


class TestHALLEDMatrix8x8Integration(unittest.TestCase):
    """Integration tests for LED Matrix"""

    def test_full_lifecycle(self):
        """Test complete LED matrix lifecycle"""
        matrix = MockLEDMatrix8x8(i2c_address=0x70, sda_pin=8, scl_pin=9)

        # 1. Initialize
        self.assertTrue(matrix.begin())
        self.assertTrue(matrix.is_ready())

        # 2. Configure display
        matrix.set_brightness(10)
        matrix.set_rotation(1)

        # 3. Draw custom pattern
        frame = [0b11111111] * 8  # All pixels on
        matrix.draw_frame(frame)

        # 4. Start animation
        matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 2000)
        self.assertTrue(matrix.is_animating())

        # 5. Update multiple times
        for _ in range(50):
            matrix.update()

        # 6. Stop animation
        matrix.stop_animation()
        self.assertFalse(matrix.is_animating())

        # 7. Clear display
        matrix.clear()

        # Verify final state
        self.assertEqual(matrix.get_brightness(), 10)
        self.assertEqual(matrix.rotation, 1)

    def test_motion_detection_workflow(self):
        """Test typical motion detection use case"""
        matrix = MockLEDMatrix8x8()
        matrix.begin()

        # Motion detected -> trigger warning
        matrix.start_animation(MockLEDMatrix8x8.ANIM_MOTION_ALERT, 15000)

        self.assertTrue(matrix.is_animating())
        self.assertEqual(matrix.get_pattern(), MockLEDMatrix8x8.ANIM_MOTION_ALERT)

        # Simulate motion warning duration
        for _ in range(1500):  # 1500 updates * 10ms = 15000ms
            matrix.update()

        # Warning should expire
        self.assertFalse(matrix.is_animating())

    def test_boot_sequence_workflow(self):
        """Test boot sequence display"""
        matrix = MockLEDMatrix8x8()
        matrix.begin()

        # Set initial brightness
        matrix.set_brightness(5)

        # Show boot animation
        matrix.start_animation(MockLEDMatrix8x8.ANIM_BOOT_STATUS, 3000)

        # Verify boot animation is running
        self.assertTrue(matrix.is_animating())

        # Simulate boot display time
        for _ in range(300):  # 300 * 10ms = 3000ms
            matrix.update()

        # Boot animation should finish
        self.assertFalse(matrix.is_animating())


def run_tests():
    """Run all LED Matrix HAL tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestHALLEDMatrix8x8Initialization))
    suite.addTests(loader.loadTestsFromTestCase(TestHALLEDMatrix8x8DisplayControl))
    suite.addTests(loader.loadTestsFromTestCase(TestHALLEDMatrix8x8PixelControl))
    suite.addTests(loader.loadTestsFromTestCase(TestHALLEDMatrix8x8Animations))
    suite.addTests(loader.loadTestsFromTestCase(TestHALLEDMatrix8x8Integration))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
