# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Track and via routing operations.
"""

from typing import TYPE_CHECKING, List, Optional, Sequence, Union, cast

from kipy.geometry import Vector2
from kipy.board_types import Track, ArcTrack, Via, Net
from kipy.proto.board import board_types_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


class RoutingOperations:
    """Track and via routing operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def route(
        self,
        points: Sequence[Vector2],
        width: int,
        layer: board_types_pb2.BoardLayer.ValueType = board_types_pb2.BoardLayer.BL_F_Cu,
        net: Optional[str] = None,
    ) -> List[Track]:
        """Create a track path connecting multiple points.

        Args:
            points: Sequence of points to connect with track segments
            width: Track width in nanometers
            layer: Board layer for the track
            net: Optional net name to assign to the tracks

        Returns:
            List of created Track objects
        """
        if len(points) < 2:
            return []

        net_obj = None
        if net:
            net_obj = Net()
            net_obj.name = net

        tracks = []
        for i in range(len(points) - 1):
            track = Track()
            track.start = points[i]
            track.end = points[i + 1]
            track.width = width
            track.layer = layer
            if net_obj:
                track.net = net_obj
            tracks.append(track)

        created = self._board.crud.create_items(tracks)
        return [cast(Track, t) for t in created] if created else tracks

    def add_track(
        self,
        start: Vector2,
        end: Vector2,
        width: int,
        layer: board_types_pb2.BoardLayer.ValueType = board_types_pb2.BoardLayer.BL_F_Cu,
        net: Optional[str] = None,
    ) -> Track:
        """Create a single track segment.

        Args:
            start: Start position in nanometers
            end: End position in nanometers
            width: Track width in nanometers
            layer: Board layer for the track
            net: Optional net name

        Returns:
            The created Track object
        """
        tracks = self.route([start, end], width, layer, net)
        return tracks[0] if tracks else Track()

    def add_via(
        self,
        position: Vector2,
        diameter: int,
        drill: int,
        net: Optional[str] = None,
        via_type: board_types_pb2.ViaType.ValueType = board_types_pb2.ViaType.VT_THROUGH,
    ) -> Via:
        """Add a via at the given position.

        Args:
            position: Via position in nanometers
            diameter: Via pad diameter in nanometers
            drill: Via drill diameter in nanometers
            net: Optional net name to assign to the via
            via_type: Type of via (VT_THROUGH, VT_BLIND_BURIED, VT_MICRO)

        Returns:
            The created Via object
        """
        via = Via()
        via.position = position
        via.type = via_type
        via.diameter = diameter
        via.drill_diameter = drill

        if net:
            net_obj = Net()
            net_obj.name = net
            via.net = net_obj

        created = self._board.crud.create_items(via)
        if created:
            return cast(Via, created[0])
        return via

    def get_tracks(self) -> Sequence[Union[Track, ArcTrack]]:
        """Get all tracks (straight and arc) on the board."""
        return self._board.crud.get_tracks()

    def get_vias(self) -> Sequence[Via]:
        """Get all vias on the board."""
        return self._board.crud.get_vias()
