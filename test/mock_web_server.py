#!/usr/bin/env python3
"""
StepAware Mock Web Server

A standalone Python server that simulates the ESP32 StepAware web API and serves
the web UI for testing without hardware.

Usage:
    python test/mock_web_server.py

Then open: http://localhost:8080
"""

import json
import time
import random
from datetime import datetime
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS

app = Flask(__name__, static_folder='../data')
CORS(app)

# Mock system state
class MockState:
    def __init__(self):
        self.start_time = time.time() * 1000
        self.mode = 2  # MOTION_DETECT
        self.warning_active = False
        self.motion_events = 0
        self.mode_changes = 0

        # Default configuration
        self.config = {
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
                "ssid": "MyNetwork",
                "password": "MyPassword123",
                "enabled": True
            },
            "device": {
                "name": "StepAware-Mock",
                "defaultMode": 2,
                "rememberMode": False
            },
            "power": {
                "savingEnabled": False,
                "deepSleepAfterMs": 3600000
            },
            "logging": {
                "level": 1,
                "serialEnabled": True,
                "fileEnabled": False
            },
            "metadata": {
                "version": "0.1.0",
                "lastModified": int(time.time())
            }
        }

        # Circular log buffer (max 256 entries)
        self.logs = []
        self.max_logs = 256

        # Add initial logs
        self.add_log(1, "System initialized")
        self.add_log(1, "Configuration loaded from SPIFFS")
        self.add_log(1, "Web server started on port 80")
        self.add_log(1, f"Operating mode: {self.get_mode_name()}")

    def add_log(self, level, message):
        """Add a log entry (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)"""
        level_names = ["DEBUG", "INFO", "WARN", "ERROR"]
        timestamp = int((time.time() * 1000) - self.start_time)

        entry = {
            "timestamp": timestamp,
            "level": level,
            "levelName": level_names[level],
            "message": message
        }

        self.logs.append(entry)

        # Maintain circular buffer
        if len(self.logs) > self.max_logs:
            self.logs = self.logs[-self.max_logs:]

    def get_mode_name(self):
        """Get human-readable mode name"""
        mode_names = ["OFF", "CONTINUOUS_ON", "MOTION_DETECT"]
        return mode_names[self.mode] if 0 <= self.mode < 3 else "UNKNOWN"

    def get_uptime(self):
        """Get uptime in milliseconds"""
        return int((time.time() * 1000) - self.start_time)

    def get_free_heap(self):
        """Simulate free heap (random between 180KB-220KB)"""
        return random.randint(180000, 220000)

    def simulate_motion(self):
        """Randomly simulate motion events"""
        if self.mode == 2:  # MOTION_DETECT mode
            if random.random() < 0.1:  # 10% chance
                self.motion_events += 1
                self.warning_active = True
                self.add_log(1, f"Motion detected! Event #{self.motion_events}")
                return True
        return False

    def update_warning(self):
        """Update warning status based on duration"""
        if self.warning_active and random.random() < 0.3:
            self.warning_active = False

# Global state
state = MockState()

# Routes

@app.route('/')
def index():
    """Serve the main dashboard"""
    return send_from_directory('../data', 'index.html')

@app.route('/<path:path>')
def static_files(path):
    """Serve static files (CSS, JS)"""
    return send_from_directory('../data', path)

@app.route('/api/status', methods=['GET'])
def get_status():
    """GET /api/status - System status"""
    # Simulate motion events
    state.simulate_motion()
    state.update_warning()

    return jsonify({
        "uptime": state.get_uptime(),
        "freeHeap": state.get_free_heap(),
        "mode": state.mode,
        "modeName": state.get_mode_name(),
        "warningActive": state.warning_active,
        "motionEvents": state.motion_events,
        "modeChanges": state.mode_changes
    })

@app.route('/api/config', methods=['GET'])
def get_config():
    """GET /api/config - Get configuration"""
    return jsonify(state.config)

@app.route('/api/config', methods=['POST'])
def post_config():
    """POST /api/config - Update configuration"""
    try:
        data = request.get_json()

        if not data:
            return jsonify({"error": "Invalid JSON"}), 400

        # Update configuration
        state.config.update(data)
        state.config["metadata"]["lastModified"] = int(time.time())

        state.add_log(1, "Configuration updated")

        return jsonify(state.config)

    except Exception as e:
        state.add_log(3, f"Failed to update config: {str(e)}")
        return jsonify({"error": str(e)}), 500

