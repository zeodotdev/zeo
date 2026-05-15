# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for PCB routing operations.

Covers:
- route_track()
- add_via()
- Coordinate verification
"""

from kipy.testing._base import TestResults, verify_position_mm, MM_TO_NM


def test_pcb_coordinates(results: TestResults):
    """Test PCB coordinate accuracy."""
    results.section("PCB Coordinate Verification")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    board = kicad.get_board()

    # Test position: 150mm, 100mm
    test_x_mm = 150.0
    test_y_mm = 100.0

    results.info(f"Testing placement at ({test_x_mm}mm, {test_y_mm}mm)")

    # Test via placement coordinate verification
    try:
        via = board.add_via(
            position=Vector2.from_xy(int(test_x_mm * MM_TO_NM), int(test_y_mm * MM_TO_NM)),
            diameter=int(0.8 * MM_TO_NM),
            drill=int(0.4 * MM_TO_NM),
        )

        if via:
            actual_x = via.position.x
            actual_y = via.position.y

            x_ok, x_msg = verify_position_mm(actual_x, test_x_mm)
            y_ok, y_msg = verify_position_mm(actual_y, test_y_mm)

            if x_ok and y_ok:
                results.ok("pcb_via_position", f"({test_x_mm}mm, {test_y_mm}mm)")
            else:
                results.fail("pcb_via_position", f"x:{x_msg}, y:{y_msg}")
        else:
            results.fail("pcb_via_placement", "No via returned")
    except Exception as e:
        results.fail("pcb_via_position", str(e))


def test_pcb_routing(results: TestResults):
    """Test PCB routing operations."""
    results.section("PCB Routing")

    from kipy import KiCad
    from kipy.geometry import Vector2

    kicad = KiCad()
    board = kicad.get_board()

    test_x = 150 * MM_TO_NM
    test_y = 100 * MM_TO_NM

    # Test route_track
    try:
        tracks_before = len(board.get_tracks())
        points = [
            Vector2.from_xy(test_x, test_y),
            Vector2.from_xy(test_x + 10 * MM_TO_NM, test_y),
        ]
        tracks = board.route_track(points=points, width=int(0.25 * MM_TO_NM))
        tracks_after = len(board.get_tracks())

        if tracks_after > tracks_before:
            results.ok("route_track", f"{tracks_after - tracks_before} segments")
        else:
            results.fail("route_track", "track count unchanged")
    except Exception as e:
        results.fail("route_track", str(e))

    # Test route_track with layer
    try:
        from kipy.proto.board.board_types_pb2 import BoardLayer
        points = [
            Vector2.from_xy(test_x + 20 * MM_TO_NM, test_y),
            Vector2.from_xy(test_x + 30 * MM_TO_NM, test_y),
        ]
        tracks = board.route_track(
            points=points,
            width=int(0.25 * MM_TO_NM),
            layer=BoardLayer.BL_F_Cu
        )
        if tracks:
            results.ok("route_track_with_layer", f"{len(tracks)} segments on F.Cu")
        else:
            results.fail("route_track_with_layer", "no tracks returned")
    except Exception as e:
        results.fail("route_track_with_layer", str(e))

    # Test add_via
    try:
        vias_before = len(board.get_vias())
        via = board.add_via(
            position=Vector2.from_xy(test_x + 5 * MM_TO_NM, test_y + 5 * MM_TO_NM),
            diameter=int(0.8 * MM_TO_NM),
            drill=int(0.4 * MM_TO_NM),
        )
        vias_after = len(board.get_vias())

        if vias_after > vias_before:
            results.ok("add_via", "created")
        else:
            results.fail("add_via", "via count unchanged")
    except Exception as e:
        results.fail("add_via", str(e))

    # Test add_via with net
    try:
        nets = board.get_nets()
        if nets:
            net_code = nets[0].code if hasattr(nets[0], 'code') else None
            if net_code:
                via = board.add_via(
                    position=Vector2.from_xy(test_x + 15 * MM_TO_NM, test_y + 5 * MM_TO_NM),
                    diameter=int(0.8 * MM_TO_NM),
                    drill=int(0.4 * MM_TO_NM),
                    net=net_code
                )
                if via:
                    results.ok("add_via_with_net", "created with net")
                else:
                    results.fail("add_via_with_net", "returned None")
            else:
                results.skip("add_via_with_net", "no net code available")
        else:
            results.skip("add_via_with_net", "no nets on board")
    except Exception as e:
        results.fail("add_via_with_net", str(e))

    # Test multi-point route
    try:
        tracks_before = len(board.get_tracks())
        points = [
            Vector2.from_xy(test_x + 40 * MM_TO_NM, test_y),
            Vector2.from_xy(test_x + 50 * MM_TO_NM, test_y),
            Vector2.from_xy(test_x + 50 * MM_TO_NM, test_y + 10 * MM_TO_NM),
            Vector2.from_xy(test_x + 60 * MM_TO_NM, test_y + 10 * MM_TO_NM),
        ]
        tracks = board.route_track(points=points, width=int(0.25 * MM_TO_NM))
        tracks_after = len(board.get_tracks())

        if tracks_after > tracks_before:
            segments = tracks_after - tracks_before
            results.ok("route_track_multi_point", f"{segments} segments (3 expected)")
        else:
            results.fail("route_track_multi_point", "track count unchanged")
    except Exception as e:
        results.fail("route_track_multi_point", str(e))
