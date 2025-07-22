#!/usr/bin/env python3
"""
Prerequisite Tests

Tests that validate the basic requirements and environment setup
before running any functional tests.
"""

from pathlib import Path
import os
import subprocess
import sys
import pytest

class TestPrerequisites:
    """Test class for validating prerequisites."""

    def setup_method(self):
        """Setup for each test method."""
        self.project_root = Path(__file__).parent.parent.parent
        self.release_dir = self.project_root / 'release'
        self.muxsw_exe = self.release_dir / 'muxsw.exe'
        self.muxsw_gui_exe = self.release_dir / 'muxsw-gui.exe'

    def setup_class(self):
        """Setup for prerequisite tests."""
        print("[INFO] Setting up prerequisite tests")

    @pytest.mark.prerequisites
    def test_python_version(self):
        """Test that Python version is adequate."""
        assert sys.version_info >= (3, 7), f"Python 3.7+ required, found {sys.version}"
        print(f"[PASS] Python version: {sys.version}")
        return True

    @pytest.mark.prerequisites
    def test_project_structure(self):
        """Test that project directory structure exists."""
        required_dirs = [
            self.project_root,
            self.project_root / 'src',
            self.project_root / 'include',
            self.project_root / 'test_suite',
            self.release_dir
        ]

        missing_dirs = []
        for directory in required_dirs:
            if not directory.exists():
                missing_dirs.append(str(directory))

        assert not missing_dirs, f"Missing directories: {', '.join(missing_dirs)}"
        print("[PASS] Project structure valid")
        return True

    @pytest.mark.prerequisites
    def test_executable_exists_console(self):
        """Test that muxsw console executable exists."""
        assert self.muxsw_exe.exists(), (
            f"muxsw.exe not found at {self.muxsw_exe}. "
            "Run build process first: cmake --build . --config Release"
        )
        print(f"[PASS] muxsw.exe found at {self.muxsw_exe}")
        return True

    @pytest.mark.prerequisites
    def test_executable_exists_gui(self):
        """Test that muxsw GUI executable exists."""
        assert self.muxsw_gui_exe.exists(), (
            f"muxsw-gui.exe not found at {self.muxsw_gui_exe}. "
            "Run build process first: cmake --build . --config Release"
        )
        print(f"[PASS] muxsw-gui.exe found at {self.muxsw_gui_exe}")
        return True

    @pytest.mark.prerequisites
    def test_executable_can_run(self):
        """Test that muxsw executable can run and show help."""
        try:
            result = subprocess.run(
                [str(self.muxsw_exe), '--help'],
                capture_output=True,
                text=True,
                timeout=10,
                check=False
            )

            assert result.returncode == 0, f"muxsw.exe failed to run: {result.stderr}"

            help_output = result.stdout.lower()
            assert 'usage' in help_output, "muxsw.exe help output doesn't contain usage information"

            print("[PASS] muxsw.exe runs and shows help")

        except subprocess.TimeoutExpired:
            pytest.fail("muxsw.exe timed out")
        except (subprocess.SubprocessError, OSError) as error:
            pytest.fail(f"Could not execute muxsw.exe: {error}")

    @pytest.mark.prerequisites
    def test_windows_media_foundation(self):
        """Test Windows Media Foundation availability (no DLLs needed)."""
        # This is more of a documentation test since WMF is built into Windows 10+
        # We just verify we're on Windows
        assert os.name == 'nt', "Windows required for Windows Media Foundation"
        print("[PASS] Windows platform detected (WMF available)")
        return True

    @pytest.mark.prerequisites
    def test_test_directories(self):
        """Test that test directories can be created."""
        test_dirs = [
            self.project_root / 'test_suite' / 'outputs',
            self.project_root / 'test_suite' / 'logs'
        ]

        try:
            for test_dir in test_dirs:
                test_dir.mkdir(parents=True, exist_ok=True)
                assert test_dir.exists(), f"Could not create test directory: {test_dir}"

            print("[PASS] Test directories can be created")

        except (OSError, PermissionError) as error:
            pytest.fail(f"Failed to create test directories: {error}")

    @pytest.mark.prerequisites
    def test_file_permissions(self):
        """Test that we have necessary file permissions."""
        test_file = self.project_root / 'test_suite' / 'outputs' / '.permission_test'

        try:
            # Ensure parent directory exists
            test_file.parent.mkdir(parents=True, exist_ok=True)

            # Test write permission
            with open(test_file, 'w', encoding='utf-8') as file_handle:
                file_handle.write('permission test')

            # Test read permission
            with open(test_file, 'r', encoding='utf-8') as file_handle:
                content = file_handle.read()

            # Test delete permission
            test_file.unlink()

            assert content == 'permission test', "File read/write permission test failed"
            print("[PASS] File permissions adequate")

        except (OSError, PermissionError) as error:
            pytest.fail(f"File permission test failed: {error}")

    def teardown_class(self):
        """Cleanup after prerequisite tests."""
        print("[INFO] Prerequisite tests completed")
