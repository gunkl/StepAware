#!/usr/bin/env python3
"""
Unit tests for Watchdog Manager

Tests watchdog functionality including health checks, recovery actions,
and hardware watchdog integration.
"""

import unittest
import json


class MockWatchdog:
    """Mock Watchdog Manager for testing"""

    MODULE_STATE_MACHINE = 0
    MODULE_CONFIG_MANAGER = 1
    MODULE_LOGGER = 2
    MODULE_HAL_BUTTON = 3
    MODULE_HAL_LED = 4
    MODULE_HAL_PIR = 5
    MODULE_WEB_SERVER = 6
    MODULE_MEMORY = 7

    HEALTH_OK = 0
    HEALTH_WARNING = 1
    HEALTH_CRITICAL = 2
    HEALTH_FAILED = 3

    RECOVERY_NONE = 0
    RECOVERY_SOFT = 1
    RECOVERY_MODULE_RESTART = 2
    RECOVERY_SYSTEM_REBOOT = 3
    RECOVERY_HW_WATCHDOG = 4

    def __init__(self):
        self.modules = {}
        self.hw_wdt_fed = False
        self.hw_wdt_feed_count = 0
        self.system_rebooted = False
        self.recovery_actions_taken = []

        # Configuration
        self.soft_recovery_threshold = 2
        self.module_restart_threshold = 5
        self.system_recovery_threshold = 10

    def register_module(self, module_id, check_func, recovery_func=None):
        """Register a module for monitoring"""
        self.modules[module_id] = {
            'check_func': check_func,
            'recovery_func': recovery_func,
            'status': self.HEALTH_OK,
            'failure_count': 0,
            'total_failures': 0,
            'message': None
        }

    def update(self):
        """Update watchdog (check health, feed HW WDT)"""
        # Check all module health
        for module_id, info in self.modules.items():
            if info['check_func']:
                status, message = info['check_func']()
                self._update_module_health(module_id, status, message)

        # Feed HW WDT if system healthy
        if self.is_healthy():
            self.feed_hw_watchdog()

    def _update_module_health(self, module_id, status, message):
        """Update module health status"""
        info = self.modules[module_id]
        old_status = info['status']
        info['status'] = status
        info['message'] = message

        # Handle failures
        if status in [self.HEALTH_CRITICAL, self.HEALTH_FAILED]:
            info['failure_count'] += 1
            info['total_failures'] += 1
            self._handle_module_failure(module_id)
        elif status == self.HEALTH_OK and info['failure_count'] > 0:
            # Module recovered
            info['failure_count'] = 0

    def _handle_module_failure(self, module_id):
        """Handle module failure with recovery actions"""
        info = self.modules[module_id]
        action = self._determine_recovery_action(info['failure_count'])

        self.recovery_actions_taken.append({
            'module': module_id,
            'action': action,
            'failure_count': info['failure_count']
        })

        # Execute recovery
        if action == self.RECOVERY_SOFT:
            if info['recovery_func']:
                info['recovery_func'](action)
        elif action == self.RECOVERY_MODULE_RESTART:
            if info['recovery_func']:
                info['recovery_func'](action)
        elif action == self.RECOVERY_SYSTEM_REBOOT:
            self.system_rebooted = True
        elif action == self.RECOVERY_HW_WATCHDOG:
            # Stop feeding HW WDT
            pass

    def _determine_recovery_action(self, failure_count):
        """Determine recovery action based on failure count"""
        if failure_count >= self.system_recovery_threshold:
            return self.RECOVERY_HW_WATCHDOG
        elif failure_count >= self.module_restart_threshold:
            return self.RECOVERY_SYSTEM_REBOOT
        elif failure_count >= self.soft_recovery_threshold:
            return self.RECOVERY_MODULE_RESTART
        else:
            return self.RECOVERY_SOFT

    def feed_hw_watchdog(self):
        """Feed hardware watchdog"""
        self.hw_wdt_fed = True
        self.hw_wdt_feed_count += 1

    def get_system_health(self):
        """Get worst health status across all modules"""
        worst = self.HEALTH_OK
        for info in self.modules.values():
            if info['status'] > worst:
                worst = info['status']
        return worst

    def get_module_health(self, module_id):
        """Get specific module health"""
        if module_id in self.modules:
            return self.modules[module_id]['status']
        return self.HEALTH_FAILED

    def is_healthy(self):
        """Check if system is healthy"""
        status = self.get_system_health()
        return status in [self.HEALTH_OK, self.HEALTH_WARNING]


