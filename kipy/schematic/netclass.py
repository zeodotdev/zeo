# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Net class management operations.
"""

from typing import TYPE_CHECKING, List, Optional, Dict

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


# Line style names mapping
LINE_STYLES = {
    "solid": schematic_commands_pb2.SLS_SOLID,
    "dash": schematic_commands_pb2.SLS_DASH,
    "dot": schematic_commands_pb2.SLS_DOT,
    "dash_dot": schematic_commands_pb2.SLS_DASH_DOT,
    "dash_dot_dot": schematic_commands_pb2.SLS_DASH_DOT_DOT,
}

LINE_STYLE_NAMES = {v: k for k, v in LINE_STYLES.items()}


class NetClassOperations:
    """Net class management operations.

    Provides CRUD operations for net classes and net class assignments
    (pattern-based net to netclass mappings).

    Net classes define visual and electrical properties for groups of nets:
    - Wire thickness (mils)
    - Bus thickness (mils)
    - Color
    - Line style (solid, dash, dot, etc.)

    Net class assignments map net name patterns to net classes using
    wildcard matching (e.g., "VCC*" matches "VCC", "VCC_3V3", etc.).
    """

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Net Class CRUD
    # =========================================================================

    def get_all(self) -> dict:
        """Get all net classes.

        Returns:
            Dictionary with:
            - default: The default netclass (always exists)
            - netclasses: List of user-defined netclass dicts

        Example:
            >>> nc = sch.netclass.get_all()
            >>> print(nc['default']['name'])
            'Default'
            >>> for c in nc['netclasses']:
            ...     print(f"{c['name']}: {c['wire_width_mils']} mils")
        """
        cmd = schematic_commands_pb2.GetNetClasses()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetNetClassesResponse)

        def nc_to_dict(nc) -> dict:
            result = {"name": nc.name}
            if nc.HasField("description"):
                result["description"] = nc.description
            if nc.HasField("wire_width_mils"):
                result["wire_width_mils"] = nc.wire_width_mils
            if nc.HasField("bus_width_mils"):
                result["bus_width_mils"] = nc.bus_width_mils
            if nc.HasField("color"):
                result["color"] = nc.color
            if nc.HasField("line_style"):
                result["line_style"] = LINE_STYLE_NAMES.get(nc.line_style, "solid")
            if nc.HasField("priority"):
                result["priority"] = nc.priority
            return result

        return {
            "default": nc_to_dict(response.default_netclass),
            "netclasses": [nc_to_dict(nc) for nc in response.netclasses],
        }

    def get(self, name: str) -> Optional[dict]:
        """Get a specific net class by name.

        Args:
            name: Net class name

        Returns:
            Net class dict or None if not found

        Example:
            >>> nc = sch.netclass.get("Power")
            >>> print(nc['wire_width_mils'])
        """
        all_nc = self.get_all()
        if name == "Default":
            return all_nc["default"]
        for nc in all_nc["netclasses"]:
            if nc["name"] == name:
                return nc
        return None

    def create(
        self,
        name: str,
        wire_width_mils: Optional[int] = None,
        bus_width_mils: Optional[int] = None,
        color: Optional[str] = None,
        line_style: Optional[str] = None,
        description: Optional[str] = None,
        priority: Optional[int] = None,
    ) -> None:
        """Create a new net class.

        Args:
            name: Net class name (must be unique)
            wire_width_mils: Wire thickness in mils (default: 6)
            bus_width_mils: Bus thickness in mils (default: 12)
            color: Color in hex format (#RRGGBB) or "transparent"
            line_style: Line style: "solid", "dash", "dot", "dash_dot", "dash_dot_dot"
            description: Optional description
            priority: Priority for multi-netclass resolution (lower = higher priority)

        Example:
            >>> sch.netclass.create("Power",
            ...     wire_width_mils=10,
            ...     bus_width_mils=20,
            ...     color="#FF0000",
            ...     line_style="solid")
        """
        self._set_netclass(
            name=name,
            wire_width_mils=wire_width_mils,
            bus_width_mils=bus_width_mils,
            color=color,
            line_style=line_style,
            description=description,
            priority=priority,
        )

    def update(
        self,
        name: str,
        wire_width_mils: Optional[int] = None,
        bus_width_mils: Optional[int] = None,
        color: Optional[str] = None,
        line_style: Optional[str] = None,
        description: Optional[str] = None,
        priority: Optional[int] = None,
    ) -> None:
        """Update an existing net class.

        Only provided parameters are updated; others remain unchanged.

        Args:
            name: Net class name (must exist, or "Default" to update default)
            wire_width_mils: Wire thickness in mils
            bus_width_mils: Bus thickness in mils
            color: Color in hex format (#RRGGBB) or "transparent"
            line_style: Line style: "solid", "dash", "dot", "dash_dot", "dash_dot_dot"
            description: Optional description
            priority: Priority for multi-netclass resolution

        Example:
            >>> # Update default netclass wire width
            >>> sch.netclass.update("Default", wire_width_mils=8)
            >>> # Update a custom netclass color
            >>> sch.netclass.update("Power", color="#00FF00")
        """
        self._set_netclass(
            name=name,
            wire_width_mils=wire_width_mils,
            bus_width_mils=bus_width_mils,
            color=color,
            line_style=line_style,
            description=description,
            priority=priority,
        )

    def _set_netclass(
        self,
        name: str,
        wire_width_mils: Optional[int] = None,
        bus_width_mils: Optional[int] = None,
        color: Optional[str] = None,
        line_style: Optional[str] = None,
        description: Optional[str] = None,
        priority: Optional[int] = None,
    ) -> None:
        """Internal method to set netclass properties."""
        cmd = schematic_commands_pb2.SetNetClass()
        cmd.document.CopyFrom(self._doc)
        cmd.netclass.name = name

        if description is not None:
            cmd.netclass.description = description
        if wire_width_mils is not None:
            cmd.netclass.wire_width_mils = wire_width_mils
        if bus_width_mils is not None:
            cmd.netclass.bus_width_mils = bus_width_mils
        if color is not None:
            cmd.netclass.color = color
        if line_style is not None:
            if line_style in LINE_STYLES:
                cmd.netclass.line_style = LINE_STYLES[line_style]
        if priority is not None:
            cmd.netclass.priority = priority

        self._kicad.send(cmd, Empty)

    def delete(self, name: str) -> None:
        """Delete a net class.

        Note: The "Default" net class cannot be deleted.

        Args:
            name: Net class name to delete

        Example:
            >>> sch.netclass.delete("Power")
        """
        if name == "Default":
            raise ValueError("Cannot delete the default netclass")

        cmd = schematic_commands_pb2.DeleteNetClass()
        cmd.document.CopyFrom(self._doc)
        cmd.name = name
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Net Class Assignments (Pattern -> NetClass)
    # =========================================================================

    def get_assignments(self) -> List[dict]:
        """Get all net class pattern assignments.

        Returns:
            List of assignment dicts with 'pattern' and 'netclass' keys

        Example:
            >>> assignments = sch.netclass.get_assignments()
            >>> for a in assignments:
            ...     print(f"{a['pattern']} -> {a['netclass']}")
        """
        cmd = schematic_commands_pb2.GetNetClassAssignments()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(
            cmd, schematic_commands_pb2.GetNetClassAssignmentsResponse
        )

        return [
            {"pattern": a.pattern, "netclass": a.netclass}
            for a in response.assignments
        ]

    def set_assignments(self, assignments: List[dict]) -> None:
        """Replace all net class pattern assignments.

        Args:
            assignments: List of dicts with 'pattern' and 'netclass' keys

        Example:
            >>> sch.netclass.set_assignments([
            ...     {"pattern": "VCC*", "netclass": "Power"},
            ...     {"pattern": "GND*", "netclass": "Power"},
            ...     {"pattern": "CLK*", "netclass": "HighSpeed"},
            ... ])
        """
        cmd = schematic_commands_pb2.SetNetClassAssignments()
        cmd.document.CopyFrom(self._doc)

        for a in assignments:
            assignment = cmd.assignments.add()
            assignment.pattern = a.get("pattern", "")
            assignment.netclass = a.get("netclass", "Default")

        self._kicad.send(cmd, Empty)

    def add_assignment(self, pattern: str, netclass: str) -> None:
        """Add a single net class pattern assignment.

        Args:
            pattern: Wildcard pattern to match net names (e.g., "VCC*", "*_CLK")
            netclass: Net class name to assign

        Example:
            >>> sch.netclass.add_assignment("VCC*", "Power")
            >>> sch.netclass.add_assignment("*_DATA*", "Signal")
        """
        cmd = schematic_commands_pb2.AddNetClassAssignment()
        cmd.document.CopyFrom(self._doc)
        cmd.assignment.pattern = pattern
        cmd.assignment.netclass = netclass
        self._kicad.send(cmd, Empty)

    def remove_assignment(self, pattern: str) -> None:
        """Remove a net class pattern assignment.

        Args:
            pattern: Exact pattern to remove

        Example:
            >>> sch.netclass.remove_assignment("VCC*")
        """
        cmd = schematic_commands_pb2.RemoveNetClassAssignment()
        cmd.document.CopyFrom(self._doc)
        cmd.pattern = pattern
        self._kicad.send(cmd, Empty)

    def assign_net(self, net_name: str, netclass: str) -> None:
        """Assign a specific net to a net class.

        This is a convenience method that creates a pattern assignment
        for an exact net name (no wildcards).

        Args:
            net_name: Exact net name
            netclass: Net class name

        Example:
            >>> sch.netclass.assign_net("VCC_3V3", "Power")
        """
        self.add_assignment(net_name, netclass)
