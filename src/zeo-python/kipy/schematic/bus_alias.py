# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Bus alias management operations.
"""

from typing import TYPE_CHECKING, List, Optional, Dict

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class BusAliasOperations:
    """Bus alias management operations.

    Provides CRUD operations for bus alias definitions.

    Bus aliases define named groups of signals that can be used together
    as a bus. For example, a "DATA_BUS" alias could contain signals
    D0, D1, D2, ..., D7.

    Example:
        >>> # Create a data bus alias
        >>> sch.bus_alias.create("DATA_BUS", ["D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"])
        >>>
        >>> # Get all bus aliases
        >>> aliases = sch.bus_alias.get_all()
        >>> for alias in aliases:
        ...     print(f"{alias['name']}: {alias['members']}")
    """

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    def get_all(self) -> List[dict]:
        """Get all bus aliases.

        Returns:
            List of bus alias dicts with 'name' and 'members' keys.

        Example:
            >>> aliases = sch.bus_alias.get_all()
            >>> for alias in aliases:
            ...     print(f"{alias['name']}: {', '.join(alias['members'])}")
        """
        cmd = schematic_commands_pb2.GetBusAliases()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetBusAliasesResponse)

        return [
            {"name": alias.name, "members": list(alias.members)}
            for alias in response.aliases
        ]

    def get(self, name: str) -> Optional[dict]:
        """Get a specific bus alias by name.

        Args:
            name: Bus alias name

        Returns:
            Bus alias dict or None if not found

        Example:
            >>> alias = sch.bus_alias.get("DATA_BUS")
            >>> if alias:
            ...     print(f"Members: {alias['members']}")
        """
        all_aliases = self.get_all()
        for alias in all_aliases:
            if alias["name"] == name:
                return alias
        return None

    def create(self, name: str, members: List[str]) -> None:
        """Create a new bus alias.

        Args:
            name: Bus alias name (must be unique)
            members: List of member signal names

        Example:
            >>> sch.bus_alias.create("ADDR_BUS", ["A0", "A1", "A2", "A3"])
            >>> sch.bus_alias.create("DATA_BUS", ["D0", "D1", "D2", "D3"])
        """
        cmd = schematic_commands_pb2.SetBusAlias()
        cmd.document.CopyFrom(self._doc)
        cmd.alias.name = name
        for member in members:
            cmd.alias.members.append(member)
        self._kicad.send(cmd, Empty)

    def update(self, name: str, members: List[str]) -> None:
        """Update an existing bus alias.

        Args:
            name: Bus alias name (must exist)
            members: New list of member signal names (replaces existing)

        Example:
            >>> # Expand data bus to 16 bits
            >>> sch.bus_alias.update("DATA_BUS", [f"D{i}" for i in range(16)])
        """
        # SetBusAlias handles both create and update
        self.create(name, members)

    def delete(self, name: str) -> None:
        """Delete a bus alias.

        Args:
            name: Bus alias name to delete

        Example:
            >>> sch.bus_alias.delete("OLD_BUS")
        """
        cmd = schematic_commands_pb2.DeleteBusAlias()
        cmd.document.CopyFrom(self._doc)
        cmd.name = name
        self._kicad.send(cmd, Empty)

    def set_all(self, aliases: List[dict]) -> None:
        """Replace all bus aliases at once.

        Args:
            aliases: List of dicts with 'name' and 'members' keys

        Example:
            >>> sch.bus_alias.set_all([
            ...     {"name": "DATA_BUS", "members": ["D0", "D1", "D2", "D3"]},
            ...     {"name": "ADDR_BUS", "members": ["A0", "A1", "A2", "A3"]},
            ... ])
        """
        cmd = schematic_commands_pb2.SetBusAliases()
        cmd.document.CopyFrom(self._doc)

        for alias_data in aliases:
            alias = cmd.aliases.add()
            alias.name = alias_data.get("name", "")
            for member in alias_data.get("members", []):
                alias.members.append(member)

        self._kicad.send(cmd, Empty)

    def add_member(self, alias_name: str, member: str) -> None:
        """Add a member to an existing bus alias.

        Args:
            alias_name: Bus alias name
            member: Member signal name to add

        Example:
            >>> sch.bus_alias.add_member("DATA_BUS", "D8")
        """
        alias = self.get(alias_name)
        if alias is None:
            raise ValueError(f"Bus alias not found: {alias_name}")

        members = alias["members"]
        if member not in members:
            members.append(member)
            self.update(alias_name, members)

    def remove_member(self, alias_name: str, member: str) -> None:
        """Remove a member from an existing bus alias.

        Args:
            alias_name: Bus alias name
            member: Member signal name to remove

        Example:
            >>> sch.bus_alias.remove_member("DATA_BUS", "D7")
        """
        alias = self.get(alias_name)
        if alias is None:
            raise ValueError(f"Bus alias not found: {alias_name}")

        members = alias["members"]
        if member in members:
            members.remove(member)
            self.update(alias_name, members)
