# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
Shared test infrastructure for kipy IPC API testing.

This module provides:
- TestResults: Collects and reports test results
- Coordinate verification helpers for catching scaling bugs
- Common test utilities
"""

from typing import List, Tuple, Optional, Callable, Any
from dataclasses import dataclass, field


import os
import tempfile
from datetime import datetime

# Log file for full test output
_LOG_FILE = os.path.join(tempfile.gettempdir(), "kipy_test_results.log")


def _init_log():
    """Initialize the log file with a header."""
    with open(_LOG_FILE, "w") as f:
        f.write(f"KiCad IPC API Test Results\n")
        f.write(f"Run at: {datetime.now().isoformat()}\n")
        f.write("=" * 60 + "\n\n")


def _log(msg: str):
    """Write a message to the log file."""
    with open(_LOG_FILE, "a") as f:
        f.write(msg + "\n")


def _print(msg: str):
    """Print a message to stdout and log file."""
    print(msg)
    _log(msg)


# =============================================================================
# CONSTANTS
# =============================================================================

# Conversion constants
MM_TO_NM = 1_000_000  # 1mm = 1,000,000 nm (PCB units)
MM_TO_IU = 10_000     # 1mm = 10,000 IU (Schematic internal units)

# Default test positions (chosen to clearly show 100x scaling errors)
DEFAULT_TEST_X_MM = 80.0
DEFAULT_TEST_Y_MM = 70.0


# =============================================================================
# TEST RESULTS COLLECTOR
# =============================================================================

@dataclass
class TestResults:
    """Collects and reports test results."""

    passed: int = 0
    failed: int = 0
    skipped: int = 0
    errors: List[tuple] = field(default_factory=list)
    _current_section: str = ""

    def section(self, name: str):
        """Start a new test section."""
        self._current_section = name
        _print(f"\n=== {name} ===")

    def ok(self, name: str, detail: str = ""):
        """Record a passed test."""
        self.passed += 1
        suffix = f": {detail}" if detail else ""
        _print(f"  [PASS] {name}{suffix}")

    def fail(self, name: str, error: str):
        """Record a failed test."""
        self.failed += 1
        full_name = f"{self._current_section}.{name}" if self._current_section else name
        # Truncate for display, but log full error
        short_error = error[:100] + "..." if len(error) > 100 else error
        self.errors.append((full_name, short_error))
        print(f"  [FAIL] {name}: {short_error}")
        # Log full error to file
        _log(f"  [FAIL] {name}: {error}")

    def skip(self, name: str, reason: str = ""):
        """Record a skipped test."""
        self.skipped += 1
        suffix = f": {reason}" if reason else ""
        _print(f"  [SKIP] {name}{suffix}")

    def info(self, message: str):
        """Print an info message."""
        _print(f"  [INFO] {message}")

    def summary(self) -> bool:
        """Print summary and return True if all tests passed."""
        _print("\n" + "=" * 60)
        total = self.passed + self.failed + self.skipped
        result_str = f"Results: {self.passed}/{total} passed"
        if self.failed:
            result_str += f", {self.failed} failed"
        if self.skipped:
            result_str += f", {self.skipped} skipped"
        _print(result_str)

        if self.errors:
            _print("\nFailed tests:")
            for name, error in self.errors:
                _print(f"  - {name}: {error}")

        return self.failed == 0


# =============================================================================
# COORDINATE VERIFICATION HELPERS
# =============================================================================

def verify_position_mm(
    actual_nm: int,
    expected_mm: float,
    tolerance_mm: float = 0.1
) -> Tuple[bool, str]:
    """Verify a position in nanometers matches expected millimeters.

    Args:
        actual_nm: Actual position in nanometers
        expected_mm: Expected position in millimeters
        tolerance_mm: Acceptable tolerance in millimeters

    Returns:
        (passed, message) tuple
    """
    actual_mm = actual_nm / MM_TO_NM
    diff_mm = abs(actual_mm - expected_mm)

    if diff_mm <= tolerance_mm:
        return True, f"{actual_mm:.2f}mm (expected {expected_mm:.2f}mm)"
    else:
        # Check for common scaling bugs
        if abs(actual_mm - expected_mm * 100) < tolerance_mm:
            return False, f"{actual_mm:.2f}mm - 100x TOO LARGE (expected {expected_mm:.2f}mm) - nm/IU bug!"
        elif abs(actual_mm * 100 - expected_mm) < tolerance_mm:
            return False, f"{actual_mm:.2f}mm - 100x TOO SMALL (expected {expected_mm:.2f}mm) - IU/nm bug!"
        else:
            return False, f"{actual_mm:.2f}mm != {expected_mm:.2f}mm (diff: {diff_mm:.2f}mm)"


def verify_schematic_position_mm(
    actual_nm: int,
    expected_mm: float,
    tolerance_mm: float = 0.5
) -> Tuple[bool, str]:
    """Verify a schematic position.

    Schematic uses IU internally (1mm = 10,000 IU) but API should use nm (1mm = 1,000,000 nm).
    This test helps catch the 100x scaling bug.
    """
    return verify_position_mm(actual_nm, expected_mm, tolerance_mm)


def verify_position_xy(
    actual_x: int,
    actual_y: int,
    expected_x_mm: float,
    expected_y_mm: float,
    tolerance_mm: float = 0.5
) -> Tuple[bool, str, str]:
    """Verify X and Y positions together.

    Returns:
        (both_ok, x_message, y_message) tuple
    """
    x_ok, x_msg = verify_position_mm(actual_x, expected_x_mm, tolerance_mm)
    y_ok, y_msg = verify_position_mm(actual_y, expected_y_mm, tolerance_mm)
    return (x_ok and y_ok), x_msg, y_msg


def check_zero_position_bug(actual_x: int, actual_y: int) -> Optional[str]:
    """Check for the (0,0) position bug where items are placed at origin.

    Returns:
        Error message if bug detected, None otherwise
    """
    if actual_x == 0 and actual_y == 0:
        return "Item at (0,0) - position field not read by C++ handler!"
    return None


# =============================================================================
# TEST UTILITIES
# =============================================================================

def run_test(
    results: TestResults,
    name: str,
    test_fn: Callable[[], Any],
    cleanup_fn: Optional[Callable[[], None]] = None
) -> Any:
    """Run a single test with error handling and optional cleanup.

    Args:
        results: TestResults instance
        name: Test name
        test_fn: Function to run
        cleanup_fn: Optional cleanup function

    Returns:
        Result of test_fn, or None if failed
    """
    try:
        result = test_fn()
        return result
    except Exception as e:
        error_str = str(e)
        # Check for common expected failures
        if "library" in error_str.lower() or "not found" in error_str.lower():
            results.skip(name, "library not available")
        else:
            results.fail(name, error_str)
        return None
    finally:
        if cleanup_fn:
            try:
                cleanup_fn()
            except:
                pass


def expect_count_increase(
    results: TestResults,
    name: str,
    get_count_fn: Callable[[], int],
    action_fn: Callable[[], Any],
    cleanup_fn: Optional[Callable[[Any], None]] = None
) -> Any:
    """Test that an action increases a count.

    Args:
        results: TestResults instance
        name: Test name
        get_count_fn: Function that returns current count
        action_fn: Function that should increase the count
        cleanup_fn: Optional cleanup function that receives the action result

    Returns:
        Result of action_fn, or None if failed
    """
    try:
        count_before = get_count_fn()
        result = action_fn()
        count_after = get_count_fn()

        if count_after > count_before:
            results.ok(name, "created")
            if cleanup_fn and result:
                try:
                    cleanup_fn(result)
                except:
                    pass
            return result
        else:
            results.fail(name, "count unchanged")
            return None
    except Exception as e:
        error_str = str(e)
        if "library" in error_str.lower() or "not found" in error_str.lower():
            results.skip(name, "library not available")
        else:
            results.fail(name, error_str)
        return None


def verify_item_position(
    results: TestResults,
    name: str,
    item: Any,
    expected_x_mm: float,
    expected_y_mm: float,
    tolerance_mm: float = 0.5
) -> bool:
    """Verify an item's position and report results.

    Args:
        results: TestResults instance
        name: Test name base
        item: Item with .position.x and .position.y attributes
        expected_x_mm: Expected X position in mm
        expected_y_mm: Expected Y position in mm
        tolerance_mm: Acceptable tolerance

    Returns:
        True if position is correct
    """
    actual_x = item.position.x
    actual_y = item.position.y

    # Check for (0,0) bug first
    zero_bug = check_zero_position_bug(actual_x, actual_y)
    if zero_bug:
        results.fail(f"{name}_position", zero_bug)
        return False

    x_ok, x_msg = verify_schematic_position_mm(actual_x, expected_x_mm, tolerance_mm)
    y_ok, y_msg = verify_schematic_position_mm(actual_y, expected_y_mm, tolerance_mm)

    if x_ok and y_ok:
        results.ok(f"{name}_position_x", x_msg)
        results.ok(f"{name}_position_y", y_msg)
        return True
    else:
        if not x_ok:
            results.fail(f"{name}_position_x", x_msg)
        else:
            results.ok(f"{name}_position_x", x_msg)
        if not y_ok:
            results.fail(f"{name}_position_y", y_msg)
        else:
            results.ok(f"{name}_position_y", y_msg)
        return False
