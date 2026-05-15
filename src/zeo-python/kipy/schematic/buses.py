# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Bus operations for schematic design.

Provides functionality for creating and managing buses, bus entries,
and bus-related connections.
"""

from typing import TYPE_CHECKING, List, Optional, Tuple, Dict
from dataclasses import dataclass
import re

from kipy.geometry import Vector2
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


@dataclass
class BusDefinition:
    """Definition of a bus."""
    name: str
    members: List[str]
    is_vector: bool = True
    prefix: str = ""
    start_index: int = 0
    end_index: int = 0


class BusOperations:
    """Bus operations for schematic design."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Bus Information
    # =========================================================================

    def get_all(self) -> List[Dict]:
        """Get all buses in the schematic.

        Returns:
            List of bus info dictionaries with:
            - name: Bus name
            - is_vector: True if vector bus (D[0..7])
            - members: List of member net names
            - prefix: Vector prefix (if vector)
            - start_index, end_index: Vector range (if vector)

        Example:
            >>> buses = sch.buses.get_all()
            >>> for bus in buses:
            ...     print(f"{bus['name']}: {len(bus['members'])} members")
        """
        try:
            response = self._sch.connectivity.get_buses()
            buses = []
            for bus in response.buses:
                buses.append({
                    "name": bus.name,
                    "is_vector": bus.is_vector,
                    "members": list(bus.members),
                    "prefix": bus.vector_prefix,
                    "start_index": bus.vector_start,
                    "end_index": bus.vector_end,
                })
            return buses
        except Exception:
            return []

    def get_members(self, bus_name: str) -> List[str]:
        """Get member nets of a bus.

        Args:
            bus_name: Name of the bus (e.g., "D[0..7]" or "{DATA, ADDR}")

        Returns:
            List of member net names

        Example:
            >>> members = sch.buses.get_members("D[0..7]")
            >>> print(members)  # ['D0', 'D1', ..., 'D7']
        """
        try:
            response = self._sch.connectivity.get_bus_members(bus_name)
            return list(response.members)
        except Exception:
            # Try to parse locally
            return self._parse_bus_members(bus_name)

    def _parse_bus_members(self, bus_name: str) -> List[str]:
        """Parse bus member names from bus name string."""
        members = []

        # Vector bus: prefix[start..end]
        vector_match = re.match(r'(\w+)\[(\d+)\.\.(\d+)\]', bus_name)
        if vector_match:
            prefix = vector_match.group(1)
            start = int(vector_match.group(2))
            end = int(vector_match.group(3))
            for i in range(min(start, end), max(start, end) + 1):
                members.append(f"{prefix}{i}")
            return members

        # Group bus: {sig1, sig2, sig3}
        group_match = re.match(r'\{([^}]+)\}', bus_name)
        if group_match:
            return [m.strip() for m in group_match.group(1).split(',')]

        return members

    # =========================================================================
    # Bus Definition Helpers
    # =========================================================================

    def define_vector_bus(
        self,
        prefix: str,
        start_index: int,
        end_index: int,
    ) -> BusDefinition:
        """Define a vector bus.

        Args:
            prefix: Signal prefix (e.g., "D" for D0, D1, ...)
            start_index: Starting index
            end_index: Ending index

        Returns:
            BusDefinition object

        Example:
            >>> data_bus = sch.buses.define_vector_bus("D", 0, 7)
            >>> print(data_bus.name)  # "D[0..7]"
            >>> print(data_bus.members)  # ['D0', 'D1', ..., 'D7']
        """
        name = f"{prefix}[{start_index}..{end_index}]"
        members = [f"{prefix}{i}" for i in range(start_index, end_index + 1)]

        return BusDefinition(
            name=name,
            members=members,
            is_vector=True,
            prefix=prefix,
            start_index=start_index,
            end_index=end_index,
        )

    def define_group_bus(
        self,
        name: str,
        members: List[str],
    ) -> BusDefinition:
        """Define a group bus (named collection of signals).

        Args:
            name: Bus name
            members: List of signal names

        Returns:
            BusDefinition object

        Example:
            >>> ctrl_bus = sch.buses.define_group_bus("CTRL", ["RD", "WR", "CS"])
            >>> print(ctrl_bus.name)  # "{RD, WR, CS}"
        """
        bus_name = "{" + ", ".join(members) + "}"

        return BusDefinition(
            name=bus_name,
            members=members,
            is_vector=False,
        )

    # =========================================================================
    # Bus Line Operations
    # =========================================================================

    def add_bus_line(
        self,
        start: Vector2,
        end: Vector2,
    ) -> Wrapper:
        """Add a bus line (thick wire for bus routing).

        Note: Bus lines are visually distinguished from regular wires
        but the IPC API may not fully support creating them directly.
        This creates a line on the BUS layer.

        Args:
            start: Start position
            end: End position

        Returns:
            The created line object

        Example:
            >>> bus_line = sch.buses.add_bus_line(
            ...     Vector2.from_xy_mm(100, 50),
            ...     Vector2.from_xy_mm(100, 150)
            ... )
        """
        # Note: This would need proper proto support for bus layer
        # For now, this is a placeholder that documents the intended API
        from kipy.schematic_types import Wire
        wire = Wire.create(start, end)
        # wire.layer = SchematicLayer.LAYER_BUS  # Would need this
        created = self._sch.crud.create_items(wire)
        return created[0] if created else wire

    def add_bus_entry(
        self,
        position: Vector2,
        direction: str = "right_down",
        entry_type: str = "wire_to_bus",
    ) -> Wrapper:
        """Add a bus entry (diagonal connector from bus to wire).

        Args:
            position: Position of the bus entry
            direction: Entry direction:
                - "right_down": Bus on left, wire exits down-right
                - "right_up": Bus on left, wire exits up-right
                - "left_down": Bus on right, wire exits down-left
                - "left_up": Bus on right, wire exits up-left
            entry_type: "wire_to_bus" or "bus_to_bus"

        Returns:
            The created bus entry object

        Example:
            >>> entry = sch.buses.add_bus_entry(pos, direction="right_down")
        """
        from kipy.schematic_types import BusEntry

        if BusEntry is None:
            raise NotImplementedError("BusEntry support requires proto regeneration")

        # Bus entry size is typically 100 mils (2.54mm) diagonal
        size_nm = 2_540_000  # 2.54mm = 100 mils

        if direction == "right_down":
            size_x, size_y = size_nm, size_nm
        elif direction == "right_up":
            size_x, size_y = size_nm, -size_nm
        elif direction == "left_down":
            size_x, size_y = -size_nm, size_nm
        elif direction == "left_up":
            size_x, size_y = -size_nm, -size_nm
        else:
            raise ValueError(f"Invalid direction: {direction}")

        size = Vector2.from_xy(size_x, size_y)

        if entry_type == "wire_to_bus":
            entry = BusEntry.create_wire_to_bus(position, size)
        elif entry_type == "bus_to_bus":
            entry = BusEntry.create_bus_to_bus(position, size)
        else:
            raise ValueError(f"Invalid entry_type: {entry_type}")

        created = self._sch.crud.create_items(entry)
        return created[0] if created else entry

    # =========================================================================
    # Bus Label Operations
    # =========================================================================

    def add_bus_label(
        self,
        bus_def: BusDefinition,
        position: Vector2,
        label_type: str = "local",
    ) -> Wrapper:
        """Add a label for a bus.

        Args:
            bus_def: BusDefinition object
            position: Position for the label
            label_type: "local" or "global"

        Returns:
            The created label

        Example:
            >>> data_bus = sch.buses.define_vector_bus("D", 0, 7)
            >>> label = sch.buses.add_bus_label(data_bus, pos)
        """
        return self._sch.labels.add(bus_def.name, position, label_type)

    def add_member_labels(
        self,
        bus_def: BusDefinition,
        positions: List[Vector2],
        label_type: str = "local",
    ) -> List[Wrapper]:
        """Add labels for individual bus members.

        Args:
            bus_def: BusDefinition object
            positions: List of positions (one per member)
            label_type: "local" or "global"

        Returns:
            List of created labels

        Example:
            >>> data_bus = sch.buses.define_vector_bus("D", 0, 7)
            >>> # Create positions for each member
            >>> positions = [Vector2.from_xy_mm(100 + i*10, 100) for i in range(8)]
            >>> labels = sch.buses.add_member_labels(data_bus, positions)
        """
        if len(positions) < len(bus_def.members):
            raise ValueError(
                f"Need {len(bus_def.members)} positions, got {len(positions)}"
            )

        labels = []
        for member, pos in zip(bus_def.members, positions):
            label = self._sch.labels.add(member, pos, label_type)
            labels.append(label)

        return labels

    # =========================================================================
    # Bus Routing Helpers
    # =========================================================================

    def create_bus_tap(
        self,
        bus_position: Vector2,
        wire_end: Vector2,
        net_name: str,
    ) -> Tuple[Wrapper, Wrapper, Wrapper]:
        """Create a complete bus tap (entry + wire + label).

        Args:
            bus_position: Position on the bus line
            wire_end: End position for the signal wire
            net_name: Name of the signal (bus member)

        Returns:
            Tuple of (bus_entry, wire, label)

        Example:
            >>> # Tap D0 from bus at (100, 80) to pin at (120, 90)
            >>> entry, wire, label = sch.buses.create_bus_tap(
            ...     Vector2.from_xy_mm(100, 80),
            ...     Vector2.from_xy_mm(120, 90),
            ...     "D0"
            ... )
        """
        # Determine entry direction based on wire end position
        dx = wire_end.x - bus_position.x
        dy = wire_end.y - bus_position.y

        if dx >= 0 and dy >= 0:
            direction = "right_down"
        elif dx >= 0 and dy < 0:
            direction = "right_up"
        elif dx < 0 and dy >= 0:
            direction = "left_down"
        else:
            direction = "left_up"

        # Create bus entry
        entry = self.add_bus_entry(bus_position, direction)

        # Calculate entry end point
        entry_size_nm = 2_540_000
        if "right" in direction:
            entry_end_x = bus_position.x + entry_size_nm
        else:
            entry_end_x = bus_position.x - entry_size_nm

        if "down" in direction:
            entry_end_y = bus_position.y + entry_size_nm
        else:
            entry_end_y = bus_position.y - entry_size_nm

        entry_end = Vector2.from_xy(entry_end_x, entry_end_y)

        # Create wire from entry to destination
        wire = self._sch.wiring.add_wire(entry_end, wire_end)

        # Add label at wire end
        label = self._sch.labels.add_local(net_name, wire_end)

        return (entry, wire, label)

    def expand_bus_to_pins(
        self,
        bus_def: BusDefinition,
        bus_start: Vector2,
        pin_positions: List[Vector2],
        vertical_bus: bool = True,
    ) -> Dict:
        """Expand a bus to connect to individual pins.

        Creates bus line, entries, wires, and labels to connect
        each bus member to a pin position.

        Args:
            bus_def: BusDefinition object
            bus_start: Starting position of the bus line
            pin_positions: List of pin positions (one per member)
            vertical_bus: True for vertical bus line, False for horizontal

        Returns:
            Dictionary with created items:
            - bus_line: The bus line
            - entries: List of bus entries
            - wires: List of wires
            - labels: List of labels

        Example:
            >>> data_bus = sch.buses.define_vector_bus("D", 0, 7)
            >>> # Pin positions for 8-bit data port
            >>> pins = [sch.wiring.get_pin_position(chip, f"D{i}") for i in range(8)]
            >>> result = sch.buses.expand_bus_to_pins(data_bus, bus_pos, pins)
        """
        if len(pin_positions) != len(bus_def.members):
            raise ValueError(
                f"Need {len(bus_def.members)} pin positions, got {len(pin_positions)}"
            )

        result = {
            "bus_line": None,
            "entries": [],
            "wires": [],
            "labels": [],
        }

        # Calculate bus line extent
        if vertical_bus:
            # Vertical bus: spans Y range of pins
            y_coords = [p.y for p in pin_positions]
            bus_end = Vector2.from_xy(
                bus_start.x,
                max(y_coords) + 5_000_000  # 5mm past last pin
            )
        else:
            # Horizontal bus: spans X range of pins
            x_coords = [p.x for p in pin_positions]
            bus_end = Vector2.from_xy(
                max(x_coords) + 5_000_000,
                bus_start.y
            )

        # Create bus line
        result["bus_line"] = self.add_bus_line(bus_start, bus_end)

        # Create taps for each member
        for member, pin_pos in zip(bus_def.members, pin_positions):
            # Find closest point on bus to this pin
            if vertical_bus:
                tap_pos = Vector2.from_xy(bus_start.x, pin_pos.y)
            else:
                tap_pos = Vector2.from_xy(pin_pos.x, bus_start.y)

            entry, wire, label = self.create_bus_tap(tap_pos, pin_pos, member)
            result["entries"].append(entry)
            result["wires"].append(wire)
            result["labels"].append(label)

        return result

    # =========================================================================
    # Bus Analysis
    # =========================================================================

    def analyze_bus_usage(self) -> Dict:
        """Analyze bus usage in the schematic.

        Returns:
            Dictionary with:
            - buses: List of buses found
            - unused_members: Members defined but not connected
            - connection_count: Number of connections per bus

        Example:
            >>> analysis = sch.buses.analyze_bus_usage()
            >>> for bus in analysis['buses']:
            ...     print(f"{bus['name']}: {bus['connections']} connections")
        """
        result = {
            "buses": [],
            "unused_members": {},
            "total_bus_signals": 0,
        }

        buses = self.get_all()

        for bus in buses:
            bus_name = bus["name"]
            members = bus["members"]

            # Count connections for each member
            connections = 0
            unused = []

            for member in members:
                try:
                    net_items = self._sch.connectivity.get_net_items(member)
                    item_count = len(net_items.item_ids)
                    if item_count > 0:
                        connections += 1
                    else:
                        unused.append(member)
                except Exception:
                    unused.append(member)

            result["buses"].append({
                "name": bus_name,
                "member_count": len(members),
                "connections": connections,
                "unused_members": unused,
            })

            if unused:
                result["unused_members"][bus_name] = unused

            result["total_bus_signals"] += len(members)

        return result

    # =========================================================================
    # Bus Entry CRUD Operations
    # =========================================================================

    def get_bus_entries(self) -> List[Wrapper]:
        """Get all bus entries in the schematic.

        Returns:
            List of bus entry objects (both bus-wire and bus-bus entries)

        Example:
            >>> entries = sch.buses.get_bus_entries()
            >>> print(f"Found {len(entries)} bus entries")
        """
        return list(self._sch.crud.get_bus_entries())

    def update_bus_entry(self, entry: Wrapper) -> Wrapper:
        """Update a bus entry's properties.

        Args:
            entry: Bus entry with updated properties

        Returns:
            Updated bus entry object

        Example:
            >>> entries = sch.buses.get_bus_entries()
            >>> entry = entries[0]
            >>> entry.position = Vector2.from_xy_mm(100, 80)
            >>> sch.buses.update_bus_entry(entry)
        """
        updated = self._sch.crud.update_items(entry)
        return updated[0] if updated else entry

    def delete_bus_entry(self, entry: Wrapper) -> None:
        """Delete a bus entry.

        Args:
            entry: Bus entry to delete

        Example:
            >>> entries = sch.buses.get_bus_entries()
            >>> sch.buses.delete_bus_entry(entries[0])
        """
        self._sch.crud.remove_items(entry)

    def delete_bus_entries(self, entries: List[Wrapper]) -> None:
        """Delete multiple bus entries.

        Args:
            entries: List of bus entries to delete

        Example:
            >>> entries = sch.buses.get_bus_entries()
            >>> sch.buses.delete_bus_entries(entries)
        """
        if entries:
            self._sch.crud.remove_items(entries)
