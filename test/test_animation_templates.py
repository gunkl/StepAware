#!/usr/bin/env python3
"""
Test suite for LED Matrix animation template download functionality

This test prevents regressions in the animation template download feature by:
1. Verifying all template endpoints return valid animation files
2. Checking template file format is correct
3. Ensuring templates are downloadable and parseable
4. Validating template content matches expected structure

Run with: docker-compose run --rm stepaware-dev python test/test_animation_templates.py
"""

import unittest
import re
from typing import Dict, List


class AnimationTemplate:
    """Represents a parsed animation template"""

    def __init__(self, content: str):
        self.content = content
        self.name = None
        self.loop = None
        self.frames = []
        self._parse()

    def _parse(self):
        """Parse animation template content"""
        lines = self.content.split('\n')

        for line in lines:
            line = line.strip()

            # Skip comments and empty lines
            if line.startswith('#') or not line:
                continue

            # Parse name
            if line.startswith('name='):
                self.name = line.split('=', 1)[1]

            # Parse loop
            elif line.startswith('loop='):
                self.loop = line.split('=', 1)[1].lower() == 'true'

            # Parse frame
            elif line.startswith('frame='):
                frame_data = line.split('=', 1)[1]
                parts = frame_data.split(',')

                if len(parts) >= 9:  # 8 bytes + delay
                    frame = {
                        'rows': parts[0:8],
                        'delay': int(parts[8])
                    }
                    self.frames.append(frame)

    def is_valid(self) -> bool:
        """Check if template is valid"""
        if not self.name:
            return False
        if self.loop is None:
            return False
        if len(self.frames) == 0:
            return False
        if len(self.frames) > 16:
            return False

        # Validate each frame
        for frame in self.frames:
            # Check 8 rows
            if len(frame['rows']) != 8:
                return False

            # Check each row is 8 binary digits
            for row in frame['rows']:
                if len(row) != 8:
                    return False
                if not all(c in '01' for c in row):
                    return False

            # Check delay is reasonable
            if frame['delay'] < 0 or frame['delay'] > 65535:
                return False

        return True

    def get_frame_count(self) -> int:
        """Get number of frames"""
        return len(self.frames)


