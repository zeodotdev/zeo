# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.view module.

Covers IPC APIs:
- GetViewport()
- SetViewport()
- ZoomToFit()
- ZoomToItems()
- HighlightNet()
- ClearHighlight()
- CrossProbeToBoard()
- CrossProbeFromBoard()
- GetUndoHistory()
- Undo()
- Redo()
"""

from kipy.testing._base import TestResults


def test_view(results: TestResults):
    """Test schematic view operations."""
    results.section("Schematic View")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_viewport
    viewport = None
    try:
        viewport = sch.view.get_viewport()
        if viewport:
            results.ok("view.get_viewport", "retrieved")
        else:
            results.fail("view.get_viewport", "no viewport returned")
    except Exception as e:
        results.fail("view.get_viewport", str(e))

    # Test zoom_to_fit
    try:
        sch.view.zoom_to_fit()
        results.ok("view.zoom_to_fit")
    except Exception as e:
        results.fail("view.zoom_to_fit", str(e))

    # Test set_viewport
    try:
        if viewport:
            # viewport is a dict with 'center' and 'scale' keys
            sch.view.set_viewport(viewport['center'], viewport['scale'])
            results.ok("view.set_viewport", "restored viewport")
        else:
            results.skip("view.set_viewport", "no viewport to set")
    except Exception as e:
        results.fail("view.set_viewport", str(e))

    # Test zoom_to_items
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            sch.view.zoom_to_items(symbols[:1])
            results.ok("view.zoom_to_items", "zoomed to first symbol")
        else:
            results.skip("view.zoom_to_items", "no items")
    except Exception as e:
        results.fail("view.zoom_to_items", str(e))

    # Test clear_highlight
    try:
        sch.view.clear_highlight()
        results.ok("view.clear_highlight")
    except Exception as e:
        results.fail("view.clear_highlight", str(e))

    # Test highlight_net
    try:
        nets = sch.connectivity.get_nets()
        if hasattr(nets, 'nets') and nets.nets:
            net_name = nets.nets[0].name if hasattr(nets.nets[0], 'name') else None
            if net_name:
                sch.view.highlight_net(net_name)
                results.ok("view.highlight_net", f"highlighted '{net_name}'")
                sch.view.clear_highlight()
            else:
                results.skip("view.highlight_net", "no net name")
        else:
            results.skip("view.highlight_net", "no nets")
    except Exception as e:
        results.fail("view.highlight_net", str(e))

    # Test get_undo_history
    try:
        history = sch.view.get_undo_history()
        if history:
            undo_count = len(history.undo_items) if hasattr(history, 'undo_items') else 0
            redo_count = len(history.redo_items) if hasattr(history, 'redo_items') else 0
            results.ok("view.get_undo_history", f"undo={undo_count}, redo={redo_count}")
        else:
            results.ok("view.get_undo_history", "empty history")
    except Exception as e:
        results.fail("view.get_undo_history", str(e))

    # Test undo/redo (be careful not to disrupt the schematic)
    # We'll make a small change, undo it, then redo it
    try:
        from kipy.geometry import Vector2
        # Create a wire
        wire = sch.wiring.add_wire(Vector2.from_xy_mm(220, 220), Vector2.from_xy_mm(230, 220))
        if wire:
            # Undo
            sch.view.undo(1)
            results.ok("view.undo", "undid wire creation")

            # Redo
            sch.view.redo(1)
            results.ok("view.redo", "redid wire creation")

            # Clean up - undo again to remove the wire
            sch.view.undo(1)
        else:
            results.skip("view.undo", "could not create test item")
            results.skip("view.redo", "could not create test item")
    except Exception as e:
        results.fail("view.undo/redo", str(e))

    # Test cross_probe_to_board
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            sch.view.cross_probe_to_board([symbols[0]])
            results.ok("view.cross_probe_to_board", "probed")
        else:
            results.skip("view.cross_probe_to_board", "no symbols")
    except Exception as e:
        # This may fail if PCB editor is not open
        if "board" in str(e).lower() or "pcb" in str(e).lower():
            results.skip("view.cross_probe_to_board", "PCB editor not open")
        else:
            results.fail("view.cross_probe_to_board", str(e))

    # Test cross_probe_from_board
    try:
        result = sch.view.cross_probe_from_board(refs=["R1"], nets=[])
        results.ok("view.cross_probe_from_board", "probed")
    except Exception as e:
        if "board" in str(e).lower() or "pcb" in str(e).lower():
            results.skip("view.cross_probe_from_board", "PCB editor not open")
        else:
            results.fail("view.cross_probe_from_board", str(e))

    # Restore zoom
    try:
        sch.view.zoom_to_fit()
    except:
        pass
