#!/usr/bin/env python3
"""Run the heat pump communication library test suite.

Test layers:
  1. Python unit tests       - checksums, frame parsing, sensor mappings
  2. C++ integration tests   - mock bus and protocol integration

Usage:
    python3 tests/run_tests.py
    python3 tests/run_tests.py --unit-only
    python3 tests/run_tests.py --cpp-only
    python3 tests/run_tests.py --verbose
"""

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = PROJECT_DIR / "tests"
INCLUDE_DIR = PROJECT_DIR / "include"


def run_python_tests(verbose: bool = False) -> bool:
    print("\n" + "=" * 60, flush=True)
    print("  PYTHON UNIT TESTS", flush=True)
    print("=" * 60 + "\n", flush=True)

    pytest_check = subprocess.run(
        [sys.executable, "-m", "pytest", "--version"],
        capture_output=True,
        text=True,
    )

    if pytest_check.returncode == 0:
        args = [sys.executable, "-m", "pytest", str(TEST_DIR / "unit"), "-v"]
        if verbose:
            args.append("-s")
    else:
        print("  pytest not found; using the built-in test runner\n", flush=True)
        args = [sys.executable, str(TEST_DIR / "run_unit_tests.py")]

    result = subprocess.run(args, cwd=str(PROJECT_DIR))
    return result.returncode == 0


def run_cpp_tests(verbose: bool = False) -> bool:
    print("\n" + "=" * 60, flush=True)
    print("  C++ INTEGRATION TESTS", flush=True)
    print("=" * 60 + "\n", flush=True)

    cpp_file = TEST_DIR / "integration" / "test_integration.cpp"
    output_bin = Path("/tmp/hp_protocols_test_integration")

    compile_cmd = [
        "g++", "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-I", str(INCLUDE_DIR),
        "-I", str(TEST_DIR),
        "-o", str(output_bin),
        str(cpp_file),
    ]

    if verbose:
        print(f"  Compile: {' '.join(compile_cmd)}")

    compile_result = subprocess.run(compile_cmd, capture_output=True, text=True)
    if compile_result.returncode != 0:
        print("  COMPILE FAILED:")
        print(compile_result.stderr)
        return False

    run_result = subprocess.run([str(output_bin)], capture_output=True, text=True)
    print(run_result.stdout)
    if run_result.stderr:
        print("STDERR:", run_result.stderr)

    output_bin.unlink(missing_ok=True)
    return run_result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description="Heat pump communication library test runner")
    parser.add_argument("--unit-only", action="store_true", help="Run Python unit tests only")
    parser.add_argument("--cpp-only", action="store_true", help="Run C++ integration tests only")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if args.unit_only:
        success = run_python_tests(args.verbose)
    elif args.cpp_only:
        success = run_cpp_tests(args.verbose)
    else:
        success = run_python_tests(args.verbose) and run_cpp_tests(args.verbose)

    print(flush=True)
    print(" ALL TESTS PASSED" if success else " SOME TESTS FAILED", flush=True)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
