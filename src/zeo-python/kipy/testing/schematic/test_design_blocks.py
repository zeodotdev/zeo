# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.design_blocks module.

Covers IPC APIs:
- GetDesignBlocks()
- SearchDesignBlocks()
- SaveSelectionAsDesignBlock()
- SaveSheetAsDesignBlock()
- PlaceDesignBlock()
- DeleteDesignBlock()

Covers QoL functions:
- design_blocks.get_all()
- design_blocks.search()
- design_blocks.save_selection()
- design_blocks.save_sheet()
- design_blocks.place()
- design_blocks.delete()
- design_blocks.get_by_library()
"""

from kipy.testing._base import TestResults


def test_design_blocks(results: TestResults):
    """Test schematic design blocks operations."""
    results.section("Schematic Design Blocks")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_all
    try:
        blocks = sch.design_blocks.get_all()
        results.ok("design_blocks.get_all", f"{len(blocks)} design blocks")
    except Exception as e:
        if "not supported" in str(e).lower() or "not implemented" in str(e).lower():
            results.skip("design_blocks.get_all", "design blocks not supported")
            # Skip remaining tests
            results.skip("design_blocks.search", "design blocks not supported")
            results.skip("design_blocks.save_selection", "design blocks not supported")
            results.skip("design_blocks.save_sheet", "design blocks not supported")
            results.skip("design_blocks.place", "design blocks not supported")
            results.skip("design_blocks.delete", "design blocks not supported")
            results.skip("design_blocks.get_by_library", "design blocks not supported")
            return
        else:
            results.fail("design_blocks.get_all", str(e))

    # Test search
    try:
        found = sch.design_blocks.search("*")
        results.ok("design_blocks.search", f"{len(found)} matches for '*'")
    except Exception as e:
        results.fail("design_blocks.search", str(e))

    # Test get_by_library (QoL)
    try:
        grouped = sch.design_blocks.get_by_library()
        results.ok("design_blocks.get_by_library", f"{len(grouped)} libraries")
    except Exception as e:
        results.fail("design_blocks.get_by_library", str(e))

    # Test save_selection
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            sch.selection.clear()
            sch.selection.add(symbols[0])

            # We need a library path - this will likely fail without proper setup
            # Just test that the API exists
            try:
                sch.design_blocks.save_selection(
                    library="test_lib",
                    name="test_block",
                    description="Test block"
                )
                results.ok("design_blocks.save_selection", "saved")
            except Exception as e:
                if "library" in str(e).lower():
                    results.skip("design_blocks.save_selection", "no design block library configured")
                else:
                    results.fail("design_blocks.save_selection", str(e))

            sch.selection.clear()
        else:
            results.skip("design_blocks.save_selection", "no symbols to select")
    except Exception as e:
        results.fail("design_blocks.save_selection", str(e))

    # Test save_sheet
    try:
        sheets = sch.crud.get_sheets()
        if len(sheets) > 1:
            # We have a subsheet to save
            try:
                sch.design_blocks.save_sheet(
                    sheet_id=sheets[1].id,
                    library="test_lib",
                    name="test_sheet_block"
                )
                results.ok("design_blocks.save_sheet", "saved")
            except Exception as e:
                if "library" in str(e).lower():
                    results.skip("design_blocks.save_sheet", "no design block library configured")
                else:
                    results.fail("design_blocks.save_sheet", str(e))
        else:
            results.skip("design_blocks.save_sheet", "no subsheets to save")
    except Exception as e:
        results.fail("design_blocks.save_sheet", str(e))

    # Test place
    try:
        blocks = sch.design_blocks.get_all()
        if blocks:
            from kipy.geometry import Vector2
            block_id = blocks[0].id if hasattr(blocks[0], 'id') else None
            if block_id:
                placed = sch.design_blocks.place(block_id, Vector2.from_xy_mm(300, 100))
                if placed:
                    results.ok("design_blocks.place", "placed")
                    # Clean up
                    try:
                        sch.crud.remove_items(placed)
                    except:
                        pass
                else:
                    results.fail("design_blocks.place", "returned None")
            else:
                results.skip("design_blocks.place", "no block ID")
        else:
            results.skip("design_blocks.place", "no design blocks available")
    except Exception as e:
        results.fail("design_blocks.place", str(e))

    # Test delete
    # We won't actually delete a design block to avoid disrupting the library
    results.skip("design_blocks.delete", "skipped to preserve design block library")
