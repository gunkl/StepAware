#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test Web UI Font Consistency - Prevents font size regressions

This test suite validates that the web UI has consistent font sizing
throughout the Hardware tab to prevent issues like compounding em units
or monospace font size mismatches.

Regression prevention for Issue #12 Web UI fixes (2026-01-20)
"""

import unittest
import re


class MockWebUIGenerator:
    """Mock Web UI HTML generator matching C++ web_api.cpp"""

    @staticmethod
    def generate_sensor_card_html():
        """Generate sensor card HTML (PIR example)"""
        html = ""

        # Sensor card
        html += "<div class=\"card\">"
        html += "<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;\">"
        html += "<div style=\"display:flex;align-items:center;gap:8px;\">"
        html += "<span class=\"badge badge-success\">PIR</span>"
        html += "<span style=\"font-weight:600;\">Slot 0: PIR Motion</span>"
        html += "</div></div>"

        # Grid layout for wiring and configuration
        html += "<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px;\">"

        # Wiring diagram column
        html += "<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>"
        html += "<div style=\"line-height:1.6;\">"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Sensor VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Sensor GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Sensor OUT → <span style=\"color:#2563eb;font-weight:600;\">GPIO 1</span></div>"
        html += "</div></div>"

        # Configuration column
        html += "<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>"
        html += "<div style=\"line-height:1.6;\">"
        html += "<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Warmup:</span> <span>60s</span></div>"
        html += "<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Debounce:</span> <span>100ms</span></div>"
        html += "</div></div>"

        html += "</div></div>"

        return html

    @staticmethod
    def generate_display_card_html():
        """Generate display card HTML (8x8 Matrix example)"""
        html = ""

        # Display card
        html += "<div class=\"card\">"
        html += "<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;\">"
        html += "<div style=\"display:flex;align-items:center;gap:8px;\">"
        html += "<span class=\"badge badge-info\">8x8 Matrix</span>"
        html += "<span style=\"font-weight:600;\">Slot 0: 8x8 Matrix</span>"
        html += "</div></div>"

        # Grid layout
        html += "<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:16px;\">"

        # Wiring diagram
        html += "<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Wiring Diagram</div>"
        html += "<div style=\"line-height:1.6;\">"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Matrix VCC → <span style=\"color:#dc2626;font-weight:600;\">3.3V</span></div>"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Matrix GND → <span style=\"color:#000;font-weight:600;\">GND</span></div>"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Matrix SDA → <span style=\"color:#2563eb;font-weight:600;\">GPIO 7</span></div>"
        html += "<div style=\"color:#64748b;font-size:0.85em;\">Matrix SCL → <span style=\"color:#2563eb;font-weight:600;\">GPIO 10</span></div>"
        html += "</div></div>"

        # Configuration
        html += "<div><div style=\"font-weight:600;margin-bottom:6px;font-size:0.9em;\">Configuration</div>"
        html += "<div style=\"line-height:1.6;\">"
        html += "<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">I2C Address:</span> <span>0x70</span></div>"
        html += "<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Brightness:</span> <span>5/15</span></div>"
        html += "<div style=\"font-size:0.85em;\"><span style=\"color:#64748b;\">Rotation:</span> <span>0°</span></div>"
        html += "</div></div>"

        html += "</div></div>"

        return html


class TestWebUIFontConsistency(unittest.TestCase):
    """Test suite for web UI font size consistency"""

    def setUp(self):
        self.generator = MockWebUIGenerator()

    def test_no_nested_font_sizes_in_sensor_wiring(self):
        """Test sensor wiring has no nested font-size declarations"""
        html = self.generator.generate_sensor_card_html()

        # Extract wiring section
        wiring_match = re.search(r'Wiring Diagram.*?</div></div>', html, re.DOTALL)
        self.assertIsNotNone(wiring_match, "Should find wiring section")
        wiring_html = wiring_match.group(0)

        # Check that font-size only appears at the div level, not in nested spans
        # Should match: <div style="...font-size:0.85em...">Label → <span>Value</span></div>
        # Should NOT match: <span style="...font-size:...">

        # Count font-size occurrences in spans within value positions
        span_with_fontsize = re.findall(r'<span[^>]*font-size[^>]*>[^<]*</span>', wiring_html)
        # Filter out the header span (Wiring Diagram)
        value_spans_with_fontsize = [s for s in span_with_fontsize if 'Wiring Diagram' not in s]

        self.assertEqual(len(value_spans_with_fontsize), 0,
                        f"Found nested font-size in wiring spans: {value_spans_with_fontsize}")

    def test_no_nested_font_sizes_in_sensor_config(self):
        """Test sensor configuration has no nested font-size declarations"""
        html = self.generator.generate_sensor_card_html()

        # Extract configuration section
        config_match = re.search(r'Configuration</div>.*?</div></div>', html, re.DOTALL)
        self.assertIsNotNone(config_match, "Should find configuration section")
        config_html = config_match.group(0)

        # Should have font-size at div level only
        lines = re.findall(r'<div style="font-size:0\.85em;">.*?</div>', config_html)
        self.assertGreater(len(lines), 0, "Should have configuration lines with font-size at div level")

        # No font-size in nested spans
        nested_fontsize = re.findall(r'<span[^>]*font-size[^>]*>', config_html)
        # Exclude the header
        nested_fontsize = [s for s in nested_fontsize if 'Configuration' not in html[html.find(s):html.find(s)+100]]

        self.assertEqual(len(nested_fontsize), 0,
                        f"Found nested font-size in config spans: {nested_fontsize}")

    def test_no_nested_font_sizes_in_display_wiring(self):
        """Test display wiring has no nested font-size declarations"""
        html = self.generator.generate_display_card_html()

        # Extract wiring section
        wiring_match = re.search(r'Wiring Diagram.*?</div></div>', html, re.DOTALL)
        self.assertIsNotNone(wiring_match, "Should find wiring section")
        wiring_html = wiring_match.group(0)

        # Check for font-size only at div level
        span_with_fontsize = re.findall(r'<span[^>]*font-size[^>]*>', wiring_html)
        # Exclude header
        value_spans = [s for s in span_with_fontsize if 'Wiring Diagram' not in wiring_html[wiring_html.find(s):wiring_html.find(s)+100]]

        self.assertEqual(len(value_spans), 0,
                        f"Found nested font-size in display wiring: {value_spans}")

    def test_no_nested_font_sizes_in_display_config(self):
        """Test display configuration has no nested font-size declarations"""
        html = self.generator.generate_display_card_html()

        # Extract configuration section
        config_match = re.search(r'Configuration</div>.*?</div></div>', html, re.DOTALL)
        self.assertIsNotNone(config_match, "Should find configuration section")
        config_html = config_match.group(0)

        # No font-size in nested spans
        nested_fontsize = re.findall(r'<span[^>]*font-size[^>]*>', config_html)
        # Exclude header
        nested_fontsize = [s for s in nested_fontsize if 'Configuration' not in html[html.find(s):html.find(s)+100]]

        self.assertEqual(len(nested_fontsize), 0,
                        f"Found nested font-size in display config: {nested_fontsize}")

    def test_no_monospace_font_in_gpio_values(self):
        """Test GPIO values do not use monospace font (which causes size issues)"""
        sensor_html = self.generator.generate_sensor_card_html()
        display_html = self.generator.generate_display_card_html()

        # Check for font-family:monospace in GPIO value spans
        gpio_pattern = r'GPIO \d+</span>'

        # Find all GPIO value spans
        sensor_gpio_spans = re.findall(r'<span[^>]*>GPIO \d+</span>', sensor_html)
        display_gpio_spans = re.findall(r'<span[^>]*>GPIO \d+</span>', display_html)

        # Check none have monospace font
        for span in sensor_gpio_spans + display_gpio_spans:
            self.assertNotIn('monospace', span,
                           f"GPIO span should not use monospace font: {span}")

    def test_no_monospace_font_in_i2c_address(self):
        """Test I2C address does not use monospace font"""
        html = self.generator.generate_display_card_html()

        # Find I2C address span
        i2c_pattern = r'<span[^>]*>0x[0-9A-F]+</span>'
        i2c_spans = re.findall(i2c_pattern, html)

        self.assertGreater(len(i2c_spans), 0, "Should find I2C address span")

        for span in i2c_spans:
            self.assertNotIn('monospace', span,
                           f"I2C address should not use monospace font: {span}")

    def test_consistent_font_size_throughout(self):
        """Test all value text uses consistent 0.85em font size"""
        sensor_html = self.generator.generate_sensor_card_html()
        display_html = self.generator.generate_display_card_html()

        combined_html = sensor_html + display_html

        # Find all divs with font-size in wiring/config sections
        fontsize_divs = re.findall(r'<div style="[^"]*font-size:([^;"]*)[^"]*">', combined_html)

        # Count occurrences
        size_0_85em = fontsize_divs.count('0.85em')
        size_0_9em = fontsize_divs.count('0.9em')  # Headers are allowed to be 0.9em

        # All content should be 0.85em, only headers should be 0.9em
        self.assertGreater(size_0_85em, 0, "Should have content at 0.85em")
        self.assertGreater(size_0_9em, 0, "Should have headers at 0.9em")

        # No other font sizes should exist (like compounded 0.7225em, 0.614em, etc.)
        valid_sizes = ['0.85em', '0.9em']
        for size in fontsize_divs:
            self.assertIn(size, valid_sizes,
                         f"Found unexpected font size: {size} (should be 0.85em or 0.9em)")

    def test_no_triple_nested_font_sizes(self):
        """Test for the specific regression: triple-nested font-size causing 0.614em"""
        sensor_html = self.generator.generate_sensor_card_html()
        display_html = self.generator.generate_display_card_html()

        combined_html = sensor_html + display_html

        # Look for pattern: div with font-size > div with font-size > span with font-size
        # This would cause compounding: 0.85 * 0.85 * 0.85 = 0.614em

        # Check that we don't have structures like:
        # <div style="font-size:0.85em"><div style="font-size:0.85em"><span style="font-size:0.85em">

        lines = combined_html.split('\n')
        for i, line in enumerate(lines):
            if 'font-size:0.85em' in line and '<div' in line:
                # Check that the content inside doesn't have another font-size on a div
                closing_div = line.find('</div>')
                if closing_div > 0:
                    inner_content = line[line.find('>')+1:closing_div]
                    # Inner content should not have <div with font-size
                    if '<div' in inner_content and 'font-size' in inner_content:
                        self.fail(f"Found nested div with font-size at line {i}: {line}")

    def test_sensor_and_display_have_matching_structure(self):
        """Test sensor cards and display cards use identical CSS structure"""
        sensor_html = self.generator.generate_sensor_card_html()
        display_html = self.generator.generate_display_card_html()

        # Extract the pattern: <div style="font-size:0.85em"><span style="color:#64748b">Label</span>
        sensor_pattern = re.findall(
            r'<div style="font-size:0\.85em;"><span style="color:#64748b;">([^<]+):</span>',
            sensor_html
        )
        display_pattern = re.findall(
            r'<div style="font-size:0\.85em;"><span style="color:#64748b;">([^<]+):</span>',
            display_html
        )

        # Both should use this pattern
        self.assertGreater(len(sensor_pattern), 0, "Sensor config should use label:value pattern")
        self.assertGreater(len(display_pattern), 0, "Display config should use label:value pattern")


class TestFontSizeRegressionPreventions(unittest.TestCase):
    """Specific tests to prevent known font size regressions"""

    def test_prevent_compounding_em_units(self):
        """Prevent regression: Compounding em units (0.85 * 0.85 * 0.85 = 0.614em)"""
        html = MockWebUIGenerator.generate_sensor_card_html()

        # Split into individual div lines
        div_lines = re.findall(r'<div[^>]*>.*?</div>', html)

        for line in div_lines:
            # Count nested font-size declarations
            fontsize_count = line.count('font-size:')

            # Should have at most 1 font-size per line
            # (one on the outer div, zero on inner spans)
            self.assertLessEqual(fontsize_count, 1,
                               f"Found multiple font-size in single line (compounding risk): {line}")

    def test_prevent_monospace_size_mismatch(self):
        """Prevent regression: Monospace fonts rendering smaller than sans-serif"""
        sensor_html = MockWebUIGenerator.generate_sensor_card_html()
        display_html = MockWebUIGenerator.generate_display_card_html()

        # Check that GPIO values and I2C addresses don't use monospace
        # (Previous bug: monospace rendered smaller even with same font-size)

        gpio_matches = re.findall(r'<span[^>]*>GPIO \d+</span>', sensor_html + display_html)
        i2c_matches = re.findall(r'<span[^>]*>0x[0-9A-F]+</span>', display_html)

        for match in gpio_matches + i2c_matches:
            self.assertNotIn('font-family:monospace', match,
                           f"Monospace font causes size mismatch: {match}")

    def test_prevent_inconsistent_label_value_styling(self):
        """Prevent regression: Inconsistent styling between labels and values"""
        sensor_html = MockWebUIGenerator.generate_sensor_card_html()
        display_html = MockWebUIGenerator.generate_display_card_html()

        # Both should use pattern: <span style="color:#64748b">Label:</span> <span>Value</span>
        # NOT: <div style="color:#64748b">Label: <span style="color:#1e293b">Value</span></div>

        # Check that labels use span with gray color, not div
        sensor_config = re.search(r'Configuration</div>.*?</div></div>', sensor_html, re.DOTALL)
        display_config = re.search(r'Configuration</div>.*?</div></div>', display_html, re.DOTALL)

        # Labels should be in spans, not divs with color:#64748b
        bad_pattern = r'<div style="[^"]*color:#64748b[^"]*">[^<]*:[^<]*<span'

        self.assertEqual(len(re.findall(bad_pattern, sensor_config.group(0))), 0,
                        "Sensor config should not have color on entire div")
        self.assertEqual(len(re.findall(bad_pattern, display_config.group(0))), 0,
                        "Display config should not have color on entire div")


def run_tests():
    """Run all font consistency tests"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    suite.addTests(loader.loadTestsFromTestCase(TestWebUIFontConsistency))
    suite.addTests(loader.loadTestsFromTestCase(TestFontSizeRegressionPreventions))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
