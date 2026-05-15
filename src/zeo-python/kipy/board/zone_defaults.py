# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Zone defaults operations.
"""

from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Enum mappings
class CornerSmoothingMode:
    NONE = board_commands_pb2.ZCS_NONE
    CHAMFER = board_commands_pb2.ZCS_CHAMFER
    FILLET = board_commands_pb2.ZCS_FILLET

    _TO_STRING = {
        board_commands_pb2.ZCS_NONE: 'none',
        board_commands_pb2.ZCS_CHAMFER: 'chamfer',
        board_commands_pb2.ZCS_FILLET: 'fillet',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'none')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.NONE)


class ZonePadConnection:
    THERMAL = board_commands_pb2.ZPC_THERMAL
    SOLID = board_commands_pb2.ZPC_SOLID
    NONE = board_commands_pb2.ZPC_NONE
    THT_THERMAL = board_commands_pb2.ZPC_THT_THERMAL

    _TO_STRING = {
        board_commands_pb2.ZPC_THERMAL: 'thermal',
        board_commands_pb2.ZPC_SOLID: 'solid',
        board_commands_pb2.ZPC_NONE: 'none',
        board_commands_pb2.ZPC_THT_THERMAL: 'tht_thermal',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'thermal')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.THERMAL)


class ZoneIslandRemoval:
    ALWAYS = board_commands_pb2.ZIR_ALWAYS
    NEVER = board_commands_pb2.ZIR_NEVER
    AREA = board_commands_pb2.ZIR_AREA

    _TO_STRING = {
        board_commands_pb2.ZIR_ALWAYS: 'always',
        board_commands_pb2.ZIR_NEVER: 'never',
        board_commands_pb2.ZIR_AREA: 'area',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'always')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.ALWAYS)


@dataclass
class ZoneDefaults:
    """Default settings for new zones."""

    name: Optional[str] = None
    locked: Optional[bool] = None
    priority: Optional[int] = None
    corner_smoothing: Optional[int] = None
    corner_radius_nm: Optional[int] = None
    clearance_nm: Optional[int] = None
    min_thickness_nm: Optional[int] = None
    pad_connection: Optional[int] = None
    thermal_gap_nm: Optional[int] = None
    thermal_spoke_width_nm: Optional[int] = None
    island_removal: Optional[int] = None
    min_island_area_nm2: Optional[int] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.ZoneDefaultsResponse) -> "ZoneDefaults":
        """Create from protobuf response."""
        return cls(
            name=proto.name,
            locked=proto.locked,
            priority=proto.priority,
            corner_smoothing=proto.corner_smoothing,
            corner_radius_nm=proto.corner_radius.value_nm if proto.HasField("corner_radius") else None,
            clearance_nm=proto.clearance.value_nm if proto.HasField("clearance") else None,
            min_thickness_nm=proto.min_thickness.value_nm if proto.HasField("min_thickness") else None,
            pad_connection=proto.pad_connection,
            thermal_gap_nm=proto.thermal_gap.value_nm if proto.HasField("thermal_gap") else None,
            thermal_spoke_width_nm=proto.thermal_spoke_width.value_nm if proto.HasField("thermal_spoke_width") else None,
            island_removal=proto.island_removal,
            min_island_area_nm2=proto.min_island_area_nm2,
        )

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        result = {}
        if self.name is not None:
            result["name"] = self.name
        if self.locked is not None:
            result["locked"] = self.locked
        if self.priority is not None:
            result["priority"] = self.priority
        if self.corner_smoothing is not None:
            result["corner_smoothing"] = CornerSmoothingMode.to_string(self.corner_smoothing)
        if self.corner_radius_nm is not None:
            result["corner_radius_nm"] = self.corner_radius_nm
        if self.clearance_nm is not None:
            result["clearance_nm"] = self.clearance_nm
        if self.min_thickness_nm is not None:
            result["min_thickness_nm"] = self.min_thickness_nm
        if self.pad_connection is not None:
            result["pad_connection"] = ZonePadConnection.to_string(self.pad_connection)
        if self.thermal_gap_nm is not None:
            result["thermal_gap_nm"] = self.thermal_gap_nm
        if self.thermal_spoke_width_nm is not None:
            result["thermal_spoke_width_nm"] = self.thermal_spoke_width_nm
        if self.island_removal is not None:
            result["island_removal"] = ZoneIslandRemoval.to_string(self.island_removal)
        if self.min_island_area_nm2 is not None:
            result["min_island_area_nm2"] = self.min_island_area_nm2
        return result


class ZoneDefaultsOperations:
    """Zone defaults operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> ZoneDefaults:
        """Get default settings for new zones.

        Returns:
            ZoneDefaults with current settings
        """
        cmd = board_commands_pb2.GetZoneDefaults()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.ZoneDefaultsResponse)
        return ZoneDefaults.from_proto(response)

    def set(
        self,
        name: Optional[str] = None,
        locked: Optional[bool] = None,
        priority: Optional[int] = None,
        corner_smoothing: Optional[int] = None,
        corner_radius_nm: Optional[int] = None,
        clearance_nm: Optional[int] = None,
        min_thickness_nm: Optional[int] = None,
        pad_connection: Optional[int] = None,
        thermal_gap_nm: Optional[int] = None,
        thermal_spoke_width_nm: Optional[int] = None,
        island_removal: Optional[int] = None,
        min_island_area_nm2: Optional[int] = None,
    ) -> ZoneDefaults:
        """Set default settings for new zones (partial update).

        Args:
            name: Default zone name
            locked: Whether zones are locked by default
            priority: Default zone priority
            corner_smoothing: CornerSmoothingMode value
            corner_radius_nm: Corner radius in nanometers
            clearance_nm: Zone clearance in nanometers
            min_thickness_nm: Minimum zone thickness in nanometers
            pad_connection: ZonePadConnection value
            thermal_gap_nm: Thermal relief gap in nanometers
            thermal_spoke_width_nm: Thermal spoke width in nanometers
            island_removal: ZoneIslandRemoval value
            min_island_area_nm2: Minimum island area in square nanometers

        Returns:
            Updated ZoneDefaults
        """
        cmd = board_commands_pb2.SetZoneDefaults()
        cmd.board.CopyFrom(self._board._doc)

        if name is not None:
            cmd.name = name
        if locked is not None:
            cmd.locked = locked
        if priority is not None:
            cmd.priority = priority
        if corner_smoothing is not None:
            cmd.corner_smoothing = corner_smoothing
        if corner_radius_nm is not None:
            cmd.corner_radius.value_nm = corner_radius_nm
        if clearance_nm is not None:
            cmd.clearance.value_nm = clearance_nm
        if min_thickness_nm is not None:
            cmd.min_thickness.value_nm = min_thickness_nm
        if pad_connection is not None:
            cmd.pad_connection = pad_connection
        if thermal_gap_nm is not None:
            cmd.thermal_gap.value_nm = thermal_gap_nm
        if thermal_spoke_width_nm is not None:
            cmd.thermal_spoke_width.value_nm = thermal_spoke_width_nm
        if island_removal is not None:
            cmd.island_removal = island_removal
        if min_island_area_nm2 is not None:
            cmd.min_island_area_nm2 = min_island_area_nm2

        response = self._board._kicad.send(cmd, board_commands_pb2.ZoneDefaultsResponse)
        return ZoneDefaults.from_proto(response)
