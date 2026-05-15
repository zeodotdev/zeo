# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.sheets module.

Covers IPC APIs:
- CreateSheet()
- DeleteSheet()
- GetSheetHierarchy()
- GetCurrentSheet()
- NavigateToSheet()
- GetSheetProperties()
- SetSheetProperties()
- CreateSheetPin()
- DeleteSheetPin()
- GetSheetPins()
- SyncSheetPins()
"""

from kipy.testing._base import TestResults


def test_sheets(results: TestResults):
    """Test schematic sheet operations."""
    results.section("Schematic Sheets")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_hierarchy
    try:
        hierarchy = sch.sheets.get_hierarchy()
        if hierarchy:
            child_count = len(hierarchy.children) if hasattr(hierarchy, 'children') and hierarchy.children else 0
            name = hierarchy.name if hasattr(hierarchy, 'name') else 'root'
            results.ok("sheets.get_hierarchy", f"root={name}, children={child_count}")
        else:
            results.ok("sheets.get_hierarchy", "empty hierarchy")
    except Exception as e:
        results.fail("sheets.get_hierarchy", str(e))

    # Test get_current
    try:
        path, info = sch.sheets.get_current()
        results.ok("sheets.get_current", f"path={path}")
    except Exception as e:
        results.fail("sheets.get_current", str(e))

    # Test get_properties (for root sheet)
    try:
        sheets = sch.crud.get_sheets()
        if sheets:
            sheet_id = sheets[0].id if hasattr(sheets[0], 'id') else None
            if sheet_id:
                props = sch.sheets.get_properties(sheet_id)
                if props:
                    results.ok("sheets.get_properties", "retrieved")
                else:
                    results.fail("sheets.get_properties", "no properties returned")
            else:
                results.skip("sheets.get_properties", "no sheet ID")
        else:
            results.skip("sheets.get_properties", "no sheets")
    except Exception as e:
        results.fail("sheets.get_properties", str(e))

    # Test navigate_to (navigate to root)
    try:
        current_path, _ = sch.sheets.get_current()
        sch.sheets.navigate_to(current_path)
        results.ok("sheets.navigate_to", f"navigated to {current_path}")
    except Exception as e:
        results.fail("sheets.navigate_to", str(e))

    # Test create (creates a new subsheet)
    new_sheet = None
    try:
        from kipy.geometry import Vector2
        new_sheet = sch.sheets.create(
            name="TestSubsheet",
            filename="test_subsheet.kicad_sch",
            position=Vector2.from_xy_mm(180, 50),
            size=Vector2.from_xy_mm(40, 30)
        )
        if new_sheet:
            results.ok("sheets.create", "created subsheet")
        else:
            results.fail("sheets.create", "returned None")
    except Exception as e:
        results.fail("sheets.create", str(e))

    if new_sheet:
        # CreateSheetResponse has sheet_id (a KIID proto), access its value
        sheet_id = new_sheet.sheet_id.value if hasattr(new_sheet, 'sheet_id') else None

        # Test get_pins
        try:
            if sheet_id:
                pins = sch.sheets.get_pins(sheet_id)
                results.ok("sheets.get_pins", f"{len(pins.pins) if hasattr(pins, 'pins') else 0} pins")
            else:
                results.skip("sheets.get_pins", "no sheet ID")
        except Exception as e:
            results.fail("sheets.get_pins", str(e))

        # Test create_pin
        try:
            if sheet_id:
                from kipy.geometry import Vector2
                pin = sch.sheets.create_pin(
                    sheet_id=sheet_id,
                    name="TEST_PIN",
                    position=Vector2.from_xy_mm(180, 55),
                    shape=0  # LabelShape enum value (0 = default)
                )
                if pin:
                    results.ok("sheets.create_pin", "created")
                    # Test delete_pin - CreateSheetPinResponse has pin_id
                    try:
                        pin_id = pin.pin_id.value if hasattr(pin, 'pin_id') else None
                        if pin_id:
                            sch.sheets.delete_pin(pin_id)
                            results.ok("sheets.delete_pin", "deleted")
                        else:
                            results.skip("sheets.delete_pin", "no pin ID")
                    except Exception as e:
                        results.fail("sheets.delete_pin", str(e))
                else:
                    results.fail("sheets.create_pin", "returned None")
            else:
                results.skip("sheets.create_pin", "no sheet ID")
        except Exception as e:
            results.fail("sheets.create_pin", str(e))

        # Test sync_pins
        try:
            if sheet_id:
                sch.sheets.sync_pins(sheet_id)
                results.ok("sheets.sync_pins", "synced")
            else:
                results.skip("sheets.sync_pins", "no sheet ID")
        except Exception as e:
            results.fail("sheets.sync_pins", str(e))

        # Test set_properties
        try:
            if sheet_id:
                sch.sheets.set_properties(sheet_id, name="RenamedSheet")
                results.ok("sheets.set_properties", "updated")
            else:
                results.skip("sheets.set_properties", "no sheet ID")
        except Exception as e:
            results.fail("sheets.set_properties", str(e))

        # Test delete
        try:
            if sheet_id:
                sch.sheets.delete(sheet_id)
                results.ok("sheets.delete", "deleted subsheet")
            else:
                results.skip("sheets.delete", "no sheet ID")
        except Exception as e:
            results.fail("sheets.delete", str(e))
    else:
        results.skip("sheets.get_pins", "no sheet created")
        results.skip("sheets.create_pin", "no sheet created")
        results.skip("sheets.delete_pin", "no sheet created")
        results.skip("sheets.sync_pins", "no sheet created")
        results.skip("sheets.set_properties", "no sheet created")
        results.skip("sheets.delete", "no sheet created")
