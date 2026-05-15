# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Dimension defaults operations.
"""

from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Enum mappings
class DimensionUnitsMode:
    INCHES = board_commands_pb2.DUM_INCHES
    MILS = board_commands_pb2.DUM_MILS
    MILLIMETERS = board_commands_pb2.DUM_MILLIMETERS
    AUTOMATIC = board_commands_pb2.DUM_AUTOMATIC

    _TO_STRING = {
        board_commands_pb2.DUM_AUTOMATIC: 'automatic',
        board_commands_pb2.DUM_INCHES: 'inches',
        board_commands_pb2.DUM_MILS: 'mils',
        board_commands_pb2.DUM_MILLIMETERS: 'millimeters',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'automatic')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.AUTOMATIC)


class DimensionUnitsFormat:
    NO_SUFFIX = board_commands_pb2.DUF_NO_SUFFIX
    BARE_SUFFIX = board_commands_pb2.DUF_BARE_SUFFIX
    PAREN_SUFFIX = board_commands_pb2.DUF_PAREN_SUFFIX

    _TO_STRING = {
        board_commands_pb2.DUF_NO_SUFFIX: 'no_suffix',
        board_commands_pb2.DUF_BARE_SUFFIX: 'bare_suffix',
        board_commands_pb2.DUF_PAREN_SUFFIX: 'paren_suffix',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'bare_suffix')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.BARE_SUFFIX)


class DimensionPrecision:
    X = board_commands_pb2.DP_X
    X_X = board_commands_pb2.DP_X_X
    X_XX = board_commands_pb2.DP_X_XX
    X_XXX = board_commands_pb2.DP_X_XXX
    X_XXXX = board_commands_pb2.DP_X_XXXX
    X_XXXXX = board_commands_pb2.DP_X_XXXXX
    V_VV = board_commands_pb2.DP_V_VV
    V_VVV = board_commands_pb2.DP_V_VVV
    V_VVVV = board_commands_pb2.DP_V_VVVV
    V_VVVVV = board_commands_pb2.DP_V_VVVVV

    _TO_STRING = {
        board_commands_pb2.DP_X: 0,
        board_commands_pb2.DP_X_X: 1,
        board_commands_pb2.DP_X_XX: 2,
        board_commands_pb2.DP_X_XXX: 3,
        board_commands_pb2.DP_X_XXXX: 4,
        board_commands_pb2.DP_X_XXXXX: 5,
        board_commands_pb2.DP_V_VV: 'V.VV',
        board_commands_pb2.DP_V_VVV: 'V.VVV',
        board_commands_pb2.DP_V_VVVV: 'V.VVVV',
        board_commands_pb2.DP_V_VVVVV: 'V.VVVVV',
    }

    @classmethod
    def to_string(cls, value: int):
        return cls._TO_STRING.get(value, 2)

    @classmethod
    def from_string(cls, value) -> int:
        """Convert from string/int representation to enum value."""
        if isinstance(value, int) and 0 <= value <= 5:
            return [cls.X, cls.X_X, cls.X_XX, cls.X_XXX, cls.X_XXXX, cls.X_XXXXX][value]
        str_map = {'V.VV': cls.V_VV, 'V.VVV': cls.V_VVV, 'V.VVVV': cls.V_VVVV, 'V.VVVVV': cls.V_VVVVV}
        return str_map.get(value, cls.X_XX)


class DimensionTextPosition:
    OUTSIDE = board_commands_pb2.DTP_OUTSIDE
    INLINE = board_commands_pb2.DTP_INLINE
    MANUAL = board_commands_pb2.DTP_MANUAL

    _TO_STRING = {
        board_commands_pb2.DTP_OUTSIDE: 'outside',
        board_commands_pb2.DTP_INLINE: 'inline',
        board_commands_pb2.DTP_MANUAL: 'manual',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'outside')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.OUTSIDE)


@dataclass
class DimensionDefaults:
    """Default settings for new dimension objects."""

    units_mode: Optional[int] = None
    units_format: Optional[int] = None
    precision: Optional[int] = None
    suppress_zeroes: Optional[bool] = None
    text_position: Optional[int] = None
    keep_text_aligned: Optional[bool] = None
    arrow_length_nm: Optional[int] = None
    extension_offset_nm: Optional[int] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.DimensionDefaultsResponse) -> "DimensionDefaults":
        """Create from protobuf response."""
        return cls(
            units_mode=proto.units_mode,
            units_format=proto.units_format,
            precision=proto.precision,
            suppress_zeroes=proto.suppress_zeroes,
            text_position=proto.text_position,
            keep_text_aligned=proto.keep_text_aligned,
            arrow_length_nm=proto.arrow_length.value_nm if proto.HasField("arrow_length") else None,
            extension_offset_nm=proto.extension_offset.value_nm if proto.HasField("extension_offset") else None,
        )

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        result = {}
        if self.units_mode is not None:
            result["units_mode"] = DimensionUnitsMode.to_string(self.units_mode)
        if self.units_format is not None:
            result["units_format"] = DimensionUnitsFormat.to_string(self.units_format)
        if self.precision is not None:
            result["precision"] = DimensionPrecision.to_string(self.precision)
        if self.suppress_zeroes is not None:
            result["suppress_zeroes"] = self.suppress_zeroes
        if self.text_position is not None:
            result["text_position"] = DimensionTextPosition.to_string(self.text_position)
        if self.keep_text_aligned is not None:
            result["keep_text_aligned"] = self.keep_text_aligned
        if self.arrow_length_nm is not None:
            result["arrow_length_nm"] = self.arrow_length_nm
        if self.extension_offset_nm is not None:
            result["extension_offset_nm"] = self.extension_offset_nm
        return result


class DimensionDefaultsOperations:
    """Dimension defaults operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> DimensionDefaults:
        """Get default settings for new dimension objects.

        Returns:
            DimensionDefaults with current settings
        """
        cmd = board_commands_pb2.GetDimensionDefaults()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.DimensionDefaultsResponse)
        return DimensionDefaults.from_proto(response)

    def set(
        self,
        units_mode: Optional[int] = None,
        units_format: Optional[int] = None,
        precision: Optional[int] = None,
        suppress_zeroes: Optional[bool] = None,
        text_position: Optional[int] = None,
        keep_text_aligned: Optional[bool] = None,
        arrow_length_nm: Optional[int] = None,
        extension_offset_nm: Optional[int] = None,
    ) -> DimensionDefaults:
        """Set default settings for new dimension objects (partial update).

        Args:
            units_mode: DimensionUnitsMode value
            units_format: DimensionUnitsFormat value
            precision: DimensionPrecision value
            suppress_zeroes: Whether to suppress trailing zeroes
            text_position: DimensionTextPosition value
            keep_text_aligned: Keep text aligned with dimension line
            arrow_length_nm: Arrow length in nanometers
            extension_offset_nm: Extension line offset in nanometers

        Returns:
            Updated DimensionDefaults
        """
        cmd = board_commands_pb2.SetDimensionDefaults()
        cmd.board.CopyFrom(self._board._doc)

        if units_mode is not None:
            cmd.units_mode = units_mode
        if units_format is not None:
            cmd.units_format = units_format
        if precision is not None:
            cmd.precision = precision
        if suppress_zeroes is not None:
            cmd.suppress_zeroes = suppress_zeroes
        if text_position is not None:
            cmd.text_position = text_position
        if keep_text_aligned is not None:
            cmd.keep_text_aligned = keep_text_aligned
        if arrow_length_nm is not None:
            cmd.arrow_length.value_nm = arrow_length_nm
        if extension_offset_nm is not None:
            cmd.extension_offset.value_nm = extension_offset_nm

        response = self._board._kicad.send(cmd, board_commands_pb2.DimensionDefaultsResponse)
        return DimensionDefaults.from_proto(response)
