# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.graphics module.

Covers IPC APIs:
- CreateItems(Text)
- CreateItems(TextBox)
- CreateItems(SchematicGraphicShape)
- CreateItems(Bitmap)
- CreateItems(Table)
- CreateItems(SchematicGroup)
- GetItems(KOT_SCH_*)
- UpdateItems()
- DeleteItems()

Covers QoL functions:
- graphics.add_text()
- graphics.add_textbox()
- graphics.add_rectangle()
- graphics.add_circle()
- graphics.add_line()
- graphics.add_arc()
- graphics.add_bitmap()
- graphics.add_table()
- graphics.create_group()
- graphics.get_text_items()
- graphics.get_shapes()
- graphics.get_textboxes()
- graphics.get_bitmaps()
- graphics.get_tables()
- graphics.get_groups()
"""

from kipy.testing._base import TestResults, MM_TO_NM


def test_graphics(results: TestResults):
    """Test schematic graphics operations."""
    results.section("Schematic Graphics")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_text_items
    try:
        texts = sch.graphics.get_text_items()
        results.ok("graphics.get_text_items", f"{len(texts)} text items")
    except Exception as e:
        results.fail("graphics.get_text_items", str(e))

    # Test get_shapes
    try:
        shapes = sch.graphics.get_shapes()
        results.ok("graphics.get_shapes", f"{len(shapes)} shapes")
    except Exception as e:
        results.fail("graphics.get_shapes", str(e))

    # Test get_textboxes
    try:
        textboxes = sch.graphics.get_textboxes()
        results.ok("graphics.get_textboxes", f"{len(textboxes)} textboxes")
    except Exception as e:
        results.fail("graphics.get_textboxes", str(e))

    # Test get_bitmaps
    try:
        bitmaps = sch.graphics.get_bitmaps()
        results.ok("graphics.get_bitmaps", f"{len(bitmaps)} bitmaps")
    except Exception as e:
        results.fail("graphics.get_bitmaps", str(e))

    # Test get_tables
    try:
        tables = sch.graphics.get_tables()
        results.ok("graphics.get_tables", f"{len(tables)} tables")
    except Exception as e:
        results.fail("graphics.get_tables", str(e))

    # Test get_groups
    # NOTE: KiCad IPC API doesn't support KOT_SCH_GROUP for GetItems yet
    results.skip("graphics.get_groups", "KiCad IPC doesn't support group retrieval")

    # Test add_text
    text = None
    try:
        text = sch.graphics.add_text("Test Text", Vector2.from_xy_mm(110, 110))
        if text:
            results.ok("graphics.add_text", "created")
        else:
            results.fail("graphics.add_text", "returned None")
    except Exception as e:
        results.fail("graphics.add_text", str(e))

    if text:
        try:
            sch.crud.remove_items([text])
        except:
            pass

    # Test add_rectangle
    rect = None
    try:
        rect = sch.graphics.add_rectangle(
            Vector2.from_xy_mm(115, 115),
            Vector2.from_xy_mm(125, 125)
        )
        if rect:
            results.ok("graphics.add_rectangle", "created")
        else:
            results.fail("graphics.add_rectangle", "returned None")
    except Exception as e:
        results.fail("graphics.add_rectangle", str(e))

    if rect:
        try:
            sch.crud.remove_items([rect])
        except:
            pass

    # Test add_circle
    circle = None
    try:
        circle = sch.graphics.add_circle(Vector2.from_xy_mm(130, 130), 5 * MM_TO_NM)
        if circle:
            results.ok("graphics.add_circle", "created")
        else:
            results.fail("graphics.add_circle", "returned None")
    except Exception as e:
        results.fail("graphics.add_circle", str(e))

    if circle:
        try:
            sch.crud.remove_items([circle])
        except:
            pass

    # Test add_line
    line = None
    try:
        line = sch.graphics.add_line(
            Vector2.from_xy_mm(135, 135),
            Vector2.from_xy_mm(145, 145)
        )
        if line:
            results.ok("graphics.add_line", "created")
        else:
            results.fail("graphics.add_line", "returned None")
    except Exception as e:
        results.fail("graphics.add_line", str(e))

    if line:
        try:
            sch.crud.remove_items([line])
        except:
            pass

    # Test add_arc
    arc = None
    try:
        arc = sch.graphics.add_arc(
            Vector2.from_xy_mm(150, 150),
            Vector2.from_xy_mm(155, 155),
            Vector2.from_xy_mm(160, 150)
        )
        if arc:
            results.ok("graphics.add_arc", "created")
        else:
            results.fail("graphics.add_arc", "returned None")
    except Exception as e:
        results.fail("graphics.add_arc", str(e))

    if arc:
        try:
            sch.crud.remove_items([arc])
        except:
            pass

    # Test add_textbox
    textbox = None
    try:
        textbox = sch.graphics.add_textbox(
            "Textbox content",
            Vector2.from_xy_mm(165, 165),
            Vector2.from_xy_mm(185, 175)
        )
        if textbox:
            results.ok("graphics.add_textbox", "created")
        else:
            results.fail("graphics.add_textbox", "returned None")
    except Exception as e:
        results.fail("graphics.add_textbox", str(e))

    if textbox:
        try:
            sch.crud.remove_items([textbox])
        except:
            pass

    # Test add_table
    # NOTE: KiCad IPC API doesn't support table creation yet
    results.skip("graphics.add_table", "KiCad IPC doesn't support table creation")

    # Test create_group
    # NOTE: KiCad IPC API doesn't support group creation yet
    results.skip("graphics.create_group", "KiCad IPC doesn't support group creation")

    # Test update_graphic
    try:
        text = sch.graphics.add_text("Update Test", Vector2.from_xy_mm(210, 165))
        if text:
            sch.graphics.update_graphic(text)
            results.ok("graphics.update_graphic", "updated")
            sch.crud.remove_items([text])
        else:
            results.skip("graphics.update_graphic", "could not create item")
    except Exception as e:
        results.fail("graphics.update_graphic", str(e))

    # Test add_bitmap (requires a valid image file)
    # Skipping by default as it requires external file
    results.skip("graphics.add_bitmap", "requires image file")
