#!/usr/bin/env python3
"""
Test Execution Module

Handles execution of individual tests and test suites with proper error handling
and result tracking.
"""

import sys
import time

from typing import Any, Dict, List, Callable
import traceback

class TestResult:
    """Represents the result of a single test execution."""

    def __init__(self, test_name: str):
        self.test_name = test_name
        self.status = 'UNKNOWN'  # PASS, FAIL, SKIP, ERROR
        self.execution_time = 0.0
        self.message = ''
        self.error_details = ''
        self.start_time = 0.0
        self.end_time = 0.0

    def start(self) -> None:
        """Mark test as started."""
        self.start_time = time.time()

    def finish(self, status: str, message: str = '', error_details: str = '') -> None:
        """Mark test as finished with given status."""
        self.end_time = time.time()
        self.execution_time = self.end_time - self.start_time
        self.status = status
        self.message = message
        self.error_details = error_details

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for reporting."""
        return {
            'test_name': self.test_name,
            'status': self.status,
            'execution_time': self.execution_time,
            'message': self.message,
            'error_details': self.error_details
        }


class TestExecutor:
    """Executes tests and manages test execution lifecycle."""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.results: List[TestResult] = []
        self.current_suite = ''

    def run_test_method(self, test_instance: Any, method_name: str) -> TestResult:
        """Run a single test method."""
        test_result = TestResult(f"{test_instance.__class__.__name__}.{method_name}")

        try:
            if not hasattr(test_instance, method_name):
                test_result.finish('ERROR', f"Method {method_name} not found")
                return test_result

            method = getattr(test_instance, method_name)
            if not callable(method):
                test_result.finish('ERROR', f"{method_name} is not callable")
                return test_result

            self._log_debug(f"Starting {test_result.test_name}")
            test_result.start()

            # Run the test method
            result = method()

            # Check result
            if result is False:
                test_result.finish('FAIL', 'Test method returned False')
            elif result is None or result is True:
                test_result.finish('PASS', 'Test completed successfully')
            else:
                test_result.finish('PASS', f'Test completed with result: {result}')

        except AssertionError as error:
            test_result.finish('FAIL', f'Assertion failed: {str(error)}')
        except NotImplementedError:
            test_result.finish('SKIP', 'Test not implemented')
        except (AttributeError, TypeError, ValueError, RuntimeError, OSError) as error:
            error_details = traceback.format_exc()
            test_result.finish('ERROR', f'Unexpected error: {str(error)}', error_details)

        self._log_test_result(test_result)
        self.results.append(test_result)
        return test_result

    def run_test_class(self, test_class_instance: Any, suite_name: str = '') -> bool:
        """Run all test methods in a test class."""
        self.current_suite = suite_name
        class_name = test_class_instance.__class__.__name__

        self._log_info(f"Running test class: {class_name}")

        # Find all test methods
        test_methods = [method for method in dir(test_class_instance)
                       if method.startswith('test_') and callable(getattr(test_class_instance, method))]

        if not test_methods:
            self._log_warning(f"No test methods found in {class_name}")
            return True

        # Run setup if available
        if hasattr(test_class_instance, 'setup_class') and callable(test_class_instance.setup_class):
            try:
                test_class_instance.setup_class()
                self._log_debug("Setup completed")
            except (AttributeError, TypeError, ValueError, RuntimeError, OSError) as error:
                self._log_error(f"Setup failed: {error}")
                return False

        # Run each test method
        all_passed = True
        for method_name in sorted(test_methods):
            # Run setup_method if available
            if hasattr(test_class_instance, 'setup_method') and callable(test_class_instance.setup_method):
                try:
                    test_class_instance.setup_method()
                except (AttributeError, TypeError, ValueError, RuntimeError, OSError) as error:
                    self._log_error(f"setup_method failed for {method_name}: {error}")
                    all_passed = False
                    continue

            result = self.run_test_method(test_class_instance, method_name)
            if result.status in ['FAIL', 'ERROR']:
                all_passed = False

        # Run teardown if available
        if hasattr(test_class_instance, 'teardown_class') and callable(test_class_instance.teardown_class):
            try:
                test_class_instance.teardown_class()
                self._log_debug("Teardown completed")
            except (AttributeError, TypeError, ValueError, RuntimeError, OSError) as error:
                self._log_warning(f"Teardown failed: {error}")

        self._log_info(f"Completed test class: {class_name}")
        return all_passed

    def run_test_function(self, test_function: Callable, test_name: str) -> TestResult:
        """Run a standalone test function."""
        test_result = TestResult(test_name)

        try:
            self._log_debug(f"Starting {test_name}")
            test_result.start()

            # Run the test function
            result = test_function()

            # Check result
            if result is False:
                test_result.finish('FAIL', 'Test function returned False')
            elif result is None or result is True:
                test_result.finish('PASS', 'Test completed successfully')
            else:
                test_result.finish('PASS', f'Test completed with result: {result}')

        except AssertionError as error:
            test_result.finish('FAIL', f'Assertion failed: {str(error)}')
        except NotImplementedError:
            test_result.finish('SKIP', 'Test not implemented')
        except (AttributeError, TypeError, ValueError, RuntimeError, OSError) as error:
            error_details = traceback.format_exc()
            test_result.finish('ERROR', f'Unexpected error: {str(error)}', error_details)

        self._log_test_result(test_result)
        self.results.append(test_result)
        return test_result

    def get_summary_stats(self) -> Dict[str, int]:
        """Get summary statistics of test execution."""
        stats = {
            'total': len(self.results),
            'passed': 0,
            'failed': 0,
            'skipped': 0,
            'errors': 0
        }

        for result in self.results:
            if result.status == 'PASS':
                stats['passed'] += 1
            elif result.status == 'FAIL':
                stats['failed'] += 1
            elif result.status == 'SKIP':
                stats['skipped'] += 1
            elif result.status == 'ERROR':
                stats['errors'] += 1

        return stats

    def clear_results(self) -> None:
        """Clear previous test results."""
        self.results.clear()

    def _log_test_result(self, result: TestResult) -> None:
        """Log the result of a test."""
        status_symbols = {
            'PASS': '✅',
            'FAIL': '❌',
            'SKIP': '⏭️',
            'ERROR': '💥'
        }

        symbol = status_symbols.get(result.status, '❓')
        timing = f"({result.execution_time:.3f}s)"

        message = f"{symbol} {result.test_name} {timing}"
        if result.message:
            message += f" - {result.message}"

        if result.status in ['PASS', 'SKIP']:
            self._log_info(message)
        else:
            self._log_error(message)
            if result.error_details and self.verbose:
                self._log_debug(f"Error details:\n{result.error_details}")

    def _log_info(self, message: str) -> None:
        """Log info message."""
        print(f"[INFO] {message}")

    def _log_error(self, message: str) -> None:
        """Log error message."""
        print(f"[ERROR] {message}", file=sys.stderr)

    def _log_warning(self, message: str) -> None:
        """Log warning message."""
        print(f"[WARNING] {message}")

    def _log_debug(self, message: str) -> None:
        """Log debug message."""
        if self.verbose:
            print(f"[DEBUG] {message}")