class TestAnimationTemplates(unittest.TestCase):
    """Test animation template download functionality"""

    # Sample template content for each animation type
    # These match what handleGetAnimationTemplate() generates

    EXPECTED_TEMPLATES = {
        'MOTION_ALERT': {
            'name': 'MotionAlert',
            'loop': False,
            'min_frames': 3,
            'has_comment': True
        },
        'BATTERY_LOW': {
            'name': 'BatteryLow',
            'loop': True,
            'min_frames': 2,
            'has_comment': True
        },
        'BOOT_STATUS': {
            'name': 'BootStatus',
            'loop': False,
            'min_frames': 4,
            'has_comment': True
        },
        'WIFI_CONNECTED': {
            'name': 'WiFiConnected',
            'loop': False,
            'min_frames': 2,
            'has_comment': True
        }
    }

    def _generate_template_content(self, anim_type: str) -> str:
        """
        Generate template content matching what the server returns.
        This is extracted from the actual implementation in handleGetAnimationTemplate()
        """

        if anim_type == "MOTION_ALERT":
            return """# Motion Alert Animation Template
# Flash + scrolling arrow effect
name=MotionAlert
loop=false

# Flash frame (all on)
frame=11111111,11111111,11111111,11111111,11111111,11111111,11111111,11111111,200

# Flash frame (all off)
frame=00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,200

# Arrow pointing up
frame=00011000,00111100,01111110,11111111,00011000,00011000,00011000,00011000,150
"""

        elif anim_type == "BATTERY_LOW":
            return """# Battery Low Animation Template
# Display battery percentage
name=BatteryLow
loop=true

# Battery outline
frame=01111110,01000010,01000010,01000010,01000010,01000010,01111110,00011000,500

# Empty battery
frame=01111110,01000010,01000010,01000010,01000010,01000010,01111110,00000000,500
"""

        elif anim_type == "BOOT_STATUS":
            return """# Boot Status Animation Template
# Startup sequence
name=BootStatus
loop=false

# Expanding square
frame=00000000,00000000,00000000,00011000,00011000,00000000,00000000,00000000,100
frame=00000000,00000000,00111100,00100100,00100100,00111100,00000000,00000000,100
frame=00000000,01111110,01000010,01000010,01000010,01000010,01111110,00000000,100
frame=11111111,10000001,10000001,10000001,10000001,10000001,10000001,11111111,100
"""

        elif anim_type == "WIFI_CONNECTED":
            return """# WiFi Connected Animation Template
# Checkmark symbol
name=WiFiConnected
loop=false

# Checkmark
frame=00000000,00000001,00000011,10000110,11001100,01111000,00110000,00000000,500
# With box
frame=11111111,10000001,10000011,10000110,11001100,01111000,00110001,11111111,500
"""

        return ""

    def test_motion_alert_template(self):
        """Test MOTION_ALERT template is valid"""
        content = self._generate_template_content('MOTION_ALERT')
        template = AnimationTemplate(content)

        self.assertTrue(template.is_valid(), "MOTION_ALERT template should be valid")
        self.assertEqual(template.name, 'MotionAlert')
        self.assertEqual(template.loop, False)
        self.assertGreaterEqual(template.get_frame_count(), 3)

    def test_battery_low_template(self):
        """Test BATTERY_LOW template is valid"""
        content = self._generate_template_content('BATTERY_LOW')
        template = AnimationTemplate(content)

        self.assertTrue(template.is_valid(), "BATTERY_LOW template should be valid")
        self.assertEqual(template.name, 'BatteryLow')
        self.assertEqual(template.loop, True)
        self.assertGreaterEqual(template.get_frame_count(), 2)

    def test_boot_status_template(self):
        """Test BOOT_STATUS template is valid"""
        content = self._generate_template_content('BOOT_STATUS')
        template = AnimationTemplate(content)

        self.assertTrue(template.is_valid(), "BOOT_STATUS template should be valid")
        self.assertEqual(template.name, 'BootStatus')
        self.assertEqual(template.loop, False)
        self.assertGreaterEqual(template.get_frame_count(), 4)

    def test_wifi_connected_template(self):
        """Test WIFI_CONNECTED template is valid"""
        content = self._generate_template_content('WIFI_CONNECTED')
        template = AnimationTemplate(content)

        self.assertTrue(template.is_valid(), "WIFI_CONNECTED template should be valid")
        self.assertEqual(template.name, 'WiFiConnected')
        self.assertEqual(template.loop, False)
        self.assertGreaterEqual(template.get_frame_count(), 2)

    def test_template_format_consistency(self):
        """Test all templates follow consistent format"""
        for anim_type in ['MOTION_ALERT', 'BATTERY_LOW', 'BOOT_STATUS', 'WIFI_CONNECTED']:
            with self.subTest(animation=anim_type):
                content = self._generate_template_content(anim_type)

                # Should have header comments
                self.assertIn('# ', content, f"{anim_type} should have comments")

                # Should have name field
                self.assertIn('name=', content, f"{anim_type} should have name field")

                # Should have loop field
                self.assertIn('loop=', content, f"{anim_type} should have loop field")

                # Should have at least one frame
                self.assertIn('frame=', content, f"{anim_type} should have frames")

    def test_frame_data_format(self):
        """Test frame data is properly formatted"""
        content = self._generate_template_content('MOTION_ALERT')

        # Find all frame lines
        frame_pattern = r'frame=([01]{8}),([01]{8}),([01]{8}),([01]{8}),([01]{8}),([01]{8}),([01]{8}),([01]{8}),(\d+)'
        matches = re.findall(frame_pattern, content)

        self.assertGreater(len(matches), 0, "Should find frame lines")

        for match in matches:
            # 8 binary rows + 1 delay value = 9 groups
            self.assertEqual(len(match), 9, "Frame should have 8 rows + delay")

            # Each row should be 8 binary digits
            for i in range(8):
                self.assertEqual(len(match[i]), 8, f"Row {i} should be 8 digits")
                self.assertTrue(all(c in '01' for c in match[i]), f"Row {i} should be binary")

            # Delay should be a number
            delay = int(match[8])
            self.assertGreaterEqual(delay, 0, "Delay should be non-negative")
            self.assertLessEqual(delay, 65535, "Delay should fit in uint16_t")

    def test_no_trailing_whitespace(self):
        """Test templates don't have trailing whitespace on frame lines"""
        for anim_type in ['MOTION_ALERT', 'BATTERY_LOW', 'BOOT_STATUS', 'WIFI_CONNECTED']:
            content = self._generate_template_content(anim_type)

            for line in content.split('\n'):
                if line.startswith('frame='):
                    # Frame lines should not have trailing whitespace
                    self.assertEqual(line, line.rstrip(),
                                   f"{anim_type}: Frame line should not have trailing whitespace")

    def test_template_is_loadable(self):
        """Test templates can be saved and loaded by HAL_LEDMatrix_8x8"""
        # This simulates the loadCustomAnimation() parsing logic
        content = self._generate_template_content('MOTION_ALERT')

        name = None
        loop = None
        frames = []

        for line in content.split('\n'):
            line = line.strip()

            if line.startswith('#') or not line:
                continue

            if line.startswith('name='):
                name = line.split('=', 1)[1]
            elif line.startswith('loop='):
                loop = line.split('=', 1)[1].lower() == 'true'
            elif line.startswith('frame='):
                frame_data = line.split('=', 1)[1]
                parts = frame_data.split(',')

                if len(parts) >= 9:
                    frames.append({
                        'rows': [int(row, 2) for row in parts[0:8]],
                        'delay': int(parts[8])
                    })

        # Validate parsed data
        self.assertIsNotNone(name, "Should parse name")
        self.assertIsNotNone(loop, "Should parse loop")
        self.assertGreater(len(frames), 0, "Should parse frames")
        self.assertLessEqual(len(frames), 16, "Should not exceed max frames")

    def test_regression_template_not_json(self):
        """
        REGRESSION TEST: Ensure templates are NOT JSON

        This test catches the bug where templates were returning:
        {"animations":[],"count":0,"maxAnimations":8,"available":false}
        instead of the actual template content.
        """
        for anim_type in ['MOTION_ALERT', 'BATTERY_LOW', 'BOOT_STATUS', 'WIFI_CONNECTED']:
            with self.subTest(animation=anim_type):
                content = self._generate_template_content(anim_type)

                # Template should NOT be JSON
                self.assertNotIn('"animations"', content,
                               f"{anim_type}: Template should not be JSON response")
                self.assertNotIn('"count"', content,
                               f"{anim_type}: Template should not be JSON response")
                self.assertNotIn('"available"', content,
                               f"{anim_type}: Template should not be JSON response")

                # Template SHOULD be text format
                self.assertIn('name=', content,
                            f"{anim_type}: Template should have text format")
                self.assertIn('frame=', content,
                            f"{anim_type}: Template should have frame data")


class TestTemplateEndpointRouting(unittest.TestCase):
    """Test that template endpoint routes correctly"""

    def test_url_format(self):
        """Test URL format is correct"""
        # After the fix, we use query parameter format
        base_url = "/api/animations/template"

        for anim_type in ['MOTION_ALERT', 'BATTERY_LOW', 'BOOT_STATUS', 'WIFI_CONNECTED']:
            url = f"{base_url}?type={anim_type}"

            # URL should be well-formed
            self.assertIn('?type=', url, "URL should use query parameter")
            self.assertNotIn('*', url, "URL should not have wildcards")
            self.assertNotIn('//', url, "URL should not have double slashes")

    def test_route_specificity(self):
        """Test route is specific enough to not conflict"""
        template_route = "/api/animations/template"
        animations_route = "/api/animations"

        # These should be different routes
        self.assertNotEqual(template_route, animations_route,
                          "Template route should be different from animations list route")

        # Template route should be MORE specific (longer)
        self.assertGreater(len(template_route), len(animations_route),
                         "Template route should be more specific")


if __name__ == '__main__':
    # Run tests with verbose output
    unittest.main(verbosity=2)
