#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import time
import os
import shutil

from typing import Dict, Any, Optional
import argparse

from core.test_discovery import TestDiscovery
from core.test_environment import TestEnvironment
from core.test_execution import TestExecutor
from core.test_reporting import TestReporter
from helpers.cleanup_helper import TestCleanupHelper

# Optional import for video analysis (inline implementation)
try:
    import cv2
    def analyze_video(video_path):
        """Basic video analysis using OpenCV."""
        if not os.path.exists(video_path):
            return {"error": f"File {video_path} not found"}
        
        try:
            cap = cv2.VideoCapture(video_path)
            if not cap.isOpened():
                return {"error": f"Cannot open video file {video_path}"}
            
            frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            fps = cap.get(cv2.CAP_PROP_FPS)
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            duration = frame_count / fps if fps > 0 else 0
            
            cap.release()
            
            return {
                "width": width,
                "height": height,
                "fps": round(fps, 2),
                "frame_count": frame_count,
                "duration": round(duration, 2),
            }
        except Exception as e:
            return {"error": f"Error analyzing {video_path}: {str(e)}"}
    
    VIDEO_ANALYSIS_AVAILABLE = True
except ImportError:
    VIDEO_ANALYSIS_AVAILABLE = False
    def analyze_video(video_path):  # pylint: disable=unused-argument
        """Dummy function when video analysis is not available."""
        return {'error': 'Video analysis not available'}

sys.path.insert(0, str(Path(__file__).parent))

