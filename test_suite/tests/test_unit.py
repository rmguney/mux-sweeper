#!/usr/bin/env python3
"""
Unit Tests

Basic functional unit tests for core muxsw functionality.
"""

from pathlib import Path
import subprocess
import time


class TestUnit:
    """Unit tests for core functionality."""

    def setup_method(self):
        """Setup for each test method."""
        self.project_root = Path(__file__).parent.parent.parent
        self.release_dir = self.project_root / 'release'
        self.muxsw_exe = self.release_dir / 'muxsw.exe'
        self.outputs_dir = self.project_root / 'test_suite' / 'outputs'
        self.outputs_dir.mkdir(parents=True, exist_ok=True)

    def test_help_command_displays_usage(self):
        """Test --help command displays proper usage information."""
        try:
            result = subprocess.run([str(self.muxsw_exe), '--help'],
                                   capture_output=True, text=True, timeout=10, check=False)

            if result.returncode != 0:
                print(f"[ERROR] Help command failed with exit code {result.returncode}")
                return False

            output = result.stdout.lower()
            required_terms = ['usage', 'options']

            missing_terms = [term for term in required_terms if term not in output]
            if missing_terms:
                print(f"[ERROR] Help output missing: {', '.join(missing_terms)}")
                return False

            print("[PASS] Help command shows proper usage")
            return True

        except subprocess.TimeoutExpired:
            print("[ERROR] Help command timed out")
            return False
        except (subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Help command failed: {error}")
            return False

    def test_invalid_arguments_handling(self):
        """Test handling of invalid command line arguments."""
        invalid_args = [
            ['--invalid-option'],
            ['--output'],
            ['--fps', 'invalid'],
            ['--time', '-5']
        ]

        for args in invalid_args:
            try:
                result = subprocess.run([str(self.muxsw_exe)] + args,
                                       capture_output=True, text=True, timeout=5, check=False)

                if result.returncode == 0:
                    print(f"[ERROR] Invalid args should fail: {args}")
                    return False

            except subprocess.TimeoutExpired:
                print(f"[ERROR] Invalid args test timed out: {args}")
                return False
            except (subprocess.SubprocessError, OSError) as error:
                print(f"[ERROR] Invalid args test failed: {error}")
                return False

        print("[PASS] Invalid arguments properly rejected")
        return True

    def test_basic_screen_capture(self):
        """Test basic screen capture functionality."""
        output_file = self.outputs_dir / 'test_unit_screen.mp4'

        try:
            if output_file.exists():
                output_file.unlink()

            cmd = [str(self.muxsw_exe), '--video', '--out', str(output_file), '--time', '2']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=15, check=False)

            if result.returncode != 0:
                print(f"[ERROR] Screen capture failed: exit code {result.returncode}")
                print(f"[ERROR] stderr: {result.stderr}")
                return False

            if not output_file.exists():
                print("[ERROR] Screen capture output file not created")
                return False

            file_size = output_file.stat().st_size
            if file_size < 1000:
                print(f"[ERROR] Screen capture file too small: {file_size} bytes")
                return False

            print(f"[PASS] Screen capture successful ({file_size} bytes)")
            return True

        except subprocess.TimeoutExpired:
            print("[ERROR] Screen capture timed out")
            return False
        except (subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Screen capture failed: {error}")
            return False

    def test_different_frame_rates(self):
        """Test different frame rates."""
        frame_rates = [15, 30, 60]

        for fps in frame_rates:
            output_file = self.outputs_dir / f'test_unit_fps_{fps}.mp4'

            try:
                if output_file.exists():
                    output_file.unlink()

                cmd = [str(self.muxsw_exe), '--video', '--fps', str(fps),
                       '--out', str(output_file), '--time', '1']
                result = subprocess.run(cmd,
                    capture_output=True,
                    text=True, timeout=10, check=False)

                if result.returncode != 0:
                    print(f"[ERROR] FPS {fps} test failed: exit code {result.returncode}")
                    return False

                if not output_file.exists():
                    print(f"[ERROR] FPS {fps} output file not created")
                    return False

                file_size = output_file.stat().st_size
                if file_size < 500:
                    print(f"[ERROR] FPS {fps} file too small: {file_size} bytes")
                    return False

            except subprocess.TimeoutExpired:
                print(f"[ERROR] FPS {fps} test timed out")
                return False
            except (subprocess.SubprocessError, OSError) as error:
                print(f"[ERROR] FPS {fps} test failed: {error}")
                return False

        print(f"[PASS] Frame rate tests successful for {frame_rates}")
        return True

    def test_recording_duration_accuracy(self):
        """Test recording duration accuracy."""
        durations = [1, 3, 5]

        for duration in durations:
            output_file = self.outputs_dir / f'test_unit_duration_{duration}s.mp4'

            try:
                if output_file.exists():
                    output_file.unlink()

                start_time = time.time()

                cmd = [str(self.muxsw_exe), '--video', '--out', str(output_file),
                       '--time', str(duration)]
                result = subprocess.run(cmd, capture_output=True, text=True,
                                       timeout=duration + 10, check=False)

                actual_duration = time.time() - start_time

                if actual_duration < duration - 1 or actual_duration > duration + 5:
                    print(f"[ERROR] Duration {duration}s inaccurate: took {actual_duration:.1f}s")
                    return False

                if result.returncode != 0:
                    print(f"[ERROR] Duration {duration}s test failed:"
                          f"xit code {result.returncode}")
                    return False

                if not output_file.exists():
                    print(f"[ERROR] Duration {duration}s output file not created")
                    return False

            except subprocess.TimeoutExpired:
                print(f"[ERROR] Duration {duration}s test timed out")
                return False
            except (subprocess.SubprocessError, OSError) as error:
                print(f"[ERROR] Duration {duration}s test failed: {error}")
                return False

        print(f"[PASS] Duration accuracy tests successful for {durations}")
        return True

    def test_output_file_handling(self):
        """Test output file handling and validation."""
        test_files = [
            'test_unit_output.mp4',
            'test with spaces.mp4',
            'test-with-dashes.mp4'
        ]

        for filename in test_files:
            output_file = self.outputs_dir / filename

            try:
                if output_file.exists():
                    output_file.unlink()

                cmd = [str(self.muxsw_exe), '--video', '--out', str(output_file), '--time', '1']
                result = subprocess.run(cmd,
                    capture_output=True,
                    text=True, timeout=10, check=False)

                if result.returncode != 0:
                    print(f"[ERROR] Output file test failed for '{filename}': exit code {result.returncode}")
                    return False

                if not output_file.exists():
                    print(f"[ERROR] Output file '{filename}' not created")
                    return False

            except subprocess.TimeoutExpired:
                print(f"[ERROR] Output file test '{filename}' timed out")
                return False
            except (subprocess.SubprocessError, OSError) as error:
                print(f"[ERROR] Output file test '{filename}' failed: {error}")
                return False

        print(f"[PASS] Output file handling tests successful for {len(test_files)} files")
        return True

    def test_microphone_capture_basic(self):
        """Test basic microphone capture functionality."""
        output_file = self.outputs_dir / 'test_unit_microphone.mp4'

        try:
            if output_file.exists():
                output_file.unlink()

            cmd = [str(self.muxsw_exe), '--microphone', '--out', str(output_file), '--time', '2']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=15, check=False)

            if result.returncode != 0:
                print(f"[ERROR] Microphone capture failed: exit code {result.returncode}")
                return False

            if not output_file.exists():
                print("[ERROR] Microphone output file not created")
                return False

            file_size = output_file.stat().st_size
            if file_size < 500:
                print(f"[ERROR] Microphone file too small: {file_size} bytes")
                return False

            print(f"[PASS] Microphone capture successful ({file_size} bytes)")
            return True

        except subprocess.TimeoutExpired:
            print("[ERROR] Microphone capture timed out")
            return False
        except (subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Microphone capture failed: {error}")
            return False

    def teardown_class(self):
        """Cleanup after unit tests."""
        print("[INFO] Unit tests completed")
