# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Editor selection operations.
"""

import logging
from typing import TYPE_CHECKING, Optional, Sequence, Union

from google.protobuf.empty_pb2 import Empty
from kipy.proto.common.commands import editor_commands_pb2
from kipy.wrapper import Wrapper, unwrap

logger = logging.getLogger(__name__)

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class SelectionOperations:
    """Editor selection operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    def get(self, types: Optional[Union[int, Sequence[int]]] = None) -> Sequence[Wrapper]:
        """Get current selection.

        Args:
            types: Optional filter for item types (KiCadObjectType values)

        Returns:
            List of selected items

        Example:
            >>> selected = sch.selection.get()
            >>> for item in selected:
            ...     print(type(item).__name__)
        """
        cmd = editor_commands_pb2.GetSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(types, int):
            cmd.types.append(types)
        elif types is not None:
            cmd.types.extend(types)

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        items = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                items.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in selection.get: {e}")
        return items

    def add(self, items: Union[Wrapper, Sequence[Wrapper]]) -> Sequence[Wrapper]:
        """Add items to selection.

        Args:
            items: Item(s) to add to selection

        Returns:
            Updated selection

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.selection.add(symbols)
        """
        cmd = editor_commands_pb2.AddToSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        result = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                result.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in selection.add: {e}")
        return result

    def remove(self, items: Union[Wrapper, Sequence[Wrapper]]) -> Sequence[Wrapper]:
        """Remove items from selection.

        Args:
            items: Item(s) to remove from selection

        Returns:
            Updated selection
        """
        cmd = editor_commands_pb2.RemoveFromSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        result = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                result.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in selection.remove: {e}")
        return result

    def clear(self):
        """Clear the selection."""
        cmd = editor_commands_pb2.ClearSelection()
        cmd.header.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)
