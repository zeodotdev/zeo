# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for PCB selection operations.

Covers:
- clear_selection()
- get_selection()
- add_to_selection()
- remove_from_selection()
"""

from kipy.testing._base import TestResults


def test_pcb_selection(results: TestResults):
    """Test PCB selection operations."""
    results.section("PCB Selection")

    from kipy import KiCad

    kicad = KiCad()
    board = kicad.get_board()

    # Test clear_selection
    try:
        board.clear_selection()
        results.ok("clear_selection")
    except Exception as e:
        results.fail("clear_selection", str(e))

    # Test get_selection
    try:
        sel = board.get_selection()
        results.ok("get_selection", f"{len(sel)} items")
    except Exception as e:
        results.fail("get_selection", str(e))

    # Test add_to_selection and remove_from_selection
    footprints = board.get_footprints()
    if footprints:
        # Test add_to_selection
        try:
            board.add_to_selection(footprints[0])
            sel = board.get_selection()
            results.ok("add_to_selection", f"{len(sel)} items selected")
        except Exception as e:
            results.fail("add_to_selection", str(e))

        # Test remove_from_selection
        try:
            board.remove_from_selection(footprints[0])
            sel = board.get_selection()
            results.ok("remove_from_selection", f"{len(sel)} items remaining")
        except Exception as e:
            results.fail("remove_from_selection", str(e))

        # Test add multiple
        if len(footprints) >= 2:
            try:
                board.add_to_selection(footprints[:2])
                sel = board.get_selection()
                results.ok("add_to_selection_multiple", f"{len(sel)} items selected")
            except Exception as e:
                results.fail("add_to_selection_multiple", str(e))
        else:
            results.skip("add_to_selection_multiple", "need 2+ footprints")
    else:
        results.skip("add_to_selection", "no footprints on board")
        results.skip("remove_from_selection", "no footprints on board")
        results.skip("add_to_selection_multiple", "no footprints on board")

    # Test with tracks
    tracks = board.get_tracks()
    if tracks:
        try:
            board.clear_selection()
            board.add_to_selection(tracks[0])
            sel = board.get_selection()
            results.ok("add_to_selection_track", f"track selected, {len(sel)} items")
        except Exception as e:
            results.fail("add_to_selection_track", str(e))
    else:
        results.skip("add_to_selection_track", "no tracks on board")

    # Final clear
    try:
        board.clear_selection()
        sel = board.get_selection()
        if len(sel) == 0:
            results.ok("clear_selection_final", "selection empty")
        else:
            results.fail("clear_selection_final", f"still have {len(sel)} items")
    except Exception as e:
        results.fail("clear_selection_final", str(e))