# Import test modules
class MainTestRunner:
    """
    Main test runner orchestrating all test execution.

    This class serves as the primary entry point for running all tests
    in the Mux Sweeper project.
    """

    def __init__(self, verbose: bool = False, quiet: bool = False):
        """Initialize the main test runner."""
        self.verbose = verbose
        self.quiet = quiet
        self.project_root = Path(__file__).parent.parent
        self.tests_dir = Path(__file__).parent

        # Initialize core components
        self.environment = TestEnvironment(self.project_root)
        self.discovery = TestDiscovery(self.tests_dir)
        self.executor = TestExecutor(verbose=verbose)
        self.reporter = TestReporter(verbose=verbose, quiet=quiet)
        self.cleanup = TestCleanupHelper(verbose=verbose)

        # Test results storage
        self.results: Dict[str, Any] = {
            'total_tests': 0,
            'passed': 0,
            'failed': 0,
            'skipped': 0,
            'errors': 0,
            'execution_time': 0.0,
            'test_results': []
        }

    def run_all_tests(self) -> bool:
        """Run the complete test suite."""
        self._log_info("Starting comprehensive test suite execution")

        success = True
        start_time = time.time()

        try:
            # Setup environment
            if not self._setup_environment():
                return False

            # Discover and run tests in order
            test_suites = [
                ('prerequisites', self.run_prerequisite_tests),
                ('unit', self.run_unit_tests),
                ('integration', self.run_integration_tests),
                ('gui', self.run_gui_tests),
                ('quality', self.run_quality_tests),
            ]

            for suite_name, suite_func in test_suites:
                self._log_info(f"Running {suite_name} tests...")
                if not suite_func():
                    success = False
                    if not self.verbose:  # Continue on failure unless in verbose mode
                        self._log_error(f"{suite_name} tests failed, continuing...")

            # Final reporting
            self.results['execution_time'] = time.time() - start_time
            self._generate_final_report()

        except KeyboardInterrupt:
            self._log_error("Test execution interrupted by user")
            success = False
        except (OSError, AttributeError, ImportError) as error:
            self._log_error(f"Unexpected error during test execution: {error}")
            success = False
        finally:
            self._cleanup_after_tests()

        return success

    def run_quick_tests(self) -> bool:
        """Run quick smoke tests for rapid feedback."""
        self._log_info("Running quick smoke tests")

        start_time = time.time()

        try:
            # Quick environment check
            if not self.environment.verify_executables():
                return False

            # Run essential tests only
            success = (
                self.run_prerequisite_tests() and
                self._run_basic_capture_test() and
                self._run_basic_audio_test()
            )

            self.results['execution_time'] = time.time() - start_time
            self._generate_quick_report()

            return success

        except (OSError, AttributeError, ImportError) as error:
            self._log_error(f"Quick test execution failed: {error}")
            return False
        finally:
            self._cleanup_after_tests()

    def run_pylint_checks(self) -> bool:
        """Run pylint code quality checks."""
        self._log_info("Running pylint code quality checks")

        try:
            python_files = list(self.tests_dir.glob("**/*.py"))
            if not python_files:
                self._log_error("No Python files found for pylint checking")
                return False

            overall_score = 0.0
            file_count = 0

            for py_file in python_files:
                if py_file.name.startswith('__'):
                    continue

                score = self._run_pylint_on_file(py_file)
                if score is not None:
                    overall_score += score
                    file_count += 1

            if file_count > 0:
                average_score = overall_score / file_count
                self._log_info(f"Average pylint score: {average_score:.2f}/10.0")

                if average_score >= 9.0:
                    self._log_info("✅ Excellent code quality (9.0+/10)")
                    return True
                if average_score >= 8.0:
                    self._log_info("✅ Good code quality (8.0+/10)")
                    return True

                self._log_error(f"❌ Code quality below threshold: {average_score:.2f}/10")
                return False

            self._log_error("No files were successfully checked by pylint")
            return False

        except (OSError, AttributeError, subprocess.SubprocessError) as error:
            self._log_error(f"Pylint execution failed: {error}")
            return False

    def run_with_coverage(self) -> bool:
        """Run tests with coverage reporting."""
        self._log_info("Running tests with coverage analysis")

        try:
            # Use pytest with coverage
            cmd = [
                sys.executable, '-m', 'pytest',
                '--cov=.',
                '--cov-report=html:coverage_html',
                '--cov-report=term-missing',
                str(self.tests_dir)
            ]

            result = subprocess.run(cmd, cwd=self.project_root, capture_output=True,
                                   text=True, check=False)

            if result.returncode == 0:
                self._log_info("✅ Coverage analysis completed successfully")
                coverage_dir = self.project_root / 'coverage_html'
                if coverage_dir.exists():
                    self._log_info(f"Coverage report available at: {coverage_dir / 'index.html'}")
                return True

            self._log_error("❌ Coverage analysis failed")
            self._log_error(result.stderr)
            return False

        except (subprocess.CalledProcessError, OSError, FileNotFoundError) as error:
            self._log_error(f"Coverage execution failed: {error}")
            return False

    def run_with_pytest(self) -> bool:
        """Run tests using pytest framework."""
        self._log_info("Running tests with pytest framework")

        try:
            # Ensure environment is set up
            if not self._setup_environment():
                return False

            # Use venv python if available
            python_cmd = str(self.environment.get_venv_python_executable())
            if not Path(python_cmd).exists():
                python_cmd = sys.executable

            # Run pytest
            cmd = [
                python_cmd, '-m', 'pytest',
                str(self.tests_dir / 'tests'),
                '--tb=short',
                '--verbose',
                '--color=yes'
            ]

            if self.verbose:
                cmd.append('-v')

            result = subprocess.run(cmd, cwd=self.project_root, check=False)

            if result.returncode == 0:
                self._log_info("✅ All pytest tests passed")
                return True

            self._log_error("❌ Some pytest tests failed")
            return False

        except (subprocess.SubprocessError, OSError) as error:
            self._log_error(f"Pytest execution failed: {error}")
            return False

    def run_pytest_specific_markers(self, markers: list) -> bool:
        """Run pytest tests with specific markers."""
        self._log_info(f"Running pytest tests with markers: {', '.join(markers)}")

        try:
            # Ensure environment is set up
            if not self._setup_environment():
                return False

            # Use venv python if available
            python_cmd = str(self.environment.get_venv_python_executable())
            if not Path(python_cmd).exists():
                python_cmd = sys.executable

            # Build marker expression
            marker_expr = ' or '.join(markers)

            # Run pytest with markers
            cmd = [
                python_cmd, '-m', 'pytest',
                str(self.tests_dir / 'tests'),
                '-m', marker_expr,
                '--tb=short',
                '--verbose',
                '--color=yes'
            ]

            if self.verbose:
                cmd.append('-v')

            result = subprocess.run(cmd, cwd=self.project_root, check=False)

            if result.returncode == 0:
                self._log_info(f"✅ All pytest tests with markers [{marker_expr}] passed")
                return True

            self._log_error(f"❌ Some pytest tests with markers [{marker_expr}] failed")
            return False

        except (subprocess.SubprocessError, OSError) as error:
            self._log_error(f"Pytest execution failed: {error}")
            return False

    def _setup_environment(self) -> bool:
        """Setup the test environment."""
        try:
            # Ensure virtual environment is ready
            if not self.environment.ensure_virtual_environment():
                self._log_error("Virtual environment setup failed")
                return False

            # Activate virtual environment for current process
            if not self.environment.activate_virtual_environment():
                self._log_error("Virtual environment activation failed")
                return False

            # Verify Python environment
            if not self.environment.verify_python_environment():
                return False

            # Verify project executables
            if not self.environment.verify_executables():
                return False

            # Setup output directories
            self.environment.setup_test_directories()

            # Pre-test cleanup
            self.cleanup.clean_outputs_directory()

            return True

        except (OSError, FileNotFoundError) as error:
            self._log_error(f"Environment setup failed: {error}")
            return False

    def run_prerequisite_tests(self) -> bool:
        """Run prerequisite validation tests."""
        # pylint: disable=import-outside-toplevel
        from tests.test_prerequisites import TestPrerequisites
        test_class = TestPrerequisites()
        return self.executor.run_test_class(test_class, "prerequisites")

    def run_unit_tests(self) -> bool:
        """Run unit tests."""
        # pylint: disable=import-outside-toplevel
        from tests.test_unit import TestUnit
        test_class = TestUnit()
        return self.executor.run_test_class(test_class, "unit")

    def run_integration_tests(self) -> bool:
        """Run integration tests (essential quality tests)."""
        # pylint: disable=import-outside-toplevel
        from tests.test_essential_quality import TestEssentialQuality
        test_class = TestEssentialQuality()
        return self.executor.run_test_class(test_class, "integration")

    def run_gui_tests(self) -> bool:
        """Run GUI-specific tests."""
        # pylint: disable=import-outside-toplevel
        from tests.test_gui_basic import TestGuiBasic
        test_class = TestGuiBasic()
        return self.executor.run_test_class(test_class, "gui")

    def run_quality_tests(self) -> bool:
        """Run essential quality tests."""
        # pylint: disable=import-outside-toplevel
        from tests.test_essential_quality import TestEssentialQuality
        test_class = TestEssentialQuality()
        return self.executor.run_test_class(test_class, "quality")

    def _run_basic_capture_test(self) -> bool:
        """Run a basic capture test for quick validation."""
        try:
            # pylint: disable=import-outside-toplevel
            from tests.test_unit import TestUnit
            test_unit = TestUnit()
            # Call setup_method if available
            if hasattr(test_unit, 'setup_method'):
                test_unit.setup_method()
            return test_unit.test_basic_screen_capture()
        except (ImportError, AttributeError) as error:
            self._log_error(f"Basic capture test failed: {error}")
            return False

    def _run_basic_audio_test(self) -> bool:
        """Run a basic audio test for quick validation."""
        try:
            # pylint: disable=import-outside-toplevel
            from tests.test_unit import TestUnit
            test_unit = TestUnit()
            # Call setup_method if available
            if hasattr(test_unit, 'setup_method'):
                test_unit.setup_method()
            return test_unit.test_microphone_capture_basic()
        except (ImportError, AttributeError) as error:
            self._log_error(f"Basic audio test failed: {error}")
            return False

    def _run_pylint_on_file(self, file_path: Path) -> Optional[float]:
        """Run pylint on a single file and return the score."""
        try:
            cmd = [sys.executable, '-m', 'pylint', str(file_path), '--score=yes']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60, check=False)

            # Extract score from output
            for line in result.stdout.split('\n'):
                if 'Your code has been rated at' in line:
                    try:
                        score_part = line.split('rated at ')[1].split('/')[0]
                        score = float(score_part)
                        self._log_debug(f"{file_path.name}: {score:.2f}/10.0")
                        return score
                    except (IndexError, ValueError):
                        continue

            # If no score found, check for errors
            if result.returncode != 0:
                self._log_error(f"Pylint failed for {file_path.name}: {result.stderr}")

            return None

        except (subprocess.TimeoutExpired, OSError) as error:
            self._log_error(f"Pylint error for {file_path.name}: {error}")
            return None

    def _cleanup_after_tests(self) -> None:
        """Cleanup after test execution."""
        try:
            if not self.verbose:  # Only cleanup if not in verbose mode (for debugging)
                self.cleanup.run_cleanup(clean_all=False)
        except (OSError, FileNotFoundError) as error:
            self._log_error(f"Cleanup failed: {error}")

    def _generate_final_report(self) -> None:
        """Generate final test report."""
        self.reporter.generate_summary_report(self.results)

    def _generate_quick_report(self) -> None:
        """Generate quick test report."""
        self.reporter.generate_quick_report(self.results)

    def _log_info(self, message: str) -> None:
        """Log info message."""
        if not self.quiet:
            print(f"[INFO] {message}")

    def _log_error(self, message: str) -> None:
        """Log error message."""
        print(f"[ERROR] {message}", file=sys.stderr)

    def _log_debug(self, message: str) -> None:
        """Log debug message."""
        if self.verbose:
            print(f"[DEBUG] {message}")

    def setup_virtual_environment(self) -> bool:
        """Setup virtual environment and dependencies."""
        self._log_info("Setting up virtual environment...")

        try:
            if not self.environment.ensure_virtual_environment():
                return False

            # Display venv information
            venv_python = self.environment.get_venv_python_executable()
            self._log_info(f"Virtual environment ready at: {self.environment.venv_dir}")
            self._log_info(f"Python executable: {venv_python}")

            # Show how to activate
            if sys.platform == "win32":
                activate_script = self.environment.venv_dir / 'Scripts' / 'activate.bat'
                self._log_info(f"To activate manually: {activate_script}")
            else:
                activate_script = self.environment.venv_dir / 'bin' / 'activate'
                self._log_info(f"To activate manually: source {activate_script}")

            return True

        except (OSError, ImportError) as error:
            self._log_error(f"Virtual environment setup failed: {error}")
            return False

    def check_virtual_environment(self) -> bool:
        """Check virtual environment status."""
        self._log_info("Checking virtual environment status...")

        try:
            if not self.environment.venv_dir.exists():
                self._log_info("❌ Virtual environment not found")
                self._log_info("Run with --setup-venv to create it")
                return False

            venv_python = self.environment.get_venv_python_executable()
            if not venv_python.exists():
                self._log_info("❌ Virtual environment corrupted (Python executable missing)")
                self._log_info("Run with --recreate-venv to fix it")
                return False

            # Test venv python
            result = subprocess.run([
                str(venv_python), '--version'
            ], capture_output=True, text=True, timeout=10, check=False)

            if result.returncode != 0:
                self._log_info("❌ Virtual environment corrupted (Python not working)")
                self._log_info("Run with --recreate-venv to fix it")
                return False

            self._log_info("✅ Virtual environment is healthy")
            self._log_info(f"Location: {self.environment.venv_dir}")
            self._log_info(f"Python: {result.stdout.strip()}")

            # Check if we're currently in the venv
            if self.environment.is_in_virtual_environment():
                current_venv = os.environ.get('VIRTUAL_ENV', '')
                if str(self.environment.venv_dir) in current_venv:
                    self._log_info("✅ Currently running in project virtual environment")
                else:
                    self._log_info("⚠️  Running in different virtual environment")
            else:
                self._log_info("⚠️  Not currently in virtual environment")

            # Check dependencies
            deps = self.environment.check_dependencies()
            missing_deps = [dep for dep, available in deps.items() if not available]

            if missing_deps:
                self._log_info(f"⚠️  Missing dependencies: {', '.join(missing_deps)}")
                self._log_info("Run with --setup-venv to install them")
            else:
                self._log_info("✅ All essential dependencies available")

            return True

        except (subprocess.SubprocessError, OSError, subprocess.TimeoutExpired) as error:
            self._log_error(f"Environment check failed: {error}")
            return False

    def recreate_virtual_environment(self) -> bool:
        """Recreate virtual environment from scratch."""
        self._log_info("Recreating virtual environment from scratch...")

        try:
            # Remove existing venv
            if self.environment.venv_dir.exists():
                self._log_info("Removing existing virtual environment...")
                shutil.rmtree(str(self.environment.venv_dir))

            # Create new venv and install dependencies
            if not self.environment.ensure_virtual_environment():
                return False

            self._log_info("✅ Virtual environment recreated successfully")
            return True

        except (OSError, ImportError) as error:
            self._log_error(f"Virtual environment recreation failed: {error}")
            return False

    def run_diagnostic_helpers(self) -> bool:
        """Run diagnostic helpers to validate test environment and outputs."""
        self._log_info("Running diagnostic helpers...")

        try:
            # Check if there are any test outputs to validate
            outputs_dir = self.tests_dir / 'outputs'
            if not outputs_dir.exists():
                self._log_info("No outputs directory found - creating it")
                outputs_dir.mkdir(parents=True, exist_ok=True)
                return True

            # Find all MP4 files in outputs
            mp4_files = list(outputs_dir.glob('*.mp4'))

            if not mp4_files:
                self._log_info("No MP4 files found in outputs directory")
                return True

            self._log_info(f"Found {len(mp4_files)} MP4 files to validate")

            # Validate each MP4 file exists and has reasonable size
            validation_results = []
            for mp4_file in mp4_files:
                try:
                    file_size = mp4_file.stat().st_size
                    is_valid = file_size > 1000  # At least 1KB
                    error_msg = None if is_valid else f"File too small: {file_size} bytes"
                    validation_results.append({
                        'file': mp4_file.name,
                        'valid': is_valid,
                        'error': error_msg
                    })

                    if is_valid:
                        self._log_info(f"✅ {mp4_file.name} - Valid MP4")
                        # Try video analysis if OpenCV is available
                        try:
                            analysis = analyze_video(str(mp4_file))
                            if 'error' not in analysis:
                                width = analysis.get('width', '?')
                                height = analysis.get('height', '?')
                                fps = analysis.get('fps', '?')
                                self._log_info(f"   � {width}x{height} @ {fps} fps")

                                frames = analysis.get('frame_count', '?')
                                duration = analysis.get('duration', '?')
                                self._log_info(f"   📊 {frames} frames, {duration}s")
                        except ImportError:
                            self._log_debug("OpenCV not available for video analysis")
                        except (OSError, ValueError) as e:
                            self._log_debug(f"Video analysis failed: {e}")
                    else:
                        self._log_error(f"❌ {mp4_file.name} - {error_msg}")

                except (OSError, ValueError) as e:
                    self._log_error(f"❌ {mp4_file.name} - Validation failed: {e}")
                    validation_results.append({
                        'file': mp4_file.name,
                        'valid': False,
                        'error': f"Validation error: {e}"
                    })

            # Summary
            valid_count = sum(1 for r in validation_results if r['valid'])
            total_count = len(validation_results)

            if valid_count == total_count:
                self._log_info(f"✅ All {total_count} MP4 files are valid")
                return True

            invalid_count = total_count - valid_count
            self._log_error(f"❌ {invalid_count}/{total_count} MP4 files are invalid")
            return False

        except (OSError, subprocess.SubprocessError) as error:
            self._log_error(f"Diagnostic helpers failed: {error}")
            return False

    def run_generate_audio(self, duration: int) -> bool:
        """Generate test audio for specified duration."""
        self._log_info(f"Generating test audio for {duration} seconds...")
        
        try:
            from helpers.generate_test_audio import generate_test_audio
            return generate_test_audio(duration)
        except ImportError as e:
            self._log_error(f"Could not import generate_test_audio: {e}")
            return False

    def run_moov_analysis(self, file_path: str) -> bool:
        """Analyze MOOV atom in specified MP4 file (functionality removed)."""
        self._log_info(f"MOOV analysis functionality has been removed for simplicity")
        self._log_info(f"Use external tools like ffprobe to analyze: {file_path}")
        return True

    def run_mp4_diagnostic(self, file_path: str) -> bool:
        """Run detailed MP4 metadata diagnostic (functionality removed)."""
        self._log_info(f"MP4 diagnostic functionality has been removed for simplicity")
        self._log_info(f"Use external tools like ffprobe to diagnose: {file_path}")
        return True


