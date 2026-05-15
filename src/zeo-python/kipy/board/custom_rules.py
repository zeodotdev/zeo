# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Custom DRC rules operations.
"""

from typing import TYPE_CHECKING

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


class CustomRulesOperations:
    """Custom DRC rules operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> str:
        """Get custom DRC rules text.

        Returns:
            The full text content of the .kicad_dru file
        """
        cmd = board_commands_pb2.GetCustomRules()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.CustomRulesResponse)
        return response.rules_text

    def set(self, rules_text: str) -> str:
        """Set custom DRC rules text.

        Args:
            rules_text: The full text content for the .kicad_dru file

        Returns:
            The updated rules text
        """
        cmd = board_commands_pb2.SetCustomRules()
        cmd.board.CopyFrom(self._board._doc)
        cmd.rules_text = rules_text
        response = self._board._kicad.send(cmd, board_commands_pb2.CustomRulesResponse)
        return response.rules_text
