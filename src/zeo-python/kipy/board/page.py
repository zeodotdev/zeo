# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Page settings and title block operations.
"""

from typing import TYPE_CHECKING, Optional, Dict

from kipy.geometry import Vector2
from kipy.common_types import TitleBlockInfo
from kipy.proto.common.commands import editor_commands_pb2
from kipy.proto.common.types import base_types_pb2
from kipy.proto.board import board_commands_pb2

from google.protobuf.empty_pb2 import Empty

if TYPE_CHECKING:
    from kipy.board.base import Board


class PageOperations:
    """Page settings and title block operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_title_block(self) -> TitleBlockInfo:
        """Get the title block information for the board."""
        cmd = editor_commands_pb2.GetTitleBlockInfo()
        cmd.document.CopyFrom(self._board._doc)
        return TitleBlockInfo(self._board._kicad.send(cmd, base_types_pb2.TitleBlockInfo))

    def set_title_block(
        self,
        title: Optional[str] = None,
        date: Optional[str] = None,
        revision: Optional[str] = None,
        company: Optional[str] = None,
        comments: Optional[Dict[int, str]] = None,
    ) -> None:
        """Set title block information.

        Only provided fields are updated.

        Args:
            title: Document title
            date: Document date
            revision: Revision string
            company: Company name
            comments: Dict mapping comment number (1-9) to text

        Example:
            >>> board.page.set_title_block(
            ...     title="My PCB",
            ...     revision="1.0",
            ...     company="ACME Inc",
            ...     comments={1: "Author: John", 2: "Status: Draft"}
            ... )
        """
        cmd = editor_commands_pb2.SetTitleBlockInfo()
        cmd.document.CopyFrom(self._board._doc)

        if title is not None:
            cmd.title_block.title = title
        if date is not None:
            cmd.title_block.date = date
        if revision is not None:
            cmd.title_block.revision = revision
        if company is not None:
            cmd.title_block.company = company
        if comments is not None:
            for i, text in comments.items():
                if 1 <= i <= 9:
                    setattr(cmd.title_block, f"comment{i}", text)

        self._board._kicad.send(cmd, Empty)

    def get_origin(self, origin_type: board_commands_pb2.BoardOriginType.ValueType) -> Vector2:
        """Get the specified board origin (grid or drill/place).

        Args:
            origin_type: Type of origin to retrieve

        Returns:
            Vector2 with the origin position
        """
        cmd = board_commands_pb2.GetBoardOrigin()
        cmd.board.CopyFrom(self._board._doc)
        cmd.type = origin_type
        return Vector2(self._board._kicad.send(cmd, base_types_pb2.Vector2))

    def set_origin(
        self,
        origin_type: board_commands_pb2.BoardOriginType.ValueType,
        origin: Vector2,
    ):
        """Set the specified board origin (grid or drill/place).

        Args:
            origin_type: Type of origin to set
            origin: New origin position
        """
        cmd = board_commands_pb2.SetBoardOrigin()
        cmd.board.CopyFrom(self._board._doc)
        cmd.type = origin_type
        cmd.origin.CopyFrom(origin.proto)
        self._board._kicad.send(cmd, Empty)
