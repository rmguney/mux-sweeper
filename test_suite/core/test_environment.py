#!/usr/bin/env python3
"""
Test Environment Management

Handles environment setup, validation, and configuration for test execution.
Includes comprehensive virtual environment management.
"""

from pathlib import Path
import os
import subprocess
import sys

from typing import Dict, Any

class TestEnvironment:
    """Manages test environment setup and validation."""

    def __init__(self, project_root: Path):
        """Initialize test environment manager."""
        self.project_root = project_root
        self.tests_dir = project_root / 'test_suite'
        self.release_dir = project_root / 'release'
        self.outputs_dir = self.tests_dir / 'outputs'
        self.logs_dir = self.tests_dir / 'logs'
        self.venv_dir = self.tests_dir / 'venv'

        # Executable paths
        self.muxsw_exe = self.release_dir / 'muxsw.exe'
        self.muxsw_gui_exe = self.release_dir / 'muxsw-gui.exe'

    def verify_python_environment(self) -> bool:
        """Verify Python environment is suitable for testing."""
        try:
            # Check Python version
            if sys.version_info < (3, 7):
                print(f"[ERROR] Python 3.7+ required, found {sys.version}")
                return False

            # Check if we're in a virtual environment (recommended)
            in_venv = hasattr(sys, 'real_prefix') or (
                hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix
            )

            if not in_venv:
                print("[WARNING] Not running in virtual environment (recommended but not required)")

            # Try to import required modules
            required_modules = ['subprocess', 'pathlib', 'time', 'json']
            for module in required_modules:
                try:
                    __import__(module)
                except ImportError:
                    print(f"[ERROR] Required module not available: {module}")
                    return False

            print("[INFO] Python environment validation passed")
            return True

        except ImportError as error:
            print(f"[ERROR] Python environment validation failed: {error}")
            return False

    def verify_executables(self) -> bool:
        """Verify that required executables exist."""
        executables = [
            ('muxsw.exe', self.muxsw_exe),
            ('muxsw-gui.exe', self.muxsw_gui_exe)
        ]

        missing_executables = []
        for name, path in executables:
            if not path.exists():
                missing_executables.append(name)

        if missing_executables:
            print(f"[ERROR] Missing executables: {', '.join(missing_executables)}")
            print(f"[ERROR] Expected location: {self.release_dir}")
            print("[ERROR] Run build process first: cmake --build . --config Release")
            return False

        # Test that executables can run
        try:
            result = subprocess.run(
                [str(self.muxsw_exe), '--help'],
                capture_output=True,
                text=True,
                timeout=10,
                check=False
            )
            if result.returncode != 0:
                print(f"[ERROR] muxsw.exe failed to run: {result.stderr}")
                return False
        except (subprocess.TimeoutExpired, subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Could not execute muxsw.exe: {error}")
            return False

        print("[INFO] Executable validation passed")
        return True

    def setup_test_directories(self) -> bool:
        """Setup required test directories."""
        try:
            directories = [
                self.outputs_dir,
                self.logs_dir,
                self.tests_dir / '__pycache__'
            ]

            for directory in directories:
                directory.mkdir(parents=True, exist_ok=True)

            print("[INFO] Test directories created/verified")
            return True

        except (OSError, PermissionError) as error:
            print(f"[ERROR] Failed to setup test directories: {error}")
            return False

    def get_system_info(self) -> Dict[str, Any]:
        """Get system information for test reporting."""
        info = {
            'python_version': sys.version,
            'python_executable': sys.executable,
            'platform': sys.platform,
            'working_directory': str(Path.cwd()),
            'project_root': str(self.project_root),
            'executables': {
                'muxsw': str(self.muxsw_exe) if self.muxsw_exe.exists() else 'NOT_FOUND',
                'muxsw_gui': str(self.muxsw_gui_exe) if self.muxsw_gui_exe.exists() else 'NOT_FOUND'
            }
        }

        # Add virtual environment info
        if (hasattr(sys, 'real_prefix') or
            (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix)):
            info['virtual_env'] = os.environ.get('VIRTUAL_ENV', 'DETECTED')
        else:
            info['virtual_env'] = None

        return info

    def check_dependencies(self) -> Dict[str, bool]:
        """Check for optional dependencies."""
        optional_deps = {
            'pytest': False,
            'colorlog': False,
            'rich': False,
            'tabulate': False,
            'pylint': False
        }

        for dep in optional_deps:
            try:
                __import__(dep)
                optional_deps[dep] = True
            except ImportError:
                pass

        return optional_deps

    def install_dependencies(self) -> bool:
        """Install test dependencies if possible."""
        try:
            requirements_file = self.tests_dir / 'requirements.txt'
            if not requirements_file.exists():
                print("[WARNING] requirements.txt not found, skipping dependency installation")
                return True

            print("[INFO] Installing test dependencies...")
            result = subprocess.run([
                sys.executable, '-m', 'pip', 'install', '-r', str(requirements_file)
            ], capture_output=True, text=True, check=False)

            if result.returncode == 0:
                print("[INFO] Dependencies installed successfully")
                return True

            print(f"[WARNING] Dependency installation failed: {result.stderr}")
            print("[INFO] Tests will continue with available modules")
            return True  # Don't fail tests due to optional deps

        except (subprocess.SubprocessError, OSError) as error:
            print(f"[WARNING] Could not install dependencies: {error}")
            return True  # Don't fail tests due to optional deps

    def ensure_virtual_environment(self) -> bool:
        """Ensure virtual environment exists and is properly set up."""
        try:
            # Check if venv already exists
            if not self.venv_dir.exists():
                print("[INFO] Creating virtual environment...")
                if not self._create_virtual_environment():
                    return False
            else:
                print("[INFO] Virtual environment already exists")
            # Verify venv integrity
            if not self._verify_virtual_environment():
                print("[WARNING] Virtual environment appears corrupted, recreating...")
                if not self._recreate_virtual_environment():
                    return False
            # Install/update dependencies
            if not self._install_venv_dependencies():
                print("[WARNING] Some dependencies could not be installed")
            print("[INFO] Virtual environment ready")
            return True
        except (OSError, subprocess.SubprocessError) as error:
            print(f"[ERROR] Virtual environment setup failed: {error}")
            return False

    def get_venv_python_executable(self) -> Path:
        """Get the Python executable path for the virtual environment."""
        if sys.platform == "win32":
            return self.venv_dir / 'Scripts' / 'python.exe'
        else:
            return self.venv_dir / 'bin' / 'python'

    def is_in_virtual_environment(self) -> bool:
        """Check if currently running in a virtual environment."""
        return (hasattr(sys, 'real_prefix') or 
                (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix))

    def activate_virtual_environment(self) -> bool:
        """
        Activate virtual environment for current process.
        Note: This modifies the current Python environment.
        """
        try:
            if self.is_in_virtual_environment():
                # Already in a venv, check if it's ours
                current_venv = os.environ.get('VIRTUAL_ENV', '')
                if str(self.venv_dir) in current_venv:
                    print("[INFO] Already in project virtual environment")
                    return True
                else:
                    print("[WARNING] In different virtual environment, continuing...")
                    return True

            # Set up virtual environment variables
            venv_path = str(self.venv_dir)
            os.environ['VIRTUAL_ENV'] = venv_path
            os.environ['VIRTUAL_ENV_PROMPT'] = '(muxsw-tests)'

            # Update PATH to use venv scripts
            if sys.platform == "win32":
                scripts_dir = str(self.venv_dir / 'Scripts')
            else:
                scripts_dir = str(self.venv_dir / 'bin')
            current_path = os.environ.get('PATH', '')
            os.environ['PATH'] = f"{scripts_dir}{os.pathsep}{current_path}"
            # Update sys.path to use venv packages
            venv_python = self.get_venv_python_executable()
            if venv_python.exists():
                # Add the venv site-packages to sys.path
                if sys.platform == "win32":
                    site_packages = self.venv_dir / 'Lib' / 'site-packages'
                else:
                    python_version = f"python{sys.version_info.major}.{sys.version_info.minor}"
                    site_packages = self.venv_dir / 'lib' / python_version / 'site-packages'
                if site_packages.exists():
                    sys.path.insert(0, str(site_packages))
            print(f"[INFO] Virtual environment activated: {venv_path}")
            return True
        except (OSError, KeyError) as error:
            print(f"[ERROR] Failed to activate virtual environment: {error}")
            return False

    def run_in_venv(self, command: list, **kwargs) -> subprocess.CompletedProcess:
        """Run a command using the virtual environment Python."""
        venv_python = self.get_venv_python_executable()
        if not venv_python.exists():
            raise FileNotFoundError(f"Virtual environment Python not found: {venv_python}")
        if command and command[0] == 'python':
            command[0] = str(venv_python)
        elif command and command[0] == sys.executable:
            command[0] = str(venv_python)
        return subprocess.run(command, **kwargs)

    def _create_virtual_environment(self) -> bool:
        """Create a new virtual environment."""
        try:
            print(f"[INFO] Creating virtual environment at: {self.venv_dir}")
            result = subprocess.run([
                sys.executable, '-m', 'venv', str(self.venv_dir)
            ], capture_output=True, text=True, check=False)
            if result.returncode != 0:
                print(f"[ERROR] Failed to create virtual environment: {result.stderr}")
                return False
            print("[INFO] Virtual environment created successfully")
            return True
        except (subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Virtual environment creation failed: {error}")
            return False

    def _verify_virtual_environment(self) -> bool:
        """Verify virtual environment integrity."""
        try:
            venv_python = self.get_venv_python_executable()
            if not venv_python.exists():
                return False
            # Test that venv python can run
            result = subprocess.run([
                str(venv_python), '--version'
            ], capture_output=True, text=True, timeout=10, check=False)
            return result.returncode == 0
        except (subprocess.SubprocessError, OSError, subprocess.TimeoutExpired):
            return False
    def _recreate_virtual_environment(self) -> bool:
        """Remove and recreate virtual environment."""
        try:
            if self.venv_dir.exists():
                import shutil
                shutil.rmtree(str(self.venv_dir))
            return self._create_virtual_environment()

        except (OSError, ImportError) as error:
            print(f"[ERROR] Failed to recreate virtual environment: {error}")
            return False
    def _install_venv_dependencies(self) -> bool:
        """Install dependencies in virtual environment."""
        try:
            venv_python = self.get_venv_python_executable()
            # Upgrade pip first
            result = subprocess.run([
                str(venv_python), '-m', 'pip', 'install', '--upgrade', 'pip'
            ], capture_output=True, text=True, check=False)
            if result.returncode != 0:
                print(f"[WARNING] Failed to upgrade pip: {result.stderr}")
            # Install requirements if available
            requirements_file = self.tests_dir / 'requirements.txt'
            if requirements_file.exists():
                print("[INFO] Installing requirements from requirements.txt...")
                result = subprocess.run([
                    str(venv_python), '-m', 'pip', 'install', '-r', str(requirements_file)
                ], capture_output=True, text=True, check=False)
                if result.returncode != 0:
                    print(f"[WARNING] Some requirements failed to install: {result.stderr}")
                    return False
            # Install essential packages
            essential_packages = ['pytest', 'pylint']
            for package in essential_packages:
                result = subprocess.run([
                    str(venv_python), '-m', 'pip', 'install', package
                ], capture_output=True, text=True, check=False)
                if result.returncode != 0:
                    print(f"[WARNING] Failed to install {package}: {result.stderr}")
            return True
        except (subprocess.SubprocessError, OSError) as error:
            print(f"[ERROR] Dependency installation failed: {error}")
            return False

    def setup_logging(self, log_level='INFO') -> bool:
        """Setup centralized logging configuration to write to logs directory."""
        try:
            import logging
            from datetime import datetime
            
            # Ensure logs directory exists
            self.logs_dir.mkdir(parents=True, exist_ok=True)
            
            # Create timestamped log file
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            log_file = self.logs_dir / f'test_run_{timestamp}.log'
            
            # Configure logging
            logging.basicConfig(
                level=getattr(logging, log_level.upper()),
                format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                handlers=[
                    logging.FileHandler(str(log_file)),
                    logging.StreamHandler()  # Also log to console
                ]
            )
            
            logger = logging.getLogger(__name__)
            logger.info(f"Logging configured - writing to {log_file}")
            return True
            
        except Exception as error:
            print(f"[ERROR] Failed to setup logging: {error}")
            return False
