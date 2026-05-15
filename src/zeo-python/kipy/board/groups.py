# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Group management operations for organizing board items.
"""

from typing import TYPE_CHECKING, List, Optional, Sequence, Union
from dataclasses import dataclass, field

from kipy.proto.board import board_commands_pb2
from kipy.proto.common.types import KIID
from kipy.board_types import unwrap
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.board.base import Board


@dataclass
class GroupInfo:
    """Information about a group on the board."""
    id: KIID
    name: str
    member_ids: List[KIID] = field(default_factory=list)
    locked: bool = False


class GroupOperations:
    """Group management operations for organizing board items."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_all(self) -> List[GroupInfo]:
        """Get all groups on the board.

        Returns:
            List of GroupInfo objects

        Example:
            >>> groups = board.groups.get_all()
            >>> for group in groups:
            ...     print(f"{group.name}: {len(group.member_ids)} items")
        """
        cmd = board_commands_pb2.GetGroups()
        cmd.board.CopyFrom(self._board._doc)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetGroupsResponse
        )

        result = []
        for group_proto in response.groups:
            result.append(GroupInfo(
                id=group_proto.id,
                name=group_proto.name,
                member_ids=list(group_proto.member_ids),
                locked=group_proto.locked,
            ))

        return result

    def get_by_name(self, name: str) -> Optional[GroupInfo]:
        """Find a group by name.

        Args:
            name: Group name to find

        Returns:
            GroupInfo if found, None otherwise
        """
        for group in self.get_all():
            if group.name == name:
                return group
        return None

    def create(
        self,
        name: str,
        items: Optional[Sequence[Union[Wrapper, KIID]]] = None,
    ) -> GroupInfo:
        """Create a new group.

        Args:
            name: Name for the new group
            items: Items to add to the group (Wrapper objects or KIIDs)

        Returns:
            GroupInfo for the created group

        Example:
            >>> # Create group from footprints
            >>> fps = [board.footprints.get_by_reference(r) for r in ["U1", "U2"]]
            >>> group = board.groups.create("MCU Section", fps)
            >>> print(f"Created group with ID: {group.id}")
        """
        cmd = board_commands_pb2.CreateGroup()
        cmd.board.CopyFrom(self._board._doc)
        cmd.name = name

        if items:
            for item in items:
                if isinstance(item, Wrapper):
                    cmd.member_ids.append(item.id)
                else:
                    cmd.member_ids.append(item)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.CreateGroupResponse
        )

        return GroupInfo(
            id=response.group_id,
            name=response.group.name,
            member_ids=list(response.group.member_ids),
            locked=response.group.locked,
        )

    def delete(
        self,
        group: Union[GroupInfo, KIID],
        ungroup_members: bool = True,
    ) -> bool:
        """Delete a group.

        Args:
            group: GroupInfo or group KIID to delete
            ungroup_members: If True, remove items from group first (default)

        Returns:
            True if deletion was successful

        Note:
            Deleting a group does NOT delete its member items.
        """
        cmd = board_commands_pb2.DeleteGroup()
        cmd.board.CopyFrom(self._board._doc)
        cmd.ungroup_members = ungroup_members

        if isinstance(group, GroupInfo):
            cmd.group_id.CopyFrom(group.id)
        else:
            cmd.group_id.CopyFrom(group)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.DeleteGroupResponse
        )

        return response.success

    def add_items(
        self,
        group: Union[GroupInfo, KIID],
        items: Sequence[Union[Wrapper, KIID]],
    ) -> int:
        """Add items to an existing group.

        Args:
            group: GroupInfo or group KIID
            items: Items to add (Wrapper objects or KIIDs)

        Returns:
            Number of items successfully added

        Note:
            Items already in another group cannot be added.

        Example:
            >>> group = board.groups.get_by_name("Power Section")
            >>> caps = board.footprints.get_by_value("100nF")
            >>> added = board.groups.add_items(group, caps)
            >>> print(f"Added {added} capacitors to group")
        """
        cmd = board_commands_pb2.AddToGroup()
        cmd.board.CopyFrom(self._board._doc)

        if isinstance(group, GroupInfo):
            cmd.group_id.CopyFrom(group.id)
        else:
            cmd.group_id.CopyFrom(group)

        for item in items:
            if isinstance(item, Wrapper):
                cmd.item_ids.append(item.id)
            else:
                cmd.item_ids.append(item)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.AddToGroupResponse
        )

        return response.items_added

    def remove_items(
        self,
        group: Union[GroupInfo, KIID],
        items: Sequence[Union[Wrapper, KIID]],
    ) -> int:
        """Remove items from a group.

        Args:
            group: GroupInfo or group KIID
            items: Items to remove (Wrapper objects or KIIDs)

        Returns:
            Number of items successfully removed
        """
        cmd = board_commands_pb2.RemoveFromGroup()
        cmd.board.CopyFrom(self._board._doc)

        if isinstance(group, GroupInfo):
            cmd.group_id.CopyFrom(group.id)
        else:
            cmd.group_id.CopyFrom(group)

        for item in items:
            if isinstance(item, Wrapper):
                cmd.item_ids.append(item.id)
            else:
                cmd.item_ids.append(item)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.RemoveFromGroupResponse
        )

        return response.items_removed

    def get_members(
        self,
        group: Union[GroupInfo, KIID],
    ) -> List[Wrapper]:
        """Get all items in a group.

        Args:
            group: GroupInfo or group KIID

        Returns:
            List of board items (Wrapper objects) in the group

        Example:
            >>> group = board.groups.get_by_name("MCU Section")
            >>> members = board.groups.get_members(group)
            >>> for item in members:
            ...     print(f"{type(item).__name__}: {item}")
        """
        cmd = board_commands_pb2.GetGroupMembers()
        cmd.board.CopyFrom(self._board._doc)

        if isinstance(group, GroupInfo):
            cmd.group_id.CopyFrom(group.id)
        else:
            cmd.group_id.CopyFrom(group)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetGroupMembersResponse
        )

        result = []
        for item_any in response.items:
            try:
                item = unwrap(item_any)
                result.append(item)
            except (ValueError, NotImplementedError):
                pass

        return result

    def rename(self, group: Union[GroupInfo, KIID], new_name: str) -> bool:
        """Rename a group.

        Args:
            group: GroupInfo or group KIID
            new_name: New name for the group

        Returns:
            True if rename was successful
        """
        # Get current group info
        group_info = group if isinstance(group, GroupInfo) else None

        if group_info is None:
            for g in self.get_all():
                if g.id == group:
                    group_info = g
                    break

        if group_info is None:
            return False

        # Delete and recreate with new name
        # Note: A proper implementation would use an UpdateGroup command
        # but since that doesn't exist, we work around it
        members = [m.id for m in self.get_members(group_info)]

        if self.delete(group_info, ungroup_members=True):
            new_group = self.create(new_name, members)
            return new_group is not None

        return False

    def clear(self, group: Union[GroupInfo, KIID]) -> int:
        """Remove all items from a group without deleting it.

        Args:
            group: GroupInfo or group KIID

        Returns:
            Number of items removed
        """
        # Get current members
        members = self.get_members(group)

        if not members:
            return 0

        return self.remove_items(group, members)
