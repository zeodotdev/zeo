# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.selection module.

Covers IPC APIs:
- GetSelection()
- AddToSelection()
- RemoveFromSelection()
- ClearSelection()
"""

from kipy.testing._base import TestResults


def test_selection(results: TestResults):
    """Test schematic selection operations."""
    results.section("Schematic Selection")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test clear
    try:
        sch.selection.clear()
        results.ok("selection.clear")
    except Exception as e:
        results.fail("selection.clear", str(e))

    # Test get
    try:
        sel = sch.selection.get()
        results.ok("selection.get", f"{len(sel)} items")
    except Exception as e:
        results.fail("selection.get", str(e))

    # Test add and remove (requires items in schematic)
    symbols = sch.symbols.get_all()
    if symbols:
        # Test add
        try:
            sel = sch.selection.add(symbols[0])
            if len(sel) > 0:
                results.ok("selection.add", f"{len(sel)} items selected")
            else:
                results.fail("selection.add", "selection empty after add")
        except Exception as e:
            results.fail("selection.add", str(e))

        # Test remove
        try:
            sel = sch.selection.remove(symbols[0])
            results.ok("selection.remove", f"{len(sel)} items remaining")
        except Exception as e:
            results.fail("selection.remove", str(e))

        # Test add multiple
        if len(symbols) >= 2:
            try:
                sel = sch.selection.add(symbols[:2])
                results.ok("selection.add_multiple", f"{len(sel)} items selected")
            except Exception as e:
                results.fail("selection.add_multiple", str(e))
        else:
            results.skip("selection.add_multiple", "need 2+ symbols")

    else:
        results.skip("selection.add", "no symbols in schematic")
        results.skip("selection.remove", "no symbols in schematic")
        results.skip("selection.add_multiple", "no symbols in schematic")

    # Test with wires
    wires = sch.wiring.get_wires()
    if wires:
        try:
            sch.selection.clear()
            sel = sch.selection.add(wires[0])
            results.ok("selection.add_wire", f"wire selected, {len(sel)} items")
        except Exception as e:
            results.fail("selection.add_wire", str(e))
    else:
        results.skip("selection.add_wire", "no wires in schematic")

    # Test with labels
    labels = sch.labels.get_all()
    if labels:
        try:
            sch.selection.clear()
            sel = sch.selection.add(labels[0])
            results.ok("selection.add_label", f"label selected, {len(sel)} items")
        except Exception as e:
            results.fail("selection.add_label", str(e))
    else:
        results.skip("selection.add_label", "no labels in schematic")

    # Final clear
    try:
        sch.selection.clear()
        sel = sch.selection.get()
        if len(sel) == 0:
            results.ok("selection.clear_final", "selection empty")
        else:
            results.fail("selection.clear_final", f"still have {len(sel)} items")
    except Exception as e:
        results.fail("selection.clear_final", str(e))
