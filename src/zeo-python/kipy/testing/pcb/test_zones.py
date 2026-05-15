# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for PCB zone operations.

Covers:
- add_zone()
- get_zones()
- Zone manipulation
"""

from kipy.testing._base import TestResults, MM_TO_NM


def test_pcb_zones(results: TestResults):
    """Test PCB zone operations."""
    results.section("PCB Zones")

    from kipy import KiCad
    from kipy.geometry import PolyLine, PolyLineNode, PolygonWithHoles
    from kipy.proto.board.board_types_pb2 import BoardLayer

    kicad = KiCad()
    board = kicad.get_board()

    x0 = 170 * MM_TO_NM
    y0 = 100 * MM_TO_NM
    size = 5 * MM_TO_NM

    # Test add_zone
    zone = None
    try:
        zones_before = len(board.get_zones())

        # Create zone outline
        outline = PolyLine()
        outline.append(PolyLineNode.from_xy(x0, y0))
        outline.append(PolyLineNode.from_xy(x0 + size, y0))
        outline.append(PolyLineNode.from_xy(x0 + size, y0 + size))
        outline.append(PolyLineNode.from_xy(x0, y0 + size))
        outline.append(PolyLineNode.from_xy(x0, y0))

        polygon = PolygonWithHoles()
        polygon.outline = outline

        zone = board.add_zone(
            outline=polygon,
            layers=BoardLayer.BL_F_Cu,
            name="TEST_ZONE"
        )

        zones_after = len(board.get_zones())

        if zones_after > zones_before:
            results.ok("add_zone", "created")
        else:
            results.fail("add_zone", "zone count unchanged")
    except Exception as e:
        results.fail("add_zone", str(e))

    # Test add_zone with net
    try:
        nets = board.get_nets()
        if nets:
            net_code = nets[0].code if hasattr(nets[0], 'code') else None
            if net_code:
                outline = PolyLine()
                x1 = x0 + 10 * MM_TO_NM
                outline.append(PolyLineNode.from_xy(x1, y0))
                outline.append(PolyLineNode.from_xy(x1 + size, y0))
                outline.append(PolyLineNode.from_xy(x1 + size, y0 + size))
                outline.append(PolyLineNode.from_xy(x1, y0 + size))
                outline.append(PolyLineNode.from_xy(x1, y0))

                polygon = PolygonWithHoles()
                polygon.outline = outline

                zone2 = board.add_zone(
                    outline=polygon,
                    layers=BoardLayer.BL_F_Cu,
                    name="TEST_ZONE_NET",
                    net=net_code
                )
                if zone2:
                    results.ok("add_zone_with_net", "created with net")
                else:
                    results.fail("add_zone_with_net", "returned None")
            else:
                results.skip("add_zone_with_net", "no net code available")
        else:
            results.skip("add_zone_with_net", "no nets on board")
    except Exception as e:
        results.fail("add_zone_with_net", str(e))

    # Test add_zone on multiple layers
    try:
        outline = PolyLine()
        x2 = x0 + 20 * MM_TO_NM
        outline.append(PolyLineNode.from_xy(x2, y0))
        outline.append(PolyLineNode.from_xy(x2 + size, y0))
        outline.append(PolyLineNode.from_xy(x2 + size, y0 + size))
        outline.append(PolyLineNode.from_xy(x2, y0 + size))
        outline.append(PolyLineNode.from_xy(x2, y0))

        polygon = PolygonWithHoles()
        polygon.outline = outline

        # Create zone on both F.Cu and B.Cu
        zone3 = board.add_zone(
            outline=polygon,
            layers=[BoardLayer.BL_F_Cu, BoardLayer.BL_B_Cu],
            name="TEST_ZONE_MULTI"
        )
        if zone3:
            results.ok("add_zone_multi_layer", "created on F.Cu and B.Cu")
        else:
            results.fail("add_zone_multi_layer", "returned None")
    except Exception as e:
        results.fail("add_zone_multi_layer", str(e))

    # Test zone with hole
    try:
        # Outer outline
        outer = PolyLine()
        x3 = x0 + 30 * MM_TO_NM
        outer_size = 10 * MM_TO_NM
        outer.append(PolyLineNode.from_xy(x3, y0))
        outer.append(PolyLineNode.from_xy(x3 + outer_size, y0))
        outer.append(PolyLineNode.from_xy(x3 + outer_size, y0 + outer_size))
        outer.append(PolyLineNode.from_xy(x3, y0 + outer_size))
        outer.append(PolyLineNode.from_xy(x3, y0))

        # Inner hole
        hole = PolyLine()
        hole_offset = 2 * MM_TO_NM
        hole_size = 6 * MM_TO_NM
        hole.append(PolyLineNode.from_xy(x3 + hole_offset, y0 + hole_offset))
        hole.append(PolyLineNode.from_xy(x3 + hole_offset + hole_size, y0 + hole_offset))
        hole.append(PolyLineNode.from_xy(x3 + hole_offset + hole_size, y0 + hole_offset + hole_size))
        hole.append(PolyLineNode.from_xy(x3 + hole_offset, y0 + hole_offset + hole_size))
        hole.append(PolyLineNode.from_xy(x3 + hole_offset, y0 + hole_offset))

        polygon = PolygonWithHoles()
        polygon.outline = outer
        polygon.add_hole(hole)

        zone4 = board.add_zone(
            outline=polygon,
            layers=BoardLayer.BL_F_Cu,
            name="TEST_ZONE_HOLE"
        )
        if zone4:
            results.ok("add_zone_with_hole", "created with hole")
        else:
            results.fail("add_zone_with_hole", "returned None")
    except Exception as e:
        results.fail("add_zone_with_hole", str(e))

    # Test get_zones
    try:
        zones = board.get_zones()
        results.ok("get_zones", f"{len(zones)} zones total")
    except Exception as e:
        results.fail("get_zones", str(e))
