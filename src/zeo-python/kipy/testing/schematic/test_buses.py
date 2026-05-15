# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.buses module.

Covers IPC APIs:
- CreateItems(Line[layer=BUS])
- CreateItems(BusEntry)
- GetBuses()
- GetBusMembers(name)
- GetItems(KOT_SCH_BUS_ENTRY)
- UpdateItems()
- DeleteItems()

Covers QoL functions:
- buses.get_all()
- buses.get_members()
- buses.add_bus_line()
- buses.add_bus_entry()
- buses.get_bus_entries()
- buses.define_vector_bus()
- buses.define_group_bus()
- buses.create_bus_tap()
"""

from kipy.testing._base import TestResults


def test_buses(results: TestResults):
    """Test schematic bus operations."""
    results.section("Schematic Buses")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_all
    try:
        buses = sch.buses.get_all()
        results.ok("buses.get_all", f"{len(buses)} buses")
    except Exception as e:
        results.fail("buses.get_all", str(e))

    # Test get_bus_entries
    try:
        entries = sch.buses.get_bus_entries()
        results.ok("buses.get_bus_entries", f"{len(entries)} bus entries")
    except Exception as e:
        results.fail("buses.get_bus_entries", str(e))

    # Test add_bus_line
    bus_line = None
    try:
        bus_line = sch.buses.add_bus_line(
            Vector2.from_xy_mm(140, 140),
            Vector2.from_xy_mm(140, 160)
        )
        if bus_line:
            results.ok("buses.add_bus_line", "created")
        else:
            results.fail("buses.add_bus_line", "returned None")
    except Exception as e:
        results.fail("buses.add_bus_line", str(e))

    if bus_line:
        try:
            sch.crud.remove_items([bus_line])
        except:
            pass

    # Test add_bus_entry
    entry = None
    try:
        entries_before = len(sch.buses.get_bus_entries())
        entry = sch.buses.add_bus_entry(Vector2.from_xy_mm(150, 150), "right_down")
        entries_after = len(sch.buses.get_bus_entries())

        if entries_after > entries_before:
            results.ok("buses.add_bus_entry", "created")
        else:
            results.fail("buses.add_bus_entry", "entry count unchanged")
    except Exception as e:
        results.fail("buses.add_bus_entry", str(e))

    if entry:
        try:
            sch.buses.delete_bus_entry(entry)
        except:
            pass

    # Test get_members (if there are buses)
    try:
        buses = sch.buses.get_all()
        if buses:
            bus_name = buses[0].name if hasattr(buses[0], 'name') else None
            if bus_name:
                members = sch.buses.get_members(bus_name)
                results.ok("buses.get_members", f"{len(members)} members in '{bus_name}'")
            else:
                results.skip("buses.get_members", "no bus name available")
        else:
            results.skip("buses.get_members", "no buses in schematic")
    except Exception as e:
        results.fail("buses.get_members", str(e))

    # Test define_vector_bus (client-side helper)
    try:
        bus_def = sch.buses.define_vector_bus("D", 0, 7)
        expected_name = "D[0..7]"
        if bus_def.name == expected_name:
            results.ok("buses.define_vector_bus", f"'{bus_def.name}'")
        else:
            results.fail("buses.define_vector_bus", f"expected '{expected_name}', got '{bus_def.name}'")
    except Exception as e:
        results.fail("buses.define_vector_bus", str(e))

    # Test define_group_bus (client-side helper)
    try:
        bus_def = sch.buses.define_group_bus("CTRL", ["CLK", "RST", "EN"])
        expected = "{CLK, RST, EN}"
        if bus_def.name == expected:
            results.ok("buses.define_group_bus", f"'{bus_def.name}'")
        else:
            results.fail("buses.define_group_bus", f"expected '{expected}', got '{bus_def.name}'")
    except Exception as e:
        results.fail("buses.define_group_bus", str(e))

    # Test create_bus_tap
    try:
        tap_items = sch.buses.create_bus_tap(
            bus_position=Vector2.from_xy_mm(155, 155),
            wire_end=Vector2.from_xy_mm(165, 155),
            net_name="D0"
        )
        if tap_items and len(tap_items) > 0:
            results.ok("buses.create_bus_tap", f"{len(tap_items)} items created")
            # Clean up
            try:
                sch.crud.remove_items(tap_items)
            except:
                pass
        else:
            results.fail("buses.create_bus_tap", "no items created")
    except Exception as e:
        results.fail("buses.create_bus_tap", str(e))

    # Test update_bus_entry
    try:
        entry = sch.buses.add_bus_entry(Vector2.from_xy_mm(160, 160), "right_down")
        if entry:
            # Try to update it
            sch.buses.update_bus_entry(entry)
            results.ok("buses.update_bus_entry", "updated")
            sch.buses.delete_bus_entry(entry)
        else:
            results.skip("buses.update_bus_entry", "could not create entry")
    except Exception as e:
        results.fail("buses.update_bus_entry", str(e))
