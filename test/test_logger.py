#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test Logger - Validates logging functionality

This test suite ensures the logger correctly handles log levels, circular buffer,
and entry management.
"""

import unittest


class MockLogger:
    """Mock logger matching C++ implementation"""

    # Log levels
    LEVEL_DEBUG = 0
    LEVEL_INFO = 1
    LEVEL_WARN = 2
    LEVEL_ERROR = 3
    LEVEL_NONE = 4

    BUFFER_SIZE = 256

    class LogEntry:
        def __init__(self, timestamp, level, message):
            self.timestamp = timestamp
            self.level = level
            self.message = message[:127]  # 128 char max

    def __init__(self):
        self.level = self.LEVEL_INFO
        self.serial_enabled = True
        self.file_enabled = False
        self.buffer = []
        self.buffer_head = 0
        self.buffer_tail = 0
        self.total_entries = 0
        self.current_time_ms = 0
        self.serial_output = []

    def set_time_ms(self, time_ms):
        self.current_time_ms = time_ms

    def advance_time(self, delta_ms):
        self.current_time_ms += delta_ms

    def set_level(self, level):
        self.level = level

    def get_level(self):
        return self.level

    def set_serial_enabled(self, enabled):
        self.serial_enabled = enabled

    def set_file_enabled(self, enabled):
        self.file_enabled = enabled

    def debug(self, message):
        if self.level <= self.LEVEL_DEBUG:
            self._add_entry(self.LEVEL_DEBUG, message)

    def info(self, message):
        if self.level <= self.LEVEL_INFO:
            self._add_entry(self.LEVEL_INFO, message)

    def warn(self, message):
        if self.level <= self.LEVEL_WARN:
            self._add_entry(self.LEVEL_WARN, message)

    def error(self, message):
        if self.level <= self.LEVEL_ERROR:
            self._add_entry(self.LEVEL_ERROR, message)

    def get_entry_count(self):
        if self.total_entries < self.BUFFER_SIZE:
            return self.total_entries
        return self.BUFFER_SIZE

    def get_entry(self, index):
        count = self.get_entry_count()
        if index >= count:
            return None

        buffer_index = (self.buffer_tail + index) % self.BUFFER_SIZE
        if buffer_index < len(self.buffer):
            return self.buffer[buffer_index]
        return None

    def clear(self):
        self.buffer = []
        self.buffer_head = 0
        self.buffer_tail = 0
        self.total_entries = 0
        self.serial_output = []

    def _add_entry(self, level, message):
        entry = self.LogEntry(self.current_time_ms, level, message)

        # Add to buffer
        if len(self.buffer) < self.BUFFER_SIZE:
            self.buffer.append(entry)
        else:
            # Circular buffer: replace oldest
            self.buffer[self.buffer_head] = entry
            self.buffer_tail = (self.buffer_tail + 1) % self.BUFFER_SIZE

        self.buffer_head = (self.buffer_head + 1) % self.BUFFER_SIZE
        self.total_entries += 1

        # Write to serial
        if self.serial_enabled:
            self.serial_output.append(f"[{self._format_timestamp(entry.timestamp)}] [{self._get_level_name(level)}] {message}")

    @staticmethod
    def _get_level_name(level):
        names = {0: "DEBUG", 1: "INFO ", 2: "WARN ", 3: "ERROR", 4: "NONE "}
        return names.get(level, "?????")

    @staticmethod
    def _format_timestamp(timestamp):
        seconds = timestamp // 1000
        minutes = seconds // 60
        hours = minutes // 60
        seconds %= 60
        minutes %= 60
        hours %= 24
        ms = timestamp % 1000
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{ms:03d}"


class TestLogger(unittest.TestCase):
    """Test suite for logger"""

    def setUp(self):
        self.logger = MockLogger()
        self.logger.set_time_ms(0)

    def test_initial_state(self):
        """Test logger initializes correctly"""
        self.assertEqual(self.logger.get_level(), MockLogger.LEVEL_INFO)
        self.assertEqual(self.logger.get_entry_count(), 0)
        self.assertTrue(self.logger.serial_enabled)
        self.assertFalse(self.logger.file_enabled)

    def test_log_level_filtering(self):
        """Test that log level filters messages correctly"""
        self.logger.set_level(MockLogger.LEVEL_WARN)

        # These should be filtered out
        self.logger.debug("Debug message")
        self.logger.info("Info message")

        # These should pass
        self.logger.warn("Warning message")
        self.logger.error("Error message")

        self.assertEqual(self.logger.get_entry_count(), 2)

    def test_debug_level_shows_all(self):
        """Test DEBUG level shows all messages"""
        self.logger.set_level(MockLogger.LEVEL_DEBUG)

        self.logger.debug("Debug")
        self.logger.info("Info")
        self.logger.warn("Warn")
        self.logger.error("Error")

        self.assertEqual(self.logger.get_entry_count(), 4)

    def test_error_level_shows_only_errors(self):
        """Test ERROR level shows only errors"""
        self.logger.set_level(MockLogger.LEVEL_ERROR)

        self.logger.debug("Debug")
        self.logger.info("Info")
        self.logger.warn("Warn")
        self.logger.error("Error")

        self.assertEqual(self.logger.get_entry_count(), 1)
        entry = self.logger.get_entry(0)
        self.assertEqual(entry.message, "Error")

    def test_none_level_filters_all(self):
        """Test NONE level filters all messages"""
        self.logger.set_level(MockLogger.LEVEL_NONE)

        self.logger.debug("Debug")
        self.logger.info("Info")
        self.logger.warn("Warn")
        self.logger.error("Error")

        self.assertEqual(self.logger.get_entry_count(), 0)

    def test_circular_buffer_wraparound(self):
        """Test circular buffer wraps around when full"""
        self.logger.set_level(MockLogger.LEVEL_DEBUG)

        # Fill buffer beyond capacity
        for i in range(MockLogger.BUFFER_SIZE + 50):
            self.logger.info(f"Message {i}")

        # Should have exactly BUFFER_SIZE entries
        self.assertEqual(self.logger.get_entry_count(), MockLogger.BUFFER_SIZE)

        # Oldest entry should be message 50 (first 50 were dropped)
        entry = self.logger.get_entry(0)
        self.assertEqual(entry.message, "Message 50")

        # Newest entry should be message 255+49 = 305
        entry = self.logger.get_entry(MockLogger.BUFFER_SIZE - 1)
        self.assertEqual(entry.message, f"Message {MockLogger.BUFFER_SIZE + 49}")

    def test_entry_retrieval(self):
        """Test retrieving specific log entries"""
        self.logger.info("First")
        self.logger.warn("Second")
        self.logger.error("Third")

        self.assertEqual(self.logger.get_entry_count(), 3)

        entry0 = self.logger.get_entry(0)
        self.assertEqual(entry0.message, "First")
        self.assertEqual(entry0.level, MockLogger.LEVEL_INFO)

        entry1 = self.logger.get_entry(1)
        self.assertEqual(entry1.message, "Second")
        self.assertEqual(entry1.level, MockLogger.LEVEL_WARN)

        entry2 = self.logger.get_entry(2)
        self.assertEqual(entry2.message, "Third")
        self.assertEqual(entry2.level, MockLogger.LEVEL_ERROR)

    def test_invalid_entry_index(self):
        """Test retrieving entry with invalid index"""
        self.logger.info("Test")

        entry = self.logger.get_entry(100)
        self.assertIsNone(entry)

    def test_clear_buffer(self):
        """Test clearing log buffer"""
        self.logger.info("Test 1")
        self.logger.info("Test 2")
        self.logger.info("Test 3")

        self.assertEqual(self.logger.get_entry_count(), 3)

        self.logger.clear()

        self.assertEqual(self.logger.get_entry_count(), 0)

    def test_timestamp_formatting(self):
        """Test timestamp formatting"""
        self.logger.set_time_ms(3661234)  # 1h 1m 1s 234ms
        self.logger.info("Test")

        entry = self.logger.get_entry(0)
        self.assertEqual(entry.timestamp, 3661234)

    def test_serial_output_enabled(self):
        """Test serial output when enabled"""
        self.logger.set_serial_enabled(True)
        self.logger.info("Test message")

        self.assertEqual(len(self.logger.serial_output), 1)
        self.assertIn("INFO", self.logger.serial_output[0])
        self.assertIn("Test message", self.logger.serial_output[0])

    def test_serial_output_disabled(self):
        """Test no serial output when disabled"""
        self.logger.set_serial_enabled(False)
        self.logger.info("Test message")

        self.assertEqual(len(self.logger.serial_output), 0)

    def test_message_truncation(self):
        """Test long messages are truncated"""
        long_message = "A" * 200
        self.logger.info(long_message)

        entry = self.logger.get_entry(0)
        self.assertEqual(len(entry.message), 127)  # Max 127 chars

    def test_multiple_log_levels(self):
        """Test logging at multiple levels"""
        self.logger.set_level(MockLogger.LEVEL_DEBUG)

        self.logger.debug("Debug msg")
        self.logger.info("Info msg")
        self.logger.warn("Warn msg")
        self.logger.error("Error msg")

        self.assertEqual(self.logger.get_entry_count(), 4)

        # Check levels are correct
        self.assertEqual(self.logger.get_entry(0).level, MockLogger.LEVEL_DEBUG)
        self.assertEqual(self.logger.get_entry(1).level, MockLogger.LEVEL_INFO)
        self.assertEqual(self.logger.get_entry(2).level, MockLogger.LEVEL_WARN)
        self.assertEqual(self.logger.get_entry(3).level, MockLogger.LEVEL_ERROR)


def run_tests():
    """Run all logger tests"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestLogger)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    sys.exit(run_tests())
