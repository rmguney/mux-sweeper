from pathlib import Path
import sys

import argparse
import shutil

#!/usr/bin/env python3
"""
Test Output Cleanup Helper Tool

This tool automatically cleans up the tests/outputs directory and any spurious
test files that might be created during testing. It can be run manually or
automatically after test completion.

Usage:
    python cleanup_helper.py [--all] [--verbose]

Options:
    --all       Clean up all test-related files including those outside tests/outputs
    --verbose   Show detailed information about what's being cleaned
"""

class TestCleanupHelper:
    """Helper tool for cleaning up test outputs and spurious files."""

    def __init__(self, verbose=False):
        self.verbose = verbose
        self.tests_dir = Path(__file__).parent
        self.project_root = self.tests_dir.parent
        self.outputs_dir = self.tests_dir / "outputs"
        self.cleaned_files = []
        self.cleaned_dirs = []

    def log(self, message, force=False):
        """Log a message if verbose mode is enabled."""
        if self.verbose or force:
            print(message)

    def clean_outputs_directory(self):
        """Clean the tests/outputs directory."""
        if self.outputs_dir.exists():
            self.log(f"Cleaning outputs directory: {self.outputs_dir}")
            try:
                if self.verbose:
                    files = list(self.outputs_dir.rglob("*"))
                    if files:
                        self.log("Files to be removed:")
                        for file_path in files:
                            if file_path.is_file():
                                self.log(f"  - {file_path.name} ({file_path.stat().st_size} bytes)")

                shutil.rmtree(self.outputs_dir, ignore_errors=True)
                self.cleaned_dirs.append(str(self.outputs_dir))
                self.log("✓ Outputs directory cleaned successfully")
            except OSError as error:
                self.log(f"✗ Error cleaning outputs directory: {error}", force=True)
        else:
            self.log("✓ Outputs directory doesn't exist (already clean)")

    def clean_spurious_files(self):
        """Clean spurious test files from the project root."""
        self.log("Scanning for spurious test files in project root...")

        problematic_files = []
        try:
            for item in self.project_root.iterdir():
                if item.is_file():
                    name = item.name.strip()
                    is_empty_name = name == '' or name == ' ' or name in ['""', "''"]
                    is_test_file = any(item.name.startswith(prefix)
                                     for prefix in ['test_', 'tmp_', 'temp_'])
                    is_quality_test = any(pattern in item.name.lower()
                                        for pattern in ['quality_test', 'fps_test'])
                    is_numeric_video = (item.suffix in ['.mp4', '.avi'] and
                                      item.name.replace(item.suffix, '').isdigit())

                    if (is_empty_name or is_test_file or is_quality_test or is_numeric_video):
                        problematic_files.append(item)

            if problematic_files:
                self.log(f"Found {len(problematic_files)} spurious files to clean:")
                for file_path in problematic_files:
                    try:
                        if self.verbose:
                            size = file_path.stat().st_size if file_path.exists() else 0
                            display_name = f"'{file_path.name}'" if file_path.name else "<empty name>"
                            self.log(f"  - {display_name} ({size} bytes)")
                        file_path.unlink(missing_ok=True)
                        self.cleaned_files.append(str(file_path))
                    except OSError as error:
                        self.log(f"  ✗ Could not remove {file_path.name}: {error}", force=True)
                self.log(f"✓ Cleaned {len(problematic_files)} spurious files")
            else:
                self.log("✓ No spurious files found")

        except OSError as error:
            self.log(f"✗ Error scanning for spurious files: {error}", force=True)

    def clean_temp_directories(self):
        """Clean temporary directories that might be left behind."""
        temp_dirs = [
            self.project_root / "test_outputs",  # Old location
            self.project_root / "temp",
            self.project_root / "tmp",
        ]

        for temp_dir in temp_dirs:
            if temp_dir.exists() and temp_dir.is_dir():
                try:
                    self.log(f"Removing temporary directory: {temp_dir}")
                    shutil.rmtree(temp_dir, ignore_errors=True)
                    self.cleaned_dirs.append(str(temp_dir))
                except OSError as error:
                    self.log(f"✗ Could not remove {temp_dir}: {error}", force=True)

    def run_cleanup(self, clean_all=False):
        """Run the cleanup process."""
        self.log("Starting test cleanup...", force=True)

        # Always clean the outputs directory
        self.clean_outputs_directory()

        # If --all is specified, clean everything
        if clean_all:
            self.log("Running comprehensive cleanup (--all mode)...")
            self.clean_spurious_files()
            self.clean_temp_directories()

        # Print summary
        total_cleaned = len(self.cleaned_files) + len(self.cleaned_dirs)
        if total_cleaned > 0:
            files_count = len(self.cleaned_files)
            dirs_count = len(self.cleaned_dirs)
            summary = (f"\n✓ Cleanup completed: {files_count} files and "
                      f"{dirs_count} directories removed")
            self.log(summary, force=True)
            if self.verbose and self.cleaned_files:
                self.log("Files cleaned:")
                for file_path in self.cleaned_files:
                    self.log(f"  - {file_path}")
            if self.verbose and self.cleaned_dirs:
                self.log("Directories cleaned:")
                for dir_path in self.cleaned_dirs:
                    self.log(f"  - {dir_path}")
        else:
            self.log("✓ No cleanup needed - everything was already clean", force=True)


def main():
    """Main entry point for the cleanup helper."""
    parser = argparse.ArgumentParser(
        description="Test Output Cleanup Helper Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    parser.add_argument('--all', action='store_true',
                       help='Clean up all test-related files including those outside tests/outputs')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Show detailed information about what\'s being cleaned')

    args = parser.parse_args()

    cleaner = TestCleanupHelper(verbose=args.verbose)
    try:
        cleaner.run_cleanup(clean_all=args.all)
        return 0
    except KeyboardInterrupt:
        print("\nCleanup interrupted by user")
        return 130
    except (OSError, ImportError, ValueError) as error:
        print(f"Error during cleanup: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
