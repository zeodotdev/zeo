# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
CRUD (Create, Read, Update, Delete) operations for schematic items.
"""

import logging
from typing import TYPE_CHECKING, List, Optional, Sequence, Union, cast

from kipy.proto.common.types import KiCadObjectType, KIID

logger = logging.getLogger(__name__)
from kipy.proto.common.commands.editor_commands_pb2 import (
    BeginCommit, BeginCommitResponse, CommitAction,
    EndCommit, EndCommitResponse,
    UpdateItems, UpdateItemsResponse,
    DeleteItems, DeleteItemsResponse,
)
from kipy.proto.common.commands import editor_commands_pb2
from kipy.wrapper import Wrapper, unwrap
from kipy.util import pack_any
from kipy.common_types import Commit
from kipy.client import ApiError
from kipy.schematic_types import (
    Symbol, Pin, Field, Wire, LocalLabel, GlobalLabel, HierarchicalLabel,
    Junction, NoConnect, SchematicText, SchematicGraphicShape, SchematicTextBox
)

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class CRUDOperations:
    """Create, Read, Update, Delete operations for schematic items."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Create Operations
    # =========================================================================

    def create_items(self, items: Union[Wrapper, Sequence[Wrapper]]) -> List[Wrapper]:
        """Create one or more items in the schematic.

        Args:
            items: A single item or sequence of items to create

        Returns:
            List of successfully created items

        Raises:
            ApiError: If any item fails to create

        Example:
            >>> wire = Wire.create(start, end)
            >>> created = sch.crud.create_items(wire)
        """
        command = editor_commands_pb2.CreateItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            command.items.append(pack_any(items.proto))
        else:
            command.items.extend([pack_any(i.proto) for i in items])

        response = self._kicad.send(command, editor_commands_pb2.CreateItemsResponse)

        created = []
        errors = []

        for result in response.created_items:
            if result.status.code != 1:  # ISC_OK = 1
                error_msg = result.status.error_message or f"status code {result.status.code}"
                errors.append(error_msg)
                continue

            try:
                wrapped = unwrap(result.item)
                created.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in create_items: {e}")

        if errors:
            raise ApiError(f"Failed to create items: {'; '.join(errors)}")

        return created

    # =========================================================================
    # Read Operations
    # =========================================================================

    def get_items(self, types: Union[int, Sequence[int]]) -> Sequence[Wrapper]:
        """Get items by type.

        Args:
            types: KiCadObjectType value(s) to retrieve

        Returns:
            List of items matching the type(s)

        Example:
            >>> symbols = sch.crud.get_items(KiCadObjectType.KOT_SCH_SYMBOL)
            >>> wires = sch.crud.get_items(KiCadObjectType.KOT_SCH_LINE)
        """
        cmd = editor_commands_pb2.GetItems()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(types, int):
            cmd.types.append(types)
        else:
            cmd.types.extend(types)

        response = self._kicad.send(cmd, editor_commands_pb2.GetItemsResponse)

        items = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                items.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in get_items: {e}")
        return items

    def get_by_id(self, item_ids: Union[str, Sequence[str]]) -> Sequence[Wrapper]:
        """Get items by their unique KIID.

        Args:
            item_ids: Single KIID string or list of KIIDs

        Returns:
            List of matching items

        Example:
            >>> items = sch.crud.get_by_id("12345678-abcd-efgh-ijkl-mnop")
            >>> items = sch.crud.get_by_id(["id1", "id2", "id3"])
        """
        cmd = editor_commands_pb2.GetItemsById()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(item_ids, str):
            kiid = cmd.items.add()
            kiid.value = item_ids
        else:
            for item_id in item_ids:
                kiid = cmd.items.add()
                kiid.value = item_id

        response = self._kicad.send(cmd, editor_commands_pb2.GetItemsResponse)

        items = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                items.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in get_by_id: {e}")
        return items

    # Convenience getters for specific types
    def get_symbols(self) -> List[Symbol]:
        """Get all symbols in the schematic."""
        return [cast(Symbol, i) for i in self.get_items(KiCadObjectType.KOT_SCH_SYMBOL)]

    def get_wires(self) -> List[Wire]:
        """Get all wires in the schematic."""
        return [cast(Wire, i) for i in self.get_items(KiCadObjectType.KOT_SCH_LINE)]

    def get_junctions(self) -> List[Junction]:
        """Get all junctions in the schematic."""
        items = self.get_items(KiCadObjectType.KOT_SCH_JUNCTION)
        return [cast(Junction, i) for i in items if Junction is not None]

    def get_no_connects(self) -> List[NoConnect]:
        """Get all no-connect markers in the schematic."""
        items = self.get_items(KiCadObjectType.KOT_SCH_NO_CONNECT)
        return [cast(NoConnect, i) for i in items if NoConnect is not None]

    def get_local_labels(self) -> List[LocalLabel]:
        """Get all local labels in the schematic."""
        return [cast(LocalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_LABEL)]

    def get_global_labels(self) -> List[GlobalLabel]:
        """Get all global labels in the schematic."""
        return [cast(GlobalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_GLOBAL_LABEL)]

    def get_hierarchical_labels(self) -> List[HierarchicalLabel]:
        """Get all hierarchical labels in the schematic."""
        return [cast(HierarchicalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_HIER_LABEL)]

    def get_labels(self) -> Sequence[Wrapper]:
        """Get all labels (local, global, hierarchical)."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_LABEL,
            KiCadObjectType.KOT_SCH_GLOBAL_LABEL,
            KiCadObjectType.KOT_SCH_HIER_LABEL,
        ])

    def get_sheets(self) -> Sequence[Wrapper]:
        """Get all sheet references in the schematic."""
        return self.get_items(KiCadObjectType.KOT_SCH_SHEET)

    def get_pins(self) -> List[Pin]:
        """Get all pins from all symbols."""
        return [cast(Pin, i) for i in self.get_items(KiCadObjectType.KOT_SCH_PIN)]

    def get_fields(self) -> List[Field]:
        """Get all fields from all symbols."""
        return [cast(Field, i) for i in self.get_items(KiCadObjectType.KOT_SCH_FIELD)]

    def get_text_items(self) -> Sequence[Wrapper]:
        """Get all text and textbox items."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_TEXT,
            KiCadObjectType.KOT_SCH_TEXTBOX,
        ])

    def get_graphic_shapes(self) -> List[SchematicGraphicShape]:
        """Get all graphic shapes."""
        return [cast(SchematicGraphicShape, i) for i in self.get_items(KiCadObjectType.KOT_SCH_SHAPE)]

    def get_bus_entries(self) -> Sequence[Wrapper]:
        """Get all bus entries."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_BUS_WIRE_ENTRY,
            KiCadObjectType.KOT_SCH_BUS_BUS_ENTRY,
        ])

    def get_directive_labels(self) -> Sequence[Wrapper]:
        """Get all directive labels (SPICE)."""
        return self.get_items(KiCadObjectType.KOT_SCH_DIRECTIVE_LABEL)

    def get_bitmaps(self) -> Sequence[Wrapper]:
        """Get all bitmap images."""
        return self.get_items(KiCadObjectType.KOT_SCH_BITMAP)

    def get_tables(self) -> Sequence[Wrapper]:
        """Get all tables."""
        return self.get_items(KiCadObjectType.KOT_SCH_TABLE)

    def get_groups(self) -> Sequence[Wrapper]:
        """Get all groups."""
        return self.get_items(KiCadObjectType.KOT_SCH_GROUP)

    # =========================================================================
    # Update Operations
    # =========================================================================

    def update_items(self, items: Union[Wrapper, Sequence[Wrapper]]) -> List[Wrapper]:
        """Update item properties.

        Items are matched by their internal UUID. All other properties
        are updated from those passed in.

        Args:
            items: Item(s) to update

        Returns:
            List of updated items

        Example:
            >>> symbol = sch.symbols.get_by_ref("R1")
            >>> symbol.value = "4.7k"
            >>> sch.crud.update_items(symbol)
        """
        command = UpdateItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            command.items.append(pack_any(items.proto))
        else:
            command.items.extend([pack_any(i.proto) for i in items])

        if len(command.items) == 0:
            return []

        response = self._kicad.send(command, UpdateItemsResponse)
        updated = []
        for result in response.updated_items:
            try:
                wrapped = unwrap(result.item)
                updated.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in update_items: {e}")
        return updated

    # =========================================================================
    # Delete Operations
    # =========================================================================

    def remove_items(self, items: Union[Wrapper, Sequence[Wrapper]]):
        """Delete items from the schematic.

        Args:
            items: Item(s) to delete

        Example:
            >>> wire = sch.crud.get_wires()[0]
            >>> sch.crud.remove_items(wire)
        """
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            command.item_ids.append(items.id)
        else:
            command.item_ids.extend([item.id for item in items])

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    def remove_by_id(self, item_ids: Union[KIID, Sequence[KIID]]):
        """Delete items by their KIID.

        Args:
            item_ids: KIID(s) of items to delete
        """
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(item_ids, KIID):
            command.item_ids.append(item_ids)
        else:
            command.item_ids.extend(item_ids)

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    # =========================================================================
    # Transaction Control
    # =========================================================================

    def begin_commit(self) -> Commit:
        """Begin a commit transaction.

        Changes made after begin_commit() won't appear in the editor
        until push_commit() is called. This groups multiple changes
        into a single undo step.

        Returns:
            Commit object to pass to push_commit() or drop_commit()

        Example:
            >>> commit = sch.crud.begin_commit()
            >>> # Make multiple changes...
            >>> sch.crud.push_commit(commit, "Added power section")
        """
        command = BeginCommit()
        return Commit(self._kicad.send(command, BeginCommitResponse).id)

    def push_commit(self, commit: Commit, message: str = ""):
        """Commit pending changes.

        Args:
            commit: Commit from begin_commit()
            message: Optional message for undo history
        """
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_COMMIT
        command.message = message
        self._kicad.send(command, EndCommitResponse)

    def drop_commit(self, commit: Commit):
        """Cancel a commit, discarding all changes.

        Args:
            commit: Commit from begin_commit()
        """
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_DROP
        self._kicad.send(command, EndCommitResponse)

    # =========================================================================
    # String Parsing Operations
    # =========================================================================

    def parse_and_create(self, contents: str) -> List[Wrapper]:
        """Parse and create items from a string.

        Parses the provided string (in KiCad file format) and creates
        the items in the current schematic. This uses the IPC command
        ParseAndCreateItemsFromString.

        Args:
            contents: String containing schematic items in KiCad format

        Returns:
            List of created Wrapper objects

        Raises:
            ApiError: If parsing or creation fails

        Example:
            >>> # Read from a file
            >>> with open("symbols.kicad_sch") as f:
            ...     contents = f.read()
            >>> items = sch.crud.parse_and_create(contents)
            >>> print(f"Created {len(items)} items")

            >>> # Or paste previously copied items
            >>> items = sch.crud.parse_and_create(copied_contents)
        """
        command = editor_commands_pb2.ParseAndCreateItemsFromString()
        command.document.CopyFrom(self._doc)
        command.contents = contents

        response = self._kicad.send(command, editor_commands_pb2.CreateItemsResponse)

        created = []
        errors = []

        for result in response.created_items:
            if result.status.code != 1:  # ISC_OK = 1
                error_msg = result.status.error_message or f"status code {result.status.code}"
                errors.append(error_msg)
                continue

            try:
                wrapped = unwrap(result.item)
                created.append(wrapped)
            except (ValueError, NotImplementedError) as e:
                logger.debug(f"Skipping unregistered item type in parse_and_create: {e}")

        if errors:
            raise ApiError(f"Failed to parse/create items: {'; '.join(errors)}")

        return created
