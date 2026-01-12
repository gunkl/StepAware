#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test Display Alignment - Validates border alignment in all UI components

This test suite ensures that all bordered display elements (banners, status boxes,
command menus) have properly aligned borders by checking character counts.
"""

import sys
import os
import unittest
from io import StringIO

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')

from mock_simulator import MockStepAware, OperatingMode


class TestDisplayAlignment(unittest.TestCase):
    """Test suite for validating UI border alignment"""

    def setUp(self):
        """Set up test fixtures"""
        self.simulator = MockStepAware()

    def capture_output(self, func):
        """Capture printed output from a function"""
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        try:
            func()
            output = sys.stdout.getvalue()
        finally:
            sys.stdout = old_stdout
        return output

    def validate_box_borders(self, output, description=""):
        """
        Validate that all lines in a bordered box have the same length.

        Args:
            output: The captured output string
            description: Description of what's being tested (for error messages)

        Returns:
            tuple: (is_valid, error_message)
        """
        lines = output.strip().split('\n')

        # Filter out empty lines
        lines = [line for line in lines if line.strip()]

        if not lines:
            return False, f"{description}: No lines found in output"

        # Find all lines with box drawing characters
        box_lines = [line for line in lines if any(c in line for c in '╔╗║╚╝╠╣═')]

        if not box_lines:
            return True, ""  # No box to validate

        # Check that all box lines have the same length
        expected_length = len(box_lines[0])
        misaligned = []

        for i, line in enumerate(box_lines, 1):
            if len(line) != expected_length:
                misaligned.append(f"  Line {i}: expected {expected_length}, got {len(line)}")
                misaligned.append(f"    Content: '{line}'")

        if misaligned:
            error_msg = f"{description}: Border alignment mismatch!\n"
            error_msg += f"Expected width: {expected_length} characters\n"
            error_msg += "\n".join(misaligned)
            return False, error_msg

        return True, ""

    def test_banner_alignment(self):
        """Test that the banner box has aligned borders"""
        output = self.capture_output(self.simulator.print_banner)
        is_valid, error = self.validate_box_borders(output, "Banner")
        self.assertTrue(is_valid, error)

    def test_help_menu_alignment(self):
        """Test that the help menu box has aligned borders"""
        output = self.capture_output(self.simulator.print_help)
        is_valid, error = self.validate_box_borders(output, "Help Menu")
        self.assertTrue(is_valid, error)

    def test_status_display_alignment_off_mode(self):
        """Test status display alignment in OFF mode"""
        self.simulator.mode = OperatingMode.OFF
        output = self.capture_output(self.simulator.print_status)
        is_valid, error = self.validate_box_borders(output, "Status (OFF mode)")
        self.assertTrue(is_valid, error)

    def test_status_display_alignment_continuous_on(self):
        """Test status display alignment in CONTINUOUS_ON mode"""
        self.simulator.mode = OperatingMode.CONTINUOUS_ON
        self.simulator.led_on = True
        output = self.capture_output(self.simulator.print_status)
        is_valid, error = self.validate_box_borders(output, "Status (CONTINUOUS_ON)")
        self.assertTrue(is_valid, error)

    def test_status_display_alignment_motion_detect(self):
        """Test status display alignment in MOTION_DETECT mode"""
        self.simulator.mode = OperatingMode.MOTION_DETECT
        output = self.capture_output(self.simulator.print_status)
        is_valid, error = self.validate_box_borders(output, "Status (MOTION_DETECT)")
        self.assertTrue(is_valid, error)

    def test_status_display_alignment_with_warning(self):
        """Test status display alignment when warning is active"""
        import time
        self.simulator.mode = OperatingMode.MOTION_DETECT
        self.simulator.led_warning_active = True
        self.simulator.warning_end_time = time.time() + 12

        output = self.capture_output(self.simulator.print_status)
        is_valid, error = self.validate_box_borders(output, "Status (with warning)")
        self.assertTrue(is_valid, error)

    def test_status_display_alignment_with_counters(self):
        """Test status display alignment with various counter values"""
        self.simulator.mode = OperatingMode.MOTION_DETECT
        self.simulator.motion_events = 999
        self.simulator.mode_changes = 999
        self.simulator.button_clicks = 999

        output = self.capture_output(self.simulator.print_status)
        is_valid, error = self.validate_box_borders(output, "Status (high counters)")
        self.assertTrue(is_valid, error)

    def test_box_inner_content_width(self):
        """Test that box content lines have correct inner width"""
        # Test help menu - should be 56 chars inside (58 total including ║)
        output = self.capture_output(self.simulator.print_help)
        lines = [line for line in output.split('\n') if '║' in line and '═' not in line]

        for line in lines:
            # Extract content between ║ characters
            if line.count('║') >= 2:
                content = line.split('║')[1]
                self.assertEqual(len(content), 56,
                    f"Help menu content should be 56 chars, got {len(content)}: '{content}'")

    def test_border_characters_are_consistent(self):
        """Test that border characters are used consistently"""
        output = self.capture_output(self.simulator.print_status)
        lines = output.split('\n')

        # Check for proper box drawing characters
        top_borders = [line for line in lines if '╔' in line]
        bottom_borders = [line for line in lines if '╚' in line]
        middle_borders = [line for line in lines if '╠' in line]

        # Should have exactly one top and bottom
        self.assertEqual(len(top_borders), 1, "Should have exactly one top border (╔)")
        self.assertEqual(len(bottom_borders), 1, "Should have exactly one bottom border (╚)")

        # Should have exactly one middle separator
        self.assertEqual(len(middle_borders), 1, "Should have exactly one middle border (╠)")


class TestBorderRegressionPrevention(unittest.TestCase):
    """Tests to prevent specific border alignment regressions"""

    def test_command_line_spacing(self):
        """Regression test: Command lines must end with proper spacing"""
        simulator = MockStepAware()
        output = StringIO()
        old_stdout = sys.stdout
        sys.stdout = output
        try:
            simulator.print_help()
            result = output.getvalue()
        finally:
            sys.stdout = old_stdout

        # Find command lines
        lines = result.split('\n')
        cmd_line1 = [l for l in lines if 's - Status' in l][0]
        cmd_line2 = [l for l in lines if '0 - OFF' in l][0]

        # Both should end with proper spacing before the border
        # Pattern: "║  ...content...  ║" where content + spaces = 56 chars
        self.assertTrue(cmd_line1.endswith('║'),
            "Command line 1 should end with ║")
        self.assertTrue(cmd_line2.endswith('║'),
            "Command line 2 should end with ║")

        # Extract content between borders and verify it's exactly 56 chars
        content1 = cmd_line1.split('║')[1]
        content2 = cmd_line2.split('║')[1]

        self.assertEqual(len(content1), 56,
            f"Command line 1 content should be 56 chars, got {len(content1)}")
        self.assertEqual(len(content2), 56,
            f"Command line 2 content should be 56 chars, got {len(content2)}")

    def test_banner_hazard_warning_line(self):
        """Regression test: 'Motion-Activated Hazard Warning' line alignment"""
        simulator = MockStepAware()
        output = StringIO()
        old_stdout = sys.stdout
        sys.stdout = output
        try:
            simulator.print_banner()
            result = output.getvalue()
        finally:
            sys.stdout = old_stdout

        lines = result.split('\n')
        hazard_line = [l for l in lines if 'Motion-Activated Hazard Warning' in l][0]

        # Should be 42 chars total (40 inside + 2 for ║)
        self.assertEqual(len(hazard_line), 42,
            f"Banner hazard warning line should be 42 chars, got {len(hazard_line)}")


def run_tests():
    """Run all display alignment tests"""
    # Create test suite
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    suite.addTests(loader.loadTestsFromTestCase(TestDisplayAlignment))
    suite.addTests(loader.loadTestsFromTestCase(TestBorderRegressionPrevention))

    # Run tests with verbose output
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    # Return exit code
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())
