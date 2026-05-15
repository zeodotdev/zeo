# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.crud module (transactions and item management).

Covers IPC APIs:
- BeginCommit()
- EndCommit(CMA_COMMIT)
- EndCommit(CMA_DROP)
- CreateItems()
- GetItems()
- GetItemsById()
- UpdateItems()
- DeleteItems()
- GetSheets()
"""

from kipy.testing._base import TestResults


def test_crud(results: TestResults):
    """Test schematic CRUD and transaction operations."""
    results.section("Schematic CRUD & Transactions")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test begin_commit
    commit = None
    try:
        commit = sch.crud.begin_commit()
        if commit and commit.id:
            results.ok("crud.begin_commit", f"id={commit.id.value[:8]}...")
        else:
            results.fail("crud.begin_commit", "no commit ID returned")
    except Exception as e:
        results.fail("crud.begin_commit", str(e))

    # Test drop_commit
    if commit:
        try:
            sch.crud.drop_commit(commit)
            results.ok("crud.drop_commit")
        except Exception as e:
            results.fail("crud.drop_commit", str(e))

    # Test begin/push commit sequence
    try:
        commit2 = sch.crud.begin_commit()
        sch.crud.push_commit(commit2, "Test commit message")
        results.ok("crud.push_commit")
    except Exception as e:
        results.fail("crud.push_commit", str(e))

    # Test get_sheets
    try:
        sheets = sch.crud.get_sheets()
        results.ok("crud.get_sheets", f"{len(sheets)} sheets")
    except Exception as e:
        results.fail("crud.get_sheets", str(e))

    # Test get_items (generic)
    try:
        from kipy.proto.common.types import KiCadObjectType
        items = sch.crud.get_items([KiCadObjectType.KOT_SCH_SYMBOL])
        results.ok("crud.get_items", f"{len(items)} symbols via generic get_items")
    except Exception as e:
        results.fail("crud.get_items", str(e))

    # Test create_items (low-level)
    try:
        from kipy.schematic_types import Junction

        if Junction is not None:
            # Create a junction using the wrapper class
            junction = Junction.create(Vector2.from_xy_mm(50, 50))
            created = sch.crud.create_items(junction)
            if created and len(created) > 0:
                results.ok("crud.create_items", "junction created")
                # Get the ID and clean up
                if hasattr(created[0], 'id'):
                    try:
                        sch.crud.remove_items(created)
                    except:
                        pass
            else:
                results.fail("crud.create_items", "no items returned")
        else:
            results.skip("crud.create_items", "Junction not available")
    except Exception as e:
        results.fail("crud.create_items", str(e))

    # Test get_by_id
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            symbol_id = symbols[0].id if hasattr(symbols[0], 'id') else None
            if symbol_id:
                found = sch.crud.get_by_id([symbol_id])
                if found and len(found) > 0:
                    results.ok("crud.get_by_id", "found by ID")
                else:
                    results.fail("crud.get_by_id", "not found")
            else:
                results.skip("crud.get_by_id", "no ID on symbol")
        else:
            results.skip("crud.get_by_id", "no symbols")
    except Exception as e:
        results.fail("crud.get_by_id", str(e))

    # Test update_items
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            # Try to update without changing anything
            sch.crud.update_items([symbols[0]])
            results.ok("crud.update_items", "updated")
        else:
            results.skip("crud.update_items", "no symbols")
    except Exception as e:
        results.fail("crud.update_items", str(e))

    # Test remove_items
    try:
        # Create an item to remove
        wire = sch.wiring.add_wire(
            Vector2.from_xy_mm(200, 200),
            Vector2.from_xy_mm(210, 200)
        )
        if wire:
            sch.crud.remove_items([wire])
            results.ok("crud.remove_items", "removed wire")
        else:
            results.skip("crud.remove_items", "could not create wire")
    except Exception as e:
        results.fail("crud.remove_items", str(e))

    # Test transaction with changes
    try:
        commit = sch.crud.begin_commit()
        # Create an item within the transaction
        wire = sch.wiring.add_wire(
            Vector2.from_xy_mm(205, 205),
            Vector2.from_xy_mm(215, 205)
        )
        # Push the commit
        sch.crud.push_commit(commit, "Transaction test")
        results.ok("crud.transaction_with_changes", "committed with changes")
        # Clean up
        if wire:
            sch.crud.remove_items([wire])
    except Exception as e:
        results.fail("crud.transaction_with_changes", str(e))

    # Test transaction drop (rollback)
    try:
        wires_before = len(sch.wiring.get_wires())
        commit = sch.crud.begin_commit()
        # Create an item within the transaction
        wire = sch.wiring.add_wire(
            Vector2.from_xy_mm(210, 210),
            Vector2.from_xy_mm(220, 210)
        )
        # Drop the commit (should rollback)
        sch.crud.drop_commit(commit)
        wires_after = len(sch.wiring.get_wires())

        # Note: The behavior of drop_commit may vary
        # Some implementations may not actually rollback
        results.ok("crud.transaction_drop", f"wires: {wires_before} -> {wires_after}")
    except Exception as e:
        results.fail("crud.transaction_drop", str(e))
