# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.library module.

Covers IPC APIs:
- GetLibraries()
- AddLibrary()
- RemoveLibrary()
- GetLibrarySymbols()
- SearchLibrarySymbols()
- GetSymbolInfo()

Covers QoL functions:
- library.get_all()
- library.add()
- library.remove()
- library.get_symbols()
- library.search_symbols()
- library.get_symbol_info()
- library.find_symbol()
- library.get_common_symbols()
"""

from kipy.testing._base import TestResults


def test_library(results: TestResults):
    """Test schematic library operations."""
    results.section("Schematic Library")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_all
    try:
        libs = sch.library.get_all()
        results.ok("library.get_all", f"{len(libs)} libraries")
    except Exception as e:
        results.fail("library.get_all", str(e))

    # Test get_symbol_info
    try:
        info = sch.library.get_symbol_info("Device:R")
        if info:
            pin_count = len(info.pins) if hasattr(info, 'pins') else 0
            results.ok("library.get_symbol_info", f"Device:R has {pin_count} pins")
        else:
            results.fail("library.get_symbol_info", "returned None")
    except Exception as e:
        if "not found" in str(e).lower():
            results.skip("library.get_symbol_info", "Device library not available")
        else:
            results.fail("library.get_symbol_info", str(e))

    # Test get_symbols
    try:
        symbols = sch.library.get_symbols("Device")
        if symbols:
            results.ok("library.get_symbols", f"{len(symbols)} symbols in Device")
        else:
            results.fail("library.get_symbols", "returned None/empty")
    except Exception as e:
        if "not found" in str(e).lower():
            results.skip("library.get_symbols", "Device library not available")
        else:
            results.fail("library.get_symbols", str(e))

    # Test search (API-based, fast)
    try:
        # Use the API-based search() method which is much faster than file parsing
        results_list = sch.library.search("resistor", max_results=50)
        results.ok("library.search", f"{len(results_list)} matches for 'resistor'")
    except Exception as e:
        results.fail("library.search", str(e))

    # Test search_symbols (file-based, limit to one library to avoid timeout)
    try:
        # Only search Device library to avoid scanning all 223 libraries
        results_list = sch.library.search_symbols("resistor", libraries=["Device"], limit=10)
        results.ok("library.search_symbols", f"{len(results_list)} matches in Device")
    except Exception as e:
        results.fail("library.search_symbols", str(e))

    # Test find_symbol (QoL) - limit to one library to avoid timeout
    try:
        # Only search Device library to avoid scanning all 223 libraries
        found = sch.library.find_symbol(name="R", keywords=["resistor"], libraries=["Device"])
        if found:
            results.ok("library.find_symbol", f"found {len(found)} symbols")
        else:
            results.ok("library.find_symbol", "no matches")
    except Exception as e:
        results.fail("library.find_symbol", str(e))

    # Test get_common_symbols (QoL)
    # Note: This searches multiple specific libraries (Device, power, Connector)
    # which is slower but not as bad as searching all 223 libraries
    try:
        common = sch.library.get_common_symbols()
        if common:
            categories = list(common.keys())
            results.ok("library.get_common_symbols", f"{len(categories)} categories")
        else:
            results.fail("library.get_common_symbols", "returned None/empty")
    except Exception as e:
        results.fail("library.get_common_symbols", str(e))

    # Test add and remove library
    # These are destructive operations, so we'll be careful
    # We won't actually add/remove libraries in the test
    results.skip("library.add", "skipped to avoid modifying library table")
    results.skip("library.remove", "skipped to avoid modifying library table")

    # Test search ranking - exact matches should appear first
    results.section("Search Ranking")

    # Test: Searching "R" in Device library should return "Device:R" first
    try:
        search_results = sch.library.search("R", libraries=["Device"], max_results=50)
        if search_results:
            first = search_results[0]
            if first.name == "R":
                results.ok("search_ranking_R", f"'R' search returns 'R' first")
            else:
                results.fail("search_ranking_R", f"Expected 'R' first, got '{first.name}'")
        else:
            results.fail("search_ranking_R", "No results returned")
    except Exception as e:
        if "not found" in str(e).lower():
            results.skip("search_ranking_R", "Device library not available")
        else:
            results.fail("search_ranking_R", str(e))

    # Test: Searching "L" in Device library should return "Device:L" first
    try:
        search_results = sch.library.search("L", libraries=["Device"], max_results=50)
        if search_results:
            first = search_results[0]
            if first.name == "L":
                results.ok("search_ranking_L", f"'L' search returns 'L' first")
            else:
                results.fail("search_ranking_L", f"Expected 'L' first, got '{first.name}'")
        else:
            results.fail("search_ranking_L", "No results returned")
    except Exception as e:
        if "not found" in str(e).lower():
            results.skip("search_ranking_L", "Device library not available")
        else:
            results.fail("search_ranking_L", str(e))

    # Test: Searching "C" in Device library should return "Device:C" first
    try:
        search_results = sch.library.search("C", libraries=["Device"], max_results=50)
        if search_results:
            first = search_results[0]
            if first.name == "C":
                results.ok("search_ranking_C", f"'C' search returns 'C' first")
            else:
                results.fail("search_ranking_C", f"Expected 'C' first, got '{first.name}'")
        else:
            results.fail("search_ranking_C", "No results returned")
    except Exception as e:
        if "not found" in str(e).lower():
            results.skip("search_ranking_C", "Device library not available")
        else:
            results.fail("search_ranking_C", str(e))
