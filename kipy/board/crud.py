# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
CRUD (Create, Read, Update, Delete) operations for board items.
"""

from typing import TYPE_CHECKING, List, Sequence, Union, cast

from google.protobuf.empty_pb2 import Empty

from kipy.proto.common.types import KiCadObjectType, KIID
from kipy.proto.common.commands.editor_commands_pb2 import (
    BeginCommit, BeginCommitResponse, CommitAction,
    EndCommit, EndCommitResponse,
    CreateItems, CreateItemsResponse,
    UpdateItems, UpdateItemsResponse,
    GetItems, GetItemsResponse,
    DeleteItems, DeleteItemsResponse,
)
from kipy.wrapper import Wrapper
from kipy.util import pack_any
from kipy.common_types import Commit
from kipy.client import ApiError
from kipy.board_types import (
    BoardItem, FootprintInstance, Track, ArcTrack, Via, Pad, Zone,
    BoardShape, BoardText, BoardTextBox, Dimension,
    to_concrete_board_shape, to_concrete_dimension, unwrap
)

if TYPE_CHECKING:
    from kipy.board.base import Board


class CRUDOperations:
    """Create, Read, Update, Delete operations for board items."""

    def __init__(self, board: "Board"):
        self._board = board

    @property
    def _kicad(self):
        return self._board._kicad

    @property
    def _doc(self):
        return self._board._doc

    # =========================================================================
    # Create Operations
    # =========================================================================

    def create_items(self, items: Union[Wrapper, Sequence[Wrapper]]) -> List[Wrapper]:
        """Create one or more items on the board.

        Args:
            items: A single item or sequence of items to create

        Returns:
            List of successfully created items

        Raises:
            ApiError: If any item fails to create
        """
        command = CreateItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            command.items.append(pack_any(items.proto))
        else:
            command.items.extend([pack_any(i.proto) for i in items])

        response = self._kicad.send(command, CreateItemsResponse)

        created = []
        errors = []

        for result in response.created_items:
            if result.status.code != 1:  # ISC_OK = 1
                error_msg = result.status.error_message or f"status code {result.status.code}"
                errors.append(error_msg)
                continue

            try:
                created.append(unwrap(result.item))
            except (ValueError, NotImplementedError):
                pass

        if errors:
            raise ApiError(f"Failed to create items: {'; '.join(errors)}")

        return created

    # =========================================================================
    # Read Operations
    # =========================================================================

    def get_items(
        self, types: Union[KiCadObjectType.ValueType, Sequence[KiCadObjectType.ValueType]]
    ) -> Sequence[Wrapper]:
        """Get items by type.

        Args:
            types: Single type or sequence of types to retrieve

        Returns:
            Sequence of items matching the requested types
        """
        command = GetItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(types, int):
            command.types.append(types)
        else:
            command.types.extend(types)

        items = [unwrap(item) for item in self._kicad.send(command, GetItemsResponse).items]
        return self._to_concrete_items(items)

    def _to_concrete_items(self, items: Sequence[Wrapper]) -> List[BoardItem]:
        """Convert generic items to concrete types."""
        items_converted = []
        for it in items:
            if isinstance(it, BoardShape):
                items_converted.append(to_concrete_board_shape(cast(BoardShape, it)))
            elif isinstance(it, Dimension):
                items_converted.append(to_concrete_dimension(cast(Dimension, it)))
            else:
                items_converted.append(it)
        return items_converted

    def get_footprints(self) -> Sequence[FootprintInstance]:
        """Get all footprints on the board."""
        return [
            cast(FootprintInstance, item)
            for item in self.get_items(types=[KiCadObjectType.KOT_PCB_FOOTPRINT])
        ]

    def get_tracks(self) -> Sequence[Union[Track, ArcTrack]]:
        """Get all tracks (straight and arc) on the board."""
        return [
            cast(Track, item) if isinstance(item, Track) else cast(ArcTrack, item)
            for item in self.get_items(
                types=[KiCadObjectType.KOT_PCB_TRACE, KiCadObjectType.KOT_PCB_ARC]
            )
        ]

    def get_vias(self) -> Sequence[Via]:
        """Get all vias on the board."""
        return [cast(Via, item) for item in self.get_items(types=[KiCadObjectType.KOT_PCB_VIA])]

    def get_pads(self) -> Sequence[Pad]:
        """Get all pads on the board (pads belong to footprints)."""
        return [cast(Pad, item) for item in self.get_items(types=[KiCadObjectType.KOT_PCB_PAD])]

    def get_zones(self) -> Sequence[Zone]:
        """Get all zones (including rule areas) on the board."""
        return [cast(Zone, item) for item in self.get_items(types=[KiCadObjectType.KOT_PCB_ZONE])]

    def get_shapes(self) -> Sequence[BoardShape]:
        """Get all graphic shapes on the board."""
        return [
            item
            for item in (
                to_concrete_board_shape(cast(BoardShape, item))
                for item in self.get_items(types=[KiCadObjectType.KOT_PCB_SHAPE])
            )
            if item is not None
        ]

    def get_text(self) -> Sequence[Union[BoardText, BoardTextBox]]:
        """Get all text items on the board."""
        return [
            cast(BoardText, item) if isinstance(item, BoardText) else cast(BoardTextBox, item)
            for item in self.get_items(
                types=[KiCadObjectType.KOT_PCB_TEXT, KiCadObjectType.KOT_PCB_TEXTBOX]
            )
        ]

    def get_dimensions(self) -> Sequence[Dimension]:
        """Get all dimension objects on the board."""
        return [
            item
            for item in (
                to_concrete_dimension(cast(Dimension, item))
                for item in self.get_items(types=[KiCadObjectType.KOT_PCB_DIMENSION])
            )
            if item is not None
        ]

    # =========================================================================
    # Update Operations
    # =========================================================================

    def update_items(self, items: Union[BoardItem, Sequence[BoardItem]]) -> List[BoardItem]:
        """Update the properties of one or more items.

        Args:
            items: Single item or sequence of items to update

        Returns:
            Updated items (may differ from input if values were clamped)
        """
        command = UpdateItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, BoardItem):
            command.items.append(pack_any(items.proto))
        else:
            command.items.extend([pack_any(i.proto) for i in items])

        if len(command.items) == 0:
            return []

        return self._to_concrete_items(
            [
                unwrap(result.item)
                for result in self._kicad.send(command, UpdateItemsResponse).updated_items
            ]
        )

    # =========================================================================
    # Delete Operations
    # =========================================================================

    def remove_items(self, items: Union[BoardItem, Sequence[BoardItem]]):
        """Delete one or more items from the board."""
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, BoardItem):
            command.item_ids.append(items.id)
        else:
            command.item_ids.extend([item.id for item in items])

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    def remove_items_by_id(self, ids: Union[KIID, Sequence[KIID]]):
        """Delete one or more items by their unique IDs."""
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(ids, KIID):
            command.item_ids.append(ids)
        else:
            command.item_ids.extend(ids)

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    # =========================================================================
    # Transaction Control
    # =========================================================================

    def begin_commit(self) -> Commit:
        """Begin a commit transaction.

        Returns:
            Commit object for use with push_commit or drop_commit
        """
        command = BeginCommit()
        return Commit(self._kicad.send(command, BeginCommitResponse).id)

    def push_commit(self, commit: Commit, message: str = ""):
        """Push a commit, applying changes to the board."""
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_COMMIT
        command.message = message
        self._kicad.send(command, EndCommitResponse)

    def drop_commit(self, commit: Commit):
        """Cancel a commit, discarding changes."""
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_DROP
        self._kicad.send(command, EndCommitResponse)
