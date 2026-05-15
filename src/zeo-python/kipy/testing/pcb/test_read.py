# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for PCB read operations.

Covers:
- get_tracks()
- get_footprints()
- get_vias()
- get_pads()
- get_zones()
- get_shapes()
- get_text()
- get_dimensions()
- get_nets()
"""

from kipy.testing._base import TestResults


def test_pcb_read(results: TestResults):
    """Test PCB read operations."""
    results.section("PCB Read")

    from kipy import KiCad

    kicad = KiCad()
    board = kicad.get_board()

    # Test get_tracks
    try:
        tracks = board.get_tracks()
        results.ok("get_tracks", f"{len(tracks)} tracks")
    except Exception as e:
        results.fail("get_tracks", str(e))

    # Test get_footprints
    try:
        footprints = board.get_footprints()
        results.ok("get_footprints", f"{len(footprints)} footprints")
    except Exception as e:
        results.fail("get_footprints", str(e))

    # Test get_vias
    try:
        vias = board.get_vias()
        results.ok("get_vias", f"{len(vias)} vias")
    except Exception as e:
        results.fail("get_vias", str(e))

    # Test get_pads
    try:
        pads = board.get_pads()
        results.ok("get_pads", f"{len(pads)} pads")
    except Exception as e:
        results.fail("get_pads", str(e))

    # Test get_zones
    try:
        zones = board.get_zones()
        results.ok("get_zones", f"{len(zones)} zones")
    except Exception as e:
        results.fail("get_zones", str(e))

    # Test get_shapes (graphic shapes on board)
    try:
        shapes = board.get_shapes()
        results.ok("get_shapes", f"{len(shapes)} shapes")
    except Exception as e:
        results.fail("get_shapes", str(e))

    # Test get_text
    try:
        text_items = board.get_text()
        results.ok("get_text", f"{len(text_items)} text items")
    except Exception as e:
        results.fail("get_text", str(e))

    # Test get_dimensions
    try:
        dimensions = board.get_dimensions()
        results.ok("get_dimensions", f"{len(dimensions)} dimensions")
    except Exception as e:
        results.fail("get_dimensions", str(e))

    # Test get_nets
    try:
        nets = board.get_nets()
        results.ok("get_nets", f"{len(nets)} nets")
    except Exception as e:
        results.fail("get_nets", str(e))
