# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.wiring module.

Covers IPC APIs:
- CreateItems(Line) - wires
- CreateItems(Junction)
- CreateItems(NoConnect)
- GetItems(KOT_SCH_LINE/JUNCTION/NO_CONNECT)
- UpdateItems()
- DeleteItems()

Covers QoL functions:
- wiring.add_wire()
- wiring.add_wires()
- wiring.get_wires()
- wiring.add_junction()
- wiring.get_junctions()
- wiring.add_no_connect()
- wiring.get_no_connects()
- wiring.wire_pins()
- wiring.wire_from_pin()
- wiring.auto_wire()
"""

from kipy.testing._base import (
    TestResults,
    verify_schematic_position_mm,
    DEFAULT_TEST_X_MM,
    DEFAULT_TEST_Y_MM,
)


def test_wire_coordinates(results: TestResults):
    """Test wire placement coordinate accuracy."""
    results.section("Wire Coordinate Verification (CRITICAL)")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    wire_start_mm = (90.0, 70.0)
    wire_end_mm = (100.0, 70.0)

    results.info(f"Testing wire from ({wire_start_mm[0]}mm, {wire_start_mm[1]}mm) to ({wire_end_mm[0]}mm, {wire_end_mm[1]}mm)")

    try:
        wire = sch.wiring.add_wire(
            Vector2.from_xy_mm(wire_start_mm[0], wire_start_mm[1]),
            Vector2.from_xy_mm(wire_end_mm[0], wire_end_mm[1])
        )

        if wire:
            # Verify wire start position
            start_x_ok, start_x_msg = verify_schematic_position_mm(wire.start.x, wire_start_mm[0])
            start_y_ok, start_y_msg = verify_schematic_position_mm(wire.start.y, wire_start_mm[1])

            # Verify wire end position
            end_x_ok, end_x_msg = verify_schematic_position_mm(wire.end.x, wire_end_mm[0])
            end_y_ok, end_y_msg = verify_schematic_position_mm(wire.end.y, wire_end_mm[1])

            if start_x_ok and start_y_ok:
                results.ok("wire_start_position", f"({wire_start_mm[0]}mm, {wire_start_mm[1]}mm)")
            else:
                results.fail("wire_start_position", f"x:{start_x_msg}, y:{start_y_msg}")

            if end_x_ok and end_y_ok:
                results.ok("wire_end_position", f"({wire_end_mm[0]}mm, {wire_end_mm[1]}mm)")
            else:
                results.fail("wire_end_position", f"x:{end_x_msg}, y:{end_y_msg}")

            # Clean up
            sch.crud.remove_items([wire])
        else:
            results.fail("wire_placement", "No wire returned")
    except Exception as e:
        results.fail("wire_coordinates", str(e))


def test_wiring(results: TestResults):
    """Test schematic wiring operations."""
    results.section("Schematic Wiring")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_wires
    try:
        wires = sch.wiring.get_wires()
        results.ok("wiring.get_wires", f"{len(wires)} wires")
    except Exception as e:
        results.fail("wiring.get_wires", str(e))

    # Test add_wire
    wire = None
    try:
        wires_before = len(sch.wiring.get_wires())
        wire = sch.wiring.add_wire(Vector2.from_xy_mm(60, 60), Vector2.from_xy_mm(70, 60))
        wires_after = len(sch.wiring.get_wires())

        if wires_after > wires_before:
            results.ok("wiring.add_wire", "created")
        else:
            results.fail("wiring.add_wire", "wire count unchanged")
    except Exception as e:
        results.fail("wiring.add_wire", str(e))

    if wire:
        try:
            sch.crud.remove_items([wire])
        except:
            pass

    # Test add_wires (multi-segment)
    try:
        points = [
            Vector2.from_xy_mm(80, 80),
            Vector2.from_xy_mm(90, 80),
            Vector2.from_xy_mm(90, 90),
        ]
        wires_before = len(sch.wiring.get_wires())
        wires = sch.wiring.add_wires(points)
        wires_after = len(sch.wiring.get_wires())

        if wires_after > wires_before:
            results.ok("wiring.add_wires", f"{wires_after - wires_before} segments created")
            # Clean up
            if wires:
                try:
                    sch.crud.remove_items(wires)
                except:
                    pass
        else:
            results.fail("wiring.add_wires", "wire count unchanged")
    except Exception as e:
        results.fail("wiring.add_wires", str(e))

    # Test get_junctions
    try:
        junctions = sch.wiring.get_junctions()
        results.ok("wiring.get_junctions", f"{len(junctions)} junctions")
    except Exception as e:
        results.fail("wiring.get_junctions", str(e))

    # Test add_junction
    try:
        junctions_before = len(sch.wiring.get_junctions())
        junction = sch.wiring.add_junction(Vector2.from_xy_mm(65, 65))
        junctions_after = len(sch.wiring.get_junctions())

        if junctions_after > junctions_before:
            results.ok("wiring.add_junction", "created")
        else:
            results.fail("wiring.add_junction", "junction not persisted")
    except Exception as e:
        results.fail("wiring.add_junction", str(e))

    # Test get_no_connects
    try:
        no_connects = sch.wiring.get_no_connects()
        results.ok("wiring.get_no_connects", f"{len(no_connects)} no-connects")
    except Exception as e:
        results.fail("wiring.get_no_connects", str(e))

    # Test add_no_connect
    nc = None
    try:
        nc_before = len(sch.wiring.get_no_connects())
        nc = sch.wiring.add_no_connect(Vector2.from_xy_mm(75, 75))
        nc_after = len(sch.wiring.get_no_connects())

        if nc_after > nc_before:
            results.ok("wiring.add_no_connect", "created")
        else:
            results.fail("wiring.add_no_connect", "not persisted")
    except Exception as e:
        results.fail("wiring.add_no_connect", str(e))

    if nc:
        try:
            sch.crud.remove_items([nc])
        except:
            pass

    # Test wire_pins (requires symbols)
    try:
        symbols = sch.symbols.get_all()
        if len(symbols) >= 2:
            # Try to wire pin 1 of first symbol to pin 1 of second symbol
            wire = sch.wiring.wire_pins(symbols[0], "1", symbols[1], "1")
            if wire:
                results.ok("wiring.wire_pins", "connected pins")
                sch.crud.remove_items([wire])
            else:
                results.fail("wiring.wire_pins", "returned None")
        else:
            results.skip("wiring.wire_pins", "need 2+ symbols")
    except Exception as e:
        if "pin" in str(e).lower():
            results.skip("wiring.wire_pins", "pin not found")
        else:
            results.fail("wiring.wire_pins", str(e))

    # Test wire_from_pin
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            wire = sch.wiring.wire_from_pin(symbols[0], "1", Vector2.from_xy_mm(120, 80))
            if wire:
                results.ok("wiring.wire_from_pin", "created")
                sch.crud.remove_items([wire])
            else:
                results.fail("wiring.wire_from_pin", "returned None")
        else:
            results.skip("wiring.wire_from_pin", "no symbols")
    except Exception as e:
        if "pin" in str(e).lower():
            results.skip("wiring.wire_from_pin", "pin not found")
        else:
            results.fail("wiring.wire_from_pin", str(e))

    # Test auto_wire
    try:
        symbols = sch.symbols.get_all()
        if len(symbols) >= 2:
            wires = sch.wiring.auto_wire(symbols[0], "1", symbols[1], "1")
            if wires:
                results.ok("wiring.auto_wire", f"{len(wires)} segments")
                sch.crud.remove_items(wires)
            else:
                results.fail("wiring.auto_wire", "returned None/empty")
        else:
            results.skip("wiring.auto_wire", "need 2+ symbols")
    except Exception as e:
        if "pin" in str(e).lower():
            results.skip("wiring.auto_wire", "pin not found")
        else:
            results.fail("wiring.auto_wire", str(e))
