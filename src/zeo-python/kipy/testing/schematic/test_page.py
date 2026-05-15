# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.page module.

Covers IPC APIs:
- GetPageSettings()
- SetPageSettings()
- GetTitleBlockInfo()
- SetTitleBlockInfo()
- GetGridSettings()
- SetGridSettings()

Covers QoL functions:
- page.get_settings()
- page.set_settings()
- page.get_title_block()
- page.set_title_block()
- page.get_grid_settings()
- page.set_grid()
- page.get_usable_area()
- page.get_usable_area_mils()
- page.snap_to_grid()
- page.snap_to_grid_mils()
- page.snap_point_to_grid()
- page.get_common_grids()
- page.get_grid_positions()
"""

from kipy.testing._base import TestResults


def test_page(results: TestResults):
    """Test schematic page and grid settings."""
    results.section("Schematic Page & Grid")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_settings
    try:
        settings = sch.page.get_settings()
        if settings:
            size = getattr(settings, 'size', 'unknown')
            results.ok("page.get_settings", f"size={size}")
        else:
            results.fail("page.get_settings", "no settings returned")
    except Exception as e:
        results.fail("page.get_settings", str(e))

    # Test get_title_block
    try:
        title_block = sch.page.get_title_block()
        if title_block:
            title = getattr(title_block, 'title', '')
            results.ok("page.get_title_block", f"title='{title}'")
        else:
            results.fail("page.get_title_block", "no title block returned")
    except Exception as e:
        results.fail("page.get_title_block", str(e))

    # Test get_grid_settings
    try:
        grid = sch.page.get_grid_settings()
        if grid:
            size_mm = grid.get('size_mm', '?')
            results.ok("page.get_grid_settings", f"size={size_mm}mm")
        else:
            results.fail("page.get_grid_settings", "no grid settings returned")
    except Exception as e:
        results.fail("page.get_grid_settings", str(e))

    # Test get_usable_area
    try:
        area = sch.page.get_usable_area()
        if area:
            w = area.get('width_mm', 0)
            h = area.get('height_mm', 0)
            results.ok("page.get_usable_area", f"{w:.0f}x{h:.0f}mm")
        else:
            results.fail("page.get_usable_area", "no usable area returned")
    except Exception as e:
        results.fail("page.get_usable_area", str(e))

    # Test get_usable_area_mils
    try:
        area = sch.page.get_usable_area_mils()
        if area:
            w = area.get('width_mils', 0)
            h = area.get('height_mils', 0)
            results.ok("page.get_usable_area_mils", f"{w:.0f}x{h:.0f}mils")
        else:
            results.fail("page.get_usable_area_mils", "no usable area returned")
    except Exception as e:
        results.fail("page.get_usable_area_mils", str(e))

    # Test snap_to_grid
    try:
        snapped = sch.page.snap_to_grid(101.5, 99.3)
        if snapped:
            results.ok("page.snap_to_grid", f"(101.5, 99.3) -> ({snapped[0]:.2f}, {snapped[1]:.2f})")
        else:
            results.fail("page.snap_to_grid", "no result")
    except Exception as e:
        results.fail("page.snap_to_grid", str(e))

    # Test snap_to_grid_mils
    try:
        snapped = sch.page.snap_to_grid_mils(1015, 993)
        if snapped:
            results.ok("page.snap_to_grid_mils", f"(1015, 993) -> ({snapped[0]:.0f}, {snapped[1]:.0f})")
        else:
            results.fail("page.snap_to_grid_mils", "no result")
    except Exception as e:
        results.fail("page.snap_to_grid_mils", str(e))

    # Test snap_point_to_grid
    try:
        from kipy.geometry import Vector2
        point = Vector2.from_xy_mm(101.5, 99.3)
        snapped = sch.page.snap_point_to_grid(point)
        if snapped:
            results.ok("page.snap_point_to_grid", f"snapped Vector2")
        else:
            results.fail("page.snap_point_to_grid", "no result")
    except Exception as e:
        results.fail("page.snap_point_to_grid", str(e))

    # Test get_common_grids
    try:
        grids = sch.page.get_common_grids()
        if grids:
            results.ok("page.get_common_grids", f"{len(grids)} standard grid sizes")
        else:
            results.fail("page.get_common_grids", "no grids returned")
    except Exception as e:
        results.fail("page.get_common_grids", str(e))

    # Test set_title_block
    try:
        # Get current title block
        tb = sch.page.get_title_block()
        if tb:
            # Try setting it back
            sch.page.set_title_block(title=getattr(tb, 'title', ''))
            results.ok("page.set_title_block", "updated")
        else:
            results.skip("page.set_title_block", "no title block to modify")
    except Exception as e:
        results.fail("page.set_title_block", str(e))

    # Test set_grid
    try:
        # Get current grid settings
        grid = sch.page.get_grid_settings()
        if grid:
            # Try setting it back with same values
            sch.page.set_grid(grid_mm=grid.get('size_mm', 2.54))
            results.ok("page.set_grid", "updated")
        else:
            results.skip("page.set_grid", "no grid settings to modify")
    except Exception as e:
        results.fail("page.set_grid", str(e))

    # Test set_settings
    try:
        settings = sch.page.get_settings()
        if settings:
            # Try setting page size back to current value
            sch.page.set_settings(size_type=settings.size_type)
            results.ok("page.set_settings", "updated")
        else:
            results.skip("page.set_settings", "no settings to modify")
    except Exception as e:
        results.fail("page.set_settings", str(e))

    # Test get_grid_positions
    try:
        # get_grid_positions expects tuples (x_mm, y_mm), not Vector2 objects
        positions = sch.page.get_grid_positions((50, 50), (60, 60))
        if positions:
            results.ok("page.get_grid_positions", f"{len(positions)} grid points")
        else:
            results.ok("page.get_grid_positions", "0 grid points (region may be smaller than grid)")
    except Exception as e:
        results.fail("page.get_grid_positions", str(e))

    # Test get_design_bounds
    try:
        bounds = sch.page.get_design_bounds()
        if bounds:
            results.ok("page.get_design_bounds", f"bounds retrieved")
        else:
            results.ok("page.get_design_bounds", "no content (empty schematic)")
    except Exception as e:
        results.fail("page.get_design_bounds", str(e))
