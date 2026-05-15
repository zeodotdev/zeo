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
Comprehensive IPC API testing module for kipy.

This module provides integration tests for the KiCad IPC API with emphasis on:
1. Coordinate verification - ensures items are placed at correct positions
2. Unit conversion validation - catches nm/IU mismatch bugs
3. API coverage - tests all major operations

Usage:
    import kipy
    kipy.test_ipc()                              # Run all tests
    kipy.test_ipc(['sch_coordinates'])           # Run specific test
    kipy.test_ipc(['pcb_read', 'sch_symbols'])   # Run multiple tests

Test Categories:
    Coordinate Verification (CRITICAL):
        - sch_coordinates: Verifies schematic items at correct positions
        - pcb_coordinates: Verifies PCB items at correct positions

    Schematic Tests:
        - sch_symbols: Symbol operations
        - sch_wiring: Wire, junction, no-connect operations
        - sch_labels: Label operations (local, global, hierarchical, power)
        - sch_buses: Bus operations
        - sch_graphics: Text, shapes, lines
        - sch_selection: Selection management
        - sch_crud: CRUD and transaction operations
        - sch_page: Page and grid settings
        - sch_sheets: Sheet hierarchy
        - sch_connectivity: Net and bus queries
        - sch_erc: ERC operations
        - sch_library: Library operations
        - sch_document: Document operations
        - sch_view: View operations
        - sch_export: Export operations
        - sch_simulation: SPICE simulation
        - sch_transform: Transform operations
        - sch_design_blocks: Design block operations

    PCB Tests:
        - pcb_read: Basic read operations
        - pcb_selection: Selection management
        - pcb_routing: Track and via creation
        - pcb_footprints: Footprint placement
        - pcb_zones: Zone creation
"""

from typing import Optional, List, Callable

from kipy.testing._base import TestResults, _print, _init_log, _LOG_FILE

# Import schematic tests
from kipy.testing.schematic import (
    test_symbols, test_symbol_coordinates,
    test_wiring, test_wire_coordinates,
    test_labels, test_label_coordinates,
    test_buses,
    test_graphics,
    test_selection,
    test_crud,
    test_page,
    test_sheets,
    test_connectivity,
    test_erc,
    test_library,
    test_document,
    test_view,
    test_export,
    test_simulation,
    test_transform,
    test_design_blocks,
)

# Import PCB tests
from kipy.testing.pcb import (
    test_pcb_read,
    test_pcb_selection,
    test_pcb_routing, test_pcb_coordinates,
    test_pcb_zones,
)


def _test_connection(results: TestResults):
    """Test basic connection to KiCad."""
    results.section("Connection")
    try:
        from kipy import KiCad
        kicad = KiCad()
        version = kicad.get_version()
        results.ok("connect", f"v{version}")
        return kicad
    except Exception as e:
        results.fail("connect", str(e))
        return None


# All available tests organized by category
ALL_TESTS: dict[str, Callable[[TestResults], None]] = {
    # Connection (always runs first)
    'connection': _test_connection,

    # Coordinate Verification (CRITICAL - catches scaling bugs)
    'sch_coordinates': lambda r: (test_symbol_coordinates(r), test_wire_coordinates(r), test_label_coordinates(r)),
    'pcb_coordinates': test_pcb_coordinates,

    # Schematic Tests
    'sch_symbols': test_symbols,
    'sch_wiring': test_wiring,
    'sch_labels': test_labels,
    'sch_buses': test_buses,
    'sch_graphics': test_graphics,
    'sch_selection': test_selection,
    'sch_crud': test_crud,
    'sch_page': test_page,
    'sch_sheets': test_sheets,
    'sch_connectivity': test_connectivity,
    'sch_erc': test_erc,
    'sch_library': test_library,
    'sch_document': test_document,
    'sch_view': test_view,
    'sch_export': test_export,
    'sch_simulation': test_simulation,
    'sch_transform': test_transform,
    'sch_design_blocks': test_design_blocks,

    # PCB Tests
    'pcb_read': test_pcb_read,
    'pcb_selection': test_pcb_selection,
    'pcb_routing': test_pcb_routing,
    'pcb_zones': test_pcb_zones,
}


def test_ipc(tests: Optional[List[str]] = None, verbose: bool = True) -> bool:
    """Run IPC API tests.

    Args:
        tests: Optional list of test names to run. If None, runs all tests.
        verbose: If True, print detailed output.

    Returns:
        True if all tests passed, False otherwise.

    Examples:
        >>> import kipy
        >>> kipy.test_ipc()                          # Run all tests
        >>> kipy.test_ipc(['sch_coordinates'])       # Test coordinate accuracy
        >>> kipy.test_ipc(['pcb_read', 'sch_read'])  # Run multiple tests
    """
    # Initialize log file
    _init_log()

    results = TestResults()

    _print("=" * 60)
    _print("KiCad IPC API Test Suite")
    _print("=" * 60)

    # Determine which tests to run
    if tests is None:
        tests_to_run = list(ALL_TESTS.keys())
    else:
        tests_to_run = [t for t in tests if t in ALL_TESTS]
        unknown = set(tests) - set(ALL_TESTS.keys())
        if unknown:
            _print(f"Unknown tests: {unknown}")
            _print(f"Available: {list(ALL_TESTS.keys())}")

    # Connection test first
    kicad = _test_connection(results)
    if not kicad:
        _print("\nCannot continue without connection.")
        return results.summary()

    # Run remaining tests
    for test_name in tests_to_run:
        if test_name == 'connection':
            continue
        try:
            ALL_TESTS[test_name](results)
        except Exception as e:
            results.fail(test_name, f"Unexpected: {e}")

    success = results.summary()
    _print(f"\nFull log written to: {_LOG_FILE}")
    return success


__all__ = ["test_ipc", "TestResults", "ALL_TESTS"]
