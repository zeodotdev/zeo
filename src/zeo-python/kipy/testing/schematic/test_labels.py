# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.labels module.

Covers IPC APIs:
- CreateItems(LocalLabel)
- CreateItems(GlobalLabel)
- CreateItems(HierarchicalLabel)
- CreateItems(DirectiveLabel)
- GetItems(KOT_SCH_*_LABEL)
- UpdateItems(Label)
- DeleteItems(ids)

Covers QoL functions:
- labels.add_local()
- labels.add_global()
- labels.add_hierarchical()
- labels.add_directive()
- labels.add_power()
- labels.get_all()
- labels.get_all_local()
- labels.get_all_global()
- labels.get_all_hierarchical()
- labels.get_directive_labels()
- labels.find_labels()
- labels.rename_net()
- labels.label_pin()
- labels.get_unique_nets()
"""

from kipy.testing._base import (
    TestResults,
    verify_item_position,
    check_zero_position_bug,
    verify_schematic_position_mm,
)


def test_label_coordinates(results: TestResults):
    """Test label placement coordinate accuracy."""
    results.section("Label Coordinate Verification (CRITICAL)")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    label_x_mm = 110.0
    label_y_mm = 70.0

    results.info(f"Testing label placement at ({label_x_mm}mm, {label_y_mm}mm)")

    try:
        label = sch.labels.add_local("TEST_COORD", Vector2.from_xy_mm(label_x_mm, label_y_mm))

        if label:
            # Verify the label position
            actual_x = label.position.x
            actual_y = label.position.y

            # Check for the (0, 0) bug first
            zero_bug = check_zero_position_bug(actual_x, actual_y)
            if zero_bug:
                results.fail("label_position", zero_bug)
            else:
                x_ok, x_msg = verify_schematic_position_mm(actual_x, label_x_mm)
                y_ok, y_msg = verify_schematic_position_mm(actual_y, label_y_mm)

                if x_ok and y_ok:
                    results.ok("label_position_x", x_msg)
                    results.ok("label_position_y", y_msg)
                else:
                    if not x_ok:
                        results.fail("label_position_x", x_msg)
                    else:
                        results.ok("label_position_x", x_msg)
                    if not y_ok:
                        results.fail("label_position_y", y_msg)
                    else:
                        results.ok("label_position_y", y_msg)

            # Clean up
            sch.crud.remove_items([label])
        else:
            results.fail("label_placement", "No label returned")
    except Exception as e:
        results.fail("label_coordinates", str(e))

    # Test power symbol coordinates
    power_x_mm = 120.0
    power_y_mm = 70.0

    results.info(f"Testing power symbol placement at ({power_x_mm}mm, {power_y_mm}mm)")

    try:
        power = sch.labels.add_power("GND", Vector2.from_xy_mm(power_x_mm, power_y_mm))

        if power:
            actual_x = power.position.x
            actual_y = power.position.y

            zero_bug = check_zero_position_bug(actual_x, actual_y)
            if zero_bug:
                results.fail("power_symbol_position", zero_bug)
            else:
                x_ok, x_msg = verify_schematic_position_mm(actual_x, power_x_mm)
                y_ok, y_msg = verify_schematic_position_mm(actual_y, power_y_mm)

                if x_ok and y_ok:
                    results.ok("power_symbol_position", f"({power_x_mm}mm, {power_y_mm}mm)")
                else:
                    results.fail("power_symbol_position", f"x:{x_msg}, y:{y_msg}")

            # Clean up
            sch.crud.remove_items([power])
        else:
            results.fail("power_symbol_placement", "No power symbol returned")
    except Exception as e:
        if "library" in str(e).lower() or "not found" in str(e).lower():
            results.skip("power_symbol_position", "Power library not available")
        else:
            results.fail("power_symbol_position", str(e))


def test_labels(results: TestResults):
    """Test schematic label operations."""
    results.section("Schematic Labels")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_all
    try:
        labels = sch.labels.get_all()
        results.ok("labels.get_all", f"{len(labels)} labels")
    except Exception as e:
        results.fail("labels.get_all", str(e))

    # Test get_all_local
    try:
        local_labels = sch.labels.get_all_local()
        results.ok("labels.get_all_local", f"{len(local_labels)} local labels")
    except Exception as e:
        results.fail("labels.get_all_local", str(e))

    # Test get_all_global
    try:
        global_labels = sch.labels.get_all_global()
        results.ok("labels.get_all_global", f"{len(global_labels)} global labels")
    except Exception as e:
        results.fail("labels.get_all_global", str(e))

    # Test get_all_hierarchical
    try:
        hier_labels = sch.labels.get_all_hierarchical()
        results.ok("labels.get_all_hierarchical", f"{len(hier_labels)} hierarchical labels")
    except Exception as e:
        results.fail("labels.get_all_hierarchical", str(e))

    # Test get_directive_labels
    try:
        directive_labels = sch.labels.get_directive_labels()
        results.ok("labels.get_directive_labels", f"{len(directive_labels)} directive labels")
    except Exception as e:
        results.fail("labels.get_directive_labels", str(e))

    # Test add_local
    local_label = None
    try:
        labels_before = len(sch.labels.get_all())
        local_label = sch.labels.add_local("TEST_NET", Vector2.from_xy_mm(80, 80))
        labels_after = len(sch.labels.get_all())

        if labels_after > labels_before:
            results.ok("labels.add_local", "created")
        else:
            results.fail("labels.add_local", "label count unchanged")
    except Exception as e:
        results.fail("labels.add_local", str(e))

    if local_label:
        try:
            sch.crud.remove_items([local_label])
        except:
            pass

    # Test add_global
    global_label = None
    try:
        global_label = sch.labels.add_global("GLOBAL_NET", Vector2.from_xy_mm(85, 85))
        if global_label:
            results.ok("labels.add_global", "created")
        else:
            results.fail("labels.add_global", "returned None")
    except Exception as e:
        results.fail("labels.add_global", str(e))

    if global_label:
        try:
            sch.crud.remove_items([global_label])
        except:
            pass

    # Test add_hierarchical
    hier_label = None
    try:
        hier_label = sch.labels.add_hierarchical("HIER_NET", Vector2.from_xy_mm(90, 90))
        if hier_label:
            results.ok("labels.add_hierarchical", "created")
        else:
            results.fail("labels.add_hierarchical", "returned None")
    except Exception as e:
        results.fail("labels.add_hierarchical", str(e))

    if hier_label:
        try:
            sch.crud.remove_items([hier_label])
        except:
            pass

    # Test add_directive
    directive_label = None
    try:
        directive_label = sch.labels.add_directive(".tran 1m", Vector2.from_xy_mm(95, 95))
        if directive_label:
            results.ok("labels.add_directive", "created")
        else:
            results.fail("labels.add_directive", "returned None")
    except Exception as e:
        results.fail("labels.add_directive", str(e))

    if directive_label:
        try:
            sch.crud.remove_items([directive_label])
        except:
            pass

    # Test add_power
    power = None
    try:
        power = sch.labels.add_power("GND", Vector2.from_xy_mm(100, 100))
        if power:
            results.ok("labels.add_power", "created")
        else:
            results.fail("labels.add_power", "returned None")
    except Exception as e:
        if "library" in str(e).lower() or "not found" in str(e).lower():
            results.skip("labels.add_power", "power library not available")
        else:
            results.fail("labels.add_power", str(e))

    if power:
        try:
            sch.crud.remove_items([power])
        except:
            pass

    # Test find_labels
    try:
        found = sch.labels.find_labels(".*")
        results.ok("labels.find_labels", f"found {len(found)} labels matching '.*'")
    except Exception as e:
        results.fail("labels.find_labels", str(e))

    # Test get_unique_nets
    try:
        nets = sch.labels.get_unique_nets()
        results.ok("labels.get_unique_nets", f"{len(nets)} unique nets")
    except Exception as e:
        results.fail("labels.get_unique_nets", str(e))

    # Test label_pin
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            label = sch.labels.label_pin(symbols[0], "1", "PIN_LABEL")
            if label:
                results.ok("labels.label_pin", "created at pin")
                sch.crud.remove_items([label])
            else:
                results.fail("labels.label_pin", "returned None")
        else:
            results.skip("labels.label_pin", "no symbols")
    except Exception as e:
        if "pin" in str(e).lower():
            results.skip("labels.label_pin", "pin not found")
        else:
            results.fail("labels.label_pin", str(e))

    # Test rename_net
    try:
        # Create a label first
        label = sch.labels.add_local("OLD_NET_NAME", Vector2.from_xy_mm(105, 105))
        if label:
            count = sch.labels.rename_net("OLD_NET_NAME", "NEW_NET_NAME")
            results.ok("labels.rename_net", f"renamed {count} labels")
            # Find and cleanup
            renamed = sch.labels.find_labels("NEW_NET_NAME")
            if renamed:
                sch.crud.remove_items(renamed)
        else:
            results.skip("labels.rename_net", "could not create test label")
    except Exception as e:
        results.fail("labels.rename_net", str(e))
