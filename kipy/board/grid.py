# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Grid settings operations.
"""

from typing import TYPE_CHECKING, Optional

from google.protobuf.empty_pb2 import Empty

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board
    from kipy.board.types import PCBGridSettings


class GridOperations:
    """Grid settings operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_settings(self) -> "PCBGridSettings":
        """Get the PCB grid settings.

        Returns:
            PCBGridSettings object with grid size, visibility, and style
        """
        from kipy.board.types import PCBGridSettings
        cmd = board_commands_pb2.GetPCBGridSettings()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.PCBGridSettingsResponse)
        return PCBGridSettings(response.settings)

    def set_settings(
        self,
        grid_size_x_nm: Optional[int] = None,
        grid_size_y_nm: Optional[int] = None,
        show_grid: Optional[bool] = None,
        style: Optional[board_commands_pb2.GridStyle.ValueType] = None,
    ):
        """Set the PCB grid settings.

        Args:
            grid_size_x_nm: Grid X spacing in nanometers (optional)
            grid_size_y_nm: Grid Y spacing in nanometers (optional)
            show_grid: Whether to show the grid (optional)
            style: Grid style - GS_DOTS, GS_LINES, or GS_SMALL_CROSS (optional)
        """
        cmd = board_commands_pb2.SetPCBGridSettings()
        cmd.board.CopyFrom(self._board._doc)
        if grid_size_x_nm is not None:
            cmd.grid_size_x_nm = grid_size_x_nm
        if grid_size_y_nm is not None:
            cmd.grid_size_y_nm = grid_size_y_nm
        if show_grid is not None:
            cmd.show_grid = show_grid
        if style is not None:
            cmd.style = style
        self._board._kicad.send(cmd, Empty)

    def set_size(self, size_x_nm: int, size_y_nm: Optional[int] = None):
        """Set the grid size.

        Args:
            size_x_nm: Grid X spacing in nanometers
            size_y_nm: Grid Y spacing in nanometers (defaults to size_x_nm if not specified)
        """
        self.set_settings(
            grid_size_x_nm=size_x_nm,
            grid_size_y_nm=size_y_nm if size_y_nm is not None else size_x_nm,
        )

    def show(self):
        """Show the grid."""
        self.set_settings(show_grid=True)

    def hide(self):
        """Hide the grid."""
        self.set_settings(show_grid=False)
