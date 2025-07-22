#!/usr/bin/env python3
"""
Test Discovery Module

Handles discovery and categorization of test files and test cases.
"""

from pathlib import Path

from typing import List, Dict, Any, Type, Callable
import importlib.util
import inspect

class TestDiscovery:
    """Discovers and categorizes test files and methods."""

    def __init__(self, tests_dir: Path):
        """Initialize test discovery."""
        self.tests_dir = tests_dir
        self.discovered_tests: Dict[str, Any] = {}

    def discover_all_tests(self) -> Dict[str, List[str]]:
        """Discover all test files and categorize them."""
        test_categories = {
            'prerequisites': [],
            'unit': [],
            'integration': [],
            'audio': [],
            'gui': [],
            'performance': []
        }

        # Scan for test files
        test_files = list(self.tests_dir.glob('test_*.py'))

        for test_file in test_files:
            category = self._categorize_test_file(test_file)
            if category in test_categories:
                test_categories[category].append(str(test_file))

        return test_categories

    def discover_test_methods(self, test_class: Type) -> List[str]:
        """Discover test methods in a test class."""
        test_methods = []

        for name, _ in inspect.getmembers(test_class, predicate=inspect.ismethod):
            if name.startswith('test_'):
                test_methods.append(name)

        return test_methods

    def load_test_class(self, file_path: Path, class_name: str) -> Type:
        """Dynamically load a test class from a file."""
        try:
            spec = importlib.util.spec_from_file_location("test_module", file_path)
            if spec is None or spec.loader is None:
                raise ImportError(f"Could not load spec from {file_path}")

            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            if hasattr(module, class_name):
                return getattr(module, class_name)
            
            raise AttributeError(f"Class {class_name} not found in {file_path}")

        except (ImportError, AttributeError) as error:
            print(f"[ERROR] Could not load test class {class_name} from {file_path}: {error}")
            raise

    def get_test_function(self, test_class: Type, method_name: str) -> Callable:
        """Get a specific test method from a test class."""
        if hasattr(test_class, method_name):
            return getattr(test_class, method_name)
        
        raise AttributeError(f"Method {method_name} not found in {test_class.__name__}")

    def _categorize_test_file(self, test_file: Path) -> str:
        """Categorize a test file based on its name and content."""
        file_name = test_file.name.lower()

        # Category mapping based on file names
        category_mappings = [
            (['prerequisite', 'requirement'], 'prerequisites'),
            (['unit', 'basic'], 'unit'),
            (['integration', 'combination'], 'integration'),
            (['audio', 'sound'], 'audio'),
            (['gui', 'interface'], 'gui'),
            (['performance', 'speed', 'regression'], 'performance'),
        ]

        for keywords, category in category_mappings:
            if any(keyword in file_name for keyword in keywords):
                return category

        # Default to unit tests
        return 'unit'

    def get_test_dependencies(self, test_file: Path) -> List[str]:
        """Get dependencies for a test file by analyzing imports."""
        dependencies = []

        try:
            with open(test_file, 'r', encoding='utf-8') as f:
                content = f.read()

            # Simple dependency detection
            lines = content.split('\n')
            for line in lines:
                line = line.strip()
                if line.startswith('import ') or line.startswith('from '):
                    if 'pytest' in line:
                        dependencies.append('pytest')
                    elif 'colorlog' in line:
                        dependencies.append('colorlog')
                    elif 'rich' in line:
                        dependencies.append('rich')
                    elif 'tabulate' in line:
                        dependencies.append('tabulate')
                    elif 'subprocess' in line:
                        dependencies.append('subprocess')

        except (OSError, UnicodeDecodeError):
            pass  # Skip if can't read file

        return list(set(dependencies))  # Remove duplicates

    def validate_test_structure(self, test_file: Path) -> Dict[str, Any]:
        """Validate that a test file follows expected structure."""
        validation = {
            'valid': True,
            'issues': [],
            'has_docstring': False,
            'has_test_methods': False,
            'test_method_count': 0
        }

        try:
            with open(test_file, 'r', encoding='utf-8') as f:
                content = f.read()

            # Check for docstring
            if '"""' in content:
                validation['has_docstring'] = True
            else:
                validation['issues'].append('Missing module docstring')

            # Check for test methods
            test_methods = [line for line in content.split('\n')
                           if line.strip().startswith('def test_')]

            validation['test_method_count'] = len(test_methods)

            if test_methods:
                validation['has_test_methods'] = True
            else:
                validation['issues'].append('No test methods found')
                validation['valid'] = False

            # Check for basic structure
            if 'class ' not in content and len(test_methods) == 0:
                validation['issues'].append('No test class or test functions found')
                validation['valid'] = False

        except (OSError, UnicodeDecodeError) as error:
            validation['valid'] = False
            validation['issues'].append(f'Could not read file: {error}')

        return validation
