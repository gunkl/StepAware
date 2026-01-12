#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test Web API - Validates REST API endpoints

This test suite ensures the web API correctly handles requests,
returns proper JSON responses, and validates inputs.
"""

import unittest
import json


class MockWebAPI:
    """Mock Web API matching C++ implementation"""

    def __init__(self):
        self.system_status = {
            "uptime": 0,
            "freeHeap": 200000,
            "mode": 2,
            "modeName": "MOTION_DETECT",
            "warningActive": False,
            "motionEvents": 0,
            "modeChanges": 0
        }
        self.config = {}
        self.logs = []
        self.cors_enabled = True

    def get_status(self):
        """GET /api/status"""
        return {
            "code": 200,
            "body": self.system_status
        }

    def get_config(self):
        """GET /api/config"""
        if not self.config:
            return {
                "code": 500,
                "body": {"error": "Configuration not loaded"}
            }
        return {
            "code": 200,
            "body": self.config
        }

    def post_config(self, config_json):
        """POST /api/config"""
        try:
            config = json.loads(config_json) if isinstance(config_json, str) else config_json

            # Validate required fields
            required = ["motion", "button", "led", "battery"]
            for field in required:
                if field not in config:
                    return {
                        "code": 400,
                        "body": {"error": f"Missing required field: {field}"}
                    }

            # Validate motion settings
            motion = config.get("motion", {})
            warning_duration = motion.get("warningDuration", 0)
            if warning_duration < 1000 or warning_duration > 300000:
                return {
                    "code": 400,
                    "body": {"error": "Invalid motion warning duration"}
                }

            self.config = config
            return {
                "code": 200,
                "body": self.config
            }

        except json.JSONDecodeError:
            return {
                "code": 400,
                "body": {"error": "Invalid JSON"}
            }

    def get_mode(self):
        """GET /api/mode"""
        return {
            "code": 200,
            "body": {
                "mode": self.system_status["mode"],
                "modeName": self.system_status["modeName"]
            }
        }

    def post_mode(self, mode_json):
        """POST /api/mode"""
        try:
            data = json.loads(mode_json) if isinstance(mode_json, str) else mode_json

            if "mode" not in data:
                return {
                    "code": 400,
                    "body": {"error": "Missing 'mode' field"}
                }

            mode = data["mode"]

            # Validate mode (0=OFF, 1=CONTINUOUS_ON, 2=MOTION_DETECT)
            if mode < 0 or mode > 2:
                return {
                    "code": 400,
                    "body": {"error": "Invalid mode value"}
                }

            mode_names = {0: "OFF", 1: "CONTINUOUS_ON", 2: "MOTION_DETECT"}
            self.system_status["mode"] = mode
            self.system_status["modeName"] = mode_names[mode]
            self.system_status["modeChanges"] += 1

            return self.get_mode()

        except json.JSONDecodeError:
            return {
                "code": 400,
                "body": {"error": "Invalid JSON"}
            }

    def get_logs(self, max_entries=50):
        """GET /api/logs"""
        logs_to_return = self.logs[-max_entries:] if len(self.logs) > max_entries else self.logs

        return {
            "code": 200,
            "body": {
                "logs": logs_to_return,
                "count": len(self.logs),
                "returned": len(logs_to_return)
            }
        }

    def post_reset(self):
        """POST /api/reset"""
        self.config = {}
        self.system_status["modeChanges"] = 0
        self.system_status["motionEvents"] = 0

        return {
            "code": 200,
            "body": {
                "success": True,
                "message": "Configuration reset to factory defaults"
            }
        }

    def get_version(self):
        """GET /api/version"""
        return {
            "code": 200,
            "body": {
                "firmware": "StepAware",
                "version": "0.1.0",
                "buildDate": "Jan 11 2026",
                "buildTime": "12:00:00"
            }
        }

    def add_log(self, level, message):
        """Add log entry for testing"""
        self.logs.append({
            "timestamp": len(self.logs) * 1000,
            "level": level,
            "levelName": ["DEBUG", "INFO", "WARN", "ERROR"][level],
            "message": message
        })


class TestWebAPI(unittest.TestCase):
    """Test suite for Web API"""

    def setUp(self):
        self.api = MockWebAPI()

    def test_get_status(self):
        """Test GET /api/status endpoint"""
        response = self.api.get_status()

        self.assertEqual(response["code"], 200)
        self.assertIn("uptime", response["body"])
        self.assertIn("mode", response["body"])
        self.assertIn("modeName", response["body"])
        self.assertIn("warningActive", response["body"])

    def test_get_status_returns_correct_mode(self):
        """Test status endpoint returns current mode"""
        response = self.api.get_status()

        self.assertEqual(response["body"]["mode"], 2)
        self.assertEqual(response["body"]["modeName"], "MOTION_DETECT")

    def test_get_config_without_config(self):
        """Test GET /api/config returns error when config not loaded"""
        response = self.api.get_config()

        self.assertEqual(response["code"], 500)
        self.assertIn("error", response["body"])

    def test_post_config_valid(self):
        """Test POST /api/config with valid configuration"""
        config = {
            "motion": {"warningDuration": 20000, "pirWarmup": 60000},
            "button": {"debounceMs": 50, "longPressMs": 1000},
            "led": {"brightnessFull": 255},
            "battery": {"voltageFull": 4200}
        }

        response = self.api.post_config(config)

        self.assertEqual(response["code"], 200)
        self.assertEqual(response["body"]["motion"]["warningDuration"], 20000)

    def test_post_config_invalid_json(self):
        """Test POST /api/config with invalid JSON"""
        response = self.api.post_config("{invalid json}")

        self.assertEqual(response["code"], 400)
        self.assertIn("error", response["body"])

    def test_post_config_missing_required_field(self):
        """Test POST /api/config missing required fields"""
        config = {
            "motion": {"warningDuration": 15000}
            # Missing button, led, battery
        }

        response = self.api.post_config(config)

        self.assertEqual(response["code"], 400)
        self.assertIn("error", response["body"])

    def test_post_config_invalid_warning_duration(self):
        """Test POST /api/config with invalid warning duration"""
        config = {
            "motion": {"warningDuration": 500},  # Too short
            "button": {"debounceMs": 50},
            "led": {"brightnessFull": 255},
            "battery": {"voltageFull": 4200}
        }

        response = self.api.post_config(config)

        self.assertEqual(response["code"], 400)
        self.assertIn("warning duration", response["body"]["error"].lower())

    def test_get_mode(self):
        """Test GET /api/mode endpoint"""
        response = self.api.get_mode()

        self.assertEqual(response["code"], 200)
        self.assertIn("mode", response["body"])
        self.assertIn("modeName", response["body"])
        self.assertEqual(response["body"]["mode"], 2)

    def test_post_mode_valid(self):
        """Test POST /api/mode with valid mode"""
        request = {"mode": 0}  # OFF mode
        response = self.api.post_mode(request)

        self.assertEqual(response["code"], 200)
        self.assertEqual(response["body"]["mode"], 0)
        self.assertEqual(response["body"]["modeName"], "OFF")

    def test_post_mode_changes_mode(self):
        """Test POST /api/mode actually changes the mode"""
        # Start in MOTION_DETECT (2)
        self.assertEqual(self.api.system_status["mode"], 2)

        # Change to CONTINUOUS_ON (1)
        self.api.post_mode({"mode": 1})

        # Verify change
        self.assertEqual(self.api.system_status["mode"], 1)
        self.assertEqual(self.api.system_status["modeName"], "CONTINUOUS_ON")

    def test_post_mode_increments_counter(self):
        """Test POST /api/mode increments mode change counter"""
        initial_changes = self.api.system_status["modeChanges"]

        self.api.post_mode({"mode": 0})

        self.assertEqual(self.api.system_status["modeChanges"], initial_changes + 1)

    def test_post_mode_invalid_mode(self):
        """Test POST /api/mode with invalid mode value"""
        response = self.api.post_mode({"mode": 5})  # Invalid

        self.assertEqual(response["code"], 400)
        self.assertIn("error", response["body"])

    def test_post_mode_missing_field(self):
        """Test POST /api/mode without mode field"""
        response = self.api.post_mode({})

        self.assertEqual(response["code"], 400)
        self.assertIn("Missing 'mode'", response["body"]["error"])

    def test_get_logs_empty(self):
        """Test GET /api/logs with no logs"""
        response = self.api.get_logs()

        self.assertEqual(response["code"], 200)
        self.assertEqual(response["body"]["count"], 0)
        self.assertEqual(response["body"]["returned"], 0)
        self.assertEqual(len(response["body"]["logs"]), 0)

    def test_get_logs_with_entries(self):
        """Test GET /api/logs returns log entries"""
        self.api.add_log(1, "System started")
        self.api.add_log(2, "Warning: Low battery")
        self.api.add_log(3, "Error: Sensor failure")

        response = self.api.get_logs()

        self.assertEqual(response["code"], 200)
        self.assertEqual(response["body"]["count"], 3)
        self.assertEqual(response["body"]["returned"], 3)
        self.assertEqual(len(response["body"]["logs"]), 3)

    def test_get_logs_limits_returned(self):
        """Test GET /api/logs limits entries returned"""
        # Add 100 log entries
        for i in range(100):
            self.api.add_log(1, f"Log entry {i}")

        response = self.api.get_logs(max_entries=50)

        self.assertEqual(response["body"]["count"], 100)
        self.assertEqual(response["body"]["returned"], 50)
        # Should return last 50
        self.assertEqual(response["body"]["logs"][0]["message"], "Log entry 50")
        self.assertEqual(response["body"]["logs"][-1]["message"], "Log entry 99")

    def test_post_reset(self):
        """Test POST /api/reset endpoint"""
        # Set some state
        self.api.config = {"test": "data"}
        self.api.system_status["modeChanges"] = 10
        self.api.system_status["motionEvents"] = 5

        response = self.api.post_reset()

        self.assertEqual(response["code"], 200)
        self.assertTrue(response["body"]["success"])
        self.assertEqual(self.api.system_status["modeChanges"], 0)
        self.assertEqual(self.api.system_status["motionEvents"], 0)

    def test_get_version(self):
        """Test GET /api/version endpoint"""
        response = self.api.get_version()

        self.assertEqual(response["code"], 200)
        self.assertIn("firmware", response["body"])
        self.assertIn("version", response["body"])
        self.assertIn("buildDate", response["body"])
        self.assertEqual(response["body"]["firmware"], "StepAware")


def run_tests():
    """Run all web API tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestWebAPI)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
