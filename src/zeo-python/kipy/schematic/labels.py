# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Label and net naming operations.
"""

from typing import TYPE_CHECKING, Optional, Tuple, List, Dict
import re

from kipy.schematic_types import (
    LocalLabel, GlobalLabel, HierarchicalLabel, DirectiveLabel, Symbol
)
from kipy.geometry import Vector2
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class LabelOperations:
    """Label and net naming operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Add Label Operations
    # =========================================================================

    def add_local(self, text: str, position: Vector2) -> Wrapper:
        """Add a local net label.

        Local labels define net names within the current sheet only.

        Args:
            text: Label text (net name)
            position: Position for the label

        Returns:
            The created LocalLabel object

        Example:
            >>> label = sch.labels.add_local("SW_NODE", pos)
        """
        label = LocalLabel.create(position, text)
        created = self._sch.crud.create_items(label)
        if created:
            return created[0]
        return label

    def add_global(self, text: str, position: Vector2) -> Wrapper:
        """Add a global net label.

        Global labels define net names visible across all sheets.

        Args:
            text: Label text (net name)
            position: Position for the label

        Returns:
            The created GlobalLabel object

        Example:
            >>> label = sch.labels.add_global("VCC", pos)
        """
        label = GlobalLabel.create(position, text)
        created = self._sch.crud.create_items(label)
        if created:
            return created[0]
        return label

    def add_hierarchical(self, text: str, position: Vector2) -> Wrapper:
        """Add a hierarchical label.

        Hierarchical labels connect to sheet pins for inter-sheet connections.

        Args:
            text: Label text (net name)
            position: Position for the label

        Returns:
            The created HierarchicalLabel object

        Example:
            >>> label = sch.labels.add_hierarchical("DATA_BUS", pos)
        """
        label = HierarchicalLabel.create(position, text)
        created = self._sch.crud.create_items(label)
        if created:
            return created[0]
        return label

    def add(
        self,
        text: str,
        position: Vector2,
        label_type: str = "local",
    ) -> Wrapper:
        """Add a net label of the specified type.

        Args:
            text: Label text (net name)
            position: Position for the label
            label_type: "local", "global", or "hierarchical"

        Returns:
            The created label object

        Example:
            >>> sch.labels.add("VCC", pos, "global")
            >>> sch.labels.add("SW_NODE", pos, "local")
        """
        if label_type == "local":
            return self.add_local(text, position)
        elif label_type == "global":
            return self.add_global(text, position)
        elif label_type == "hierarchical":
            return self.add_hierarchical(text, position)
        else:
            raise ValueError(f"Unknown label type: {label_type}")

    # Common power library names across KiCad versions/installations
    POWER_LIBRARIES = ["power", "Power_Symbols", "Device"]

    def add_power(
        self,
        name: str,
        position: Vector2,
        angle: float = 0.0,
    ) -> Symbol:
        """Add a power symbol (VCC, GND, etc.).

        Power symbols are searched in multiple libraries:
        - power (standard KiCad library)
        - Power_Symbols (alternative name)
        - Device (fallback)

        Common symbol names:
        - GND, GND1, GND2 (grounds)
        - VCC, VDD, VSS (power rails)
        - +3V3, +5V, +12V (voltage rails)

        Args:
            name: Power symbol name (e.g., "GND", "VCC")
            position: Position for the symbol
            angle: Rotation angle (0 or 180 typical)

        Returns:
            The created Symbol object

        Raises:
            ApiError: If power symbol not found in any known library

        Example:
            >>> gnd = sch.labels.add_power("GND", pos)
            >>> vcc = sch.labels.add_power("VCC", pos, angle=180)
        """
        from kipy.client import ApiError

        last_error = None
        for lib in self.POWER_LIBRARIES:
            lib_id = f"{lib}:{name}"
            try:
                return self._sch.symbols.add(lib_id, position, angle=angle)
            except ApiError as e:
                last_error = e
                continue

        # All libraries failed
        raise ApiError(
            f"Power symbol '{name}' not found. "
            f"Tried libraries: {self.POWER_LIBRARIES}. "
            f"Last error: {last_error}"
        )

    def add_directive(self, text: str, position: Vector2) -> Wrapper:
        """Add a SPICE directive label.

        Directive labels are used for simulation commands like
        .tran, .ac, .dc, or component parameters.

        Args:
            text: Directive text (e.g., ".tran 1m")
            position: Position for the label

        Returns:
            The created DirectiveLabel object

        Example:
            >>> directive = sch.labels.add_directive(".tran 1m", pos)
        """
        label = DirectiveLabel.create(position, text)
        created = self._sch.crud.create_items(label)
        if created:
            return created[0]
        return label

    # =========================================================================
    # Pin Labeling Helpers
    # =========================================================================

    def label_pin(
        self,
        symbol: Symbol,
        pin_id: str,
        net_name: str,
        label_type: str = "local",
        offset_mm: Tuple[float, float] = (0, 0),
    ) -> Wrapper:
        """Add a net label at a symbol's pin.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name or number
            net_name: Name for the net
            label_type: "local", "global", or "hierarchical"
            offset_mm: Optional (x, y) offset from pin in mm

        Returns:
            The created label object

        Example:
            >>> sch.labels.label_pin(mosfet, "G", "GATE_DRIVE")
            >>> sch.labels.label_pin(mosfet, "D", "VIN", label_type="global")
        """
        pin_pos = self._sch.symbols.get_pin_position(symbol, pin_id)
        if pin_pos is None:
            raise ValueError(f"Pin '{pin_id}' not found on symbol")

        if offset_mm != (0, 0):
            offset_x_nm = int(offset_mm[0] * 1_000_000)
            offset_y_nm = int(offset_mm[1] * 1_000_000)
            label_pos = Vector2.from_xy(
                pin_pos.x + offset_x_nm,
                pin_pos.y + offset_y_nm
            )
        else:
            label_pos = pin_pos

        return self.add(net_name, label_pos, label_type)

    # =========================================================================
    # Net Name Queries
    # =========================================================================

    def get_net_name(self, item: Wrapper) -> Optional[str]:
        """Get the net name for an item.

        Args:
            item: The item to query (wire, pin, junction, etc.)

        Returns:
            Net name if connected, None otherwise

        Example:
            >>> wire = sch.crud.get_wires()[0]
            >>> net = sch.labels.get_net_name(wire)
            >>> print(f"Wire is on net: {net}")
        """
        if not hasattr(item, 'id'):
            return None

        try:
            result = self._sch.connectivity.get_net_for_item(item.id.value)
            if result.is_connected:
                return result.connection.name
        except Exception:
            pass

        return None

    # =========================================================================
    # Get Label Operations
    # =========================================================================

    def get_all_local(self):
        """Get all local labels."""
        return self._sch.crud.get_local_labels()

    def get_all_global(self):
        """Get all global labels."""
        return self._sch.crud.get_global_labels()

    def get_all_hierarchical(self):
        """Get all hierarchical labels."""
        return self._sch.crud.get_hierarchical_labels()

    def get_all(self):
        """Get all labels of all types."""
        return self._sch.crud.get_labels()

    def get_directive_labels(self):
        """Get all directive labels (SPICE directives).

        Returns:
            List of DirectiveLabel objects

        Example:
            >>> directives = sch.labels.get_directive_labels()
            >>> for d in directives:
            ...     print(f"Directive: {d.text}")
        """
        return list(self._sch.crud.get_directive_labels())

    # =========================================================================
    # Update Label Operations
    # =========================================================================

    def update_label(self, label: Wrapper) -> Wrapper:
        """Update a label's properties.

        Args:
            label: Label with updated properties

        Returns:
            Updated label object

        Example:
            >>> label = sch.labels.get_all_local()[0]
            >>> label.text = "NEW_NET_NAME"
            >>> sch.labels.update_label(label)
        """
        updated = self._sch.crud.update_items(label)
        return updated[0] if updated else label

    def update_labels(self, labels: List[Wrapper]) -> List[Wrapper]:
        """Update multiple labels' properties.

        Args:
            labels: List of labels with updated properties

        Returns:
            List of updated label objects

        Example:
            >>> labels = sch.labels.find_labels(pattern="OLD_")
            >>> for label in labels:
            ...     label.text = label.text.replace("OLD_", "NEW_")
            >>> sch.labels.update_labels(labels)
        """
        if labels:
            return self._sch.crud.update_items(labels)
        return []

    # =========================================================================
    # Net Management Operations
    # =========================================================================

    def rename_net(self, old_name: str, new_name: str) -> int:
        """Rename a net by updating all labels with that name.

        Args:
            old_name: Current net name
            new_name: New net name

        Returns:
            Number of labels updated

        Example:
            >>> count = sch.labels.rename_net("SW_NODE", "SWITCH_OUTPUT")
            >>> print(f"Updated {count} labels")
        """
        count = 0
        labels_to_update = []

        # Find all labels with the old name
        for label in self.get_all_local():
            if hasattr(label, 'text') and label.text == old_name:
                label.text = new_name
                labels_to_update.append(label)
                count += 1

        for label in self.get_all_global():
            if hasattr(label, 'text') and label.text == old_name:
                label.text = new_name
                labels_to_update.append(label)
                count += 1

        for label in self.get_all_hierarchical():
            if hasattr(label, 'text') and label.text == old_name:
                label.text = new_name
                labels_to_update.append(label)
                count += 1

        if labels_to_update:
            self._sch.crud.update_items(labels_to_update)

        return count

    def get_net_labels(self, net_name: str) -> List[Wrapper]:
        """Get all labels associated with a net.

        Args:
            net_name: Name of the net

        Returns:
            List of label objects

        Example:
            >>> labels = sch.labels.get_net_labels("VCC")
            >>> print(f"Found {len(labels)} VCC labels")
        """
        labels = []

        for label in self.get_all():
            if hasattr(label, 'text') and label.text == net_name:
                labels.append(label)

        return labels

    def get_net_connections(self, net_name: str) -> Dict:
        """Get detailed information about a net's connections.

        Args:
            net_name: Name of the net

        Returns:
            Dictionary with:
            - name: Net name
            - labels: List of labels
            - items: List of item IDs on the net
            - connection_points: List of connection positions

        Example:
            >>> info = sch.labels.get_net_connections("VCC")
            >>> print(f"VCC has {len(info['items'])} connected items")
        """
        result = {
            "name": net_name,
            "labels": [],
            "items": [],
            "connection_points": [],
        }

        # Get labels
        result["labels"] = self.get_net_labels(net_name)

        # Get items on net
        try:
            net_items = self._sch.connectivity.get_net_items(net_name)
            result["items"] = [item.value for item in net_items.item_ids]
            result["connection_points"] = [
                {"x_mm": pt.x_nm / 1e6, "y_mm": pt.y_nm / 1e6}
                for pt in net_items.connection_points
            ]
        except Exception:
            pass

        return result

    def find_labels(
        self,
        pattern: Optional[str] = None,
        label_type: Optional[str] = None,
    ) -> List[Wrapper]:
        """Find labels matching a pattern.

        Args:
            pattern: Regex pattern to match label text
            label_type: "local", "global", "hierarchical", or None for all

        Returns:
            List of matching labels

        Example:
            >>> # Find all labels starting with "V"
            >>> labels = sch.labels.find_labels(pattern="^V")
            >>> # Find all global labels
            >>> globals = sch.labels.find_labels(label_type="global")
        """
        if label_type == "local":
            labels = self.get_all_local()
        elif label_type == "global":
            labels = self.get_all_global()
        elif label_type == "hierarchical":
            labels = self.get_all_hierarchical()
        else:
            labels = self.get_all()

        if pattern:
            compiled = re.compile(pattern)
            labels = [l for l in labels if hasattr(l, 'text') and compiled.search(l.text)]

        return labels

    def get_unique_nets(self) -> List[str]:
        """Get list of all unique net names in the schematic.

        Returns:
            Sorted list of net names

        Example:
            >>> nets = sch.labels.get_unique_nets()
            >>> for net in nets:
            ...     print(net)
        """
        try:
            nets_response = self._sch.connectivity.get_nets()
            return sorted([net.name for net in nets_response.nets])
        except Exception:
            # Fallback: collect from labels
            names = set()
            for label in self.get_all():
                if hasattr(label, 'text') and label.text:
                    names.add(label.text)
            return sorted(names)

    def move_label(
        self,
        label: Wrapper,
        new_position: Vector2,
    ) -> Wrapper:
        """Move a label to a new position.

        Args:
            label: The label to move
            new_position: New position

        Returns:
            Updated label

        Example:
            >>> label = sch.labels.find_labels(pattern="VCC")[0]
            >>> sch.labels.move_label(label, Vector2.from_xy_mm(100, 80))
        """
        if hasattr(label, 'position'):
            label.position = new_position
            updated = self._sch.crud.update_items(label)
            return updated[0] if updated else label
        return label

    def delete_label(self, label: Wrapper) -> None:
        """Delete a label.

        Args:
            label: The label to delete

        Example:
            >>> labels = sch.labels.find_labels(pattern="OLD_NET")
            >>> for label in labels:
            ...     sch.labels.delete_label(label)
        """
        self._sch.crud.remove_items(label)

    def delete_labels_by_name(self, net_name: str) -> int:
        """Delete all labels with a specific name.

        Args:
            net_name: Name of the labels to delete

        Returns:
            Number of labels deleted

        Example:
            >>> count = sch.labels.delete_labels_by_name("UNUSED_NET")
        """
        labels = self.get_net_labels(net_name)
        if labels:
            self._sch.crud.remove_items(labels)
        return len(labels)

    # =========================================================================
    # Label at Pin Helpers
    # =========================================================================

    def label_symbol_pins(
        self,
        symbol: Symbol,
        pin_labels: Dict[str, str],
        label_type: str = "local",
    ) -> List[Wrapper]:
        """Add labels to multiple pins of a symbol.

        Args:
            symbol: The symbol
            pin_labels: Dictionary mapping pin IDs to net names
            label_type: Label type for all labels

        Returns:
            List of created labels

        Example:
            >>> mosfet = sch.symbols.get_by_ref("Q1")
            >>> labels = sch.labels.label_symbol_pins(mosfet, {
            ...     "G": "GATE_DRIVE",
            ...     "D": "VOUT",
            ...     "S": "GND",
            ... })
        """
        created = []
        for pin_id, net_name in pin_labels.items():
            try:
                label = self.label_pin(symbol, pin_id, net_name, label_type)
                created.append(label)
            except ValueError:
                pass  # Skip pins that don't exist
        return created

    def get_pin_net(self, symbol: Symbol, pin_id: str) -> Optional[str]:
        """Get the net name connected to a symbol's pin.

        Args:
            symbol: The symbol
            pin_id: Pin name or number

        Returns:
            Net name if connected, None otherwise

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> net = sch.labels.get_pin_net(r1, "1")
            >>> print(f"R1 pin 1 is on net: {net}")
        """
        pin = symbol.get_pin(pin_id)
        if not pin:
            return None

        # Try to find connectivity info
        try:
            result = self._sch.connectivity.get_net_for_item(pin.id.value)
            if result.is_connected:
                return result.connection.name
        except Exception:
            pass

        # Fallback: check for labels at pin position
        pin_pos = pin.position
        tolerance = 100000  # 0.1mm

        for label in self.get_all():
            if hasattr(label, 'position') and hasattr(label, 'text'):
                if (abs(label.position.x - pin_pos.x) <= tolerance and
                    abs(label.position.y - pin_pos.y) <= tolerance):
                    return label.text

        return None

    def get_symbol_nets(self, symbol: Symbol) -> Dict[str, Optional[str]]:
        """Get all net connections for a symbol.

        Args:
            symbol: The symbol

        Returns:
            Dictionary mapping pin IDs to net names (None if unconnected)

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> nets = sch.labels.get_symbol_nets(r1)
            >>> for pin, net in nets.items():
            ...     print(f"  Pin {pin}: {net or 'unconnected'}")
        """
        result = {}
        for pin in symbol.pins:
            pin_id = pin.number or pin.name
            result[pin_id] = self.get_pin_net(symbol, pin_id)
        return result
