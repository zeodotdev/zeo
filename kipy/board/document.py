# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Document management operations.
"""

from typing import TYPE_CHECKING

from google.protobuf.empty_pb2 import Empty

from kipy.proto.common.commands import editor_commands_pb2
from kipy.proto.common.types import FrameType

if TYPE_CHECKING:
    from kipy.board.base import Board


class DocumentOperations:
    """Document management operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def save(self):
        """Save the board to disk."""
        cmd = editor_commands_pb2.SaveDocument()
        cmd.document.CopyFrom(self._board._doc)
        self._board._kicad.send(cmd, Empty)

    def save_as(self, filename: str, overwrite: bool = False, include_project: bool = True):
        """Save the board to a new file.

        Args:
            filename: The path to save the board to
            overwrite: If True, overwrite if file exists
            include_project: If True, save the project along with the board
        """
        cmd = editor_commands_pb2.SaveCopyOfDocument()
        cmd.document.CopyFrom(self._board._doc)
        cmd.path = filename
        cmd.options.overwrite = overwrite
        cmd.options.include_project = include_project
        self._board._kicad.send(cmd, Empty)

    def revert(self):
        """Revert the board to the last saved state."""
        cmd = editor_commands_pb2.RevertDocument()
        cmd.document.CopyFrom(self._board._doc)
        self._board._kicad.send(cmd, Empty)

    def refresh(self):
        """Refresh the PCB editor view.

        This triggers a redraw of the PCB editor window, ensuring that any
        changes made through the API are visually reflected in the editor.
        """
        cmd = editor_commands_pb2.RefreshEditor()
        cmd.frame = FrameType.FT_PCB_EDITOR
        self._board._kicad.send(cmd, Empty)
