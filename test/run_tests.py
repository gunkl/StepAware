#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
StepAware Test Runner
Runs all tests, records results in SQLite database, and generates HTML reports.
"""

import subprocess
import sys
import os
import json
import sqlite3
import datetime
from pathlib import Path
import argparse

# Fix Windows console encoding
if sys.platform == 'win32':
    os.system('chcp 65001 >nul 2>&1')
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

class TestRunner:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.db_path = self.project_root / "test" / "test_results.db"
        self.test_output = []
        self.init_database()

    def init_database(self):
        """Initialize SQLite database for test history"""
        self.db_path.parent.mkdir(parents=True, exist_ok=True)

        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        # Create test_runs table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS test_runs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                git_commit TEXT,
                git_branch TEXT,
                total_tests INTEGER,
                passed_tests INTEGER,
                failed_tests INTEGER,
                duration_seconds REAL,
                environment TEXT,
                status TEXT
            )
        ''')

        # Create test_results table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS test_results (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id INTEGER,
                test_suite TEXT,
                test_name TEXT,
                status TEXT,
                duration_ms INTEGER,
                error_message TEXT,
                FOREIGN KEY (run_id) REFERENCES test_runs(id)
            )
        ''')

        conn.commit()
        conn.close()

    def get_git_info(self):
        """Get current git commit and branch"""
        try:
            commit = subprocess.check_output(
                ['git', 'rev-parse', 'HEAD'],
                cwd=self.project_root,
                stderr=subprocess.DEVNULL
            ).decode().strip()
        except:
            commit = 'unknown'

        try:
            branch = subprocess.check_output(
                ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
                cwd=self.project_root,
                stderr=subprocess.DEVNULL
            ).decode().strip()
        except:
            branch = 'unknown'

        return commit, branch

    def run_platformio_tests(self, environment='native'):
        """Run PlatformIO tests and capture output"""
        print(f"\n{'='*60}")
        print(f"Running PlatformIO Tests ({environment} environment)")
        print(f"{'='*60}\n")

        start_time = datetime.datetime.now()

        try:
            # Run tests with verbose output
            result = subprocess.run(
                ['python3', '-m', 'platformio', 'test', '-e', environment, '-v'],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout
            )

            duration = (datetime.datetime.now() - start_time).total_seconds()

            output = result.stdout + result.stderr
            self.test_output.append(output)

            print(output)

            # Parse test results from output
            test_results = self.parse_test_output(output, environment)

            return {
                'success': result.returncode == 0,
                'duration': duration,
                'results': test_results,
                'output': output
            }

        except subprocess.TimeoutExpired:
            print("ERROR: Tests timed out after 5 minutes")
            return {
                'success': False,
                'duration': 300,
                'results': [],
                'output': 'Test execution timeout'
            }
        except Exception as e:
            print(f"ERROR: Failed to run tests: {e}")
            return {
                'success': False,
                'duration': 0,
                'results': [],
                'output': str(e)
            }

    def parse_test_output(self, output, environment):
        """Parse Unity test output to extract individual test results"""
        results = []
        current_suite = None

        for line in output.split('\n'):
            # Detect test suite
            if 'Testing' in line and environment in line:
                parts = line.split('test_')
                if len(parts) > 1:
                    current_suite = 'test_' + parts[1].split()[0].strip('>')

            # Parse Unity test results
            if line.startswith('test_'):
                parts = line.split(':')
                if len(parts) >= 2:
                    test_name = parts[0].strip()
                    status = 'PASS' if 'PASS' in line else 'FAIL'

                    error_msg = None
                    if status == 'FAIL' and len(parts) > 2:
                        error_msg = ':'.join(parts[2:]).strip()

                    results.append({
                        'suite': current_suite or 'unknown',
                        'name': test_name,
                        'status': status,
                        'error': error_msg
                    })

        return results

    def save_results(self, test_data):
        """Save test results to SQLite database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        timestamp = datetime.datetime.now().isoformat()
        commit, branch = self.get_git_info()

        total_tests = len(test_data['results'])
        passed_tests = sum(1 for r in test_data['results'] if r['status'] == 'PASS')
        failed_tests = total_tests - passed_tests
        status = 'PASS' if test_data['success'] else 'FAIL'

        # Insert test run
        cursor.execute('''
            INSERT INTO test_runs
            (timestamp, git_commit, git_branch, total_tests, passed_tests,
             failed_tests, duration_seconds, environment, status)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (timestamp, commit, branch, total_tests, passed_tests, failed_tests,
              test_data['duration'], 'native', status))

        run_id = cursor.lastrowid

        # Insert individual test results
        for result in test_data['results']:
            cursor.execute('''
                INSERT INTO test_results
                (run_id, test_suite, test_name, status, error_message)
                VALUES (?, ?, ?, ?, ?)
            ''', (run_id, result['suite'], result['name'],
                  result['status'], result.get('error')))

        conn.commit()
        conn.close()

        return run_id

    def generate_html_report(self, run_id):
        """Generate HTML report for test run"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        # Get run info
        cursor.execute('''
            SELECT timestamp, git_commit, git_branch, total_tests,
                   passed_tests, failed_tests, duration_seconds, status
            FROM test_runs WHERE id = ?
        ''', (run_id,))

        run = cursor.fetchone()
        if not run:
            print(f"ERROR: Test run {run_id} not found")
            return

        timestamp, commit, branch, total, passed, failed, duration, status = run

        # Get test results
        cursor.execute('''
            SELECT test_suite, test_name, status, error_message
            FROM test_results WHERE run_id = ?
            ORDER BY test_suite, test_name
        ''', (run_id,))

        results = cursor.fetchall()

        # Generate HTML
        html = f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>StepAware Test Report - Run #{run_id}</title>
    <style>
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background-color: #f5f5f5;
        }}
        .header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            border-radius: 10px;
            margin-bottom: 30px;
        }}
        .header h1 {{
            margin: 0 0 10px 0;
        }}
        .status-badge {{
            display: inline-block;
            padding: 5px 15px;
            border-radius: 20px;
            font-weight: bold;
            font-size: 14px;
        }}
        .status-pass {{
            background-color: #10b981;
            color: white;
        }}
        .status-fail {{
            background-color: #ef4444;
            color: white;
        }}
        .stats {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }}
        .stat-card {{
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }}
        .stat-value {{
            font-size: 36px;
            font-weight: bold;
            margin: 10px 0;
        }}
        .stat-label {{
            color: #666;
            font-size: 14px;
        }}
        .test-results {{
            background: white;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            overflow: hidden;
        }}
        .test-suite {{
            background: #f8f9fa;
            padding: 15px 20px;
            font-weight: bold;
            border-bottom: 2px solid #dee2e6;
        }}
        .test-item {{
            padding: 15px 20px;
            border-bottom: 1px solid #f0f0f0;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }}
        .test-item:hover {{
            background-color: #f8f9fa;
        }}
        .test-name {{
            flex: 1;
        }}
        .test-status {{
            padding: 4px 12px;
            border-radius: 4px;
            font-size: 12px;
            font-weight: bold;
        }}
        .pass {{
            background-color: #d1fae5;
            color: #065f46;
        }}
        .fail {{
            background-color: #fee2e2;
            color: #991b1b;
        }}
        .error-message {{
            margin-top: 10px;
            padding: 10px;
            background-color: #fef2f2;
            border-left: 4px solid #ef4444;
            font-family: monospace;
            font-size: 12px;
            color: #991b1b;
        }}
        .metadata {{
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            margin-bottom: 20px;
        }}
        .metadata-item {{
            display: flex;
            padding: 8px 0;
            border-bottom: 1px solid #f0f0f0;
        }}
        .metadata-label {{
            font-weight: bold;
            width: 150px;
            color: #666;
        }}
        .metadata-value {{
            font-family: monospace;
            color: #333;
        }}
    </style>
</head>
<body>
    <div class="header">
        <h1>StepAware Test Report</h1>
        <span class="status-badge status-{status.lower()}">{status}</span>
        <p style="margin: 10px 0 0 0; opacity: 0.9;">Run #{run_id} • {timestamp}</p>
    </div>

    <div class="stats">
        <div class="stat-card">
            <div class="stat-label">Total Tests</div>
            <div class="stat-value">{total}</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Passed</div>
            <div class="stat-value" style="color: #10b981;">{passed}</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Failed</div>
            <div class="stat-value" style="color: #ef4444;">{failed}</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Duration</div>
            <div class="stat-value" style="font-size: 24px;">{duration:.2f}s</div>
        </div>
    </div>

    <div class="metadata">
        <h2 style="margin-top: 0;">Build Information</h2>
        <div class="metadata-item">
            <div class="metadata-label">Git Branch:</div>
            <div class="metadata-value">{branch}</div>
        </div>
        <div class="metadata-item">
            <div class="metadata-label">Git Commit:</div>
            <div class="metadata-value">{commit[:12]}</div>
        </div>
        <div class="metadata-item">
            <div class="metadata-label">Environment:</div>
            <div class="metadata-value">native (PC simulation)</div>
        </div>
    </div>

    <div class="test-results">
        <h2 style="margin: 0; padding: 20px; background: #f8f9fa;">Test Results</h2>
'''

        # Group results by suite
        current_suite = None
        for suite, name, test_status, error in results:
            if suite != current_suite:
                if current_suite is not None:
                    html += '</div>'
                html += f'<div class="test-suite">{suite}</div><div>'
                current_suite = suite

            status_class = 'pass' if test_status == 'PASS' else 'fail'
            html += f'''
        <div class="test-item">
            <div class="test-name">{name}</div>
            <div class="test-status {status_class}">{test_status}</div>
        </div>
'''
            if error:
                html += f'<div class="error-message">{error}</div>'

        html += '''
        </div>
    </div>
</body>
</html>
'''

        # Save report
        report_path = self.project_root / "test" / f"report_{run_id}.html"
        report_path.write_text(html, encoding='utf-8')

        # Create latest.html symlink/copy
        latest_path = self.project_root / "test" / "report_latest.html"
        latest_path.write_text(html, encoding='utf-8')

        conn.close()

        print(f"\n✓ HTML report generated: {report_path}")
        print(f"✓ Latest report: {latest_path}")

        return report_path

    def list_test_history(self, limit=10):
        """List recent test runs"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cursor.execute('''
            SELECT id, timestamp, git_branch, total_tests, passed_tests,
                   failed_tests, status
            FROM test_runs
            ORDER BY id DESC
            LIMIT ?
        ''', (limit,))

        runs = cursor.fetchall()
        conn.close()

        if not runs:
            print("No test history found.")
            return

        print(f"\n{'='*80}")
        print(f"Test History (last {limit} runs)")
        print(f"{'='*80}")
        print(f"{'ID':<6} {'Timestamp':<20} {'Branch':<15} {'Tests':<8} {'Status':<8}")
        print(f"{'-'*80}")

        for run_id, ts, branch, total, passed, failed, status in runs:
            timestamp = ts.split('T')[0] + ' ' + ts.split('T')[1][:8]
            test_summary = f"{passed}/{total}"
            print(f"{run_id:<6} {timestamp:<20} {branch:<15} {test_summary:<8} {status:<8}")

def main():
    parser = argparse.ArgumentParser(description='StepAware Test Runner')
    parser.add_argument('--history', action='store_true', help='Show test history')
    parser.add_argument('--report', type=int, metavar='RUN_ID',
                       help='Regenerate report for specific run')
    parser.add_argument('--limit', type=int, default=10,
                       help='Number of history entries to show')

    args = parser.parse_args()

    # Find project root
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    runner = TestRunner(project_root)

    if args.history:
        runner.list_test_history(args.limit)
        return 0

    if args.report:
        runner.generate_html_report(args.report)
        return 0

    # Run tests
    print("╔════════════════════════════════════════╗")
    print("║                                        ║")
    print("║      StepAware Test Suite Runner      ║")
    print("║                                        ║")
    print("╚════════════════════════════════════════╝\n")

    test_data = runner.run_platformio_tests('native')

    if test_data['results']:
        run_id = runner.save_results(test_data)
        runner.generate_html_report(run_id)

        print(f"\n{'='*60}")
        print(f"Test Summary")
        print(f"{'='*60}")
        print(f"Total Tests: {len(test_data['results'])}")
        passed = sum(1 for r in test_data['results'] if r['status'] == 'PASS')
        failed = len(test_data['results']) - passed
        print(f"Passed: {passed}")
        print(f"Failed: {failed}")
        print(f"Duration: {test_data['duration']:.2f}s")
        print(f"Status: {'✓ PASS' if test_data['success'] else '✗ FAIL'}")
        print(f"{'='*60}\n")

        return 0 if test_data['success'] else 1
    else:
        print("\nNo test results found. Check test output above.")
        return 1

if __name__ == '__main__':
    sys.exit(main())
