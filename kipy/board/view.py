# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
View and appearance operations.
"""

from typing import TYPE_CHECKING, Iterable, Optional, Union

from google.protobuf.empty_pb2 import Empty

from kipy.geometry import Box2
from kipy.board_types import BoardEditorAppearanceSettings
from kipy.proto.common.types import KIID
from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


class ViewOperations:
    """View and appearance operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_appearance_settings(self) -> BoardEditorAppearanceSettings:
        """Get the board editor appearance settings."""
        cmd = board_commands_pb2.GetBoardEditorAppearanceSettings()
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardEditorAppearanceSettings)
        return BoardEditorAppearanceSettings(response)

    def set_appearance_settings(self, settings: BoardEditorAppearanceSettings):
        """Set the board editor appearance settings."""
        cmd = board_commands_pb2.SetBoardEditorAppearanceSettings()
        cmd.settings.CopyFrom(settings.proto)
        self._board._kicad.send(cmd, Empty)

    def interactive_move(self, items: Union[KIID, Iterable[KIID]]):
        """Initiate an interactive move operation.

        The user will be able to move the items interactively in the editor.
        This is a blocking operation - future API calls will return AS_BUSY
        until the interactive move is complete.

        Args:
            items: Item ID(s) to move
        """
        cmd = board_commands_pb2.InteractiveMoveItems()
        cmd.board.CopyFrom(self._board._doc)

        if isinstance(items, KIID):
            cmd.items.append(items)
        else:
            cmd.items.extend(items)

        self._board._kicad.send(cmd, Empty)

    def show_diff_overlay(self, bounding_box: Optional[Box2] = None):
        """Show a diff overlay for testing UI components.

        Args:
            bounding_box: Optional bounding box for the overlay
        """
        cmd = board_commands_pb2.ShowDiffOverlay()
        cmd.board.CopyFrom(self._board._doc)
        if bounding_box:
            cmd.bounding_box.CopyFrom(bounding_box.proto)
        self._board._kicad.send(cmd, Empty)