@app.route('/api/mode', methods=['GET'])
def get_mode():
    """GET /api/mode - Get current mode"""
    return jsonify({
        "mode": state.mode,
        "modeName": state.get_mode_name()
    })

@app.route('/api/mode', methods=['POST'])
def post_mode():
    """POST /api/mode - Set operating mode"""
    try:
        data = request.get_json()

        if not data or 'mode' not in data:
            return jsonify({"error": "Missing 'mode' field"}), 400

        new_mode = data['mode']

        if not isinstance(new_mode, int) or new_mode < 0 or new_mode > 2:
            return jsonify({"error": "Invalid mode value (must be 0-2)"}), 400

        old_mode_name = state.get_mode_name()
        state.mode = new_mode
        state.mode_changes += 1
        new_mode_name = state.get_mode_name()

        state.add_log(1, f"Mode changed: {old_mode_name} -> {new_mode_name}")

        # Reset warning if switching to OFF
        if new_mode == 0:
            state.warning_active = False

        return jsonify({
            "mode": state.mode,
            "modeName": state.get_mode_name()
        })

    except Exception as e:
        state.add_log(3, f"Failed to change mode: {str(e)}")
        return jsonify({"error": str(e)}), 400

@app.route('/api/logs', methods=['GET'])
def get_logs():
    """GET /api/logs - Get log entries"""
    # Return last 50 logs (most recent first)
    logs = list(reversed(state.logs[-50:]))

    return jsonify({
        "logs": logs,
        "count": len(state.logs),
        "returned": len(logs)
    })

@app.route('/api/reset', methods=['POST'])
def factory_reset():
    """POST /api/reset - Factory reset"""
    try:
        # Reset to defaults
        state.config = {
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
                "name": "StepAware-Mock",
                "defaultMode": 2,
                "rememberMode": False
            },
            "power": {
                "savingEnabled": False,
                "deepSleepAfterMs": 3600000
            },
            "logging": {
                "level": 1,
                "serialEnabled": True,
                "fileEnabled": False
            },
            "metadata": {
                "version": "0.1.0",
                "lastModified": int(time.time())
            }
        }

        state.add_log(2, "Factory reset performed - configuration restored to defaults")

        return jsonify({
            "success": True,
            "message": "Configuration reset to factory defaults"
        })

    except Exception as e:
        state.add_log(3, f"Factory reset failed: {str(e)}")
        return jsonify({"error": str(e)}), 500

@app.route('/api/version', methods=['GET'])
def get_version():
    """GET /api/version - Get firmware version"""
    return jsonify({
        "firmware": "StepAware Mock",
        "version": "0.1.0",
        "buildDate": datetime.now().strftime("%b %d %Y"),
        "buildTime": datetime.now().strftime("%H:%M:%S")
    })

# Background simulation
def simulate_activity():
    """Periodically add simulated system events"""
    import threading

    def run():
        while True:
            time.sleep(10)  # Every 10 seconds

            # Random system events
            if random.random() < 0.2:
                events = [
                    (0, "Checking battery voltage: 3850mV"),
                    (0, "Light level: 1200 lux"),
                    (1, "System health check OK"),
                    (0, "Free heap: {}KB".format(state.get_free_heap() // 1024))
                ]
                level, msg = random.choice(events)
                state.add_log(level, msg)

    thread = threading.Thread(target=run, daemon=True)
    thread.start()

if __name__ == '__main__':
    print("=" * 60)
    print("StepAware Mock Web Server")
    print("=" * 60)
    print("")
    print("Starting server...")
    print("")
    print("Dashboard URL: http://localhost:8080")
    print("API Base URL:  http://localhost:8080/api")
    print("")
    print("Available endpoints:")
    print("  GET  /api/status  - System status")
    print("  GET  /api/config  - Configuration")
    print("  POST /api/config  - Update config")
    print("  GET  /api/mode    - Current mode")
    print("  POST /api/mode    - Change mode")
    print("  GET  /api/logs    - Log entries")
    print("  POST /api/reset   - Factory reset")
    print("  GET  /api/version - Version info")
    print("")
    print("Press Ctrl+C to stop")
    print("=" * 60)
    print("")

    # Start background simulation
    simulate_activity()

    # Run Flask server
    app.run(host='0.0.0.0', port=8080, debug=False)
