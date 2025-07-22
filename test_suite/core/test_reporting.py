#!/usr/bin/env python3
"""
Test Reporting Module

Handles generation of test reports, summaries, and output formatting.
"""

from pathlib import Path
import time
from typing import Dict, List, Any
import json

class TestReporter:
    """Generates test reports and summaries."""

    def __init__(self, verbose: bool = False, quiet: bool = False):
        self.verbose = verbose
        self.quiet = quiet

    def generate_summary_report(self, results: Dict[str, Any]) -> None:
        """Generate a comprehensive summary report."""
        if self.quiet:
            return

        print("\n" + "="*80)
        print("Mux Sweeper TEST SUITE - SUMMARY REPORT")
        print("="*80)

        # Overall statistics
        total_tests = results.get('total_tests', 0)
        passed = results.get('passed', 0)
        failed = results.get('failed', 0)
        skipped = results.get('skipped', 0)
        errors = results.get('errors', 0)
        execution_time = results.get('execution_time', 0.0)

        print(f"\nExecution Time: {execution_time:.2f} seconds")
        print(f"Total Tests: {total_tests}")
        print(f"Passed: {passed} ✅")
        print(f"Failed: {failed} ❌")
        print(f"Skipped: {skipped} ⏭️")
        print(f"Errors: {errors} 💥")

        # Success rate
        if total_tests > 0:
            success_rate = (passed / total_tests) * 100
            print(f"Success Rate: {success_rate:.1f}%")

        # Verdict
        print("\n" + "-"*40)
        if failed == 0 and errors == 0:
            print("🎉 ALL TESTS PASSED!")
        elif failed > 0 or errors > 0:
            print("⚠️  SOME TESTS FAILED")

        print("-"*40)

        # Detailed results if available
        test_results = results.get('test_results', [])
        if test_results and (self.verbose or failed > 0 or errors > 0):
            self._print_detailed_results(test_results)

    def generate_quick_report(self, results: Dict[str, Any]) -> None:
        """Generate a quick summary for smoke tests."""
        if self.quiet:
            return

        total_tests = results.get('total_tests', 0)
        passed = results.get('passed', 0)
        failed = results.get('failed', 0)
        execution_time = results.get('execution_time', 0.0)

        print(f"\n🚀 Quick Test Summary: {passed}/{total_tests} passed in {execution_time:.2f}s")

        if failed == 0:
            print("✅ Quick tests passed! Ready for development.")
        else:
            print("❌ Quick tests failed. Check basic functionality.")

    def generate_junit_report(self, results: List[Dict[str, Any]], output_file: Path) -> None:
        """Generate JUnit XML report for CI/CD integration."""
        try:
            # Simple XML generation (could use xml.etree.ElementTree for more complex needs)
            xml_content = '<?xml version="1.0" encoding="UTF-8"?>\n'
            xml_content += f'<testsuite name="PixelCaptureTests" tests="{len(results)}" '

            # Calculate statistics for XML
            failed = sum(1 for r in results if r.get('status') == 'FAIL')
            errors = sum(1 for r in results if r.get('status') == 'ERROR')
            skipped = sum(1 for r in results if r.get('status') == 'SKIP')

            xml_content += f'failures="{failed}" errors="{errors}" skipped="{skipped}">\n'

            for result in results:
                test_name = result.get('test_name', 'unknown')
                status = result.get('status', 'UNKNOWN')
                execution_time = result.get('execution_time', 0.0)
                message = result.get('message', '')

                xml_content += f'  <testcase name="{test_name}" time="{execution_time:.3f}">\n'

                if status == 'FAIL':
                    xml_content += f'    <failure message="{message}"/>\n'
                elif status == 'ERROR':
                    xml_content += f'    <error message="{message}"/>\n'
                elif status == 'SKIP':
                    xml_content += f'    <skipped message="{message}"/>\n'

                xml_content += '  </testcase>\n'

            xml_content += '</testsuite>\n'

            output_file.parent.mkdir(parents=True, exist_ok=True)
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(xml_content)

            print(f"[INFO] JUnit report generated: {output_file}")

        except (OSError, PermissionError) as error:
            print(f"[ERROR] Failed to generate JUnit report: {error}")

    def generate_json_report(self, results: Dict[str, Any], output_file: Path) -> None:
        """Generate JSON report for programmatic consumption."""
        try:
            # Add metadata
            report_data = {
                'generated_at': time.strftime('%Y-%m-%d %H:%M:%S'),
                'summary': {
                    'total_tests': results.get('total_tests', 0),
                    'passed': results.get('passed', 0),
                    'failed': results.get('failed', 0),
                    'skipped': results.get('skipped', 0),
                    'errors': results.get('errors', 0),
                    'execution_time': results.get('execution_time', 0.0)
                },
                'test_results': results.get('test_results', [])
            }

            output_file.parent.mkdir(parents=True, exist_ok=True)
            with open(output_file, 'w', encoding='utf-8') as f:
                json.dump(report_data, f, indent=2, ensure_ascii=False)

            print(f"[INFO] JSON report generated: {output_file}")

        except (OSError, PermissionError, TypeError) as error:
            print(f"[ERROR] Failed to generate JSON report: {error}")

    def generate_markdown_report(self, results: Dict[str, Any], output_file: Path) -> None:
        """Generate Markdown report for documentation."""
        try:
            total_tests = results.get('total_tests', 0)
            passed = results.get('passed', 0)
            failed = results.get('failed', 0)
            skipped = results.get('skipped', 0)
            errors = results.get('errors', 0)
            execution_time = results.get('execution_time', 0.0)

            md_content = "# Mux Sweeper Test Report\n\n"
            md_content += f"**Generated:** {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n"

            # Summary table
            md_content += "## Summary\n\n"
            md_content += "| Metric | Value |\n"
            md_content += "|--------|-------|\n"
            md_content += f"| Total Tests | {total_tests} |\n"
            md_content += f"| Passed | {passed} ✅ |\n"
            md_content += f"| Failed | {failed} ❌ |\n"
            md_content += f"| Skipped | {skipped} ⏭️ |\n"
            md_content += f"| Errors | {errors} 💥 |\n"
            md_content += f"| Execution Time | {execution_time:.2f}s |\n"

            if total_tests > 0:
                success_rate = (passed / total_tests) * 100
                md_content += f"| Success Rate | {success_rate:.1f}% |\n"

            # Test results
            test_results = results.get('test_results', [])
            if test_results:
                md_content += "\n## Test Results\n\n"
                md_content += "| Test Name | Status | Time | Message |\n"
                md_content += "|-----------|--------|------|--------|\n"

                for result in test_results:
                    test_name = result.get('test_name', 'unknown')
                    status = result.get('status', 'UNKNOWN')
                    execution_time = result.get('execution_time', 0.0)
                    message = result.get('message', '')

                    status_symbol = {
                        'PASS': '✅',
                        'FAIL': '❌',
                        'SKIP': '⏭️',
                        'ERROR': '💥'
                    }.get(status, '❓')

                    md_content += f"| {test_name} | {status_symbol} {status} | {execution_time:.3f}s | {message} |\n"

            output_file.parent.mkdir(parents=True, exist_ok=True)
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(md_content)

            print(f"[INFO] Markdown report generated: {output_file}")

        except (OSError, PermissionError) as error:
            print(f"[ERROR] Failed to generate Markdown report: {error}")

    def _print_detailed_results(self, test_results: List[Dict[str, Any]]) -> None:
        """Print detailed test results."""
        print("\nDetailed Results:")
        print("-" * 60)

        for result in test_results:
            test_name = result.get('test_name', 'unknown')
            status = result.get('status', 'UNKNOWN')
            execution_time = result.get('execution_time', 0.0)
            message = result.get('message', '')

            status_symbol = {
                'PASS': '✅',
                'FAIL': '❌',
                'SKIP': '⏭️',
                'ERROR': '💥'
            }.get(status, '❓')

            print(f"{status_symbol} {test_name} ({execution_time:.3f}s)")
            if message:
                print(f"    {message}")

            # Show error details for failures if verbose
            if self.verbose and status in ['FAIL', 'ERROR']:
                error_details = result.get('error_details', '')
                if error_details:
                    print(f"    Error Details:\n{error_details}")

            print()

    def print_pylint_summary(self, file_scores: List[Dict[str, Any]]) -> None:
        """Print summary of pylint scores."""
        if self.quiet:
            return

        print("\n" + "="*60)
        print("PYLINT CODE QUALITY REPORT")
        print("="*60)

        if not file_scores:
            print("No files were checked.")
            return

        total_score = sum(score.get('score', 0) for score in file_scores)
        average_score = total_score / len(file_scores)

        print(f"\nFiles checked: {len(file_scores)}")
        print(f"Average score: {average_score:.2f}/10.0")

        # Categorize scores
        excellent = sum(1 for s in file_scores if s.get('score', 0) >= 9.0)
        good = sum(1 for s in file_scores if 8.0 <= s.get('score', 0) < 9.0)
        acceptable = sum(1 for s in file_scores if 7.0 <= s.get('score', 0) < 8.0)
        poor = sum(1 for s in file_scores if s.get('score', 0) < 7.0)

        print("\nScore distribution:")
        print(f"  Excellent (9.0+): {excellent}")
        print(f"  Good (8.0-8.9):  {good}")
        print(f"  Acceptable (7.0-7.9): {acceptable}")
        print(f"  Poor (<7.0):     {poor}")

        if self.verbose:
            print("\nDetailed scores:")
            for score_info in sorted(file_scores, key=lambda x: x.get('score', 0), reverse=True):
                file_name = score_info.get('file', 'unknown')
                score = score_info.get('score', 0)
                print(f"  {file_name}: {score:.2f}/10.0")

        print("\n" + "="*60)