def main():
    """Main entry point for test execution."""
    parser = argparse.ArgumentParser(
        description="Mux Sweeper Test Suite - Main Entry Point",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python test_main_runner.py                 # Run all tests
  python test_main_runner.py --setup-venv    # Setup virtual environment
  python test_main_runner.py --check-venv    # Check venv status
  python test_main_runner.py --quick         # Quick smoke tests
  python test_main_runner.py --pylint        # Code quality checks
  python test_main_runner.py --coverage      # Tests with coverage
  python test_main_runner.py --unit          # Unit tests only
  python test_main_runner.py --integration   # Integration tests only
  python test_main_runner.py --generate-audio 5     # Generate 5 second test audio
  python test_main_runner.py --analyze-moov file.mp4 # Analyze MOOV atom
  python test_main_runner.py --mp4-diagnostic file.mp4 # Detailed MP4 analysis
        """
    )

    parser.add_argument('--pytest', action='store_true',
                       help='Run tests using pytest framework')
    parser.add_argument('--pytest-markers', type=str, nargs='+',
                       help='Run pytest tests with specific markers')
    parser.add_argument('--diagnostics', action='store_true',
                       help='Run diagnostic helpers on test outputs')
    parser.add_argument('--setup-venv', action='store_true',
                       help='Setup virtual environment and install dependencies')
    parser.add_argument('--check-venv', action='store_true',
                       help='Check virtual environment status')
    parser.add_argument('--recreate-venv', action='store_true',
                       help='Recreate virtual environment from scratch')
    parser.add_argument('--quick', action='store_true',
                       help='Run quick smoke tests only')
    parser.add_argument('--unit', action='store_true',
                       help='Run unit tests only')
    parser.add_argument('--integration', action='store_true',
                       help='Run integration tests only')
    parser.add_argument('--audio', action='store_true',
                       help='Run audio tests only')
    parser.add_argument('--gui', action='store_true',
                       help='Run GUI tests only')
    parser.add_argument('--performance', action='store_true',
                       help='Run performance tests only')
    parser.add_argument('--pylint', action='store_true',
                       help='Run pylint code quality checks')
    parser.add_argument('--coverage', action='store_true',
                       help='Run tests with coverage analysis')
    parser.add_argument('--generate-audio', type=int, metavar='SECONDS', default=None,
                       help='Generate test audio for specified duration (in seconds)')
    parser.add_argument('--analyze-moov', type=str, metavar='FILE',
                       help='Analyze MOOV atom in specified MP4 file')
    parser.add_argument('--mp4-diagnostic', type=str, metavar='FILE',
                       help='Run detailed MP4 metadata diagnostic on specified file')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Verbose output')
    parser.add_argument('--quiet', '-q', action='store_true',
                       help='Quiet output (errors only)')

    args = parser.parse_args()

    # Initialize runner
    runner = MainTestRunner(verbose=args.verbose, quiet=args.quiet)

    # Determine which tests to run
    success = True

    try:
        if args.setup_venv:
            success = runner.setup_virtual_environment()
        elif args.check_venv:
            success = runner.check_virtual_environment()
        elif args.recreate_venv:
            success = runner.recreate_virtual_environment()
        elif args.pytest:
            success = runner.run_with_pytest()
        elif args.pytest_markers:
            success = runner.run_pytest_specific_markers(args.pytest_markers)
        elif args.diagnostics:
            success = runner.run_diagnostic_helpers()
        elif args.pylint:
            success = runner.run_pylint_checks()
        elif args.coverage:
            success = runner.run_with_coverage()
        elif args.quick:
            success = runner.run_quick_tests()
        elif args.unit:
            success = runner.run_unit_tests()
        elif args.integration:
            success = runner.run_integration_tests()
        elif args.gui:
            success = runner.run_gui_tests()
        elif args.generate_audio is not None:
            success = runner.run_generate_audio(args.generate_audio)
        elif args.analyze_moov:
            success = runner.run_moov_analysis(args.analyze_moov)
        elif args.mp4_diagnostic:
            success = runner.run_mp4_diagnostic(args.mp4_diagnostic)
        else:
            # Run all tests by default
            success = runner.run_all_tests()

        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\n[INFO] Test execution interrupted by user")
        sys.exit(1)
    except (ImportError, OSError) as error:
        print(f"[ERROR] Unexpected error: {error}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
