# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Selection operations for the board editor.
"""

from typing import TYPE_CHECKING, Optional, Sequence, Union

from google.protobuf.empty_pb2 import Empty

from kipy.proto.common.types import KiCadObjectType
from kipy.proto.common.commands import editor_commands_pb2
from kipy.wrapper import Wrapper
from kipy.board_types import BoardItem, unwrap

if TYPE_CHECKING:
    from kipy.board.base import Board


class SelectionOperations:
    """Selection operations for the board editor."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(
        self,
        types: Optional[
            Union[KiCadObjectType.ValueType, Sequence[KiCadObjectType.ValueType]]
        ] = None,
    ) -> Sequence[Wrapper]:
        """Get the current selection.

        Args:
            types: Optional type(s) to filter the selection by

        Returns:
            Sequence of selected items
        """
        cmd = editor_commands_pb2.GetSelection()
        cmd.header.document.CopyFrom(self._board._doc)

        if isinstance(types, int):
            cmd.types.append(types)
        else:
            cmd.types.extend(types or [])

        return self._board.crud._to_concrete_items([
            unwrap(item)
            for item in self._board._kicad.send(
                cmd, editor_commands_pb2.SelectionResponse
            ).items
        ])

    def add(self, items: Union[BoardItem, Sequence[BoardItem]]) -> Sequence[Wrapper]:
        """Add items to the current selection.

        Args:
            items: Item(s) to add to selection

        Returns:
            Updated selection
        """
        cmd = editor_commands_pb2.AddToSelection()
        cmd.header.document.CopyFrom(self._board._doc)

        if isinstance(items, BoardItem):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        return [
            unwrap(item)
            for item in self._board._kicad.send(
                cmd, editor_commands_pb2.SelectionResponse
            ).items
        ]

    def remove(self, items: Union[BoardItem, Sequence[BoardItem]]) -> Sequence[Wrapper]:
        """Remove items from the current selection.

        Args:
            items: Item(s) to remove from selection

        Returns:
            Updated selection
        """
        cmd = editor_commands_pb2.RemoveFromSelection()
        cmd.header.document.CopyFrom(self._board._doc)

        if isinstance(items, BoardItem):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        return [
            unwrap(item)
            for item in self._board._kicad.send(
                cmd, editor_commands_pb2.SelectionResponse
            ).items
        ]

    def clear(self):
        """Clear the current selection."""
        cmd = editor_commands_pb2.ClearSelection()
        cmd.header.document.CopyFrom(self._board._doc)
        self._board._kicad.send(cmd, Empty)

    def get_as_string(self) -> str:
        """Get the current selection as a KiCad S-expression string."""
        cmd = editor_commands_pb2.SaveSelectionToString()
        return self._board._kicad.send(cmd, editor_commands_pb2.SavedSelectionResponse).contents
