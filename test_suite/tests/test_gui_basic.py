#!/usr/bin/env python3
"""
Basic GUI Tests

Essential tests for the GUI version of muxsw.
"""

from pathlib import Path
import subprocess
import time


class TestGuiBasic:
    """Basic GUI test suite for essential functionality."""

    def setup_method(self):
        """Setup for GUI tests."""
        self.project_root = Path(__file__).parent.parent.parent
        self.release_dir = self.project_root / 'release'
        self.muxsw_gui_exe = self.release_dir / 'muxsw-gui.exe'

    def test_gui_executable_exists(self):
        """Test that GUI executable exists."""
        assert self.muxsw_gui_exe.exists(), f"GUI executable not found at {self.muxsw_gui_exe}"
        print(f"[PASS] GUI executable found")

    def test_gui_can_start_and_stop(self):
        """Test that GUI can start and be terminated."""
        try:
            process = subprocess.Popen(
                [str(self.muxsw_gui_exe)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
            )

            # Give it time to start
            time.sleep(3)

            # Check if it crashed immediately
            if process.poll() is not None:
                _, stderr = process.communicate()
                assert False, f"GUI process terminated early: {stderr.decode('utf-8', errors='ignore')}"

            # Terminate and wait
            try:
                process.terminate()
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

            print("[PASS] GUI can start and stop")

        except (subprocess.SubprocessError, OSError) as error:
            assert False, f"GUI start/stop test failed: {error}"
