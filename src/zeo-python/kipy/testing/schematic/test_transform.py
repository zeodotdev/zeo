# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.transform module.

Covers IPC APIs:
- GetBoundingBox()
- UpdateItems() (for position/rotation changes)

Covers QoL functions:
- transform.get_bounding_box()
- transform.move()
- transform.move_to()
- transform.rotate()
- transform.mirror()
- transform.align()
- transform.distribute()
- transform.snap_to_grid()
"""

from kipy.testing._base import TestResults, MM_TO_NM


def test_transform(results: TestResults):
    """Test schematic transform operations."""
    results.section("Schematic Transform")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Get some items to transform
    symbols = sch.symbols.get_all()

    # Test get_bounding_box
    try:
        if symbols:
            bbox = sch.transform.get_bounding_box(symbols[:1])
            if bbox:
                results.ok("transform.get_bounding_box", "retrieved")
            else:
                results.fail("transform.get_bounding_box", "no bounding box returned")
        else:
            results.skip("transform.get_bounding_box", "no symbols")
    except Exception as e:
        results.fail("transform.get_bounding_box", str(e))

    # Create test items for transform operations
    test_items = []
    try:
        # Create a symbol for transform testing
        sym = sch.symbols.add("Device:R", Vector2.from_xy_mm(240, 80))
        if sym:
            test_items.append(sym)
    except:
        pass

    if test_items:
        # Test move
        try:
            # API uses delta_x_mm and delta_y_mm (in mm, not nm)
            sch.transform.move(test_items, delta_x_mm=5, delta_y_mm=5)
            results.ok("transform.move", "moved by (5mm, 5mm)")
        except Exception as e:
            results.fail("transform.move", str(e))

        # Test move_to
        try:
            sch.transform.move_to(test_items, Vector2.from_xy_mm(250, 90))
            results.ok("transform.move_to", "moved to (250mm, 90mm)")
        except Exception as e:
            results.fail("transform.move_to", str(e))

        # Test rotate
        try:
            # API uses angle_degrees, not angle
            sch.transform.rotate(test_items, angle_degrees=90)
            results.ok("transform.rotate", "rotated 90°")
        except Exception as e:
            results.fail("transform.rotate", str(e))

        # Test mirror
        try:
            sch.transform.mirror(test_items, axis="x")
            results.ok("transform.mirror", "mirrored on X axis")
        except Exception as e:
            results.fail("transform.mirror", str(e))

        # Test snap_to_grid
        try:
            sch.transform.snap_to_grid(test_items)
            results.ok("transform.snap_to_grid", "snapped")
        except Exception as e:
            results.fail("transform.snap_to_grid", str(e))

        # Clean up test items
        try:
            sch.crud.remove_items(test_items)
        except:
            pass
    else:
        results.skip("transform.move", "could not create test items")
        results.skip("transform.move_to", "could not create test items")
        results.skip("transform.rotate", "could not create test items")
        results.skip("transform.mirror", "could not create test items")
        results.skip("transform.snap_to_grid", "could not create test items")

    # Test align (needs multiple items)
    try:
        # Create multiple items
        items = []
        for i in range(3):
            sym = sch.symbols.add("Device:R", Vector2.from_xy_mm(260 + i * 10, 80 + i * 5))
            if sym:
                items.append(sym)

        if len(items) >= 2:
            sch.transform.align(items, alignment="left")
            results.ok("transform.align", "aligned left")

            # Clean up
            sch.crud.remove_items(items)
        else:
            results.skip("transform.align", "could not create enough items")
    except Exception as e:
        if "library" in str(e).lower():
            results.skip("transform.align", "library not available")
        else:
            results.fail("transform.align", str(e))

    # Test distribute (needs multiple items)
    try:
        # Create multiple items
        items = []
        for i in range(3):
            sym = sch.symbols.add("Device:R", Vector2.from_xy_mm(280 + i * 10, 100))
            if sym:
                items.append(sym)

        if len(items) >= 3:
            sch.transform.distribute(items, direction="horizontal")
            results.ok("transform.distribute", "distributed horizontally")

            # Clean up
            sch.crud.remove_items(items)
        else:
            results.skip("transform.distribute", "could not create enough items")
    except Exception as e:
        if "library" in str(e).lower():
            results.skip("transform.distribute", "library not available")
        else:
            results.fail("transform.distribute", str(e))
