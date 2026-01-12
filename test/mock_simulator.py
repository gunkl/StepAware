#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
StepAware Mock Hardware Simulator
Simulates the firmware behavior for testing without physical hardware.
"""

import time
import sys
import os
from enum import Enum

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

class OperatingMode(Enum):
    OFF = 0
    CONTINUOUS_ON = 1
    MOTION_DETECT = 2

class MockStepAware:
    def __init__(self):
        self.mode = OperatingMode.OFF
        self.motion_events = 0
        self.mode_changes = 0
        self.button_clicks = 0
        self.led_on = False
        self.led_warning_active = False
        self.warning_end_time = 0

    def print_banner(self):
        print("\n\n")
        print("╔════════════════════════════════════════╗")
        print("║                                        ║")
        print("║          S T E P A W A R E             ║")
        print("║                                        ║")
        print("║   Motion-Activated Hazard Warning     ║")
        print("║                                        ║")
        print("╚════════════════════════════════════════╝")
        print()
        print("Version: 0.1.0")
        print("Board: ESP32-C3-DevKit-Lipo (SIMULATED)")
        print("Sensor: AM312 PIR (MOCK)")
        print()
        print("⚠️  MOCK HARDWARE MODE ENABLED")
        print("   Using simulated hardware for development")
        print()
        print("Phase 1 - MVP Implementation")
        print("- Motion Detection: ✓")
        print("- LED Warning: ✓")
        print("- Mode Switching: ✓")
        print()

    def print_help(self):
        print("\n========================================")
        print("StepAware - Serial Commands")
        print("========================================")
        print()
        print("Status & Info:")
        print("  h - Show this help menu")
        print("  s - Print system status")
        print()
        print("Mock Hardware Commands:")
        print("  m - Trigger motion detection")
        print("  b - Simulate button press")
        print()
        print("Mode Control:")
        print("  0 - Set mode to OFF")
        print("  1 - Set mode to CONTINUOUS_ON")
        print("  2 - Set mode to MOTION_DETECT")
        print()
        print("Utilities:")
        print("  r - Reset statistics")
        print("  q - Quit simulator")
        print()
        print("========================================\n")

    def print_status(self):
        print("\n========================================")
        print("System Status")
        print("========================================")
        print()

        mode_names = {
            OperatingMode.OFF: "OFF",
            OperatingMode.CONTINUOUS_ON: "CONTINUOUS_ON",
            OperatingMode.MOTION_DETECT: "MOTION_DETECT"
        }

        print(f"Operating Mode: {mode_names[self.mode]}")
        print()

        print("Hardware Status:")
        print(f"  PIR Sensor: {'READY' if self.mode != OperatingMode.OFF else 'IDLE'} (mock)")
        print(f"  Hazard LED: {'ON' if self.led_on or self.led_warning_active else 'OFF'} (mock)")
        print(f"  Status LED: {'BLINKING' if self.mode != OperatingMode.OFF else 'OFF'} (mock)")
        print()

        if self.led_warning_active:
            remaining = max(0, int(self.warning_end_time - time.time()))
            print(f"  ⚠️  WARNING ACTIVE: {remaining}s remaining")
        print()

        print(f"  Motion Events: {self.motion_events}")
        print()

        print(f"Mode Changes: {self.mode_changes}")
        print(f"Button Clicks: {self.button_clicks}")
        print()

        print("========================================\n")

    def cycle_mode(self):
        mode_sequence = [OperatingMode.OFF, OperatingMode.CONTINUOUS_ON, OperatingMode.MOTION_DETECT]
        current_index = mode_sequence.index(self.mode)
        next_index = (current_index + 1) % len(mode_sequence)
        self.mode = mode_sequence[next_index]
        self.mode_changes += 1

        mode_names = {
            OperatingMode.OFF: "OFF",
            OperatingMode.CONTINUOUS_ON: "CONTINUOUS_ON",
            OperatingMode.MOTION_DETECT: "MOTION_DETECT"
        }

        print(f"[StateMachine] Mode changed to: {mode_names[self.mode]}")

        # Update LED based on mode
        if self.mode == OperatingMode.CONTINUOUS_ON:
            self.led_on = True
            print("[HAL_LED] LED turned ON")
        else:
            self.led_on = False
            if not self.led_warning_active:
                print("[HAL_LED] LED turned OFF")

    def trigger_motion(self):
        print("[HAL_PIR] MOCK: Motion triggered")

        if self.mode == OperatingMode.MOTION_DETECT:
            self.motion_events += 1
            self.led_warning_active = True
            self.warning_end_time = time.time() + 15

            print("[StateMachine] Motion detected!")
            print("[StateMachine] Starting 15-second warning")
            print("[HAL_LED] Pattern started for 15000 ms")

    def simulate_button_press(self):
        print("[HAL_Button] MOCK: Simulating click")
        print("[HAL_Button] MOCK: Button pressed")
        print("[HAL_Button] Pressed")
        time.sleep(0.1)
        print("[HAL_Button] MOCK: Button released")
        print("[HAL_Button] Released")
        print("[HAL_Button] Click (count: 1)")

        self.button_clicks += 1
        self.cycle_mode()

    def set_mode(self, mode):
        if self.mode != mode:
            self.mode = mode
            self.mode_changes += 1

            mode_names = {
                OperatingMode.OFF: "OFF",
                OperatingMode.CONTINUOUS_ON: "CONTINUOUS_ON",
                OperatingMode.MOTION_DETECT: "MOTION_DETECT"
            }

            print(f"[StateMachine] Mode set to: {mode_names[self.mode]}")

            if self.mode == OperatingMode.CONTINUOUS_ON:
                self.led_on = True
                print("[HAL_LED] LED turned ON")
            else:
                self.led_on = False
                if not self.led_warning_active:
                    print("[HAL_LED] LED turned OFF")

    def reset_stats(self):
        self.motion_events = 0
        self.mode_changes = 0
        self.button_clicks = 0
        print("[StateMachine] Statistics reset")

    def update(self):
        # Check if warning period expired
        if self.led_warning_active and time.time() >= self.warning_end_time:
            self.led_warning_active = False
            print("[HAL_LED] Pattern stopped")
            if not self.led_on:
                print("[HAL_LED] LED turned OFF")

    def run(self):
        self.print_banner()
        self.print_help()

        print("Simulator ready. Type 'h' for help, 'q' to quit.")
        print("> ", end='', flush=True)

        try:
            while True:
                # Update simulation state
                self.update()

                # Non-blocking input check
                if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                    line = sys.stdin.readline().strip().lower()

                    if not line:
                        print("> ", end='', flush=True)
                        continue

                    cmd = line[0]

                    if cmd == 'q':
                        print("\nExiting simulator...")
                        break
                    elif cmd == 'h':
                        self.print_help()
                    elif cmd == 's':
                        self.print_status()
                    elif cmd == 'm':
                        self.trigger_motion()
                    elif cmd == 'b':
                        self.simulate_button_press()
                    elif cmd == '0':
                        self.set_mode(OperatingMode.OFF)
                    elif cmd == '1':
                        self.set_mode(OperatingMode.CONTINUOUS_ON)
                    elif cmd == '2':
                        self.set_mode(OperatingMode.MOTION_DETECT)
                    elif cmd == 'r':
                        self.reset_stats()
                    else:
                        print(f"Unknown command: {cmd}")

                    print("> ", end='', flush=True)

                time.sleep(0.1)

        except KeyboardInterrupt:
            print("\n\nInterrupted by user")
        except Exception as e:
            print(f"\nError: {e}")

if __name__ == "__main__":
    # For Windows, we need a different approach since select doesn't work on stdin
    import msvcrt

    class WindowsMockStepAware(MockStepAware):
        def run(self):
            self.print_banner()
            self.print_help()

            print("Simulator ready. Type 'h' for help, 'q' to quit.")

            try:
                while True:
                    self.update()

                    print("> ", end='', flush=True)
                    line = input().strip().lower()

                    if not line:
                        continue

                    cmd = line[0]

                    if cmd == 'q':
                        print("\nExiting simulator...")
                        break
                    elif cmd == 'h':
                        self.print_help()
                    elif cmd == 's':
                        self.print_status()
                    elif cmd == 'm':
                        self.trigger_motion()
                    elif cmd == 'b':
                        self.simulate_button_press()
                    elif cmd == '0':
                        self.set_mode(OperatingMode.OFF)
                    elif cmd == '1':
                        self.set_mode(OperatingMode.CONTINUOUS_ON)
                    elif cmd == '2':
                        self.set_mode(OperatingMode.MOTION_DETECT)
                    elif cmd == 'r':
                        self.reset_stats()
                    else:
                        print(f"Unknown command: {cmd}")

            except KeyboardInterrupt:
                print("\n\nInterrupted by user")
            except Exception as e:
                print(f"\nError: {e}")

    simulator = WindowsMockStepAware()
    simulator.run()
