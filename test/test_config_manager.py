#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test Configuration Manager - Validates config loading, saving, and validation

This test suite ensures the configuration manager correctly handles JSON
serialization, validation, defaults, and persistence.
"""

import unittest
import json


class MockConfigManager:
    """Mock configuration manager matching C++ implementation"""

    # Default values (from config.h)
    DEFAULTS = {
        "motion": {
            "warningDuration": 15000,
            "pirWarmup": 60000
        },
        "button": {
            "debounceMs": 50,
            "longPressMs": 1000
        },
        "led": {
            "brightnessFull": 255,
            "brightnessMedium": 128,
            "brightnessDim": 20,
            "blinkFastMs": 250,
            "blinkSlowMs": 1000,
            "blinkWarningMs": 500
        },
        "battery": {
            "voltageFull": 4200,
            "voltageLow": 3300,
            "voltageCritical": 3000
        },
        "light": {
            "thresholdDark": 500,
            "thresholdBright": 2000
        },
        "wifi": {
            "ssid": "",
            "password": "",
            "enabled": False
        },
        "device": {
            "name": "StepAware",
            "defaultMode": 2,
            "rememberMode": False
        },
        "power": {
            "savingEnabled": False,
            "deepSleepAfterMs": 3600000
        },
        "logging": {
            "level": 1,  # INFO
            "serialEnabled": True,
            "fileEnabled": False
        },
        "metadata": {
            "version": "0.1.0",
            "lastModified": 0
        }
    }

    def __init__(self):
        self.config = {}
        self.last_error = ""
        self.reset()

    def reset(self):
        """Reset to factory defaults"""
        self.config = json.loads(json.dumps(self.DEFAULTS))  # Deep copy
        self.last_error = ""

    def load_from_json(self, json_str):
        """Load configuration from JSON string"""
        try:
            data = json.loads(json_str)
            self.config = data
            return self.validate()
        except json.JSONDecodeError as e:
            self.last_error = f"JSON parse error: {e}"
            return False

    def to_json(self):
        """Convert configuration to JSON string"""
        return json.dumps(self.config, indent=2)

    def validate(self):
        """Validate all configuration parameters"""
        # Motion detection validation
        motion = self.config.get("motion", {})
        warning_duration = motion.get("warningDuration", 0)
        if warning_duration < 1000 or warning_duration > 300000:
            self.last_error = "Invalid motion warning duration (1-300s)"
            return False

        # Button validation
        button = self.config.get("button", {})
        debounce = button.get("debounceMs", 0)
        if debounce < 10 or debounce > 500:
            self.last_error = "Invalid button debounce time (10-500ms)"
            return False

        long_press = button.get("longPressMs", 0)
        if long_press < 500 or long_press > 10000:
            self.last_error = "Invalid long press duration (500-10000ms)"
            return False

        # LED brightness validation
        led = self.config.get("led", {})
        for key in ["brightnessFull", "brightnessMedium", "brightnessDim"]:
            value = led.get(key, 0)
            if value > 255:
                self.last_error = f"Invalid LED brightness (0-255): {key}"
                return False

        # LED blink validation
        blink_fast = led.get("blinkFastMs", 0)
        if blink_fast < 50 or blink_fast > 5000:
            self.last_error = "Invalid LED blink fast time (50-5000ms)"
            return False

        # Battery validation
        battery = self.config.get("battery", {})
        critical = battery.get("voltageCritical", 0)
        low = battery.get("voltageLow", 0)
        full = battery.get("voltageFull", 0)

        if not (critical < low < full):
            self.last_error = "Invalid battery voltage thresholds"
            return False

        # Log level validation (0=VERBOSE to 5=NONE)
        logging = self.config.get("logging", {})
        log_level = logging.get("level", 0)
        if log_level > 5:  # LOG_LEVEL_NONE
            self.last_error = "Invalid log level"
            return False

        return True

    def set_value(self, section, key, value):
        """Set a configuration value"""
        if section not in self.config:
            self.config[section] = {}
        self.config[section][key] = value

    def get_value(self, section, key, default=None):
        """Get a configuration value"""
        return self.config.get(section, {}).get(key, default)


class TestConfigManager(unittest.TestCase):
    """Test suite for configuration manager"""

    def setUp(self):
        self.config = MockConfigManager()

    def test_default_values(self):
        """Test that defaults are loaded correctly"""
        self.assertEqual(self.config.get_value("motion", "warningDuration"), 15000)
        self.assertEqual(self.config.get_value("button", "debounceMs"), 50)
        self.assertEqual(self.config.get_value("led", "brightnessFull"), 255)
        self.assertEqual(self.config.get_value("device", "name"), "StepAware")

    def test_json_serialization(self):
        """Test converting config to JSON"""
        json_str = self.config.to_json()
        self.assertIsNotNone(json_str)
        self.assertIn("motion", json_str)
        self.assertIn("warningDuration", json_str)

        # Should be valid JSON
        parsed = json.loads(json_str)
        self.assertEqual(parsed["motion"]["warningDuration"], 15000)

    def test_json_deserialization(self):
        """Test loading config from JSON"""
        # Start with defaults, then modify
        config_dict = json.loads(json.dumps(self.config.DEFAULTS))
        config_dict["motion"]["warningDuration"] = 20000
        config_dict["motion"]["pirWarmup"] = 30000
        config_dict["device"]["name"] = "TestDevice"
        config_dict["device"]["defaultMode"] = 1
        config_dict["device"]["rememberMode"] = True

        json_str = json.dumps(config_dict)

        result = self.config.load_from_json(json_str)
        self.assertTrue(result)
        self.assertEqual(self.config.get_value("motion", "warningDuration"), 20000)
        self.assertEqual(self.config.get_value("device", "name"), "TestDevice")

    def test_factory_reset(self):
        """Test factory reset restores defaults"""
        # Modify config
        self.config.set_value("motion", "warningDuration", 30000)
        self.config.set_value("device", "name", "Modified")

        # Reset
        self.config.reset()

        # Should be back to defaults
        self.assertEqual(self.config.get_value("motion", "warningDuration"), 15000)
        self.assertEqual(self.config.get_value("device", "name"), "StepAware")

    def test_validation_motion_duration_too_short(self):
        """Test validation rejects too short warning duration"""
        self.config.set_value("motion", "warningDuration", 500)
        self.assertFalse(self.config.validate())
        self.assertIn("warning duration", self.config.last_error.lower())

    def test_validation_motion_duration_too_long(self):
        """Test validation rejects too long warning duration"""
        self.config.set_value("motion", "warningDuration", 400000)
        self.assertFalse(self.config.validate())

    def test_validation_button_debounce_invalid(self):
        """Test validation rejects invalid debounce time"""
        # Too short
        self.config.set_value("button", "debounceMs", 5)
        self.assertFalse(self.config.validate())

        # Too long
        self.config.reset()
        self.config.set_value("button", "debounceMs", 600)
        self.assertFalse(self.config.validate())

    def test_validation_long_press_invalid(self):
        """Test validation rejects invalid long press time"""
        # Too short
        self.config.set_value("button", "longPressMs", 200)
        self.assertFalse(self.config.validate())

        # Too long
        self.config.reset()
        self.config.set_value("button", "longPressMs", 15000)
        self.assertFalse(self.config.validate())

    def test_validation_led_brightness_invalid(self):
        """Test validation rejects brightness > 255"""
        self.config.set_value("led", "brightnessFull", 300)
        self.assertFalse(self.config.validate())

    def test_validation_battery_thresholds_invalid(self):
        """Test validation rejects invalid battery thresholds"""
        # Critical >= Low
        self.config.set_value("battery", "voltageCritical", 3500)
        self.config.set_value("battery", "voltageLow", 3300)
        self.assertFalse(self.config.validate())

        # Low >= Full
        self.config.reset()
        self.config.set_value("battery", "voltageLow", 4500)
        self.assertFalse(self.config.validate())

    def test_validation_log_level_invalid(self):
        """Test validation rejects invalid log level"""
        self.config.set_value("logging", "level", 10)
        self.assertFalse(self.config.validate())

    def test_valid_custom_config(self):
        """Test that valid custom config passes validation"""
        self.config.set_value("motion", "warningDuration", 20000)
        self.config.set_value("button", "debounceMs", 75)
        self.config.set_value("button", "longPressMs", 2000)
        self.config.set_value("led", "brightnessFull", 200)
        self.config.set_value("led", "blinkFastMs", 300)

        self.assertTrue(self.config.validate())

    def test_wifi_credentials(self):
        """Test WiFi credential storage"""
        self.config.set_value("wifi", "ssid", "MyNetwork")
        self.config.set_value("wifi", "password", "SecurePassword123")
        self.config.set_value("wifi", "enabled", True)

        self.assertEqual(self.config.get_value("wifi", "ssid"), "MyNetwork")
        self.assertEqual(self.config.get_value("wifi", "password"), "SecurePassword123")
        self.assertTrue(self.config.get_value("wifi", "enabled"))

    def test_power_management_settings(self):
        """Test power management configuration"""
        self.config.set_value("power", "savingEnabled", True)
        self.config.set_value("power", "deepSleepAfterMs", 1800000)  # 30 min

        self.assertTrue(self.config.get_value("power", "savingEnabled"))
        self.assertEqual(self.config.get_value("power", "deepSleepAfterMs"), 1800000)

    def test_device_name_customization(self):
        """Test device name can be customized"""
        self.config.set_value("device", "name", "Basement-Stairs")
        self.assertEqual(self.config.get_value("device", "name"), "Basement-Stairs")

    def test_remember_last_mode(self):
        """Test remember last mode setting"""
        self.config.set_value("device", "rememberMode", True)
        self.assertTrue(self.config.get_value("device", "rememberMode"))

    def test_invalid_json_handling(self):
        """Test that invalid JSON is rejected"""
        invalid_json = '{"motion": {"warningDuration": INVALID}}'
        result = self.config.load_from_json(invalid_json)
        self.assertFalse(result)
        self.assertIn("JSON parse error", self.config.last_error)


def run_tests():
    """Run all configuration manager tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestConfigManager)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