class TestWatchdog(unittest.TestCase):
    """Test cases for Watchdog Manager"""

    def setUp(self):
        """Set up test fixtures"""
        self.watchdog = MockWatchdog()

    def test_module_registration(self):
        """Test module registration"""
        def check_func():
            return MockWatchdog.HEALTH_OK, None

        self.watchdog.register_module(
            MockWatchdog.MODULE_STATE_MACHINE,
            check_func
        )

        self.assertIn(MockWatchdog.MODULE_STATE_MACHINE, self.watchdog.modules)

    def test_healthy_system_feeds_watchdog(self):
        """Test that healthy system feeds HW watchdog"""
        def check_func():
            return MockWatchdog.HEALTH_OK, None

        self.watchdog.register_module(
            MockWatchdog.MODULE_MEMORY,
            check_func
        )

        self.watchdog.update()

        self.assertTrue(self.watchdog.hw_wdt_fed)
        self.assertEqual(self.watchdog.hw_wdt_feed_count, 1)

    def test_unhealthy_system_stops_feeding(self):
        """Test that unhealthy system stops feeding HW watchdog"""
        def check_func():
            return MockWatchdog.HEALTH_FAILED, "Critical failure"

        self.watchdog.register_module(
            MockWatchdog.MODULE_MEMORY,
            check_func
        )

        self.watchdog.update()

        # HW WDT should not be fed when system unhealthy
        # (In real implementation, this would trigger HW reset)
        self.assertFalse(self.watchdog.is_healthy())

    def test_soft_recovery_on_first_failures(self):
        """Test soft recovery on initial failures"""
        failure_count = 0

        def check_func():
            nonlocal failure_count
            failure_count += 1
            if failure_count <= 1:
                return MockWatchdog.HEALTH_FAILED, "Failed"
            return MockWatchdog.HEALTH_OK, None

        def recovery_func(action):
            self.assertEqual(action, MockWatchdog.RECOVERY_SOFT)
            return True

        self.watchdog.register_module(
            MockWatchdog.MODULE_STATE_MACHINE,
            check_func,
            recovery_func
        )

        # First failure -> soft recovery
        self.watchdog.update()
        self.assertEqual(len(self.watchdog.recovery_actions_taken), 1)
        self.assertEqual(
            self.watchdog.recovery_actions_taken[0]['action'],
            MockWatchdog.RECOVERY_SOFT
        )

    def test_module_restart_after_threshold(self):
        """Test module restart after failure threshold"""
        failure_count = [0]

        def check_func():
            return MockWatchdog.HEALTH_FAILED, "Failed"

        def recovery_func(action):
            return True

        self.watchdog.register_module(
            MockWatchdog.MODULE_STATE_MACHINE,
            check_func,
            recovery_func
        )

        # Trigger multiple failures
        for i in range(self.watchdog.module_restart_threshold):
            self.watchdog.update()

        # Should trigger module restart
        actions = [a['action'] for a in self.watchdog.recovery_actions_taken]
        self.assertIn(MockWatchdog.RECOVERY_MODULE_RESTART, actions)

    def test_system_reboot_on_critical_failures(self):
        """Test system reboot on critical failures"""
        def check_func():
            return MockWatchdog.HEALTH_FAILED, "Critical"

        self.watchdog.register_module(
            MockWatchdog.MODULE_CONFIG_MANAGER,
            check_func
        )

        # Trigger failures past reboot threshold
        for i in range(self.watchdog.module_restart_threshold + 1):
            self.watchdog.update()

        # Should trigger system reboot
        actions = [a['action'] for a in self.watchdog.recovery_actions_taken]
        self.assertIn(MockWatchdog.RECOVERY_SYSTEM_REBOOT, actions)

    def test_hw_watchdog_reset_last_resort(self):
        """Test HW watchdog reset as last resort"""
        def check_func():
            return MockWatchdog.HEALTH_FAILED, "Unrecoverable"

        self.watchdog.register_module(
            MockWatchdog.MODULE_MEMORY,
            check_func
        )

        # Trigger failures past HW WDT threshold
        for i in range(self.watchdog.system_recovery_threshold):
            self.watchdog.update()

        # Should trigger HW WDT reset
        actions = [a['action'] for a in self.watchdog.recovery_actions_taken]
        self.assertIn(MockWatchdog.RECOVERY_HW_WATCHDOG, actions)

    def test_module_recovery_resets_failure_count(self):
        """Test that successful recovery resets failure count"""
        failure_count = [0]

        def check_func():
            nonlocal failure_count
            failure_count[0] += 1
            if failure_count[0] <= 2:
                return MockWatchdog.HEALTH_FAILED, "Failed"
            return MockWatchdog.HEALTH_OK, None  # Recovered

        self.watchdog.register_module(
            MockWatchdog.MODULE_STATE_MACHINE,
            check_func
        )

        # Fail twice
        self.watchdog.update()
        self.watchdog.update()
        self.assertEqual(
            self.watchdog.modules[MockWatchdog.MODULE_STATE_MACHINE]['failure_count'],
            2
        )

        # Recover
        self.watchdog.update()
        self.assertEqual(
            self.watchdog.modules[MockWatchdog.MODULE_STATE_MACHINE]['failure_count'],
            0
        )

    def test_warning_status_feeds_watchdog(self):
        """Test that WARNING status still feeds watchdog"""
        def check_func():
            return MockWatchdog.HEALTH_WARNING, "Low memory"

        self.watchdog.register_module(
            MockWatchdog.MODULE_MEMORY,
            check_func
        )

        self.watchdog.update()

        # WARNING is acceptable, should still feed WDT
        self.assertTrue(self.watchdog.is_healthy())
        self.assertTrue(self.watchdog.hw_wdt_fed)

    def test_multiple_module_failures(self):
        """Test handling of multiple failing modules"""
        def check_memory():
            return MockWatchdog.HEALTH_FAILED, "OOM"

        def check_state_machine():
            return MockWatchdog.HEALTH_FAILED, "Stuck"

        self.watchdog.register_module(
            MockWatchdog.MODULE_MEMORY,
            check_memory
        )
        self.watchdog.register_module(
            MockWatchdog.MODULE_STATE_MACHINE,
            check_state_machine
        )

        self.watchdog.update()

        # Both modules should be marked failed
        self.assertEqual(
            self.watchdog.get_module_health(MockWatchdog.MODULE_MEMORY),
            MockWatchdog.HEALTH_FAILED
        )
        self.assertEqual(
            self.watchdog.get_module_health(MockWatchdog.MODULE_STATE_MACHINE),
            MockWatchdog.HEALTH_FAILED
        )

        # System should be unhealthy
        self.assertFalse(self.watchdog.is_healthy())

    def test_system_health_reflects_worst_module(self):
        """Test that system health reflects worst module status"""
        def check_ok():
            return MockWatchdog.HEALTH_OK, None

        def check_warning():
            return MockWatchdog.HEALTH_WARNING, "Minor issue"

        def check_critical():
            return MockWatchdog.HEALTH_CRITICAL, "Major issue"

        self.watchdog.register_module(MockWatchdog.MODULE_MEMORY, check_ok)
        self.watchdog.register_module(MockWatchdog.MODULE_LOGGER, check_warning)
        self.watchdog.register_module(MockWatchdog.MODULE_CONFIG_MANAGER, check_critical)

        self.watchdog.update()

        # System health should be CRITICAL (worst status)
        self.assertEqual(
            self.watchdog.get_system_health(),
            MockWatchdog.HEALTH_CRITICAL
        )


if __name__ == '__main__':
    print("Running Watchdog Manager tests...")
    unittest.main(verbosity=2)
