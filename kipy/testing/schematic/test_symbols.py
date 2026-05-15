# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.symbols module.

Covers IPC APIs:
- CreateItems(Symbol)
- GetItems(KOT_SCH_SYMBOL)
- GetItemsById(ids)
- UpdateItems(Symbol)
- DeleteItems(ids)
- GetTransformedPinPosition()
- GetSymbolInfo(lib_id)

Covers QoL functions:
- symbols.add()
- symbols.get_all()
- symbols.get_by_ref()
- symbols.get_by_value()
- symbols.get_by_lib()
- symbols.find()
- symbols.move()
- symbols.rotate()
- symbols.mirror()
- symbols.duplicate()
- symbols.set_value()
- symbols.set_footprint()
- symbols.get_pin_position()
"""

from kipy.testing._base import (
    TestResults,
    verify_item_position,
    expect_count_increase,
    DEFAULT_TEST_X_MM,
    DEFAULT_TEST_Y_MM,
    MM_TO_NM,
)


def test_symbol_coordinates(results: TestResults):
    """Test symbol placement coordinate accuracy (catches 100x scaling bugs)."""
    results.section("Symbol Coordinate Verification (CRITICAL)")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    test_x_mm = DEFAULT_TEST_X_MM
    test_y_mm = DEFAULT_TEST_Y_MM

    results.info(f"Testing placement at ({test_x_mm}mm, {test_y_mm}mm)")

    try:
        symbol = sch.symbols.add("Device:R", Vector2.from_xy_mm(test_x_mm, test_y_mm))
        if symbol:
            verify_item_position(results, "symbol", symbol, test_x_mm, test_y_mm)
            # Clean up
            sch.crud.remove_items([symbol])
        else:
            results.fail("symbol_placement", "No symbol returned")
    except Exception as e:
        if "library" in str(e).lower() or "not found" in str(e).lower():
            results.skip("symbol_coordinates", f"Library not available: {e}")
        else:
            results.fail("symbol_coordinates", str(e))


def test_symbols(results: TestResults):
    """Test schematic symbol operations."""
    results.section("Schematic Symbols")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_all
    try:
        symbols = sch.symbols.get_all()
        results.ok("symbols.get_all", f"{len(symbols)} symbols")
    except Exception as e:
        results.fail("symbols.get_all", str(e))

    # Test add
    symbol = None
    try:
        symbols_before = len(sch.symbols.get_all())
        symbol = sch.symbols.add("Device:R", Vector2.from_xy_mm(50, 50))
        symbols_after = len(sch.symbols.get_all())

        if symbols_after > symbols_before:
            results.ok("symbols.add", "created")
        else:
            results.fail("symbols.add", "symbol count unchanged")
    except Exception as e:
        if "library" in str(e).lower() or "not found" in str(e).lower():
            results.skip("symbols.add", "library not available")
        else:
            results.fail("symbols.add", str(e))

    if symbol:
        # Test get_pin_position
        try:
            pin_pos = sch.symbols.get_pin_position(symbol, "1")
            if pin_pos:
                results.ok("symbols.get_pin_position", f"pin 1 at ({pin_pos.x/MM_TO_NM:.1f}mm, {pin_pos.y/MM_TO_NM:.1f}mm)")
            else:
                results.fail("symbols.get_pin_position", "returned None")
        except Exception as e:
            results.fail("symbols.get_pin_position", str(e))

        # Test get_by_ref
        try:
            ref = symbol.reference if hasattr(symbol, 'reference') else None
            if ref:
                found = sch.symbols.get_by_ref(ref)
                if found:
                    results.ok("symbols.get_by_ref", f"found {ref}")
                else:
                    results.fail("symbols.get_by_ref", "not found")
            else:
                results.skip("symbols.get_by_ref", "no reference on symbol")
        except Exception as e:
            results.fail("symbols.get_by_ref", str(e))

        # Test move
        try:
            new_pos = Vector2.from_xy_mm(55, 55)
            sch.symbols.move(symbol, new_pos)
            results.ok("symbols.move", f"moved to (55mm, 55mm)")
        except Exception as e:
            results.fail("symbols.move", str(e))

        # Test rotate
        try:
            sch.symbols.rotate(symbol, 90)
            results.ok("symbols.rotate", "rotated 90°")
        except Exception as e:
            results.fail("symbols.rotate", str(e))

        # Test mirror
        try:
            sch.symbols.mirror(symbol, "x")
            results.ok("symbols.mirror", "mirrored on X axis")
        except Exception as e:
            results.fail("symbols.mirror", str(e))

        # Test set_value
        try:
            sch.symbols.set_value(symbol, "10k")
            results.ok("symbols.set_value", "set to 10k")
        except Exception as e:
            results.fail("symbols.set_value", str(e))

        # Test set_footprint
        try:
            sch.symbols.set_footprint(symbol, "Resistor_SMD:R_0805_2012Metric")
            results.ok("symbols.set_footprint", "set footprint")
        except Exception as e:
            results.fail("symbols.set_footprint", str(e))

        # Test duplicate
        try:
            dup = sch.symbols.duplicate(symbol, offset_mm=(10, 10))
            if dup:
                results.ok("symbols.duplicate", "duplicated")
                sch.crud.remove_items([dup])
            else:
                results.fail("symbols.duplicate", "returned None")
        except Exception as e:
            results.fail("symbols.duplicate", str(e))

        # Clean up
        try:
            sch.crud.remove_items([symbol])
        except:
            pass

    # Test get_by_value (with existing symbols)
    try:
        all_symbols = sch.symbols.get_all()
        if all_symbols:
            # Try to find by value of first symbol
            first_val = getattr(all_symbols[0], 'value', None)
            if first_val:
                found = sch.symbols.get_by_value(first_val)
                results.ok("symbols.get_by_value", f"found {len(found)} with value '{first_val}'")
            else:
                results.skip("symbols.get_by_value", "no value on symbol")
        else:
            results.skip("symbols.get_by_value", "no symbols in schematic")
    except Exception as e:
        results.fail("symbols.get_by_value", str(e))

    # Test find
    try:
        found = sch.symbols.find(lib_name="Device")
        results.ok("symbols.find", f"found {len(found)} Device symbols")
    except Exception as e:
        results.fail("symbols.find", str(e))
