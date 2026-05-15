# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Wire and connection operations.
"""

from typing import TYPE_CHECKING, List, Optional, Tuple

from kipy.schematic_types import Wire, Junction, NoConnect, Symbol
from kipy.geometry import Vector2
from kipy.wrapper import Wrapper
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class WiringOperations:
    """Wire and connection operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Basic Wire Operations
    # =========================================================================

    def add_wire(self, start: Vector2, end: Vector2) -> Wire:
        """Create a wire between two points.

        Args:
            start: Start point
            end: End point

        Returns:
            The created Wire object

        Example:
            >>> wire = sch.wiring.add_wire(
            ...     Vector2.from_xy_mm(100, 80),
            ...     Vector2.from_xy_mm(120, 80)
            ... )
        """
        wire = Wire.create(start, end)
        created = self._sch.crud.create_items(wire)
        if created:
            return created[0]
        return wire

    def add_wires(self, points: List[Vector2]) -> List[Wire]:
        """Create connected wire segments through multiple points.

        Args:
            points: List of points to connect

        Returns:
            List of created Wire objects

        Example:
            >>> wires = sch.wiring.add_wires([
            ...     Vector2.from_xy_mm(100, 80),
            ...     Vector2.from_xy_mm(120, 80),
            ...     Vector2.from_xy_mm(120, 100),
            ... ])
        """
        if len(points) < 2:
            return []

        wires = []
        for i in range(len(points) - 1):
            wire = Wire.create(points[i], points[i + 1])
            wires.append(wire)

        created = self._sch.crud.create_items(wires)
        return created if created else wires

    def add_junction(self, position: Vector2) -> Optional[Wrapper]:
        """Add a junction (connection point).

        Args:
            position: Position for the junction

        Returns:
            The created Junction object

        Example:
            >>> junc = sch.wiring.add_junction(Vector2.from_xy_mm(100, 80))
        """
        if Junction is None:
            raise NotImplementedError("Junction support requires proto regeneration")

        junction = Junction.create(position)
        created = self._sch.crud.create_items(junction)
        if created:
            return created[0]
        return junction

    def add_no_connect(self, position: Vector2) -> Optional[Wrapper]:
        """Add a no-connect marker.

        Args:
            position: Position for the marker

        Returns:
            The created NoConnect object

        Example:
            >>> nc = sch.wiring.add_no_connect(Vector2.from_xy_mm(100, 80))
        """
        if NoConnect is None:
            raise NotImplementedError("NoConnect support requires proto regeneration")

        nc = NoConnect.create(position)
        created = self._sch.crud.create_items(nc)
        if created:
            return created[0]
        return nc

    # =========================================================================
    # Pin Position Helpers
    # =========================================================================

    def get_pin_position(self, symbol: Symbol, pin_id: str) -> Optional[Vector2]:
        """Get the exact position of a pin.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name (e.g., "VCC") or number (e.g., "1")

        Returns:
            Vector2 position if found, None otherwise

        Example:
            >>> pos = sch.wiring.get_pin_position(resistor, "1")
        """
        return self._sch.symbols.get_pin_position(symbol, pin_id)

    # =========================================================================
    # Pin-Based Wiring
    # =========================================================================

    def wire_pins(
        self,
        symbol1: Symbol,
        pin_id1: str,
        symbol2: Symbol,
        pin_id2: str,
        style: str = "direct",
    ) -> List[Wire]:
        """Create a direct wire between two pins.

        Draws a single straight wire segment between the exact pin positions.
        For orthogonal (L-shaped) routing, use wire_path() with waypoints instead.

        Args:
            symbol1: First symbol
            pin_id1: Pin name or number on first symbol
            symbol2: Second symbol
            pin_id2: Pin name or number on second symbol
            style: Ignored (kept for backwards compatibility). Always draws direct.

        Returns:
            List containing the created Wire object

        Example:
            >>> r1 = sch.symbols.add("Device:R", pos1)
            >>> r2 = sch.symbols.add("Device:R", pos2)
            >>> wires = sch.wiring.wire_pins(r1, "2", r2, "1")
        """
        # Get exact pin positions using IPC API (in nanometers, no precision loss)
        result1 = self._sch.symbols.get_transformed_pin_position(symbol1, pin_id1)
        result2 = self._sch.symbols.get_transformed_pin_position(symbol2, pin_id2)

        if result1 is None or result2 is None:
            # Fallback to cached positions
            pos1 = self.get_pin_position(symbol1, pin_id1)
            pos2 = self.get_pin_position(symbol2, pin_id2)
        else:
            pos1 = result1['position']
            pos2 = result2['position']

        if pos1 is None or pos2 is None:
            return []

        # Always draw a direct wire - agent should use waypoints for orthogonal routing
        return [self.add_wire(pos1, pos2)]

    def wire_from_pin(
        self,
        symbol: Symbol,
        pin_id: str,
        end_point_mm: Tuple[float, float],
    ) -> Optional[Wire]:
        """Create a wire from a pin to a coordinate.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name or number
            end_point_mm: End point as (x_mm, y_mm) tuple

        Returns:
            The created Wire, or None if pin not found

        Example:
            >>> wire = sch.wiring.wire_from_pin(cap, "1", (100, 80))
        """
        pos = self.get_pin_position(symbol, pin_id)
        if pos is None:
            return None

        end = Vector2.from_xy_mm(end_point_mm[0], end_point_mm[1])
        return self.add_wire(pos, end)

    def wire_to_pin(
        self,
        start_point_mm: Tuple[float, float],
        symbol: Symbol,
        pin_id: str,
    ) -> Optional[Wire]:
        """Create a wire from a coordinate to a pin.

        Args:
            start_point_mm: Start point as (x_mm, y_mm) tuple
            symbol: The symbol containing the pin
            pin_id: Pin name or number

        Returns:
            The created Wire, or None if pin not found

        Example:
            >>> wire = sch.wiring.wire_to_pin((100, 100), cap, "2")
        """
        pos = self.get_pin_position(symbol, pin_id)
        if pos is None:
            return None

        start = Vector2.from_xy_mm(start_point_mm[0], start_point_mm[1])
        return self.add_wire(start, pos)

    def wire_path(
        self,
        start_pin: Tuple[Symbol, str],
        waypoints: List[Tuple[float, float]],
        end_pin: Tuple[Symbol, str],
    ) -> List[Wire]:
        """Create a wire path from one pin through waypoints to another.

        Args:
            start_pin: Tuple of (symbol, pin_id)
            waypoints: List of (x_mm, y_mm) intermediate points
            end_pin: Tuple of (symbol, pin_id)

        Returns:
            List of created Wire objects

        Example:
            >>> wires = sch.wiring.wire_path(
            ...     (r1, "2"),
            ...     [(100, 80), (100, 100)],
            ...     (r2, "1")
            ... )
        """
        start_symbol, start_pin_id = start_pin
        end_symbol, end_pin_id = end_pin

        start_pos = self.get_pin_position(start_symbol, start_pin_id)
        end_pos = self.get_pin_position(end_symbol, end_pin_id)

        if start_pos is None or end_pos is None:
            return []

        points = [start_pos]
        for wp in waypoints:
            points.append(Vector2.from_xy_mm(wp[0], wp[1]))
        points.append(end_pos)

        return self.add_wires(points)

    # =========================================================================
    # Auto-Routing
    # =========================================================================

    def auto_wire(
        self,
        symbol1: Symbol,
        pin_id1: str,
        symbol2: Symbol,
        pin_id2: str,
        style: str = "L",
    ) -> List[Wire]:
        """Automatically wire between two pins using orthogonal routing.

        Creates an L-shaped (or direct) wire path between pins.

        Args:
            symbol1: First symbol
            pin_id1: Pin on first symbol
            symbol2: Second symbol
            pin_id2: Pin on second symbol
            style: Routing style:
                - "direct": Single straight wire
                - "L": Auto-choose L-shape direction
                - "L_horizontal_first": Go horizontal then vertical
                - "L_vertical_first": Go vertical then horizontal

        Returns:
            List of created Wire objects

        Example:
            >>> wires = sch.wiring.auto_wire(mosfet, "D", resistor, "1", style="L")
        """
        pos1 = self.get_pin_position(symbol1, pin_id1)
        pos2 = self.get_pin_position(symbol2, pin_id2)

        if pos1 is None or pos2 is None:
            return []

        x1, y1 = pos1.x / 1e6, pos1.y / 1e6
        x2, y2 = pos2.x / 1e6, pos2.y / 1e6

        # Direct routing
        if style == "direct":
            return [self.add_wire(pos1, pos2)]

        # Check if already aligned
        if abs(x1 - x2) < 0.1 or abs(y1 - y2) < 0.1:
            return [self.add_wire(pos1, pos2)]

        # L-shaped routing
        if style == "L":
            dx = abs(x2 - x1)
            dy = abs(y2 - y1)
            horizontal_first = dx >= dy
        elif style == "L_horizontal_first":
            horizontal_first = True
        elif style == "L_vertical_first":
            horizontal_first = False
        else:
            raise ValueError(f"Unknown style '{style}'")

        if horizontal_first:
            corner = Vector2.from_xy_mm(x2, y1)
        else:
            corner = Vector2.from_xy_mm(x1, y2)

        return self.add_wires([pos1, corner, pos2])

    def auto_wire_to_point(
        self,
        symbol: Symbol,
        pin_id: str,
        point_mm: Tuple[float, float],
        style: str = "L",
    ) -> List[Wire]:
        """Auto-wire from a pin to a coordinate point.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name or number
            point_mm: Target point as (x_mm, y_mm)
            style: Routing style

        Returns:
            List of created Wire objects
        """
        pos = self.get_pin_position(symbol, pin_id)
        if pos is None:
            return []

        x1, y1 = pos.x / 1e6, pos.y / 1e6
        x2, y2 = point_mm
        end = Vector2.from_xy_mm(x2, y2)

        if style == "direct":
            return [self.add_wire(pos, end)]

        if abs(x1 - x2) < 0.1 or abs(y1 - y2) < 0.1:
            return [self.add_wire(pos, end)]

        if style == "L":
            dx = abs(x2 - x1)
            dy = abs(y2 - y1)
            horizontal_first = dx >= dy
        elif style == "L_horizontal_first":
            horizontal_first = True
        elif style == "L_vertical_first":
            horizontal_first = False
        else:
            raise ValueError(f"Unknown style '{style}'")

        if horizontal_first:
            corner = Vector2.from_xy_mm(x2, y1)
        else:
            corner = Vector2.from_xy_mm(x1, y2)

        return self.add_wires([pos, corner, end])

    def connect_to_junction(
        self,
        pins: List[Tuple[Symbol, str]],
        junction_mm: Tuple[float, float],
        style: str = "L",
    ) -> Tuple[List[Wire], Wrapper]:
        """Connect multiple pins to a common junction point.

        Args:
            pins: List of (symbol, pin_id) tuples
            junction_mm: Junction location as (x_mm, y_mm)
            style: Routing style for each wire

        Returns:
            Tuple of (list of wires, junction object)

        Example:
            >>> wires, junc = sch.wiring.connect_to_junction(
            ...     [(mosfet, "S"), (diode, "K"), (inductor, "1")],
            ...     junction_mm=(100, 60),
            ... )
        """
        all_wires = []

        for symbol, pin_id in pins:
            wires = self.auto_wire_to_point(symbol, pin_id, junction_mm, style=style)
            all_wires.extend(wires)

        junction_pos = Vector2.from_xy_mm(junction_mm[0], junction_mm[1])
        junction = self.add_junction(junction_pos)

        return all_wires, junction

    # =========================================================================
    # Wire Manipulation
    # =========================================================================

    def move_wire(
        self,
        wire: Wire,
        delta_x_mm: float,
        delta_y_mm: float,
    ) -> Wire:
        """Move a wire by a relative offset.

        Moves both endpoints.

        Args:
            wire: The wire to move
            delta_x_mm: X offset in millimeters
            delta_y_mm: Y offset in millimeters

        Returns:
            The updated wire
        """
        delta_x_nm = int(delta_x_mm * 1_000_000)
        delta_y_nm = int(delta_y_mm * 1_000_000)

        new_start = Vector2.from_xy(
            wire.start.x + delta_x_nm,
            wire.start.y + delta_y_nm
        )
        new_end = Vector2.from_xy(
            wire.end.x + delta_x_nm,
            wire.end.y + delta_y_nm
        )

        wire.start = new_start
        wire.end = new_end

        updated = self._sch.crud.update_items(wire)
        return updated[0] if updated else wire

    def find_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,
    ) -> Optional[Wire]:
        """Find a wire at or passing through a position.

        Args:
            position: Position to search
            tolerance_nm: Search tolerance in nanometers

        Returns:
            Wire if found, None otherwise
        """
        target_x = position.x
        target_y = position.y

        for wire in self._sch.crud.get_wires():
            # Check endpoints
            if (abs(wire.start.x - target_x) <= tolerance_nm and
                abs(wire.start.y - target_y) <= tolerance_nm):
                return wire
            if (abs(wire.end.x - target_x) <= tolerance_nm and
                abs(wire.end.y - target_y) <= tolerance_nm):
                return wire

            # Check if point lies on wire segment
            x1, y1 = wire.start.x, wire.start.y
            x2, y2 = wire.end.x, wire.end.y

            # Horizontal wire
            if abs(y1 - y2) <= tolerance_nm and abs(target_y - y1) <= tolerance_nm:
                min_x, max_x = min(x1, x2), max(x1, x2)
                if min_x - tolerance_nm <= target_x <= max_x + tolerance_nm:
                    return wire

            # Vertical wire
            if abs(x1 - x2) <= tolerance_nm and abs(target_x - x1) <= tolerance_nm:
                min_y, max_y = min(y1, y2), max(y1, y2)
                if min_y - tolerance_nm <= target_y <= max_y + tolerance_nm:
                    return wire

        return None

    # =========================================================================
    # Junction Operations
    # =========================================================================

    def get_junctions(self) -> List[Junction]:
        """Get all junctions in the schematic.

        Returns:
            List of Junction objects

        Example:
            >>> junctions = sch.wiring.get_junctions()
            >>> print(f"Found {len(junctions)} junctions")
        """
        return self._sch.crud.get_junctions()

    def update_junction(self, junction: Junction) -> Junction:
        """Update a junction's properties.

        Args:
            junction: Junction with updated properties

        Returns:
            Updated Junction object

        Example:
            >>> junction = sch.wiring.get_junctions()[0]
            >>> junction.position = Vector2.from_xy_mm(100, 80)
            >>> sch.wiring.update_junction(junction)
        """
        updated = self._sch.crud.update_items(junction)
        return updated[0] if updated else junction

    def delete_junction(self, junction: Junction) -> None:
        """Delete a junction.

        Args:
            junction: Junction to delete

        Example:
            >>> junctions = sch.wiring.get_junctions()
            >>> sch.wiring.delete_junction(junctions[0])
        """
        self._sch.crud.remove_items(junction)

    def delete_junctions(self, junctions: List[Junction]) -> None:
        """Delete multiple junctions.

        Args:
            junctions: List of junctions to delete

        Example:
            >>> junctions = sch.wiring.get_junctions()
            >>> sch.wiring.delete_junctions(junctions)
        """
        if junctions:
            self._sch.crud.remove_items(junctions)

    def get_needed_junctions(self, items) -> List[Vector2]:
        """Get positions where junctions are needed for the given placed items.

        Uses KiCad's AnalyzePoint to determine where 3+ wires meet or where
        wire endpoints land on another wire's interior.

        Args:
            items: List of Wire/Junction/etc objects with .id attributes

        Returns:
            List of Vector2 positions where junctions should be placed
        """
        cmd = schematic_commands_pb2.GetNeededJunctions()
        cmd.document.CopyFrom(self._sch._doc)
        for item in items:
            item_id = cmd.item_ids.add()
            item_id.value = item.id.value
        response = self._sch._kicad.send(
            cmd, schematic_commands_pb2.GetNeededJunctionsResponse
        )
        return [Vector2.from_xy(p.x_nm, p.y_nm) for p in response.positions]

    # =========================================================================
    # No-Connect Operations
    # =========================================================================

    def get_no_connects(self) -> List[NoConnect]:
        """Get all no-connect markers in the schematic.

        Returns:
            List of NoConnect objects

        Example:
            >>> ncs = sch.wiring.get_no_connects()
            >>> print(f"Found {len(ncs)} no-connect markers")
        """
        return self._sch.crud.get_no_connects()

    def update_no_connect(self, nc: NoConnect) -> NoConnect:
        """Update a no-connect marker's properties.

        Args:
            nc: NoConnect with updated properties

        Returns:
            Updated NoConnect object

        Example:
            >>> nc = sch.wiring.get_no_connects()[0]
            >>> nc.position = Vector2.from_xy_mm(100, 80)
            >>> sch.wiring.update_no_connect(nc)
        """
        updated = self._sch.crud.update_items(nc)
        return updated[0] if updated else nc

    def delete_no_connect(self, nc: NoConnect) -> None:
        """Delete a no-connect marker.

        Args:
            nc: NoConnect to delete

        Example:
            >>> ncs = sch.wiring.get_no_connects()
            >>> sch.wiring.delete_no_connect(ncs[0])
        """
        self._sch.crud.remove_items(nc)

    def delete_no_connects(self, ncs: List[NoConnect]) -> None:
        """Delete multiple no-connect markers.

        Args:
            ncs: List of no-connects to delete

        Example:
            >>> ncs = sch.wiring.get_no_connects()
            >>> sch.wiring.delete_no_connects(ncs)
        """
        if ncs:
            self._sch.crud.remove_items(ncs)

    # =========================================================================
    # Wire Update/Delete Operations
    # =========================================================================

    def get_wires(self) -> List[Wire]:
        """Get all wires in the schematic.

        Returns:
            List of Wire objects

        Example:
            >>> wires = sch.wiring.get_wires()
            >>> print(f"Found {len(wires)} wires")
        """
        return self._sch.crud.get_wires()

    def update_wire(self, wire: Wire) -> Wire:
        """Update a wire's properties.

        Args:
            wire: Wire with updated properties

        Returns:
            Updated Wire object

        Example:
            >>> wire = sch.wiring.get_wires()[0]
            >>> wire.end = Vector2.from_xy_mm(150, 80)
            >>> sch.wiring.update_wire(wire)
        """
        updated = self._sch.crud.update_items(wire)
        return updated[0] if updated else wire

    def delete_wire(self, wire: Wire) -> None:
        """Delete a wire.

        Args:
            wire: Wire to delete

        Example:
            >>> wires = sch.wiring.get_wires()
            >>> sch.wiring.delete_wire(wires[0])
        """
        self._sch.crud.remove_items(wire)

    def delete_wires(self, wires: List[Wire]) -> None:
        """Delete multiple wires.

        Args:
            wires: List of wires to delete

        Example:
            >>> wires = sch.wiring.get_wires()
            >>> sch.wiring.delete_wires(wires)
        """
        if wires:
            self._sch.crud.remove_items(wires)
