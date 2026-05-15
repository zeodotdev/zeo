# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Schematic-to-PCB synchronization operations.
"""

from typing import TYPE_CHECKING, List, Optional

from kipy.proto.board import board_commands_pb2
from kipy.proto.common.types import KIID
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.board.base import Board


class PCBUpdateChange(Wrapper):
    """A single change from updating PCB from schematic."""

    # Change type constants (mirror proto enum)
    CT_UNKNOWN = 0
    CT_FOOTPRINT_ADDED = 1
    CT_FOOTPRINT_REPLACED = 2
    CT_FOOTPRINT_DELETED = 3
    CT_FOOTPRINT_UPDATED = 4
    CT_NET_CHANGED = 5
    CT_PAD_NET_CHANGED = 6
    CT_GROUP_ADDED = 7
    CT_WARNING = 8
    CT_ERROR = 9

    def __init__(self, proto: Optional[board_commands_pb2.PCBUpdateChange] = None):
        self._proto = board_commands_pb2.PCBUpdateChange()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        type_names = {
            0: "UNKNOWN", 1: "ADDED", 2: "REPLACED", 3: "DELETED",
            4: "UPDATED", 5: "NET_CHANGED", 6: "PAD_NET_CHANGED",
            7: "GROUP_ADDED", 8: "WARNING", 9: "ERROR"
        }
        type_name = type_names.get(self.type, "UNKNOWN")
        return f"PCBUpdateChange({type_name}: {self.reference} - {self.message[:50]})"

    @property
    def type(self) -> int:
        """The type of change."""
        return self._proto.type

    @property
    def reference(self) -> str:
        """Component reference (e.g., U1, R5)."""
        return self._proto.reference

    @property
    def message(self) -> str:
        """Human-readable description of the change."""
        return self._proto.message

    @property
    def item_id(self) -> Optional[KIID]:
        """ID of the affected item, if applicable."""
        if self._proto.HasField("item_id"):
            return self._proto.item_id
        return None


class PCBUpdateResult(Wrapper):
    """Result of updating PCB from schematic."""

    def __init__(self, proto: Optional[board_commands_pb2.UpdatePCBFromSchematicResponse] = None):
        self._proto = board_commands_pb2.UpdatePCBFromSchematicResponse()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return (
            f"PCBUpdateResult(added={self.footprints_added}, "
            f"replaced={self.footprints_replaced}, deleted={self.footprints_deleted}, "
            f"applied={self.changes_applied})"
        )

    @property
    def footprints_added(self) -> int:
        """Number of footprints added to the board."""
        return self._proto.footprints_added

    @property
    def footprints_replaced(self) -> int:
        """Number of footprints replaced (different library ID)."""
        return self._proto.footprints_replaced

    @property
    def footprints_deleted(self) -> int:
        """Number of footprints deleted (not in schematic)."""
        return self._proto.footprints_deleted

    @property
    def footprints_updated(self) -> int:
        """Number of footprints updated (fields/values changed)."""
        return self._proto.footprints_updated

    @property
    def nets_changed(self) -> int:
        """Number of net connections changed."""
        return self._proto.nets_changed

    @property
    def warnings(self) -> int:
        """Number of warnings generated."""
        return self._proto.warnings

    @property
    def errors(self) -> int:
        """Number of errors encountered."""
        return self._proto.errors

    @property
    def changes_applied(self) -> bool:
        """True if changes were applied (False for dry_run)."""
        return self._proto.changes_applied

    @property
    def changes(self) -> List[PCBUpdateChange]:
        """Detailed list of all changes."""
        return [PCBUpdateChange(c) for c in self._proto.changes]

    @property
    def has_changes(self) -> bool:
        """True if any changes were made or would be made."""
        return (
            self.footprints_added > 0 or
            self.footprints_replaced > 0 or
            self.footprints_deleted > 0 or
            self.footprints_updated > 0 or
            self.nets_changed > 0
        )

    def get_changes_by_type(self, change_type: int) -> List[PCBUpdateChange]:
        """Get changes filtered by type.

        Args:
            change_type: One of PCBUpdateChange.CT_* constants

        Returns:
            List of changes of the specified type
        """
        return [c for c in self.changes if c.type == change_type]


class SyncOperations:
    """Schematic-to-PCB synchronization operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def update_from_schematic(
        self,
        dry_run: bool = False,
        lookup_by_timestamp: bool = True,
        replace_footprints: bool = True,
        delete_unused_footprints: bool = False,
        override_locks: bool = False,
        update_fields: bool = True,
        remove_extra_fields: bool = False,
        transfer_groups: bool = True,
    ) -> PCBUpdateResult:
        """Update PCB from the associated schematic.

        This synchronizes the PCB with the schematic, adding new footprints,
        updating existing ones, and optionally removing unused footprints.

        Args:
            dry_run: If True, preview changes without applying them
            lookup_by_timestamp: If True, match footprints by UUID;
                                 if False, match by reference designator
            replace_footprints: Replace footprints when library ID differs
            delete_unused_footprints: Remove footprints not in schematic
            override_locks: Allow changes to locked footprints
            update_fields: Sync custom field values from schematic
            remove_extra_fields: Remove fields not present in schematic
            transfer_groups: Copy group membership from schematic

        Returns:
            PCBUpdateResult with change counts and detailed log

        Example:
            >>> # Preview changes first
            >>> result = board.sync.update_from_schematic(dry_run=True)
            >>> print(f"Would add {result.footprints_added} footprints")
            >>> for change in result.changes:
            ...     print(f"  {change.message}")
            >>>
            >>> # Apply changes
            >>> result = board.sync.update_from_schematic()
            >>> if result.errors > 0:
            ...     print("Errors occurred during update")
        """
        cmd = board_commands_pb2.UpdatePCBFromSchematic()
        cmd.board.CopyFrom(self._board._doc)
        cmd.dry_run = dry_run

        # Set options
        cmd.options.lookup_by_timestamp = lookup_by_timestamp
        cmd.options.replace_footprints = replace_footprints
        cmd.options.delete_unused_footprints = delete_unused_footprints
        cmd.options.override_locks = override_locks
        cmd.options.update_fields = update_fields
        cmd.options.remove_extra_fields = remove_extra_fields
        cmd.options.transfer_groups = transfer_groups

        response = self._board._kicad.send(
            cmd, board_commands_pb2.UpdatePCBFromSchematicResponse
        )
        return PCBUpdateResult(response)

    def preview_update(self, **kwargs) -> PCBUpdateResult:
        """Preview what changes would be made without applying them.

        This is a convenience method that calls update_from_schematic
        with dry_run=True.

        Args:
            **kwargs: Same options as update_from_schematic (except dry_run)

        Returns:
            PCBUpdateResult with preview of changes
        """
        kwargs.pop('dry_run', None)  # Remove if passed
        return self.update_from_schematic(dry_run=True, **kwargs)
