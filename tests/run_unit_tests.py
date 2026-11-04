#!/usr/bin/env python3
"""Minimal test runner used when pytest is not installed.

The unit tests use plain classes with ``test_*`` methods and no fixtures, so
they can be executed directly. pytest remains the preferred runner.
"""

import importlib.util
import inspect
import sys
import traceback
from pathlib import Path

TEST_FILE = Path(__file__).resolve().parent / "unit" / "test_protocols.py"


def load_tests():
    spec = importlib.util.spec_from_file_location("test_protocols", TEST_FILE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {TEST_FILE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    module = load_tests()
    passed = 0
    failed = 0

    classes = [
        obj for name, obj in vars(module).items()
        if inspect.isclass(obj) and name.startswith("Test")
    ]

    for test_class in classes:
        print(f"\n{test_class.__name__}")
        instance = test_class()
        methods = sorted(
            name for name in dir(instance)
            if name.startswith("test_") and callable(getattr(instance, name))
        )
        for method_name in methods:
            try:
                getattr(instance, method_name)()
            except Exception:
                failed += 1
                print(f"  FAIL {method_name}")
                traceback.print_exc()
            else:
                passed += 1
                print(f"  PASS {method_name}")

    print(f"\n=== Results: {passed} passed, {failed} failed ===")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
