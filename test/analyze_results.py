#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Analyze test results from SQLite database
"""

import sqlite3
import sys
import os
from pathlib import Path

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')


def analyze_test_database(db_path):
    """Analyze test results database"""
    if not Path(db_path).exists():
        print(f"❌ Database not found: {db_path}")
        return

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    print("\n╔════════════════════════════════════════╗")
    print("║                                        ║")
    print("║     Test Results Analysis              ║")
    print("║                                        ║")
    print("╚════════════════════════════════════════╝\n")

    # Overall statistics
    cursor.execute('''
        SELECT
            COUNT(*) as total_runs,
            SUM(total_tests) as total_tests_run,
            SUM(passed_tests) as total_passed,
            SUM(failed_tests) as total_failed,
            AVG(duration_seconds) as avg_duration,
            COUNT(CASE WHEN status = 'PASS' THEN 1 END) as successful_runs,
            COUNT(CASE WHEN status = 'FAIL' THEN 1 END) as failed_runs
        FROM test_runs
    ''')

    stats = cursor.fetchone()
    total_runs, total_tests, total_passed, total_failed, avg_duration, successful_runs, failed_runs = stats

    if total_runs == 0:
        print("📊 No test runs found in database.\n")
        conn.close()
        return

    print("="*60)
    print("Overall Statistics")
    print("="*60)
    print(f"Total Test Runs: {total_runs}")
    print(f"Successful Runs: {successful_runs} ({100*successful_runs//total_runs if total_runs > 0 else 0}%)")
    print(f"Failed Runs: {failed_runs} ({100*failed_runs//total_runs if total_runs > 0 else 0}%)")
    print(f"Total Tests Executed: {total_tests}")
    print(f"Total Passed: {total_passed}")
    print(f"Total Failed: {total_failed}")
    print(f"Average Duration: {avg_duration:.2f}s")
    print()

    # Recent test runs
    print("="*60)
    print("Recent Test Runs (Last 10)")
    print("="*60)
    cursor.execute('''
        SELECT id, timestamp, git_branch, total_tests, passed_tests,
               failed_tests, duration_seconds, status
        FROM test_runs
        ORDER BY id DESC
        LIMIT 10
    ''')

    runs = cursor.fetchall()

    if runs:
        print(f"{'ID':<6} {'Timestamp':<20} {'Branch':<15} {'Tests':<10} {'Duration':<10} {'Status'}")
        print("-"*80)
        for run_id, ts, branch, total, passed, failed, duration, status in runs:
            timestamp = ts.split('T')[0] + ' ' + ts.split('T')[1][:8]
            test_summary = f"{passed}/{total}"
            status_icon = "✓" if status == "PASS" else "✗"
            print(f"{run_id:<6} {timestamp:<20} {branch:<15} {test_summary:<10} {duration:<10.2f} {status_icon} {status}")
    print()

    # Test suite breakdown
    print("="*60)
    print("Test Suite Breakdown")
    print("="*60)
    cursor.execute('''
        SELECT
            test_suite,
            COUNT(*) as total_tests,
            COUNT(CASE WHEN status = 'PASS' THEN 1 END) as passed,
            COUNT(CASE WHEN status = 'FAIL' THEN 1 END) as failed
        FROM test_results
        GROUP BY test_suite
        ORDER BY test_suite
    ''')

    suites = cursor.fetchall()
    if suites:
        for suite, total, passed, failed in suites:
            pass_rate = 100 * passed // total if total > 0 else 0
            print(f"{suite}:")
            print(f"  Total: {total}, Passed: {passed}, Failed: {failed}, Pass Rate: {pass_rate}%")
    print()

    # Most failed tests
    print("="*60)
    print("Most Failed Tests")
    print("="*60)
    cursor.execute('''
        SELECT
            test_suite,
            test_name,
            COUNT(CASE WHEN status = 'FAIL' THEN 1 END) as fail_count,
            COUNT(*) as total_runs
        FROM test_results
        GROUP BY test_suite, test_name
        HAVING fail_count > 0
        ORDER BY fail_count DESC
        LIMIT 10
    ''')

    failed_tests = cursor.fetchall()
    if failed_tests:
        for suite, name, fail_count, total in failed_tests:
            fail_rate = 100 * fail_count // total if total > 0 else 0
            print(f"  {suite}::{name}")
            print(f"    Failed: {fail_count}/{total} ({fail_rate}%)")
    else:
        print("  🎉 No failing tests!")
    print()

    # Trend analysis
    print("="*60)
    print("Trend Analysis (Last 5 Runs)")
    print("="*60)
    cursor.execute('''
        SELECT id, timestamp, passed_tests, failed_tests, total_tests, status
        FROM test_runs
        ORDER BY id DESC
        LIMIT 5
    ''')

    trend_runs = cursor.fetchall()
    if len(trend_runs) > 1:
        print("Run ID | Pass Rate | Status | Trend")
        print("-"*50)

        prev_pass_rate = None
        for run_id, ts, passed, failed, total, status in reversed(trend_runs):
            pass_rate = 100 * passed // total if total > 0 else 0

            if prev_pass_rate is not None:
                if pass_rate > prev_pass_rate:
                    trend = "📈 Improving"
                elif pass_rate < prev_pass_rate:
                    trend = "📉 Declining"
                else:
                    trend = "➡️  Stable"
            else:
                trend = "—"

            status_icon = "✓" if status == "PASS" else "✗"
            print(f"{run_id:<6} | {pass_rate:>3}%      | {status_icon} {status:<4} | {trend}")
            prev_pass_rate = pass_rate
    print()

    # Test execution speed
    print("="*60)
    print("Test Execution Speed")
    print("="*60)
    cursor.execute('''
        SELECT
            AVG(duration_seconds) as avg_duration,
            MIN(duration_seconds) as min_duration,
            MAX(duration_seconds) as max_duration
        FROM test_runs
        WHERE duration_seconds > 0
    ''')

    duration_stats = cursor.fetchone()
    if duration_stats and duration_stats[0]:
        avg, min_dur, max_dur = duration_stats
        print(f"Average: {avg:.2f}s")
        print(f"Fastest: {min_dur:.2f}s")
        print(f"Slowest: {max_dur:.2f}s")
    print()

    # Latest test failures
    cursor.execute('''
        SELECT r.id, r.timestamp, t.test_suite, t.test_name, t.error_message
        FROM test_results t
        JOIN test_runs r ON t.run_id = r.id
        WHERE t.status = 'FAIL'
        ORDER BY r.id DESC
        LIMIT 5
    ''')

    recent_failures = cursor.fetchall()
    if recent_failures:
        print("="*60)
        print("Recent Test Failures")
        print("="*60)
        for run_id, ts, suite, name, error in recent_failures:
            print(f"Run #{run_id} - {suite}::{name}")
            if error:
                print(f"  Error: {error}")
            print()

    conn.close()

    print("="*60)
    print("Analysis complete!")
    print("="*60)
    print()


def main():
    script_dir = Path(__file__).parent
    db_path = script_dir / "reports" / "test_results.db"

    analyze_test_database(db_path)

    # Summary recommendation
    print("\n💡 Recommendations:")
    print("  • Run tests regularly: python3 test/run_tests.py")
    print("  • View HTML reports: test/reports/report_latest.html")
    print("  • Check trends to catch regressions early")
    print("  • Investigate any failing tests immediately")
    print()


if __name__ == '__main__':
    main()
