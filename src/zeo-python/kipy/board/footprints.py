# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Footprint placement and manipulation operations.
"""

from typing import TYPE_CHECKING, Sequence, Optional, cast

from kipy.geometry import Vector2, Angle
from kipy.board_types import FootprintInstance
from kipy.proto.board import board_types_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


class FootprintOperations:
    """Footprint query and manipulation operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_all(self) -> Sequence[FootprintInstance]:
        """Get all footprints on the board."""
        return self._board.crud.get_footprints()

    def get_by_reference(self, reference: str) -> Optional[FootprintInstance]:
        """Find a footprint by its reference designator.

        Args:
            reference: Reference designator (e.g., "U1", "R5")

        Returns:
            The footprint if found, None otherwise
        """
        for fp in self.get_all():
            if fp.reference_field.text.value == reference:
                return fp
        return None

    def get_by_value(self, value: str) -> Sequence[FootprintInstance]:
        """Find footprints by their value field.

        Args:
            value: Value to search for

        Returns:
            List of matching footprints
        """
        return [fp for fp in self.get_all() if fp.value_field.text.value == value]

    def move(self, footprint: FootprintInstance, position: Vector2) -> FootprintInstance:
        """Move a footprint to a new position.

        Args:
            footprint: The footprint to move
            position: New position in nanometers

        Returns:
            The updated footprint
        """
        footprint.position = position
        updated = self._board.crud.update_items(footprint)
        return cast(FootprintInstance, updated[0]) if updated else footprint

    def rotate(self, footprint: FootprintInstance, angle: float) -> FootprintInstance:
        """Rotate a footprint by the given angle.

        Args:
            footprint: The footprint to rotate
            angle: Rotation angle in degrees

        Returns:
            The updated footprint
        """
        current_angle = footprint.orientation.degrees if footprint.orientation else 0.0
        footprint.orientation = Angle.from_degrees(current_angle + angle)
        updated = self._board.crud.update_items(footprint)
        return cast(FootprintInstance, updated[0]) if updated else footprint

    def flip(self, footprint: FootprintInstance) -> FootprintInstance:
        """Flip a footprint to the other side of the board.

        Args:
            footprint: The footprint to flip

        Returns:
            The updated footprint
        """
        current_layer = footprint.layer
        if current_layer == board_types_pb2.BoardLayer.BL_F_Cu:
            footprint.layer = board_types_pb2.BoardLayer.BL_B_Cu
        else:
            footprint.layer = board_types_pb2.BoardLayer.BL_F_Cu
        updated = self._board.crud.update_items(footprint)
        return cast(FootprintInstance, updated[0]) if updated else footprint
