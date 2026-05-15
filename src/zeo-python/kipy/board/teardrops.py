# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Teardrop settings operations.
"""

from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


@dataclass
class TeardropParameters:
    """Parameters for a teardrop shape type (round, rect, or track-to-track)."""

    best_length_ratio: Optional[float] = None
    max_length_nm: Optional[int] = None
    best_width_ratio: Optional[float] = None
    max_width_nm: Optional[int] = None
    curved_edges: Optional[bool] = None
    allow_two_segments: Optional[bool] = None
    prefer_zone_connection: Optional[bool] = None
    track_width_limit_ratio: Optional[float] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.TeardropParameters) -> "TeardropParameters":
        """Create from protobuf message."""
        return cls(
            best_length_ratio=proto.best_length_ratio if proto.HasField("best_length_ratio") else None,
            max_length_nm=proto.max_length_nm if proto.HasField("max_length_nm") else None,
            best_width_ratio=proto.best_width_ratio if proto.HasField("best_width_ratio") else None,
            max_width_nm=proto.max_width_nm if proto.HasField("max_width_nm") else None,
            curved_edges=proto.curved_edges if proto.HasField("curved_edges") else None,
            allow_two_segments=proto.allow_two_segments if proto.HasField("allow_two_segments") else None,
            prefer_zone_connection=proto.prefer_zone_connection if proto.HasField("prefer_zone_connection") else None,
            track_width_limit_ratio=proto.track_width_limit_ratio if proto.HasField("track_width_limit_ratio") else None,
        )

    def to_proto(self, proto: Optional[board_commands_pb2.TeardropParameters] = None) -> board_commands_pb2.TeardropParameters:
        """Convert to protobuf message."""
        if proto is None:
            proto = board_commands_pb2.TeardropParameters()
        if self.best_length_ratio is not None:
            proto.best_length_ratio = self.best_length_ratio
        if self.max_length_nm is not None:
            proto.max_length_nm = self.max_length_nm
        if self.best_width_ratio is not None:
            proto.best_width_ratio = self.best_width_ratio
        if self.max_width_nm is not None:
            proto.max_width_nm = self.max_width_nm
        if self.curved_edges is not None:
            proto.curved_edges = self.curved_edges
        if self.allow_two_segments is not None:
            proto.allow_two_segments = self.allow_two_segments
        if self.prefer_zone_connection is not None:
            proto.prefer_zone_connection = self.prefer_zone_connection
        if self.track_width_limit_ratio is not None:
            proto.track_width_limit_ratio = self.track_width_limit_ratio
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary (excludes None values)."""
        result = {}
        if self.best_length_ratio is not None:
            result["best_length_ratio"] = self.best_length_ratio
        if self.max_length_nm is not None:
            result["max_length_nm"] = self.max_length_nm
        if self.best_width_ratio is not None:
            result["best_width_ratio"] = self.best_width_ratio
        if self.max_width_nm is not None:
            result["max_width_nm"] = self.max_width_nm
        if self.curved_edges is not None:
            result["curved_edges"] = self.curved_edges
        if self.allow_two_segments is not None:
            result["allow_two_segments"] = self.allow_two_segments
        if self.prefer_zone_connection is not None:
            result["prefer_zone_connection"] = self.prefer_zone_connection
        if self.track_width_limit_ratio is not None:
            result["track_width_limit_ratio"] = self.track_width_limit_ratio
        return result


@dataclass
class TeardropSettings:
    """Complete teardrop settings (global flags + per-type parameters)."""

    # Global target enable flags
    target_vias: bool = False
    target_pth_pads: bool = False
    target_smd_pads: bool = False
    target_track_to_track: bool = False
    round_shapes_only: bool = False

    # Per-type parameters
    round_shapes: Optional[TeardropParameters] = None
    rect_shapes: Optional[TeardropParameters] = None
    track_to_track: Optional[TeardropParameters] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.TeardropSettings) -> "TeardropSettings":
        """Create from protobuf message."""
        return cls(
            target_vias=proto.target_vias,
            target_pth_pads=proto.target_pth_pads,
            target_smd_pads=proto.target_smd_pads,
            target_track_to_track=proto.target_track_to_track,
            round_shapes_only=proto.round_shapes_only,
            round_shapes=TeardropParameters.from_proto(proto.round_shapes),
            rect_shapes=TeardropParameters.from_proto(proto.rect_shapes),
            track_to_track=TeardropParameters.from_proto(proto.track_to_track),
        )

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "target_vias": self.target_vias,
            "target_pth_pads": self.target_pth_pads,
            "target_smd_pads": self.target_smd_pads,
            "target_track_to_track": self.target_track_to_track,
            "round_shapes_only": self.round_shapes_only,
            "round_shapes": self.round_shapes.to_dict() if self.round_shapes else {},
            "rect_shapes": self.rect_shapes.to_dict() if self.rect_shapes else {},
            "track_to_track": self.track_to_track.to_dict() if self.track_to_track else {},
        }


class TeardropOperations:
    """Teardrop settings operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> TeardropSettings:
        """Get teardrop settings.

        Returns:
            TeardropSettings with all teardrop configuration
        """
        cmd = board_commands_pb2.GetTeardropSettings()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.TeardropSettingsResponse)
        return TeardropSettings.from_proto(response.settings)

    def set(
        self,
        target_vias: Optional[bool] = None,
        target_pth_pads: Optional[bool] = None,
        target_smd_pads: Optional[bool] = None,
        target_track_to_track: Optional[bool] = None,
        round_shapes_only: Optional[bool] = None,
        round_shapes: Optional[TeardropParameters] = None,
        rect_shapes: Optional[TeardropParameters] = None,
        track_to_track: Optional[TeardropParameters] = None,
    ) -> TeardropSettings:
        """Set teardrop settings (partial update - only provided values are changed).

        Args:
            target_vias: Create teardrops for vias
            target_pth_pads: Create teardrops for PTH pads
            target_smd_pads: Create teardrops for SMD pads
            target_track_to_track: Create teardrops at track-to-track junctions
            round_shapes_only: Only apply to round shapes
            round_shapes: Parameters for round pads/vias
            rect_shapes: Parameters for rectangular pads
            track_to_track: Parameters for track-to-track junctions

        Returns:
            Updated TeardropSettings
        """
        cmd = board_commands_pb2.SetTeardropSettings()
        cmd.board.CopyFrom(self._board._doc)

        # Set global flags if provided
        if target_vias is not None:
            cmd.target_vias = target_vias
        if target_pth_pads is not None:
            cmd.target_pth_pads = target_pth_pads
        if target_smd_pads is not None:
            cmd.target_smd_pads = target_smd_pads
        if target_track_to_track is not None:
            cmd.target_track_to_track = target_track_to_track
        if round_shapes_only is not None:
            cmd.round_shapes_only = round_shapes_only

        # Set per-type parameters if provided
        if round_shapes is not None:
            round_shapes.to_proto(cmd.round_shapes)
        if rect_shapes is not None:
            rect_shapes.to_proto(cmd.rect_shapes)
        if track_to_track is not None:
            track_to_track.to_proto(cmd.track_to_track)

        response = self._board._kicad.send(cmd, board_commands_pb2.TeardropSettingsResponse)
        return TeardropSettings.from_proto(response.settings)
