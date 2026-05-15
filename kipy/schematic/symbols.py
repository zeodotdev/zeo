# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Symbol operations: add, search, move, copy, rotate, mirror.
"""

from typing import TYPE_CHECKING, List, Optional, Tuple, Dict, cast

from kipy.schematic_types import Symbol, Pin
from kipy.common_types import LibraryIdentifier
from kipy.geometry import Vector2
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class SymbolOperations:
    """Symbol operations: add, search, move, copy, rotate, mirror."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Add Operations
    # =========================================================================

    def add(
        self,
        lib_id: str,
        position: Vector2,
        unit: int = 1,
        angle: float = 0.0,
        mirror_x: bool = False,
        mirror_y: bool = False,
    ) -> Symbol:
        """Place a symbol from a library.

        Args:
            lib_id: Library identifier as "library:symbol" (e.g., "Device:R")
            position: Position for the symbol
            unit: Unit number for multi-unit symbols (default 1)
            angle: Rotation in degrees (0, 90, 180, 270)
            mirror_x: Mirror along X axis
            mirror_y: Mirror along Y axis

        Returns:
            The created Symbol object

        Example:
            >>> pos = Vector2.from_xy_mm(100, 80)
            >>> resistor = sch.symbols.add("Device:R", pos)
            >>> mosfet = sch.symbols.add("Device:Q_NMOS_GSD", pos, angle=90)
        """
        symbol = Symbol()

        lib_identifier = LibraryIdentifier()
        if ":" in lib_id:
            parts = lib_id.split(":", 1)
            lib_identifier.library = parts[0]
            lib_identifier.name = parts[1]
        else:
            lib_identifier.name = lib_id

        symbol.lib_id = lib_identifier
        symbol.position = position
        symbol.unit = unit
        symbol.angle = angle
        symbol.mirror_x = mirror_x
        symbol.mirror_y = mirror_y

        created = self._sch.crud.create_items(symbol)
        if created:
            return cast(Symbol, created[0])
        return symbol

    def add_at_mm(
        self,
        lib_id: str,
        x_mm: float,
        y_mm: float,
        **kwargs
    ) -> Symbol:
        """Place a symbol at mm coordinates.

        Args:
            lib_id: Library identifier
            x_mm: X position in millimeters
            y_mm: Y position in millimeters
            **kwargs: Additional args passed to add()

        Example:
            >>> r1 = sch.symbols.add_at_mm("Device:R", 100, 80)
        """
        return self.add(lib_id, Vector2.from_xy_mm(x_mm, y_mm), **kwargs)

    def add_at_mils(
        self,
        lib_id: str,
        x_mils: float,
        y_mils: float,
        **kwargs
    ) -> Symbol:
        """Place a symbol at mils coordinates.

        Args:
            lib_id: Library identifier
            x_mils: X position in mils
            y_mils: Y position in mils
            **kwargs: Additional args passed to add()

        Example:
            >>> r1 = sch.symbols.add_at_mils("Device:R", 4000, 3000)
        """
        return self.add(lib_id, Vector2.from_xy_mils(x_mils, y_mils), **kwargs)

    # =========================================================================
    # Search Operations
    # =========================================================================

    def get_all(self) -> List[Symbol]:
        """Get all symbols in the schematic.

        Returns:
            List of all Symbol objects
        """
        return self._sch.crud.get_symbols()

    def get_used_references(self) -> List[str]:
        """Get all used reference designators across all sheets.

        Unlike get_all() which only returns symbols on the current sheet,
        this method queries the full schematic hierarchy to find every
        reference in use. Essential for auto-numbering in multi-sheet
        designs to avoid duplicate references.

        Returns:
            List of reference strings (e.g., ["R1", "R2", "C1", "#PWR01"])
        """
        cmd = schematic_commands_pb2.GetUsedReferences()
        cmd.document.CopyFrom(self._sch._doc)
        response = self._sch._kicad.send(
            cmd, schematic_commands_pb2.GetUsedReferencesResponse
        )
        return list(response.references)

    def get_by_ref(self, reference: str) -> Optional[Symbol]:
        """Find a symbol by reference designator.

        Args:
            reference: Reference designator (e.g., "R1", "C2", "U3")

        Returns:
            Symbol if found, None otherwise

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> if r1:
            ...     print(f"R1 value: {r1.value}")
        """
        for symbol in self.get_all():
            if symbol.reference == reference:
                return symbol
        return None

    def get_by_value(self, value: str) -> List[Symbol]:
        """Find all symbols with a specific value.

        Args:
            value: Component value (e.g., "10k", "100nF")

        Returns:
            List of matching symbols

        Example:
            >>> resistors_10k = sch.symbols.get_by_value("10k")
        """
        return [s for s in self.get_all() if s.value == value]

    def get_by_lib(self, lib_name: str) -> List[Symbol]:
        """Find all symbols from a specific library.

        Args:
            lib_name: Library name (e.g., "Device", "Transistor_FET")

        Returns:
            List of matching symbols
        """
        return [s for s in self.get_all() if s.lib_id.library == lib_name]

    def find(
        self,
        reference: Optional[str] = None,
        value: Optional[str] = None,
        lib_name: Optional[str] = None,
        symbol_name: Optional[str] = None,
    ) -> List[Symbol]:
        """Find symbols matching multiple criteria.

        All provided criteria must match (AND logic). Partial string
        matching is used.

        Args:
            reference: Reference pattern (e.g., "R" matches R1, R2)
            value: Value pattern
            lib_name: Library name pattern
            symbol_name: Symbol name pattern

        Returns:
            List of matching symbols

        Example:
            >>> # Find all resistors
            >>> resistors = sch.symbols.find(reference="R")
            >>> # Find capacitors with "100" in value
            >>> caps = sch.symbols.find(reference="C", value="100")
        """
        results = self.get_all()

        if reference is not None:
            results = [s for s in results if reference in s.reference]
        if value is not None:
            results = [s for s in results if value in s.value]
        if lib_name is not None:
            results = [s for s in results if lib_name in s.lib_id.library]
        if symbol_name is not None:
            results = [s for s in results if symbol_name in s.lib_id.name]

        return results

    # =========================================================================
    # Move Operations
    # =========================================================================

    def move(self, symbol: Symbol, new_position: Vector2) -> Symbol:
        """Move a symbol to a new position.

        Args:
            symbol: The symbol to move
            new_position: New position

        Returns:
            The updated symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.move(r1, Vector2.from_xy_mm(150, 100))
        """
        symbol.position = new_position
        updated = self._sch.crud.update_items(symbol)
        self._sch.save()
        return updated[0] if updated else symbol

    def move_by(
        self,
        symbol: Symbol,
        delta_x_mm: float,
        delta_y_mm: float,
    ) -> Symbol:
        """Move a symbol by a relative offset.

        Args:
            symbol: The symbol to move
            delta_x_mm: X offset in millimeters
            delta_y_mm: Y offset in millimeters

        Returns:
            The updated symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> # Move 10mm right and 5mm down
            >>> sch.symbols.move_by(r1, 10, 5)
        """
        current = symbol.position
        delta_x_nm = int(delta_x_mm * 1_000_000)
        delta_y_nm = int(delta_y_mm * 1_000_000)
        new_pos = Vector2.from_xy(current.x + delta_x_nm, current.y + delta_y_nm)
        return self.move(symbol, new_pos)

    # =========================================================================
    # Rotate/Mirror Operations
    # =========================================================================

    def rotate(self, symbol: Symbol, angle_degrees: float) -> Symbol:
        """Rotate a symbol by the specified angle (additive).

        Args:
            symbol: The symbol to rotate
            angle_degrees: Rotation angle to ADD (positive = CCW)

        Returns:
            The updated symbol

        Note:
            This method ADDS to the existing angle. Use set_angle() to set
            an absolute angle value.

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.rotate(r1, 90)  # Adds 90° to current angle
        """
        new_angle = (symbol.angle + angle_degrees) % 360
        symbol.angle = new_angle
        updated = self._sch.crud.update_items(symbol)
        self._sch.save()
        return updated[0] if updated else symbol

    def set_angle(self, symbol: Symbol, angle_degrees: float) -> Symbol:
        """Set a symbol to an absolute angle.

        Args:
            symbol: The symbol to update
            angle_degrees: Absolute rotation angle in degrees (0, 90, 180, 270)

        Returns:
            The updated symbol

        Raises:
            RuntimeError: If the angle update fails to persist

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.set_angle(r1, 0)  # Set to 0° (vertical)
            >>> sch.symbols.set_angle(r1, 90)  # Set to 90° (horizontal)
        """
        target_angle = angle_degrees % 360
        symbol.angle = target_angle
        updated = self._sch.crud.update_items(symbol)
        self._sch.save()

        result = updated[0] if updated else symbol

        # Validate the angle was actually set
        if hasattr(result, 'angle') and abs(result.angle - target_angle) > 0.1:
            raise RuntimeError(
                f"Angle update failed: expected {target_angle}°, got {result.angle}°. "
                f"The symbol may not support this angle or the update was not persisted."
            )

        return result

    def mirror(self, symbol: Symbol, axis: str = "x") -> Symbol:
        """Mirror a symbol along an axis.

        Args:
            symbol: The symbol to mirror
            axis: "x" for horizontal, "y" for vertical

        Returns:
            The updated symbol

        Example:
            >>> q1 = sch.symbols.get_by_ref("Q1")
            >>> sch.symbols.mirror(q1, "x")
        """
        if axis.lower() == "x":
            symbol.mirror_x = not symbol.mirror_x
        elif axis.lower() == "y":
            symbol.mirror_y = not symbol.mirror_y
        else:
            raise ValueError(f"Invalid axis '{axis}'. Use 'x' or 'y'.")

        updated = self._sch.crud.update_items(symbol)
        return updated[0] if updated else symbol

    # =========================================================================
    # Copy Operation
    # =========================================================================

    def copy(
        self,
        symbol: Symbol,
        new_position: Vector2,
        new_reference: Optional[str] = None,
    ) -> Symbol:
        """Create a copy of a symbol at a new position.

        Args:
            symbol: The symbol to copy
            new_position: Position for the copy
            new_reference: Optional new reference (default: "?" for annotation)

        Returns:
            The newly created symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> r2 = sch.symbols.copy(r1, Vector2.from_xy_mm(150, 80), "R2")
        """
        lib_id_str = f"{symbol.lib_id.library}:{symbol.lib_id.name}"
        new_symbol = self.add(
            lib_id_str,
            new_position,
            unit=symbol.unit,
            angle=symbol.angle,
            mirror_x=symbol.mirror_x,
            mirror_y=symbol.mirror_y,
        )

        if symbol.value:
            new_symbol.value = symbol.value

        if new_reference:
            new_symbol.reference = new_reference

        updated = self._sch.crud.update_items(new_symbol)
        return updated[0] if updated else new_symbol

    # =========================================================================
    # Pin Operations
    # =========================================================================

    def get_pin(self, symbol: Symbol, pin_id: str) -> Optional[Pin]:
        """Get a pin from a symbol by name or number.

        Args:
            symbol: The symbol
            pin_id: Pin name (e.g., "VCC") or number (e.g., "1")

        Returns:
            Pin object if found, None otherwise

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> pin1 = sch.symbols.get_pin(r1, "1")
        """
        return symbol.get_pin(pin_id)

    def get_pin_position(self, symbol: Symbol, pin_id: str) -> Optional[Vector2]:
        """Get the transformed (world-space) position of a pin.

        Uses the IPC API (GetTransformedPinPosition) to get the exact pin
        position after applying symbol transformations (position, rotation,
        mirroring).

        Args:
            symbol: The symbol
            pin_id: Pin name or number

        Returns:
            Vector2 position in world coordinates, or None if pin not found

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> pos = sch.symbols.get_pin_position(r1, "1")
            >>> print(f"Pin 1 at ({pos.x_mm}, {pos.y_mm}) mm")
        """
        result = self.get_transformed_pin_position(symbol, pin_id)
        if result:
            return result['position']

        # Fallback to cached pin position
        pin = self.get_pin(symbol, pin_id)
        return pin.position if pin else None

    def get_transformed_pin_position(
        self,
        symbol: Symbol,
        pin_id: str,
    ) -> Optional[Dict]:
        """Get the transformed (world-space) position and orientation of a pin.

        Uses the IPC API (GetTransformedPinPosition) to get the exact pin
        position after applying symbol transformations (position, rotation,
        mirroring).

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name or number (e.g., "1", "VCC")

        Returns:
            Dict with:
                - position: Vector2 in world coordinates
                - orientation: Pin orientation in degrees
            Or None if pin not found

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> result = sch.symbols.get_transformed_pin_position(r1, "1")
            >>> if result:
            ...     print(f"Pin at ({result['position'].x_mm}, {result['position'].y_mm}) mm")
            ...     print(f"Orientation: {result['orientation']}°")
        """
        cmd = schematic_commands_pb2.GetTransformedPinPosition()
        cmd.document.CopyFrom(self._sch._doc)
        cmd.symbol_id.value = symbol.id.value
        cmd.pin_number = pin_id

        try:
            response = self._sch._kicad.send(
                cmd, schematic_commands_pb2.GetTransformedPinPositionResponse
            )

            return {
                'position': Vector2.from_xy(response.position.x_nm, response.position.y_nm),
                'orientation': response.orientation,
            }
        except Exception:
            return None

    def get_all_transformed_pin_positions(self, symbol: Symbol) -> List[Dict]:
        """Get the transformed positions and orientations of ALL pins on a symbol.

        This is a batch API that returns all pin positions in a single IPC call,
        which is much more efficient than calling get_transformed_pin_position()
        for each pin individually. Use this for symbols with many pins.

        Args:
            symbol: The symbol containing the pins

        Returns:
            List of dicts, each with:
                - pin_number: Pin number string
                - pin_name: Pin name string
                - position: Vector2 in world coordinates
                - orientation: Pin orientation in degrees

        Example:
            >>> ic = sch.symbols.get_by_ref("U1")
            >>> pins = sch.symbols.get_all_transformed_pin_positions(ic)
            >>> for pin in pins:
            ...     print(f"Pin {pin['pin_number']} ({pin['pin_name']}) at "
            ...           f"({pin['position'].x_mm}, {pin['position'].y_mm}) mm")
        """
        cmd = schematic_commands_pb2.GetAllTransformedPinPositions()
        cmd.document.CopyFrom(self._sch._doc)
        cmd.symbol_id.value = symbol.id.value

        try:
            response = self._sch._kicad.send(
                cmd, schematic_commands_pb2.GetAllTransformedPinPositionsResponse
            )

            return [
                {
                    'pin_number': p.pin_number,
                    'pin_name': p.pin_name,
                    'position': Vector2.from_xy(p.position.x_nm, p.position.y_nm),
                    'orientation': p.orientation,
                }
                for p in response.pins
            ]
        except Exception:
            return []

    def find_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,
    ) -> Optional[Symbol]:
        """Find a symbol at a position.

        Args:
            position: Position to search
            tolerance_nm: Search tolerance in nanometers (default 0.1mm)

        Returns:
            Symbol if found, None otherwise
        """
        target_x = position.x
        target_y = position.y

        for symbol in self.get_all():
            if (abs(symbol.position.x - target_x) <= tolerance_nm and
                abs(symbol.position.y - target_y) <= tolerance_nm):
                return symbol

        return None

    def find_pin_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,
    ) -> Optional[Tuple[Symbol, Pin]]:
        """Find a pin at a position.

        Args:
            position: Position to search
            tolerance_nm: Search tolerance in nanometers

        Returns:
            Tuple of (Symbol, Pin) if found, None otherwise

        Example:
            >>> pos = Vector2.from_xy_mm(100, 80)
            >>> result = sch.symbols.find_pin_at_position(pos)
            >>> if result:
            ...     symbol, pin = result
            ...     print(f"Found {symbol.reference} pin {pin.name}")
        """
        target_x = position.x
        target_y = position.y

        for symbol in self.get_all():
            for pin in symbol.pins:
                if (abs(pin.position.x - target_x) <= tolerance_nm and
                    abs(pin.position.y - target_y) <= tolerance_nm):
                    return (symbol, pin)

        return None

    # =========================================================================
    # Duplicate Operations
    # =========================================================================

    def duplicate(
        self,
        symbol: Symbol,
        offset_mm: Tuple[float, float] = (25.4, 0),
        new_reference: Optional[str] = None,
        copy_value: bool = True,
    ) -> Symbol:
        """Duplicate a symbol with an offset.

        Args:
            symbol: The symbol to duplicate
            offset_mm: Offset from original as (x_mm, y_mm)
            new_reference: New reference (default: "?" for auto-annotation)
            copy_value: Copy the value field (default True)

        Returns:
            The newly created symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> r2 = sch.symbols.duplicate(r1, offset_mm=(25, 0))
        """
        new_pos = Vector2.from_xy(
            symbol.position.x + int(offset_mm[0] * 1_000_000),
            symbol.position.y + int(offset_mm[1] * 1_000_000)
        )
        return self.copy(
            symbol,
            new_pos,
            new_reference=new_reference if new_reference else None,
        )

    def duplicate_array(
        self,
        symbol: Symbol,
        count: int,
        spacing_mm: Tuple[float, float] = (25.4, 0),
        start_ref: Optional[int] = None,
    ) -> List[Symbol]:
        """Create an array of duplicate symbols.

        Args:
            symbol: The symbol to duplicate
            count: Number of copies to create
            spacing_mm: Spacing between copies as (x_mm, y_mm)
            start_ref: Starting reference number (auto if None)

        Returns:
            List of created symbols (not including original)

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> # Create 5 resistors spaced 25mm apart horizontally
            >>> copies = sch.symbols.duplicate_array(r1, 5, spacing_mm=(25, 0))
        """
        copies = []
        base_ref = ''.join(c for c in symbol.reference if c.isalpha())

        for i in range(count):
            offset = (
                spacing_mm[0] * (i + 1),
                spacing_mm[1] * (i + 1),
            )

            if start_ref is not None:
                new_ref = f"{base_ref}{start_ref + i}"
            else:
                new_ref = None  # Let annotation assign

            new_symbol = self.duplicate(
                symbol,
                offset_mm=offset,
                new_reference=new_ref,
            )
            copies.append(new_symbol)

        return copies

    def duplicate_grid(
        self,
        symbol: Symbol,
        rows: int,
        cols: int,
        spacing_mm: Tuple[float, float] = (25.4, 25.4),
    ) -> List[Symbol]:
        """Create a grid of duplicate symbols.

        Args:
            symbol: The symbol to duplicate
            rows: Number of rows
            cols: Number of columns
            spacing_mm: Spacing as (x_mm, y_mm)

        Returns:
            List of created symbols (row-major order, not including original)

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> # Create 3x4 grid of resistors
            >>> copies = sch.symbols.duplicate_grid(r1, rows=3, cols=4)
        """
        copies = []

        for row in range(rows):
            for col in range(cols):
                if row == 0 and col == 0:
                    continue  # Skip original position

                offset = (
                    spacing_mm[0] * col,
                    spacing_mm[1] * row,
                )

                new_symbol = self.duplicate(symbol, offset_mm=offset)
                copies.append(new_symbol)

        return copies

    # =========================================================================
    # Symbol Replacement
    # =========================================================================

    def replace(
        self,
        old_symbol: Symbol,
        new_lib_id: str,
        preserve_value: bool = True,
        preserve_footprint: bool = True,
        preserve_fields: bool = False,
    ) -> Symbol:
        """Replace a symbol with a different symbol type.

        Preserves position, rotation, mirror state, and optionally value/footprint.

        Note: Wiring connections may be broken if pin positions differ!
        Consider using replace_with_reconnect() for automatic reconnection.

        Args:
            old_symbol: Symbol to replace
            new_lib_id: Library ID for replacement (e.g., "Device:R_Small")
            preserve_value: Keep the original value field
            preserve_footprint: Keep the original footprint field
            preserve_fields: Keep all custom fields

        Returns:
            The newly created replacement symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> # Replace with a small resistor symbol
            >>> r1_new = sch.symbols.replace(r1, "Device:R_Small")
        """
        # Store old properties
        old_position = old_symbol.position if hasattr(old_symbol, 'position') else Vector2.from_xy(0, 0)
        old_angle = old_symbol.angle
        old_mirror_x = old_symbol.mirror_x
        old_mirror_y = old_symbol.mirror_y
        old_unit = old_symbol.unit
        old_reference = old_symbol.reference
        old_value = old_symbol.value if preserve_value else None
        old_footprint = old_symbol.footprint if preserve_footprint else None

        # Store custom fields if needed
        old_fields = {}
        if preserve_fields:
            for f in old_symbol.fields:
                if f.name not in ["Reference", "Value", "Footprint", "Datasheet"]:
                    old_fields[f.name] = f.text

        # Delete old symbol
        self._sch.crud.remove_items(old_symbol)

        # Create new symbol
        new_symbol = self.add(
            new_lib_id,
            old_position,
            unit=old_unit,
            angle=old_angle,
            mirror_x=old_mirror_x,
            mirror_y=old_mirror_y,
        )

        # Restore properties
        new_symbol.reference = old_reference
        if old_value:
            new_symbol.value = old_value
        if old_footprint:
            new_symbol.footprint = old_footprint

        # Restore custom fields
        for name, text in old_fields.items():
            new_symbol.set_field(name, text)

        # Update the symbol
        updated = self._sch.crud.update_items(new_symbol)
        return updated[0] if updated else new_symbol

    def replace_with_reconnect(
        self,
        old_symbol: Symbol,
        new_lib_id: str,
        pin_mapping: Optional[dict] = None,
        preserve_value: bool = True,
    ) -> Tuple[Symbol, List]:
        """Replace a symbol and attempt to reconnect wires.

        Args:
            old_symbol: Symbol to replace
            new_lib_id: Library ID for replacement
            pin_mapping: Optional mapping from old pin names to new
                        (e.g., {"1": "A", "2": "K"} for diode)
            preserve_value: Keep the original value

        Returns:
            Tuple of (new_symbol, list of reconnected wires)

        Example:
            >>> d1 = sch.symbols.get_by_ref("D1")
            >>> new_d1, wires = sch.symbols.replace_with_reconnect(
            ...     d1, "Device:D_Schottky",
            ...     pin_mapping={"A": "A", "K": "K"}
            ... )
        """
        # Record wire connections to old symbol's pins
        old_connections = []
        for pin in old_symbol.pins:
            pin_pos = pin.position
            # Find wires connected to this pin
            wire = self._sch.wiring.find_at_position(pin_pos)
            if wire:
                old_connections.append({
                    "pin_name": pin.name,
                    "pin_number": pin.number,
                    "wire": wire,
                    "old_pos": pin_pos,
                })

        # Replace the symbol
        new_symbol = self.replace(old_symbol, new_lib_id, preserve_value=preserve_value)

        # Attempt to reconnect wires
        reconnected_wires = []
        for conn in old_connections:
            old_pin_id = conn["pin_number"] or conn["pin_name"]

            # Determine new pin
            if pin_mapping and old_pin_id in pin_mapping:
                new_pin_id = pin_mapping[old_pin_id]
            else:
                new_pin_id = old_pin_id  # Try same pin name/number

            # Find new pin position
            new_pin = new_symbol.get_pin(new_pin_id)
            if not new_pin:
                continue

            # Update wire endpoint
            wire = conn["wire"]
            old_pos = conn["old_pos"]

            # Check which endpoint was connected
            if (abs(wire.start.x - old_pos.x) < 100000 and
                abs(wire.start.y - old_pos.y) < 100000):
                wire.start = new_pin.position
            elif (abs(wire.end.x - old_pos.x) < 100000 and
                  abs(wire.end.y - old_pos.y) < 100000):
                wire.end = new_pin.position

            reconnected_wires.append(wire)

        # Update all modified wires
        if reconnected_wires:
            self._sch.crud.update_items(reconnected_wires)

        return new_symbol, reconnected_wires

    def swap_positions(self, symbol1: Symbol, symbol2: Symbol) -> Tuple[Symbol, Symbol]:
        """Swap the positions of two symbols.

        Args:
            symbol1: First symbol
            symbol2: Second symbol

        Returns:
            Tuple of updated (symbol1, symbol2)

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> r2 = sch.symbols.get_by_ref("R2")
            >>> r1, r2 = sch.symbols.swap_positions(r1, r2)
        """
        pos1 = symbol1.position
        pos2 = symbol2.position

        symbol1.position = pos2
        symbol2.position = pos1

        updated = self._sch.crud.update_items([symbol1, symbol2])
        if len(updated) >= 2:
            return (updated[0], updated[1])
        return (symbol1, symbol2)

    # =========================================================================
    # Field Operations
    # =========================================================================

    def set_value(self, symbol: Symbol, value: str) -> Symbol:
        """Set a symbol's value field.

        Args:
            symbol: The symbol
            value: New value

        Returns:
            Updated symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.set_value(r1, "4.7k")
        """
        symbol.value = value
        updated = self._sch.crud.update_items(symbol)
        self._sch.save()
        return updated[0] if updated else symbol

    def set_footprint(self, symbol: Symbol, footprint: str) -> Symbol:
        """Set a symbol's footprint field.

        Args:
            symbol: The symbol
            footprint: Footprint string (e.g., "Resistor_SMD:R_0805_2012Metric")

        Returns:
            Updated symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.set_footprint(r1, "Resistor_SMD:R_0402_1005Metric")
        """
        symbol.footprint = footprint
        updated = self._sch.crud.update_items(symbol)
        self._sch.save()
        return updated[0] if updated else symbol

    def set_dnp(self, symbol: Symbol, dnp: bool = True) -> Symbol:
        """Set a symbol's Do Not Populate flag.

        Args:
            symbol: The symbol
            dnp: True to mark as DNP

        Returns:
            Updated symbol

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> sch.symbols.set_dnp(r1, True)
        """
        symbol.dnp = dnp
        updated = self._sch.crud.update_items(symbol)
        return updated[0] if updated else symbol

    def bulk_set_value(self, symbols: List[Symbol], value: str) -> List[Symbol]:
        """Set value for multiple symbols.

        Args:
            symbols: List of symbols
            value: New value for all

        Returns:
            List of updated symbols

        Example:
            >>> resistors = sch.symbols.find(reference="R")
            >>> sch.symbols.bulk_set_value(resistors, "10k")
        """
        for sym in symbols:
            sym.value = value
        return self._sch.crud.update_items(symbols)

    def bulk_set_footprint(self, symbols: List[Symbol], footprint: str) -> List[Symbol]:
        """Set footprint for multiple symbols.

        Args:
            symbols: List of symbols
            footprint: New footprint for all

        Returns:
            List of updated symbols

        Example:
            >>> resistors = sch.symbols.find(reference="R")
            >>> sch.symbols.bulk_set_footprint(resistors, "Resistor_SMD:R_0402_1005Metric")
        """
        for sym in symbols:
            sym.footprint = footprint
        return self._sch.crud.update_items(symbols)
