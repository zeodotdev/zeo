# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import math
from typing import List, Optional, Union, Sequence, Tuple, cast
from dataclasses import dataclass
from google.protobuf.empty_pb2 import Empty

from kipy.client import KiCadClient, ApiError
from kipy.proto.common.types import DocumentSpecifier, DocumentType, KiCadObjectType, KIID, FrameType
from kipy.proto.common.commands import editor_commands_pb2
from kipy.proto.common.commands.editor_commands_pb2 import (
    BeginCommit, BeginCommitResponse, CommitAction,
    EndCommit, EndCommitResponse,
    UpdateItems, UpdateItemsResponse,
    DeleteItems, DeleteItemsResponse,
    GetLibraries, GetLibrariesResponse,
    AddLibrary, AddLibraryResponse, AddLibraryStatus, LibraryTableScope,
    CreateDocument, CreateDocumentResponse,
    OpenDocument, OpenDocumentResponse,
    CloseDocument, SetActiveDocument,
)
from kipy.proto.schematic import schematic_commands_pb2
from kipy.wrapper import Wrapper, unwrap
from kipy.util import pack_any
from kipy.common_types import Commit, TitleBlockInfo, PageInfo, PageSizeType
from kipy.proto.common.types import base_types_pb2
from kipy.schematic_types import (  # noqa: F401 - re-exported for convenience
    Symbol, Pin, Field, Wire, LocalLabel, GlobalLabel, HierarchicalLabel,
    Junction, NoConnect, SchematicText, SchematicGraphicShape, SchematicTextBox
)
from kipy.common_types import LibraryIdentifier
from kipy.geometry import Vector2
from kipy.project import Project


@dataclass
class LibraryInfo:
    """Information about a configured symbol library.

    Attributes:
        nickname: The library nickname used in LIB_ID references (e.g., "Device")
        uri: The URI/path to the library file
        type: Plugin type (e.g., "KiCad", "Legacy")
        description: Optional description of the library
        is_loaded: Whether the library was successfully loaded
        is_read_only: Whether the library is read-only
        scope: Which library table this belongs to ("global" or "project")
    """
    nickname: str
    uri: str
    type: str
    description: str = ""
    is_loaded: bool = False
    is_read_only: bool = False
    scope: str = "global"


class Schematic:
    def __init__(self, kicad: KiCadClient, document: DocumentSpecifier):
        self._kicad = kicad
        self._doc = document

    @property
    def client(self) -> KiCadClient:
        return self._kicad
    
    @property
    def document(self) -> DocumentSpecifier:
        return self._doc

    def get_project(self) -> Project:
        # Make a copy of _doc to avoid Project.__init__ mutating our document type
        doc_copy = DocumentSpecifier()
        doc_copy.CopyFrom(self._doc)
        return Project(self._kicad, doc_copy)

    def save(self):
        command = editor_commands_pb2.SaveDocument()
        command.document.CopyFrom(self._doc)
        self._kicad.send(command, Empty)

    def refresh(self):
        """Refreshes the schematic editor view.

        This triggers a redraw of the schematic editor window, ensuring that any
        changes made through the API are visually reflected in the editor.
        Call this after making changes if the display doesn't update automatically.
        """
        command = editor_commands_pb2.RefreshEditor()
        command.frame = FrameType.FT_SCHEMATIC_EDITOR
        self._kicad.send(command, Empty)

    def get_items(self, types: Union[int, Sequence[int]]) -> Sequence[Wrapper]:
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
            except (ValueError, NotImplementedError):
                # Skip items we don't have wrappers for yet
                pass
        return items

    def get_symbols(self) -> List[Symbol]:
        """Retrieves all symbols (components) in the schematic."""
        return [cast(Symbol, i) for i in self.get_items(KiCadObjectType.KOT_SCH_SYMBOL)]

    def get_pins(self) -> List[Pin]:
        """Retrieves all pins from all symbols in the schematic."""
        return [cast(Pin, i) for i in self.get_items(KiCadObjectType.KOT_SCH_PIN)]

    def get_fields(self) -> List[Field]:
        """Retrieves all fields from all symbols in the schematic."""
        return [cast(Field, i) for i in self.get_items(KiCadObjectType.KOT_SCH_FIELD)]

    def get_wires(self) -> Sequence[Wrapper]:
        """Retrieves all wires/lines in the schematic."""
        return self.get_items(KiCadObjectType.KOT_SCH_LINE)

    def get_junctions(self) -> Sequence[Wrapper]:
        """Retrieves all junctions in the schematic."""
        return self.get_items(KiCadObjectType.KOT_SCH_JUNCTION)

    def get_labels(self) -> Sequence[Wrapper]:
        """Retrieves all labels (local, global, hierarchical) in the schematic."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_LABEL,
            KiCadObjectType.KOT_SCH_GLOBAL_LABEL,
            KiCadObjectType.KOT_SCH_HIER_LABEL,
        ])

    def get_sheets(self) -> Sequence[Wrapper]:
        """Retrieves all sheet references in the schematic."""
        return self.get_items(KiCadObjectType.KOT_SCH_SHEET)

    def get_text_items(self) -> Sequence[Wrapper]:
        """Retrieves all text and textbox items in the schematic."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_TEXT,
            KiCadObjectType.KOT_SCH_TEXTBOX,
        ])

    def get_no_connects(self) -> List[NoConnect]:
        """Retrieves all no-connect markers in the schematic."""
        return [cast(NoConnect, i) for i in self.get_items(KiCadObjectType.KOT_SCH_NO_CONNECT)]

    def get_bus_entries(self) -> Sequence[Wrapper]:
        """Retrieves all bus entries (wire-to-bus and bus-to-bus) in the schematic."""
        return self.get_items([
            KiCadObjectType.KOT_SCH_BUS_WIRE_ENTRY,
            KiCadObjectType.KOT_SCH_BUS_BUS_ENTRY,
        ])

    def get_local_labels(self) -> List[LocalLabel]:
        """Retrieves all local net labels in the schematic."""
        return [cast(LocalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_LABEL)]

    def get_global_labels(self) -> List[GlobalLabel]:
        """Retrieves all global net labels in the schematic."""
        return [cast(GlobalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_GLOBAL_LABEL)]

    def get_hierarchical_labels(self) -> List[HierarchicalLabel]:
        """Retrieves all hierarchical labels in the schematic."""
        return [cast(HierarchicalLabel, i) for i in self.get_items(KiCadObjectType.KOT_SCH_HIER_LABEL)]

    def get_directive_labels(self) -> Sequence[Wrapper]:
        """Retrieves all directive labels (for SPICE/simulation) in the schematic."""
        return self.get_items(KiCadObjectType.KOT_SCH_DIRECTIVE_LABEL)

    def get_graphic_shapes(self) -> List[SchematicGraphicShape]:
        """Retrieves all graphic shapes (lines, rectangles, circles, arcs) in the schematic."""
        return [cast(SchematicGraphicShape, i) for i in self.get_items(KiCadObjectType.KOT_SCH_SHAPE)]

    def get_textboxes(self) -> List[SchematicTextBox]:
        """Retrieves all textbox items in the schematic."""
        return [cast(SchematicTextBox, i) for i in self.get_items(KiCadObjectType.KOT_SCH_TEXTBOX)]

    def get_by_id(self, item_ids: Union[str, Sequence[str]]) -> Sequence[Wrapper]:
        """Retrieves specific items by their KIID."""
        from kipy.proto.common.types.base_types_pb2 import KIID

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
            except (ValueError, NotImplementedError):
                pass
        return items

    def create_items(self, items: Union[Wrapper, Sequence[Wrapper]]) -> List[Wrapper]:
        """Creates one or more items in the schematic.

        Args:
            items: A single item or sequence of items to create

        Returns:
            List of successfully created items

        Raises:
            ApiError: If any item fails to create with an error from KiCad
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
            # Check status code: ISC_OK = 1
            if result.status.code != 1:
                error_msg = result.status.error_message or f"status code {result.status.code}"
                errors.append(error_msg)
                continue

            try:
                wrapped = unwrap(result.item)
                created.append(wrapped)
            except (ValueError, NotImplementedError):
                # Skip items we don't have wrappers for yet
                pass

        # Raise error if any items failed to create
        if errors:
            raise ApiError(f"Failed to create items: {'; '.join(errors)}")

        return created

    def update_items(self, items: Union[Wrapper, Sequence[Wrapper]]) -> List[Wrapper]:
        """Updates the properties of one or more items in the schematic.  The items must already exist
        in the schematic, and are matched by internal UUID.  All other properties of the items are
        updated from those passed in this call.

        Returns the updated items, which may be different from the input items if any updates
        failed to apply (for example, if any properties were out of range and were clamped)"""
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
            except (ValueError, NotImplementedError):
                pass
        return updated

    def remove_items(self, items: Union[Wrapper, Sequence[Wrapper]]):
        """Deletes one or more items from the schematic"""
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            command.item_ids.append(items.id)
        else:
            command.item_ids.extend([item.id for item in items])

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    def remove_items_by_id(self, items: Union[KIID, Sequence[KIID]]):
        """Deletes one or more items from the schematic using their unique IDs"""
        command = DeleteItems()
        command.header.document.CopyFrom(self._doc)

        if isinstance(items, KIID):
            command.item_ids.append(items)
        else:
            command.item_ids.extend(items)

        if len(command.item_ids) == 0:
            return

        self._kicad.send(command, DeleteItemsResponse)

    def begin_commit(self) -> Commit:
        """Begins a commit transaction on the schematic, returning a Commit object that can be used to
        push or drop (cancel) the commit.  Each commit represents a set of changes that can be
        undone or redone as a single operation.

        If you do not call begin_commit, any changes made to the schematic will be committed
        immediately, which will result in multiple steps being added to the undo history.

        If you call begin_commit, changes made to the schematic will not be reflected in the editor
        until you call push_commit.  This allows you to group multiple changes into a single undo
        step.
        """
        command = BeginCommit()
        return Commit(self._kicad.send(command, BeginCommitResponse).id)

    def push_commit(self, commit: Commit, message: str = ""):
        """If a commit is open, pushes the changes to the schematic and closes the commit.  This will
        result in a single undo step being added to the undo history."""
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_COMMIT
        command.message = message
        self._kicad.send(command, EndCommitResponse)

    def drop_commit(self, commit: Commit):
        """Cancel a commit, discarding any changes made since the commit was opened"""
        command = EndCommit()
        command.id.CopyFrom(commit.id)
        command.action = CommitAction.CMA_DROP
        self._kicad.send(command, EndCommitResponse)

    def get_selection(
        self,
        types: Optional[Union[int, Sequence[int]]] = None,
    ) -> Sequence[Wrapper]:
        """Gets the current selection in the schematic editor.

        :param types: Optional filter for item types (KiCadObjectType values)
        :return: List of selected items
        """
        cmd = editor_commands_pb2.GetSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(types, int):
            cmd.types.append(types)
        elif types is not None:
            cmd.types.extend(types)

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        items = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                items.append(wrapped)
            except (ValueError, NotImplementedError):
                pass
        return items

    def add_to_selection(self, items: Union[Wrapper, Sequence[Wrapper]]) -> Sequence[Wrapper]:
        """Adds one or more items to the current selection in the schematic

        :param items: The items to add to the selection
        :return: The updated selection
        """
        cmd = editor_commands_pb2.AddToSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        result = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                result.append(wrapped)
            except (ValueError, NotImplementedError):
                pass
        return result

    def remove_from_selection(self, items: Union[Wrapper, Sequence[Wrapper]]) -> Sequence[Wrapper]:
        """Removes one or more items from the current selection in the schematic

        :param items: The items to remove from the selection
        :return: The updated selection
        """
        cmd = editor_commands_pb2.RemoveFromSelection()
        cmd.header.document.CopyFrom(self._doc)

        if isinstance(items, Wrapper):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        response = self._kicad.send(cmd, editor_commands_pb2.SelectionResponse)
        result = []
        for item in response.items:
            try:
                wrapped = unwrap(item)
                result.append(wrapped)
            except (ValueError, NotImplementedError):
                pass
        return result

    def clear_selection(self):
        """Clears the current selection in the schematic"""
        cmd = editor_commands_pb2.ClearSelection()
        cmd.header.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Grid and Coordinate Helper Methods (Phase 1)
    # =========================================================================

    def get_grid_settings(self) -> dict:
        """Get the grid settings for the schematic editor.

        IMPORTANT: The KiCad IPC API does not expose grid settings, so this
        function returns the default KiCad schematic grid values. If you have
        changed the grid in KiCad preferences, these values may not match.

        Returns:
            dict with keys:
            - size_mm: Grid size in millimeters (default 1.27mm = 50 mils)
            - size_mils: Grid size in mils (default 50)
            - size_nm: Grid size in nanometers (internal units)
            - is_default: True (indicates these are default values, not queried)

        Example:
            >>> grid = sch.get_grid_settings()
            >>> print(f"Grid: {grid['size_mm']}mm ({grid['size_mils']} mils)")
            >>> if grid['is_default']:
            ...     print("Note: Using default grid - actual grid may differ")
        """
        # KiCad default schematic grid is 50 mils = 1.27mm
        # NOTE: There is no IPC API to query the actual grid settings.
        # These are the standard defaults that KiCad uses.
        DEFAULT_GRID_MILS = 50
        DEFAULT_GRID_MM = 1.27
        DEFAULT_GRID_NM = 1270000  # 1.27mm in nanometers

        return {
            'size_mm': DEFAULT_GRID_MM,
            'size_mils': DEFAULT_GRID_MILS,
            'size_nm': DEFAULT_GRID_NM,
            'is_default': True,  # Flag indicating these are defaults, not queried values
        }

    def snap_to_grid(self, x_mm: float, y_mm: float, grid_mm: float = 1.27) -> Tuple[float, float]:
        """Snap coordinates to the nearest grid point.

        Args:
            x_mm: X coordinate in millimeters
            y_mm: Y coordinate in millimeters
            grid_mm: Grid size in millimeters (default 1.27mm = 50 mils)

        Returns:
            Tuple of (snapped_x_mm, snapped_y_mm)

        Example:
            >>> x, y = sch.snap_to_grid(100.5, 80.3)
            >>> print(f"Snapped: ({x}, {y})")  # (100.33, 80.01) or similar
        """
        snapped_x = round(x_mm / grid_mm) * grid_mm
        snapped_y = round(y_mm / grid_mm) * grid_mm
        return (snapped_x, snapped_y)

    def get_usable_area(self) -> dict:
        """Get the usable drawing area within the page boundaries.

        The usable area accounts for the title block and border margins.
        This is smaller than the full page size returned by get_page_settings().

        COORDINATE SYSTEM:
        - Page origin (0, 0) is at the TOP-LEFT corner of the page
        - X increases to the RIGHT
        - Y increases DOWNWARD
        - All coordinates are in MILLIMETERS

        NOTE: The margins are approximate estimates for the standard KiCad
        drawing sheet. Actual margins may vary with custom templates.

        Returns:
            dict with keys (both naming conventions provided for compatibility):
            - min_x_mm / left_mm: Left boundary in mm (X start)
            - max_x_mm / right_mm: Right boundary in mm (X end)
            - min_y_mm / top_mm: Top boundary in mm (Y start)
            - max_y_mm / bottom_mm: Bottom boundary in mm (Y end)
            - width_mm: Usable width in mm
            - height_mm: Usable height in mm
            - center_x_mm: Center X coordinate (place components here!)
            - center_y_mm: Center Y coordinate (place components here!)
            - page_width_mm: Full page width for reference
            - page_height_mm: Full page height for reference

        Example:
            >>> area = sch.get_usable_area()
            >>> print(f"Usable area: {area['width_mm']:.0f}x{area['height_mm']:.0f}mm")
            >>> print(f"X: {area['min_x_mm']:.0f} to {area['max_x_mm']:.0f}mm")
            >>> print(f"Y: {area['min_y_mm']:.0f} to {area['max_y_mm']:.0f}mm")
            >>> print(f"Center: ({area['center_x_mm']:.0f}, {area['center_y_mm']:.0f})")
        """
        page = self.get_page_settings()

        # Standard margins for title block (approximate estimates)
        # These work for the default KiCad drawing sheet template.
        # Left/top margin is typically small, right/bottom have title block
        LEFT_MARGIN = 10.0    # ~400 mils
        TOP_MARGIN = 10.0     # ~400 mils
        RIGHT_MARGIN = 35.0   # ~1400 mils - Title block on right
        BOTTOM_MARGIN = 25.0  # ~1000 mils - Title block on bottom

        left = LEFT_MARGIN
        top = TOP_MARGIN
        right = page.width_mm - RIGHT_MARGIN
        bottom = page.height_mm - BOTTOM_MARGIN

        return {
            # Primary naming convention (min/max style)
            'min_x_mm': left,
            'max_x_mm': right,
            'min_y_mm': top,
            'max_y_mm': bottom,
            # Alternative naming convention (left/right/top/bottom style)
            'left_mm': left,
            'right_mm': right,
            'top_mm': top,
            'bottom_mm': bottom,
            # Dimensions
            'width_mm': right - left,
            'height_mm': bottom - top,
            # Center point - RECOMMENDED for starting component placement
            'center_x_mm': (left + right) / 2,
            'center_y_mm': (top + bottom) / 2,
            # Full page dimensions for reference
            'page_width_mm': page.width_mm,
            'page_height_mm': page.height_mm,
        }

    # =========================================================================
    # Pin-Based Wiring Methods (Phase 2)
    # =========================================================================

    def _transform_pin_position(
        self,
        pin_pos: Vector2,
        symbol: Symbol,
    ) -> Vector2:
        """Transform a pin position from symbol-local to world coordinates.

        This applies the symbol's rotation and mirror transformations to
        convert pin positions from the symbol's local coordinate system
        to the schematic's world coordinate system.

        Args:
            pin_pos: Pin position (potentially in symbol-local coords)
            symbol: The symbol containing the pin

        Returns:
            Transformed position in world coordinates
        """
        # Get symbol center and pin position in nanometers
        sym_x, sym_y = symbol.position.x, symbol.position.y
        pin_x, pin_y = pin_pos.x, pin_pos.y

        # Calculate offset from symbol center
        rel_x = pin_x - sym_x
        rel_y = pin_y - sym_y

        # If offset is very small (< 1mm), pins might already be in world coords
        # or the symbol has pins at center - either way, no transform needed
        if abs(rel_x) < 1e6 and abs(rel_y) < 1e6:
            # Check if we should apply rotation anyway
            # Small offsets might just be the symbol's internal pin spacing
            pass

        # Apply rotation if symbol has non-zero angle
        angle = symbol.angle
        if angle != 0:
            angle_rad = math.radians(angle)
            cos_a = math.cos(angle_rad)
            sin_a = math.sin(angle_rad)

            # Rotate around symbol center
            new_rel_x = rel_x * cos_a - rel_y * sin_a
            new_rel_y = rel_x * sin_a + rel_y * cos_a

            rel_x, rel_y = new_rel_x, new_rel_y

        # Apply mirror transformations
        if symbol.mirror_x:
            rel_y = -rel_y
        if symbol.mirror_y:
            rel_x = -rel_x

        # Translate back to world coordinates
        world_x = int(sym_x + rel_x)
        world_y = int(sym_y + rel_y)

        return Vector2.from_xy(world_x, world_y)

    def get_pin_position(
        self,
        symbol: Symbol,
        pin_id: str,
        transform: bool = True,
    ) -> Optional[Vector2]:
        """Get the exact position of a pin on a symbol.

        Args:
            symbol: The Symbol object
            pin_id: Pin identifier - can be pin name (e.g., "G", "D", "S") or
                   pin number (e.g., "1", "2", "3")
            transform: If True (default), apply symbol rotation/mirror to get
                      world coordinates. If False, return raw pin position.

        Returns:
            Vector2 position of the pin in world coordinates, or None if not found

        Note:
            By default, this method transforms pin positions to account for
            symbol rotation and mirroring. If the IPC API already returns
            world-transformed positions, set transform=False.

        Example:
            >>> mosfet = sch.add_symbol("Transistor_FET:BSS138", pos)
            >>> gate_pos = sch.get_pin_position(mosfet, "G")
            >>> drain_pos = sch.get_pin_position(mosfet, "3")  # by number
        """
        for pin in symbol.pins:
            if pin.name == pin_id or pin.number == pin_id:
                if transform:
                    return self._transform_pin_position(pin.position, symbol)
                return pin.position
        return None

    def wire_pins(
        self,
        symbol1: Symbol,
        pin_id1: str,
        symbol2: Symbol,
        pin_id2: str,
    ) -> Optional[Wire]:
        """Create a wire directly between two symbol pins.

        This method automatically retrieves the exact pin positions and creates
        a wire between them, eliminating coordinate precision errors.

        Args:
            symbol1: First symbol
            pin_id1: Pin identifier on first symbol (name or number)
            symbol2: Second symbol
            pin_id2: Pin identifier on second symbol (name or number)

        Returns:
            The created Wire object, or None if pins not found

        Raises:
            ValueError: If either pin is not found

        Example:
            >>> r1 = sch.add_symbol("Device:R", Vector2.from_xy_mm(100, 80))
            >>> r2 = sch.add_symbol("Device:R", Vector2.from_xy_mm(130, 80))
            >>> wire = sch.wire_pins(r1, "2", r2, "1")
        """
        pos1 = self.get_pin_position(symbol1, pin_id1)
        pos2 = self.get_pin_position(symbol2, pin_id2)

        if pos1 is None:
            raise ValueError(f"Pin '{pin_id1}' not found on symbol")
        if pos2 is None:
            raise ValueError(f"Pin '{pin_id2}' not found on symbol")

        return self.add_wire(pos1, pos2)

    def wire_from_pin(
        self,
        symbol: Symbol,
        pin_id: str,
        end_point_mm: Tuple[float, float],
    ) -> Optional[Wire]:
        """Create a wire from a symbol pin to a coordinate.

        This method retrieves the exact pin position and creates a wire from
        it to the specified endpoint, eliminating pin connection errors.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin identifier (name or number)
            end_point_mm: End point as (x_mm, y_mm) tuple

        Returns:
            The created Wire object, or None if pin not found

        Raises:
            ValueError: If pin is not found

        Example:
            >>> mosfet = sch.add_symbol("Transistor_FET:BSS138", pos)
            >>> # Create a stub wire from gate pin going left
            >>> wire = sch.wire_from_pin(mosfet, "G", (50, 70))
        """
        pos = self.get_pin_position(symbol, pin_id)

        if pos is None:
            raise ValueError(f"Pin '{pin_id}' not found on symbol")

        end = Vector2.from_xy_mm(end_point_mm[0], end_point_mm[1])
        return self.add_wire(pos, end)

    def wire_to_pin(
        self,
        start_point_mm: Tuple[float, float],
        symbol: Symbol,
        pin_id: str,
    ) -> Optional[Wire]:
        """Create a wire from a coordinate to a symbol pin.

        This method retrieves the exact pin position and creates a wire to it
        from the specified start point, eliminating pin connection errors.

        Args:
            start_point_mm: Start point as (x_mm, y_mm) tuple
            symbol: The symbol containing the pin
            pin_id: Pin identifier (name or number)

        Returns:
            The created Wire object, or None if pin not found

        Raises:
            ValueError: If pin is not found

        Example:
            >>> cap = sch.add_symbol("Device:C", pos)
            >>> # Create a wire from a junction to capacitor pin
            >>> wire = sch.wire_to_pin((110, 70), cap, "1")
        """
        pos = self.get_pin_position(symbol, pin_id)

        if pos is None:
            raise ValueError(f"Pin '{pin_id}' not found on symbol")

        start = Vector2.from_xy_mm(start_point_mm[0], start_point_mm[1])
        return self.add_wire(start, pos)

    def wire_path(
        self,
        start_pin: Tuple[Symbol, str],
        waypoints: List[Tuple[float, float]],
        end_pin: Tuple[Symbol, str],
    ) -> List[Wire]:
        """Create a wire path from one pin through waypoints to another pin.

        This method creates a series of connected wires starting from one pin,
        going through intermediate waypoints, and ending at another pin.
        All connections to pins use exact pin positions.

        Args:
            start_pin: Tuple of (symbol, pin_id) for the start
            waypoints: List of (x_mm, y_mm) intermediate points
            end_pin: Tuple of (symbol, pin_id) for the end

        Returns:
            List of created Wire objects

        Raises:
            ValueError: If either pin is not found

        Example:
            >>> mosfet = sch.add_symbol("Transistor_FET:BSS138", pos1)
            >>> inductor = sch.add_symbol("Device:L", pos2)
            >>> # Route from MOSFET source through switch node to inductor
            >>> wires = sch.wire_path(
            ...     start_pin=(mosfet, "S"),
            ...     waypoints=[(110, 70), (110, 85)],
            ...     end_pin=(inductor, "1")
            ... )
        """
        start_symbol, start_pin_id = start_pin
        end_symbol, end_pin_id = end_pin

        start_pos = self.get_pin_position(start_symbol, start_pin_id)
        end_pos = self.get_pin_position(end_symbol, end_pin_id)

        if start_pos is None:
            raise ValueError(f"Start pin '{start_pin_id}' not found on symbol")
        if end_pos is None:
            raise ValueError(f"End pin '{end_pin_id}' not found on symbol")

        # Build the full path: start_pin -> waypoints -> end_pin
        points = [start_pos]
        for wp in waypoints:
            points.append(Vector2.from_xy_mm(wp[0], wp[1]))
        points.append(end_pos)

        # Create wires for each segment
        return self.add_wires(points)

    def auto_wire(
        self,
        symbol1: Symbol,
        pin_id1: str,
        symbol2: Symbol,
        pin_id2: str,
        style: str = "L",
    ) -> List[Wire]:
        """Automatically wire between two pins using orthogonal routing.

        This method creates an L-shaped (or direct) wire path between two pins,
        using exact pin positions. The routing style determines how the wire
        is routed between pins that are not aligned.

        Args:
            symbol1: First symbol
            pin_id1: Pin identifier on first symbol (name or number)
            symbol2: Second symbol
            pin_id2: Pin identifier on second symbol (name or number)
            style: Routing style:
                   - "direct": Single straight wire (may be diagonal)
                   - "L": L-shaped route, automatically chooses H-first or V-first
                   - "L_horizontal_first": Go horizontal, then vertical
                   - "L_vertical_first": Go vertical, then horizontal

        Returns:
            List of created Wire objects

        Raises:
            ValueError: If either pin is not found

        Example:
            >>> mosfet = sch.add_symbol("Device:Q_NMOS_GSD", pos1)
            >>> resistor = sch.add_symbol("Device:R", pos2)
            >>> # Auto-route with L-shape
            >>> wires = sch.auto_wire(mosfet, "D", resistor, "1", style="L")
        """
        pos1 = self.get_pin_position(symbol1, pin_id1)
        pos2 = self.get_pin_position(symbol2, pin_id2)

        if pos1 is None:
            raise ValueError(f"Pin '{pin_id1}' not found on first symbol")
        if pos2 is None:
            raise ValueError(f"Pin '{pin_id2}' not found on second symbol")

        # Convert to mm for calculations
        x1, y1 = pos1.x / 1e6, pos1.y / 1e6
        x2, y2 = pos2.x / 1e6, pos2.y / 1e6

        # Direct routing - single wire
        if style == "direct":
            return [self.add_wire(pos1, pos2)]

        # Check if pins are already aligned (within 0.1mm tolerance)
        if abs(x1 - x2) < 0.1 or abs(y1 - y2) < 0.1:
            # Pins are aligned, direct wire is fine
            return [self.add_wire(pos1, pos2)]

        # L-shaped routing
        if style == "L":
            # Auto-choose: prefer the direction that creates a cleaner route
            # Heuristic: go in the direction of larger distance first
            dx = abs(x2 - x1)
            dy = abs(y2 - y1)
            horizontal_first = dx >= dy
        elif style == "L_horizontal_first":
            horizontal_first = True
        elif style == "L_vertical_first":
            horizontal_first = False
        else:
            raise ValueError(f"Unknown style '{style}'. Use 'direct', 'L', 'L_horizontal_first', or 'L_vertical_first'")

        # Create the corner point for L-routing
        if horizontal_first:
            # Go horizontal first, then vertical
            corner = Vector2.from_xy_mm(x2, y1)
        else:
            # Go vertical first, then horizontal
            corner = Vector2.from_xy_mm(x1, y2)

        # Create wires: start -> corner -> end
        return self.add_wires([pos1, corner, pos2])

    def auto_wire_to_point(
        self,
        symbol: Symbol,
        pin_id: str,
        point_mm: Tuple[float, float],
        style: str = "L",
    ) -> List[Wire]:
        """Automatically wire from a pin to a coordinate point.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin identifier (name or number)
            point_mm: Target point as (x_mm, y_mm) tuple
            style: Routing style ("direct", "L", "L_horizontal_first", "L_vertical_first")

        Returns:
            List of created Wire objects

        Example:
            >>> # Wire from resistor pin to a junction point
            >>> wires = sch.auto_wire_to_point(resistor, "2", (100, 80), style="L")
        """
        pos = self.get_pin_position(symbol, pin_id)

        if pos is None:
            raise ValueError(f"Pin '{pin_id}' not found on symbol")

        x1, y1 = pos.x / 1e6, pos.y / 1e6
        x2, y2 = point_mm

        end = Vector2.from_xy_mm(x2, y2)

        if style == "direct":
            return [self.add_wire(pos, end)]

        # Check if already aligned
        if abs(x1 - x2) < 0.1 or abs(y1 - y2) < 0.1:
            return [self.add_wire(pos, end)]

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

        return self.add_wires([pos, corner, end])

    def connect_to_junction(
        self,
        pins: List[Tuple[Symbol, str]],
        junction_mm: Tuple[float, float],
        style: str = "L",
    ) -> Tuple[List[Wire], Wrapper]:
        """Connect multiple pins to a common junction point.

        This is useful for creating star connections where multiple components
        connect to the same net point (e.g., switch node in a buck converter).

        Args:
            pins: List of (symbol, pin_id) tuples to connect
            junction_mm: Junction location as (x_mm, y_mm) tuple
            style: Routing style for each wire

        Returns:
            Tuple of (list of created wires, junction object)

        Example:
            >>> # Connect MOSFET source, diode cathode, and inductor to switch node
            >>> wires, junction = sch.connect_to_junction(
            ...     [(mosfet, "S"), (diode, "K"), (inductor, "1")],
            ...     junction_mm=(100, 60),
            ...     style="L"
            ... )
        """
        all_wires = []

        for symbol, pin_id in pins:
            wires = self.auto_wire_to_point(symbol, pin_id, junction_mm, style=style)
            all_wires.extend(wires)

        # Create junction at the connection point
        junction_pos = Vector2.from_xy_mm(junction_mm[0], junction_mm[1])
        junction = self.add_junction(junction_pos)

        return all_wires, junction

    def get_unconnected_pins(self) -> List[dict]:
        """Get a list of all unconnected pins in the schematic.

        This queries the nets and identifies pins that are on "unconnected" nets.

        Returns:
            List of dicts with keys:
            - symbol_ref: Reference designator (e.g., "R1", "Q2")
            - pin_name: Pin name
            - pin_number: Pin number
            - position_mm: (x, y) tuple in millimeters

        Example:
            >>> unconnected = sch.get_unconnected_pins()
            >>> for pin in unconnected:
            ...     print(f"{pin['symbol_ref']} pin {pin['pin_name']}: {pin['position_mm']}")
        """
        nets = self.get_nets()
        unconnected = []

        for net in nets.nets:
            if "unconnected" in net.name.lower():
                # Parse the net name to extract symbol and pin info
                # Format is typically "unconnected-(Q2-G-Pad1)" or "unconnected-(R1-Pad2)"
                name = net.name
                # Extract the part between parentheses
                if "(" in name and ")" in name:
                    inner = name[name.find("(")+1:name.find(")")]
                    parts = inner.split("-")
                    if len(parts) >= 2:
                        symbol_ref = parts[0]
                        # Try to extract pin info
                        pin_name = parts[1] if len(parts) > 2 else ""
                        pin_number = parts[-1].replace("Pad", "") if "Pad" in parts[-1] else ""

                        # Get position from connection points if available
                        pos_mm = None
                        if hasattr(net, 'connection_points') and net.connection_points:
                            cp = net.connection_points[0]
                            pos_mm = (cp.x_nm / 1e6, cp.y_nm / 1e6)

                        unconnected.append({
                            'symbol_ref': symbol_ref,
                            'pin_name': pin_name,
                            'pin_number': pin_number,
                            'position_mm': pos_mm,
                            'net_name': name,
                        })

        return unconnected

    # =========================================================================
    # Wiring & Connectivity Helper Methods
    # =========================================================================

    def add_wire(
        self,
        start: Vector2,
        end: Vector2,
    ) -> Wire:
        """Add a wire between two points in the schematic.

        Args:
            start: Start point of the wire
            end: End point of the wire

        Returns:
            The created Wire object

        Example:
            >>> from kipy.geometry import Vector2
            >>> wire = sch.add_wire(
            ...     Vector2.from_xy_mm(10, 20),
            ...     Vector2.from_xy_mm(30, 20)
            ... )
        """
        wire = Wire.create(start, end)
        created = self.create_items(wire)
        if created:
            return created[0]
        return wire

    def add_wires(
        self,
        points: Sequence[Vector2],
    ) -> List[Wire]:
        """Add a series of connected wires through multiple points.

        Args:
            points: List of points to connect with wires

        Returns:
            List of created Wire objects

        Example:
            >>> from kipy.geometry import Vector2
            >>> wires = sch.add_wires([
            ...     Vector2.from_xy_mm(0, 0),
            ...     Vector2.from_xy_mm(10, 0),
            ...     Vector2.from_xy_mm(10, 10),
            ... ])
        """
        if len(points) < 2:
            return []

        wires = []
        for i in range(len(points) - 1):
            wire = Wire.create(points[i], points[i + 1])
            wires.append(wire)

        created = self.create_items(wires)
        return created if created else wires

    def add_junction(self, position: Vector2) -> Optional[Wrapper]:
        """Add a junction (connection point) at the given position.

        Args:
            position: Position for the junction

        Returns:
            The created Junction object, or None if Junction is not available

        Example:
            >>> from kipy.geometry import Vector2
            >>> junction = sch.add_junction(Vector2.from_xy_mm(10, 20))
        """
        if Junction is None:
            raise NotImplementedError("Junction support requires proto regeneration")

        junction = Junction.create(position)
        created = self.create_items(junction)
        if created:
            return created[0]
        return junction

    def add_no_connect(self, position: Vector2) -> Optional[Wrapper]:
        """Add a no-connect marker at the given position.

        Args:
            position: Position for the no-connect marker

        Returns:
            The created NoConnect object, or None if NoConnect is not available

        Example:
            >>> from kipy.geometry import Vector2
            >>> nc = sch.add_no_connect(Vector2.from_xy_mm(10, 20))
        """
        if NoConnect is None:
            raise NotImplementedError("NoConnect support requires proto regeneration")

        nc = NoConnect.create(position)
        created = self.create_items(nc)
        if created:
            return created[0]
        return nc

    # =========================================================================
    # Symbol & Label Helper Methods
    # =========================================================================

    def add_symbol(
        self,
        lib_id: str,
        position: Vector2,
        unit: int = 1,
        angle: float = 0.0,
        mirror_x: bool = False,
        mirror_y: bool = False,
    ) -> Symbol:
        """Place a symbol from a library at the given position.

        Args:
            lib_id: Library identifier as "library:symbol" string (e.g., "Device:R")
            position: Position for the symbol in schematic internal units
            unit: Unit number for multi-unit symbols (default 1)
            angle: Rotation angle in degrees (0, 90, 180, 270)
            mirror_x: Mirror along X axis
            mirror_y: Mirror along Y axis

        Returns:
            The created Symbol object

        Raises:
            IPCError: If the library symbol is not found or creation fails

        Example:
            >>> from kipy.geometry import Vector2
            >>> # Place a resistor at 50mm, 50mm
            >>> symbol = sch.add_symbol(
            ...     "Device:R",
            ...     Vector2.from_xy(500000, 500000),  # 50mm in schematic IU
            ...     angle=90.0
            ... )
        """
        symbol = Symbol()

        # Parse library:name format and create LibraryIdentifier
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

        created = self.create_items(symbol)
        if created:
            return cast(Symbol, created[0])
        return symbol

    def add_label(
        self,
        text: str,
        position: Vector2,
        label_type: str = "local",
    ) -> Wrapper:
        """Add a net label at the given position.

        Args:
            text: The label text (net name)
            position: Position for the label in schematic internal units
            label_type: Type of label - "local", "global", or "hierarchical"

        Returns:
            The created label object (LocalLabel, GlobalLabel, or HierarchicalLabel)

        Example:
            >>> from kipy.geometry import Vector2
            >>> label = sch.add_label("VCC", Vector2.from_xy(500000, 500000), "global")
        """
        if label_type == "local":
            label = LocalLabel.create(position, text)
        elif label_type == "global":
            label = GlobalLabel()
            label.position = position
            # Note: text property not fully supported in proto yet
        elif label_type == "hierarchical":
            label = HierarchicalLabel()
            label.position = position
            # Note: text property not fully supported in proto yet
        else:
            raise ValueError(f"Unknown label type: {label_type}. Use 'local', 'global', or 'hierarchical'")

        created = self.create_items(label)
        if created:
            return created[0]
        return label

    def add_power_symbol(
        self,
        name: str,
        position: Vector2,
        angle: float = 0.0,
    ) -> Symbol:
        """Place a power symbol (VCC, GND, etc.) at the given position.

        Power symbols are regular symbols from the "power" library. Common symbols:
        - GND, GND1, GND2 (grounds)
        - VCC, VDD, VSS (power rails)
        - +3V3, +5V, +12V (voltage rails)
        - GNDPWR, GNDREF (power/reference grounds)

        Args:
            name: Power symbol name (e.g., "GND", "VCC", "+3V3")
            position: Position for the symbol in schematic internal units
            angle: Rotation angle in degrees (default 0, typically 0 or 180)

        Returns:
            The created Symbol object

        Raises:
            IPCError: If the power symbol is not found or creation fails

        Example:
            >>> from kipy.geometry import Vector2
            >>> # Place a ground symbol
            >>> gnd = sch.add_power_symbol("GND", Vector2.from_mm(50, 100))
            >>> # Place VCC rotated 180 degrees (pin pointing down)
            >>> vcc = sch.add_power_symbol("VCC", Vector2.from_mm(50, 50), angle=180)
        """
        lib_id = f"power:{name}"
        return self.add_symbol(lib_id, position, unit=1, angle=angle)

    def add_directive_label(
        self,
        text: str,
        position: Vector2,
    ) -> Wrapper:
        """Add a directive label (SPICE simulation directive) at the given position.

        Directive labels are used in SPICE simulation to add simulation commands
        like .tran, .ac, .dc, or component parameters.

        Args:
            text: The directive text (e.g., ".tran 1m", ".ac dec 10 1 1Meg")
            position: Position for the label in schematic internal units

        Returns:
            The created DirectiveLabel object

        Example:
            >>> from kipy.geometry import Vector2
            >>> # Add transient analysis directive
            >>> directive = sch.add_directive_label(".tran 1m", Vector2.from_mm(50, 150))
        """
        from kipy.schematic_types import DirectiveLabel

        label = DirectiveLabel.create(position, text)
        created = self.create_items(label)
        if created:
            return created[0]
        return label

    # =========================================================================
    # Graphics Helper Methods (Text, Shapes, TextBoxes)
    # =========================================================================

    def add_text(
        self,
        text: str,
        position: Vector2,
    ) -> SchematicText:
        """Add a text annotation to the schematic.

        Args:
            text: The text content
            position: Position for the text in schematic internal units

        Returns:
            The created SchematicText object

        Example:
            >>> from kipy.geometry import Vector2
            >>> text = sch.add_text("Note: Check values", Vector2.from_xy(500000, 500000))
        """
        text_item = SchematicText.create(text, position)
        created = self.create_items(text_item)
        if created:
            return cast(SchematicText, created[0])
        return text_item

    def add_textbox(
        self,
        text: str,
        top_left: Vector2,
        bottom_right: Vector2,
    ) -> SchematicTextBox:
        """Add a text box to the schematic.

        Args:
            text: The text content
            top_left: Top-left corner position
            bottom_right: Bottom-right corner position

        Returns:
            The created SchematicTextBox object

        Example:
            >>> from kipy.geometry import Vector2
            >>> textbox = sch.add_textbox(
            ...     "Design Notes\\nRevision 1.0",
            ...     Vector2.from_xy(100000, 100000),
            ...     Vector2.from_xy(300000, 200000)
            ... )
        """
        textbox = SchematicTextBox.create(text, top_left, bottom_right)
        created = self.create_items(textbox)
        if created:
            return cast(SchematicTextBox, created[0])
        return textbox

    def add_rectangle(
        self,
        top_left: Vector2,
        bottom_right: Vector2,
        stroke_width: int = 0,
        filled: bool = False,
    ) -> SchematicGraphicShape:
        """Add a rectangle shape to the schematic.

        Args:
            top_left: Top-left corner position
            bottom_right: Bottom-right corner position
            stroke_width: Line width in internal units (default 0 = default width)
            filled: Whether to fill the shape

        Returns:
            The created SchematicGraphicShape object

        Example:
            >>> from kipy.geometry import Vector2
            >>> rect = sch.add_rectangle(
            ...     Vector2.from_xy(100000, 100000),
            ...     Vector2.from_xy(200000, 150000),
            ...     filled=True
            ... )
        """
        shape = SchematicGraphicShape.create_rectangle(top_left, bottom_right, stroke_width, filled)
        created = self.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_circle(
        self,
        center: Vector2,
        radius: int,
        stroke_width: int = 0,
        filled: bool = False,
    ) -> SchematicGraphicShape:
        """Add a circle shape to the schematic.

        Args:
            center: Center point of the circle
            radius: Radius in internal units
            stroke_width: Line width in internal units (default 0 = default width)
            filled: Whether to fill the shape

        Returns:
            The created SchematicGraphicShape object

        Example:
            >>> from kipy.geometry import Vector2
            >>> circle = sch.add_circle(
            ...     Vector2.from_xy(150000, 150000),
            ...     radius=50000,  # 5mm radius
            ...     filled=True
            ... )
        """
        # Create radius_point by offsetting from center
        radius_point = Vector2.from_xy(center.x + radius, center.y)
        shape = SchematicGraphicShape.create_circle(center, radius_point, stroke_width, filled)
        created = self.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_line(
        self,
        start: Vector2,
        end: Vector2,
        stroke_width: int = 0,
    ) -> SchematicGraphicShape:
        """Add a graphic line to the schematic (not a wire).

        Note: This creates a graphic line (annotation), not an electrical wire.
        Use add_wire() for electrical connections.

        Args:
            start: Start point of the line
            end: End point of the line
            stroke_width: Line width in internal units (default 0 = default width)

        Returns:
            The created SchematicGraphicShape object

        Example:
            >>> from kipy.geometry import Vector2
            >>> line = sch.add_line(
            ...     Vector2.from_xy(100000, 100000),
            ...     Vector2.from_xy(200000, 200000)
            ... )
        """
        shape = SchematicGraphicShape.create_line(start, end, stroke_width)
        created = self.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_arc(
        self,
        start: Vector2,
        mid: Vector2,
        end: Vector2,
        stroke_width: int = 0,
    ) -> SchematicGraphicShape:
        """Add an arc shape to the schematic.

        Args:
            start: Start point of the arc
            mid: Mid point of the arc (defines curvature)
            end: End point of the arc
            stroke_width: Line width in internal units (default 0 = default width)

        Returns:
            The created SchematicGraphicShape object

        Example:
            >>> from kipy.geometry import Vector2
            >>> arc = sch.add_arc(
            ...     Vector2.from_xy(100000, 150000),
            ...     Vector2.from_xy(150000, 100000),
            ...     Vector2.from_xy(200000, 150000)
            ... )
        """
        shape = SchematicGraphicShape.create_arc(start, mid, end, stroke_width)
        created = self.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    # =========================================================================
    # CLI Export & Check Methods
    # =========================================================================

    def generate_netlist(
        self,
        output_path: str,
        format: str = "kicadsexpr",
    ) -> str:
        """Export netlist via kicad-cli.

        Args:
            output_path: Path for the output netlist file
            format: Netlist format. Options:
                - "kicadsexpr" (default): KiCad S-expression format
                - "kicadxml": KiCad XML format
                - "cadstar": Cadstar format
                - "orcadpcb2": OrCAD PCB2 format
                - "pads": PADS format
                - "allegro": Allegro format

        Returns:
            Path to the generated netlist file

        Raises:
            CLIError: If the netlist generation fails
            FileNotFoundError: If kicad-cli is not found

        Example:
            >>> netlist = sch.generate_netlist("/tmp/project.net")
            >>> netlist_xml = sch.generate_netlist("/tmp/project.xml", format="kicadxml")
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError

        valid_formats = ["kicadsexpr", "kicadxml", "cadstar", "orcadpcb2", "pads", "allegro"]
        if format not in valid_formats:
            raise ValueError(f"Invalid format '{format}'. Valid options: {valid_formats}")

        command = [
            get_kicad_cli_path(),
            "sch", "export", "netlist",
            f"--format={format}",
            "-o", output_path,
            self._doc.identifier,
        ]

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"Netlist generation failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return output_path

    def generate_erc_report(
        self,
        output_path: str,
        format: str = "report",
        units: str = "mm",
    ) -> Tuple[str, int]:
        """Generate ERC report file via kicad-cli.

        This method runs ERC via the command-line interface and saves results
        to a file. For interactive ERC with access to violations, use run_erc().

        Args:
            output_path: Path for the ERC report file
            format: Output format - "report" (text) or "json"
            units: Units for coordinates - "mm", "in", or "mils"

        Returns:
            Tuple of (report_path, violation_count)
            - violation_count is the exit code: 0 = no violations, >0 = number of violations

        Raises:
            CLIError: If ERC fails to run (not for violations found)
            FileNotFoundError: If kicad-cli is not found

        Example:
            >>> path, violations = sch.generate_erc_report("/tmp/erc_report.txt")
            >>> if violations > 0:
            ...     print(f"Found {violations} ERC violations")
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError

        valid_formats = ["report", "json"]
        if format not in valid_formats:
            raise ValueError(f"Invalid format '{format}'. Valid options: {valid_formats}")

        valid_units = ["mm", "in", "mils"]
        if units not in valid_units:
            raise ValueError(f"Invalid units '{units}'. Valid options: {valid_units}")

        command = [
            get_kicad_cli_path(),
            "sch", "erc",
            f"--format={format}",
            f"--units={units}",
            "-o", output_path,
            self._doc.identifier,
        ]

        result = run_cli(command)

        # ERC returns exit code based on violation count
        # Exit code 0 = success, >0 = violation count (or error)
        # We don't raise CLIError for violations, only for actual failures
        if result.returncode < 0:
            raise CLIError(
                f"ERC failed to run: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return (output_path, result.returncode)

    # =========================================================================
    # Library Management Methods
    # =========================================================================

    def get_libraries(self, scope: str = "both") -> List[LibraryInfo]:
        """Get the list of configured symbol libraries.

        Args:
            scope: Which library tables to query:
                - "global": Only global (user-level) libraries
                - "project": Only project-specific libraries
                - "both" (default): All libraries from both tables

        Returns:
            List of LibraryInfo objects describing each configured library

        Example:
            >>> libs = sch.get_libraries()
            >>> for lib in libs:
            ...     print(f"{lib.nickname}: {lib.uri} (loaded={lib.is_loaded})")
            >>> # Get only global libraries
            >>> global_libs = sch.get_libraries(scope="global")
        """
        cmd = GetLibraries()

        # Map scope string to proto enum
        scope_map = {
            "global": LibraryTableScope.LTS_GLOBAL,
            "project": LibraryTableScope.LTS_PROJECT,
            "both": LibraryTableScope.LTS_BOTH,
        }

        if scope.lower() not in scope_map:
            raise ValueError(f"Invalid scope '{scope}'. Valid options: global, project, both")

        cmd.scope = scope_map[scope.lower()]

        response = self._kicad.send(cmd, GetLibrariesResponse)

        libraries = []
        for lib_proto in response.libraries:
            # Map proto scope enum to string
            if lib_proto.scope == LibraryTableScope.LTS_GLOBAL:
                scope_str = "global"
            elif lib_proto.scope == LibraryTableScope.LTS_PROJECT:
                scope_str = "project"
            else:
                scope_str = "unknown"

            lib_info = LibraryInfo(
                nickname=lib_proto.nickname,
                uri=lib_proto.uri,
                type=lib_proto.type,
                description=lib_proto.description,
                is_loaded=lib_proto.is_loaded,
                is_read_only=lib_proto.is_read_only,
                scope=scope_str,
            )
            libraries.append(lib_info)

        return libraries

    def add_library(
        self,
        file_path: str,
        nickname: Optional[str] = None,
        scope: str = "global",
    ) -> LibraryInfo:
        """Add an existing library file to the symbol library table.

        Args:
            file_path: Path to the library file (.kicad_sym or .lib)
            nickname: Optional nickname for the library. If not provided,
                      will be derived from the filename.
            scope: Which table to add to - "global" (default) or "project"

        Returns:
            LibraryInfo for the newly added library

        Raises:
            ApiError: If the library cannot be added (file not found, already exists, etc.)

        Example:
            >>> # Add a library with auto-generated nickname
            >>> lib = sch.add_library("/path/to/my_symbols.kicad_sym")
            >>> print(f"Added library: {lib.nickname}")
            >>>
            >>> # Add with custom nickname
            >>> lib = sch.add_library("/path/to/symbols.kicad_sym", nickname="MySymbols")
            >>>
            >>> # Add to project-specific table
            >>> lib = sch.add_library("/path/to/project_parts.kicad_sym", scope="project")
        """
        cmd = AddLibrary()
        cmd.file_path = file_path

        if nickname:
            cmd.nickname = nickname

        # Map scope string to proto enum
        scope_map = {
            "global": LibraryTableScope.LTS_GLOBAL,
            "project": LibraryTableScope.LTS_PROJECT,
        }

        if scope.lower() not in scope_map:
            raise ValueError(f"Invalid scope '{scope}'. Valid options: global, project")

        cmd.scope = scope_map[scope.lower()]

        response = self._kicad.send(cmd, AddLibraryResponse)

        # Check status
        if response.status != AddLibraryStatus.ALS_OK:
            status_messages = {
                AddLibraryStatus.ALS_ALREADY_EXISTS: f"Library '{response.nickname}' already exists",
                AddLibraryStatus.ALS_FILE_NOT_FOUND: f"File not found: {file_path}",
                AddLibraryStatus.ALS_INVALID_FORMAT: "Invalid library format",
                AddLibraryStatus.ALS_TABLE_NOT_FOUND: "Library table not found",
            }
            error_msg = status_messages.get(response.status, "Unknown error")
            if response.error_message:
                error_msg = response.error_message
            raise ApiError(f"Failed to add library: {error_msg}")

        # Return a LibraryInfo for the newly added library
        return LibraryInfo(
            nickname=response.nickname,
            uri=file_path,
            type="KiCad",  # Default assumption
            description="",
            is_loaded=False,  # Not yet loaded
            is_read_only=False,
            scope=scope,
        )

    # =========================================================================
    # Sheet Hierarchy Methods
    # =========================================================================

    def get_sheet_hierarchy(self):
        """Get the full sheet hierarchy of the schematic.

        Returns:
            SheetHierarchyNode: The root node of the hierarchy tree

        Example:
            >>> hierarchy = sch.get_sheet_hierarchy()
            >>> print(f"Root: {hierarchy.name}")
            >>> for child in hierarchy.children:
            ...     print(f"  Child: {child.name}")
        """
        cmd = schematic_commands_pb2.GetSheetHierarchy()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetSheetHierarchyResponse)
        return response.root

    def get_current_sheet(self):
        """Get information about the currently displayed sheet.

        Returns:
            Tuple of (SheetPath, Sheet): The current sheet path and sheet info

        Example:
            >>> path, info = sch.get_current_sheet()
            >>> print(f"Current sheet: {path}")
        """
        cmd = schematic_commands_pb2.GetCurrentSheet()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetCurrentSheetResponse)
        return (response.current_sheet, response.sheet_info)

    def navigate_to_sheet(self, sheet_path):
        """Navigate to a specific sheet in the hierarchy.

        Args:
            sheet_path: SheetPath proto message specifying the target sheet

        Example:
            >>> hierarchy = sch.get_sheet_hierarchy()
            >>> if hierarchy.children:
            ...     sch.navigate_to_sheet(hierarchy.children[0].path)
        """
        cmd = schematic_commands_pb2.NavigateToSheet()
        cmd.document.CopyFrom(self._doc)
        cmd.target_sheet.CopyFrom(sheet_path)
        self._kicad.send(cmd, Empty)

    def create_sheet(
        self,
        name: str,
        filename: str,
        position: Vector2,
        size: Vector2,
        create_file: bool = False,
    ):
        """Create a new hierarchical sheet.

        Args:
            name: Display name for the sheet
            filename: Filename for the sheet (e.g., "subsheet.kicad_sch")
            position: Position of the sheet symbol on the parent sheet
            size: Size of the sheet symbol (width, height)
            create_file: If True, also create the .kicad_sch file

        Returns:
            CreateSheetResponse with sheet_id and new_sheet_path

        Example:
            >>> from kipy.geometry import Vector2
            >>> response = sch.create_sheet(
            ...     name="Power Supply",
            ...     filename="power.kicad_sch",
            ...     position=Vector2.from_xy_mm(50, 50),
            ...     size=Vector2.from_xy_mm(30, 20),
            ...     create_file=True
            ... )
            >>> print(f"Created sheet: {response.sheet_id.value}")
        """
        cmd = schematic_commands_pb2.CreateSheet()
        cmd.header.document.CopyFrom(self._doc)
        cmd.name = name
        cmd.filename = filename
        cmd.position.x_nm = position.x
        cmd.position.y_nm = position.y
        cmd.size.x_nm = size.x
        cmd.size.y_nm = size.y
        cmd.create_file = create_file
        return self._kicad.send(cmd, schematic_commands_pb2.CreateSheetResponse)

    def delete_sheet(self, sheet_id: str, delete_file: bool = False):
        """Delete a hierarchical sheet.

        Args:
            sheet_id: KIID of the sheet to delete
            delete_file: If True, also delete the .kicad_sch file

        Example:
            >>> sch.delete_sheet("12345678-abcd-efgh-ijkl-mnopqrstuvwx")
        """
        from kipy.proto.common.types.base_types_pb2 import KIID

        cmd = schematic_commands_pb2.DeleteSheet()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        cmd.delete_file = delete_file
        self._kicad.send(cmd, Empty)

    def get_sheet_properties(self, sheet_id: str):
        """Get properties of a specific sheet.

        Args:
            sheet_id: KIID of the sheet

        Returns:
            GetSheetPropertiesResponse containing sheet info

        Example:
            >>> props = sch.get_sheet_properties("12345678-abcd-...")
            >>> print(f"Sheet name: {props.sheet.name}")
        """
        cmd = schematic_commands_pb2.GetSheetProperties()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetSheetPropertiesResponse)

    def set_sheet_properties(
        self,
        sheet_id: str,
        name: Optional[str] = None,
        filename: Optional[str] = None,
        page_number: Optional[str] = None,
    ):
        """Set properties of a specific sheet.

        Args:
            sheet_id: KIID of the sheet
            name: New display name (optional)
            filename: New filename (optional)
            page_number: New page number (optional)

        Example:
            >>> sch.set_sheet_properties(
            ...     "12345678-abcd-...",
            ...     name="New Name",
            ...     page_number="2"
            ... )
        """
        cmd = schematic_commands_pb2.SetSheetProperties()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        if name is not None:
            cmd.name = name
        if filename is not None:
            cmd.filename = filename
        if page_number is not None:
            cmd.page_number = page_number
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Sheet Pin Methods
    # =========================================================================

    def create_sheet_pin(
        self,
        sheet_id: str,
        name: str,
        position: Vector2,
        shape: int = 0,
        side: int = 0,
    ):
        """Create a pin on a hierarchical sheet.

        Args:
            sheet_id: KIID of the parent sheet
            name: Name of the pin
            position: Position of the pin on the sheet edge
            shape: LabelShape enum value (see schematic_types.proto)
            side: SheetPinSide enum value (0=LEFT, 1=RIGHT, 2=TOP, 3=BOTTOM)

        Returns:
            CreateSheetPinResponse with pin_id

        Example:
            >>> response = sch.create_sheet_pin(
            ...     sheet_id="12345...",
            ...     name="VCC",
            ...     position=Vector2.from_xy_mm(50, 55),
            ...     shape=0,  # Input
            ...     side=0,   # Left
            ... )
        """
        from kipy.proto.schematic import schematic_types_pb2

        cmd = schematic_commands_pb2.CreateSheetPin()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        cmd.name = name
        cmd.position.x_nm = position.x
        cmd.position.y_nm = position.y
        cmd.shape = shape
        cmd.side = side
        return self._kicad.send(cmd, schematic_commands_pb2.CreateSheetPinResponse)

    def delete_sheet_pin(self, pin_id: str):
        """Delete a pin from a hierarchical sheet.

        Args:
            pin_id: KIID of the pin to delete

        Example:
            >>> sch.delete_sheet_pin("12345678-abcd-...")
        """
        cmd = schematic_commands_pb2.DeleteSheetPin()
        cmd.document.CopyFrom(self._doc)
        cmd.pin_id.value = pin_id
        self._kicad.send(cmd, Empty)

    def get_sheet_pins(self, sheet_id: str):
        """Get all pins on a hierarchical sheet.

        Args:
            sheet_id: KIID of the sheet

        Returns:
            GetSheetPinsResponse containing list of pins

        Example:
            >>> response = sch.get_sheet_pins("12345...")
            >>> for pin in response.pins:
            ...     print(f"Pin: {pin.name}")
        """
        cmd = schematic_commands_pb2.GetSheetPins()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetSheetPinsResponse)

    def sync_sheet_pins(self, sheet_id: Optional[str] = None):
        """Synchronize sheet pins with hierarchical labels in the subsheet.

        This adds missing pins for hierarchical labels and removes pins
        that no longer have corresponding labels.

        Args:
            sheet_id: KIID of the sheet to sync (optional, syncs all if not specified)

        Returns:
            SyncSheetPinsResponse with counts of pins added/removed

        Example:
            >>> response = sch.sync_sheet_pins("12345...")
            >>> print(f"Added {response.pins_added}, removed {response.pins_removed}")
        """
        cmd = schematic_commands_pb2.SyncSheetPins()
        cmd.document.CopyFrom(self._doc)
        if sheet_id is not None:
            cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.SyncSheetPinsResponse)

    # =========================================================================
    # Annotation Methods
    # =========================================================================

    def annotate(
        self,
        scope: str = "all",
        order: str = "x_y",
        algorithm: str = "incremental",
        start_number: int = 1,
        reset_existing: bool = False,
        recursive: bool = True,
        repair_timestamps: bool = False,
    ):
        """Annotate symbols with reference designators.

        Args:
            scope: Annotation scope - "all", "current_sheet", or "selection"
            order: Sort order - "x_y" (left to right, top to bottom) or "y_x"
            algorithm: Numbering algorithm - "incremental" (keep existing, fill gaps)
                       or "restart" (restart from start_number)
            start_number: Starting number for annotation (default 1)
            reset_existing: If True, clear existing annotation first
            recursive: If True, include subsheets when scope is current_sheet/selection
            repair_timestamps: If True, repair duplicate timestamps

        Returns:
            AnnotateSymbolsResponse with count of symbols annotated

        Example:
            >>> # Annotate all symbols starting from 1
            >>> response = sch.annotate()
            >>> print(f"Annotated {response.symbols_annotated} symbols")

            >>> # Re-annotate current sheet only, resetting existing
            >>> response = sch.annotate(scope="current_sheet", reset_existing=True)
        """
        cmd = schematic_commands_pb2.AnnotateSymbols()
        cmd.document.CopyFrom(self._doc)

        # Map scope string to enum
        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)

        # Map order string to enum
        order_map = {
            "x_y": schematic_commands_pb2.AO_X_Y,
            "y_x": schematic_commands_pb2.AO_Y_X,
        }
        cmd.order = order_map.get(order.lower(), schematic_commands_pb2.AO_X_Y)

        # Map algorithm string to enum
        algo_map = {
            "incremental": schematic_commands_pb2.AA_INCREMENTAL,
            "restart": schematic_commands_pb2.AA_RESTART,
        }
        cmd.algorithm = algo_map.get(algorithm.lower(), schematic_commands_pb2.AA_INCREMENTAL)

        cmd.start_number = start_number
        cmd.reset_existing = reset_existing
        cmd.recursive = recursive
        cmd.repair_timestamps = repair_timestamps

        return self._kicad.send(cmd, schematic_commands_pb2.AnnotateSymbolsResponse)

    def clear_annotation(self, scope: str = "all", recursive: bool = True):
        """Clear annotation from symbols.

        Args:
            scope: Scope - "all", "current_sheet", or "selection"
            recursive: If True, include subsheets when scope is current_sheet/selection

        Returns:
            ClearAnnotationResponse with count of symbols cleared

        Example:
            >>> response = sch.clear_annotation()
            >>> print(f"Cleared {response.symbols_cleared} symbols")
        """
        cmd = schematic_commands_pb2.ClearAnnotation()
        cmd.document.CopyFrom(self._doc)

        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)
        cmd.recursive = recursive

        return self._kicad.send(cmd, schematic_commands_pb2.ClearAnnotationResponse)

    def check_annotation(self, scope: str = "all", recursive: bool = True):
        """Check for annotation errors (duplicates, missing, etc.).

        Args:
            scope: Scope - "all", "current_sheet", or "selection"
            recursive: If True, include subsheets when scope is current_sheet/selection

        Returns:
            CheckAnnotationResponse with error count and detailed errors

        Example:
            >>> response = sch.check_annotation()
            >>> if response.error_count > 0:
            ...     for error in response.errors:
            ...         print(f"{error.error_type}: {error.message}")
        """
        cmd = schematic_commands_pb2.CheckAnnotation()
        cmd.document.CopyFrom(self._doc)

        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)
        cmd.recursive = recursive

        return self._kicad.send(cmd, schematic_commands_pb2.CheckAnnotationResponse)

    # =========================================================================
    # ERC (Electrical Rules Check) Methods
    # =========================================================================

    def run_erc(self, run_all_tests: bool = True):
        """Run ERC (Electrical Rules Check) on the schematic.

        Args:
            run_all_tests: If True, run all tests. If False, only run enabled tests.

        Returns:
            RunERCResponse with error/warning counts and list of violations

        Example:
            >>> response = sch.run_erc()
            >>> print(f"Errors: {response.error_count}, Warnings: {response.warning_count}")
            >>> for v in response.violations:
            ...     print(f"{v.error_code}: {v.description}")
        """
        cmd = schematic_commands_pb2.RunERC()
        cmd.document.CopyFrom(self._doc)
        cmd.run_all_tests = run_all_tests
        return self._kicad.send(cmd, schematic_commands_pb2.RunERCResponse)

    def get_erc_violations(self, severity: Optional[str] = None):
        """Get current ERC violations without re-running checks.

        Args:
            severity: Optional filter - "error", "warning", or "excluded"

        Returns:
            GetERCViolationsResponse with list of violations

        Example:
            >>> # Get all violations
            >>> response = sch.get_erc_violations()

            >>> # Get only errors
            >>> response = sch.get_erc_violations(severity="error")
        """
        cmd = schematic_commands_pb2.GetERCViolations()
        cmd.document.CopyFrom(self._doc)

        if severity:
            severity_map = {
                "error": schematic_commands_pb2.ERC_SEV_ERROR,
                "warning": schematic_commands_pb2.ERC_SEV_WARNING,
                "excluded": schematic_commands_pb2.ERC_SEV_EXCLUDED,
            }
            if severity.lower() in severity_map:
                cmd.filter_severity = severity_map[severity.lower()]

        return self._kicad.send(cmd, schematic_commands_pb2.GetERCViolationsResponse)

    def clear_erc_markers(self, marker_ids: Optional[List[str]] = None):
        """Clear ERC markers from the schematic.

        Args:
            marker_ids: Optional list of marker IDs to clear. If None, clears all.

        Returns:
            ClearERCMarkersResponse with count of markers cleared

        Example:
            >>> # Clear all markers
            >>> response = sch.clear_erc_markers()

            >>> # Clear specific markers
            >>> response = sch.clear_erc_markers(["marker-id-1", "marker-id-2"])
        """
        cmd = schematic_commands_pb2.ClearERCMarkers()
        cmd.document.CopyFrom(self._doc)

        if marker_ids:
            for mid in marker_ids:
                cmd.marker_ids.add().value = mid

        return self._kicad.send(cmd, schematic_commands_pb2.ClearERCMarkersResponse)

    def exclude_erc_violation(self, marker_id: str):
        """Exclude (ignore) a specific ERC violation.

        Args:
            marker_id: KIID of the ERC marker to exclude

        Example:
            >>> sch.exclude_erc_violation("12345678-abcd-...")
        """
        cmd = schematic_commands_pb2.ExcludeERCViolation()
        cmd.document.CopyFrom(self._doc)
        cmd.marker_id.value = marker_id
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Connectivity Methods (IPC API)
    # =========================================================================

    def get_nets(self):
        """Get all electrical nets in the schematic.

        Returns:
            A list of NetInfo objects containing:
            - name: Net name
            - net_code: Unique net code
            - connection_count: Number of connection points
            - item_ids: IDs of items on this net

        Example:
            >>> nets = sch.get_nets()
            >>> for net in nets.nets:
            ...     print(f"Net: {net.name}, items: {net.connection_count}")
        """
        cmd = schematic_commands_pb2.GetNets()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetsResponse)

    def get_buses(self):
        """Get all buses in the schematic.

        Returns:
            A list of BusInfo objects containing:
            - name: Bus name
            - is_vector: True if vector bus (e.g., D[0..7]), False if bus group
            - vector_prefix: For vector buses, the prefix (e.g., "D")
            - vector_start/vector_end: For vector buses, the range
            - members: Member net names
            - bus_line_ids: IDs of bus line segments

        Example:
            >>> buses = sch.get_buses()
            >>> for bus in buses.buses:
            ...     print(f"Bus: {bus.name}, members: {list(bus.members)}")
        """
        cmd = schematic_commands_pb2.GetBuses()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(cmd, schematic_commands_pb2.GetBusesResponse)

    def get_net_for_item(self, item_id: str):
        """Get connectivity information for a specific item.

        Args:
            item_id: KIID of the item to query

        Returns:
            GetNetForItemResponse containing:
            - is_connected: True if the item is connected to a net
            - connection: ConnectionInfo with net/bus details

        Example:
            >>> result = sch.get_net_for_item(wire.id)
            >>> if result.is_connected:
            ...     print(f"Connected to: {result.connection.name}")
        """
        cmd = schematic_commands_pb2.GetNetForItem()
        cmd.document.CopyFrom(self._doc)
        cmd.item_id.value = item_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetForItemResponse)

    def get_bus_members(self, bus_name: str):
        """Get member nets of a bus.

        Args:
            bus_name: Name of the bus (e.g., "D[0..7]" or "{SDA, SCL}")

        Returns:
            GetBusMembersResponse containing:
            - members: List of member net names
            - is_vector: True if vector bus
            - vector_prefix/start/end: For vector buses

        Example:
            >>> result = sch.get_bus_members("D[0..7]")
            >>> print(f"Members: {list(result.members)}")
            # Output: ['D0', 'D1', 'D2', ..., 'D7']
        """
        cmd = schematic_commands_pb2.GetBusMembers()
        cmd.document.CopyFrom(self._doc)
        cmd.bus_name = bus_name
        return self._kicad.send(cmd, schematic_commands_pb2.GetBusMembersResponse)

    def get_net_items(self, net_name: str):
        """Get all items connected to a specific net.

        Args:
            net_name: Name of the net to query

        Returns:
            GetNetItemsResponse containing:
            - item_ids: IDs of all items on this net
            - connection_points: Positions where items connect

        Example:
            >>> result = sch.get_net_items("VCC")
            >>> print(f"Items on VCC: {len(result.item_ids)}")
        """
        cmd = schematic_commands_pb2.GetNetItems()
        cmd.document.CopyFrom(self._doc)
        cmd.net_name = net_name
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetItemsResponse)

    # =========================================================================
    # Bus Entry Methods
    # =========================================================================

    def add_bus_wire_entry(
        self,
        position: Vector2,
        size: Optional[Vector2] = None,
    ):
        """Add a wire-to-bus entry to the schematic.

        A bus wire entry connects a wire to a bus line.

        Args:
            position: Position of the bus entry (start point)
            size: Size/offset to end position. Default is (2.54mm, 2.54mm).

        Returns:
            The created bus entry item

        Example:
            >>> from kipy.geometry import Vector2
            >>> entry = sch.add_bus_wire_entry(Vector2.from_mm(50, 50))
        """
        from kipy.schematic_types import BusEntry

        entry = BusEntry.create_wire_to_bus(position, size)
        created = self.create_items(entry)
        return created[0] if created else None

    def add_bus_bus_entry(
        self,
        position: Vector2,
        size: Optional[Vector2] = None,
    ):
        """Add a bus-to-bus entry to the schematic.

        A bus-bus entry connects two bus lines together.

        Args:
            position: Position of the bus entry (start point)
            size: Size/offset to end position. Default is (2.54mm, 2.54mm).

        Returns:
            The created bus entry item

        Example:
            >>> from kipy.geometry import Vector2
            >>> entry = sch.add_bus_bus_entry(Vector2.from_mm(50, 50))
        """
        from kipy.schematic_types import BusEntry

        entry = BusEntry.create_bus_to_bus(position, size)
        created = self.create_items(entry)
        return created[0] if created else None

    def add_bus(
        self,
        start: Vector2,
        end: Vector2,
    ):
        """Add a bus line to the schematic.

        A bus line is like a wire but on the bus layer, used for
        carrying multiple signals (e.g., D[0..7]).

        Args:
            start: Start point of the bus
            end: End point of the bus

        Returns:
            The created bus line item

        Example:
            >>> from kipy.geometry import Vector2
            >>> bus = sch.add_bus(
            ...     Vector2.from_mm(50, 50),
            ...     Vector2.from_mm(50, 100)
            ... )
        """
        from kipy.schematic_types import Wire

        # Layer 2 is LAYER_BUS in KiCAD schematic
        LAYER_BUS = 2
        bus_line = Wire.create(start, end, layer=LAYER_BUS)
        created = self.create_items(bus_line)
        return created[0] if created else None

    # =========================================================================
    # Title Block Methods
    # =========================================================================

    def get_title_block(self) -> TitleBlockInfo:
        """Get the title block information for the current schematic sheet.

        Returns:
            TitleBlockInfo containing:
            - title: Document title
            - date: Document date
            - revision: Revision string
            - company: Company name
            - comments: Dictionary of comments (1-9)

        Example:
            >>> title_block = sch.get_title_block()
            >>> print(f"Title: {title_block.title}")
            >>> print(f"Revision: {title_block.revision}")
            >>> print(f"Comments: {title_block.comments}")
        """
        cmd = editor_commands_pb2.GetTitleBlockInfo()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, base_types_pb2.TitleBlockInfo)
        return TitleBlockInfo(response)

    def set_title_block(
        self,
        title: Optional[str] = None,
        date: Optional[str] = None,
        revision: Optional[str] = None,
        company: Optional[str] = None,
        comments: Optional[dict] = None,
    ) -> None:
        """Set the title block information for the current schematic sheet.

        Only the fields that are provided will be updated. Fields set to None
        or not provided will retain their current values.

        Args:
            title: Document title
            date: Document date string
            revision: Revision string
            company: Company name
            comments: Dictionary mapping comment number (1-9) to comment text

        Example:
            >>> sch.set_title_block(
            ...     title="My Schematic",
            ...     revision="1.0",
            ...     company="ACME Inc",
            ...     comments={1: "Author: John Doe", 2: "Status: Draft"}
            ... )
        """
        cmd = editor_commands_pb2.SetTitleBlockInfo()
        cmd.document.CopyFrom(self._doc)

        if title is not None:
            cmd.title_block.title = title
        if date is not None:
            cmd.title_block.date = date
        if revision is not None:
            cmd.title_block.revision = revision
        if company is not None:
            cmd.title_block.company = company
        if comments is not None:
            if 1 in comments:
                cmd.title_block.comment1 = comments[1]
            if 2 in comments:
                cmd.title_block.comment2 = comments[2]
            if 3 in comments:
                cmd.title_block.comment3 = comments[3]
            if 4 in comments:
                cmd.title_block.comment4 = comments[4]
            if 5 in comments:
                cmd.title_block.comment5 = comments[5]
            if 6 in comments:
                cmd.title_block.comment6 = comments[6]
            if 7 in comments:
                cmd.title_block.comment7 = comments[7]
            if 8 in comments:
                cmd.title_block.comment8 = comments[8]
            if 9 in comments:
                cmd.title_block.comment9 = comments[9]

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Page Settings Methods
    # =========================================================================

    def get_page_settings(self) -> PageInfo:
        """Get the page/paper settings for the current schematic sheet.

        Returns:
            PageInfo containing:
            - size_type: Paper size type (use PageSizeType constants)
            - size_type_name: Human-readable name (e.g., "A4", "US Letter")
            - portrait: True for portrait, False for landscape
            - width_mm: Page width in millimeters
            - height_mm: Page height in millimeters

        Example:
            >>> page = sch.get_page_settings()
            >>> print(f"Size: {page.size_type_name}")
            >>> print(f"Dimensions: {page.width_mm}x{page.height_mm}mm")
            >>> print(f"Orientation: {'portrait' if page.portrait else 'landscape'}")
        """
        cmd = editor_commands_pb2.GetPageSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, base_types_pb2.PageInfo)
        return PageInfo(response)

    def set_page_settings(
        self,
        size_type: Optional[int] = None,
        portrait: Optional[bool] = None,
        width_mm: Optional[float] = None,
        height_mm: Optional[float] = None,
    ) -> None:
        """Set the page/paper settings for the current schematic sheet.

        For standard paper sizes, just specify size_type and optionally portrait.
        For custom sizes, use size_type=PageSizeType.USER and specify width_mm/height_mm.

        Args:
            size_type: Paper size (use PageSizeType constants, e.g., PageSizeType.A4)
            portrait: True for portrait orientation, False for landscape
            width_mm: Page width in mm (only used when size_type is PageSizeType.USER)
            height_mm: Page height in mm (only used when size_type is PageSizeType.USER)

        Example:
            >>> from kipy.common_types import PageSizeType
            >>> # Set to A4 landscape
            >>> sch.set_page_settings(size_type=PageSizeType.A4, portrait=False)
            >>>
            >>> # Set to US Letter portrait
            >>> sch.set_page_settings(size_type=PageSizeType.US_LETTER, portrait=True)
            >>>
            >>> # Set custom size
            >>> sch.set_page_settings(
            ...     size_type=PageSizeType.USER,
            ...     width_mm=300,
            ...     height_mm=200
            ... )
        """
        cmd = editor_commands_pb2.SetPageSettings()
        cmd.document.CopyFrom(self._doc)

        if size_type is not None:
            cmd.page_info.size_type = size_type
        if portrait is not None:
            cmd.page_info.portrait = portrait
        if width_mm is not None:
            cmd.page_info.width_mm = width_mm
        if height_mm is not None:
            cmd.page_info.height_mm = height_mm

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Bitmap Methods
    # =========================================================================

    def get_bitmaps(self) -> Sequence[Wrapper]:
        """Get all bitmap/image items in the current schematic sheet.

        Returns:
            List of Bitmap items

        Example:
            >>> bitmaps = sch.get_bitmaps()
            >>> for bmp in bitmaps:
            ...     print(f"Bitmap at {bmp.position}, scale={bmp.scale}")
        """
        return self.get_items(KiCadObjectType.KOT_SCH_BITMAP)

    def add_bitmap(
        self,
        image_path: str,
        position: Vector2,
        scale: float = 1.0,
    ) -> Optional[Wrapper]:
        """Add a bitmap/image to the schematic.

        Args:
            image_path: Path to the image file (PNG recommended)
            position: Position for the image center
            scale: Scale factor (1.0 = original size)

        Returns:
            The created Bitmap item, or None if creation failed

        Example:
            >>> from kipy.geometry import Vector2
            >>> bitmap = sch.add_bitmap(
            ...     "/path/to/logo.png",
            ...     Vector2.from_mm(100, 50),
            ...     scale=0.5
            ... )
        """
        from kipy.schematic_types import Bitmap as BitmapType

        if BitmapType is None:
            raise NotImplementedError("Bitmap support requires proto regeneration")

        bitmap = BitmapType.create(position, image_path, scale)
        created = self.create_items(bitmap)
        return created[0] if created else None

    # =========================================================================
    # Table Methods
    # =========================================================================

    def get_tables(self) -> Sequence[Wrapper]:
        """Get all table items in the current schematic sheet.

        Returns:
            List of Table items

        Example:
            >>> tables = sch.get_tables()
            >>> for table in tables:
            ...     print(f"Table at {table.position}: {table.columns}x{table.rows}")
        """
        return self.get_items(KiCadObjectType.KOT_SCH_TABLE)

    def add_table(
        self,
        position: Vector2,
        columns: int,
        rows: int,
        col_width: int = 0,
        row_height: int = 0,
        header_on: bool = False,
    ) -> Optional[Wrapper]:
        """Add a table to the schematic.

        Args:
            position: Position for the table
            columns: Number of columns
            rows: Number of rows
            col_width: Default column width in internal units (0 for default)
            row_height: Default row height in internal units (0 for default)
            header_on: Whether first row is a header

        Returns:
            The created Table item, or None if creation failed

        Example:
            >>> from kipy.geometry import Vector2
            >>> table = sch.add_table(
            ...     Vector2.from_mm(100, 100),
            ...     columns=3,
            ...     rows=5,
            ...     header_on=True
            ... )
        """
        from kipy.schematic_types import Table as TableType

        if TableType is None:
            raise NotImplementedError("Table support requires proto regeneration")

        table = TableType.create(position, columns, rows, col_width, row_height, header_on)
        created = self.create_items(table)
        return created[0] if created else None

    # =========================================================================
    # Group Methods
    # =========================================================================

    def get_groups(self) -> Sequence[Wrapper]:
        """Get all group items in the current schematic sheet.

        Returns:
            List of SchematicGroup items

        Example:
            >>> groups = sch.get_groups()
            >>> for group in groups:
            ...     print(f"Group '{group.name}' with {len(group.members)} members")
        """
        return self.get_items(KiCadObjectType.KOT_SCH_GROUP)

    def create_group(
        self,
        name: str,
        items: Sequence[Wrapper],
    ) -> Optional[Wrapper]:
        """Create a group from the given schematic items.

        Args:
            name: Name for the group
            items: Items to include in the group

        Returns:
            The created SchematicGroup item, or None if creation failed

        Example:
            >>> # Select some items first
            >>> symbols = sch.get_symbols()[:5]
            >>> group = sch.create_group("Power Section", symbols)
        """
        from kipy.schematic_types import SchematicGroup

        if SchematicGroup is None:
            raise NotImplementedError("Group support requires proto regeneration")

        member_ids = [item.id.value for item in items if hasattr(item, 'id')]
        group = SchematicGroup.create(name, member_ids)
        created = self.create_items(group)
        return created[0] if created else None

    # =========================================================================
    # Phase 1: Grid and Coordinate Helper Methods
    # =========================================================================

    def get_grid_settings(self) -> dict:
        """Get the current grid settings.

        Returns a dictionary with grid information in multiple units:
        - size_mm: Grid size in millimeters (default: 1.27mm = 50 mils)
        - size_mils: Grid size in mils (default: 50)
        - size_nm: Grid size in nanometers (default: 1270000)

        Example:
            >>> grid = sch.get_grid_settings()
            >>> print(f"Grid: {grid['size_mils']} mils = {grid['size_mm']}mm")
            Grid: 50 mils = 1.27mm
        """
        # KiCad default grid is 50 mils = 1.27mm
        # Future: could query this from KiCad if API supports it
        default_grid_mils = 50
        default_grid_mm = default_grid_mils * 0.0254  # 1 mil = 0.0254mm
        default_grid_nm = int(default_grid_mm * 1_000_000)

        return {
            "size_mm": default_grid_mm,
            "size_mils": default_grid_mils,
            "size_nm": default_grid_nm,
        }

    def snap_to_grid(
        self,
        x_mm: float,
        y_mm: float,
        grid_mm: Optional[float] = None
    ) -> Tuple[float, float]:
        """Snap coordinates to the nearest grid point.

        Args:
            x_mm: X coordinate in millimeters
            y_mm: Y coordinate in millimeters
            grid_mm: Optional grid size in mm. If not provided, uses default (1.27mm)

        Returns:
            Tuple of (x_mm, y_mm) snapped to grid

        Example:
            >>> # Snap arbitrary position to grid
            >>> x, y = sch.snap_to_grid(100.5, 75.3)
            >>> print(f"Snapped: ({x}, {y})")
            Snapped: (100.33, 75.565)

            >>> # Place symbol at snapped position
            >>> pos = Vector2.from_xy_mm(*sch.snap_to_grid(100.5, 75.3))
            >>> symbol = sch.add_symbol("Device:R", pos)
        """
        if grid_mm is None:
            grid_mm = self.get_grid_settings()["size_mm"]

        snapped_x = round(x_mm / grid_mm) * grid_mm
        snapped_y = round(y_mm / grid_mm) * grid_mm

        return (snapped_x, snapped_y)

    def get_usable_area(self) -> dict:
        """Get the usable drawing area for the current page.

        Returns a dictionary with bounds that account for the title block
        and standard margins. Components should be placed within this area.

        Returns:
            Dictionary with:
            - min_x_mm: Left boundary (default: 20mm)
            - max_x_mm: Right boundary (page_width - 37mm for title block)
            - min_y_mm: Top boundary (default: 20mm)
            - max_y_mm: Bottom boundary (page_height - 30mm for title block)
            - width_mm: Usable width
            - height_mm: Usable height
            - center_x_mm: Center X coordinate
            - center_y_mm: Center Y coordinate

        Example:
            >>> area = sch.get_usable_area()
            >>> print(f"Usable area: {area['width_mm']}x{area['height_mm']}mm")
            >>> print(f"Center at: ({area['center_x_mm']}, {area['center_y_mm']})")

            >>> # Place component in center
            >>> pos = Vector2.from_xy_mm(area['center_x_mm'], area['center_y_mm'])
        """
        page = self.get_page_settings()

        # Standard margins for title block
        left_margin = 20.0
        top_margin = 20.0
        right_margin = 37.0  # Title block on right side
        bottom_margin = 30.0  # Title block at bottom

        min_x = left_margin
        max_x = page.width_mm - right_margin
        min_y = top_margin
        max_y = page.height_mm - bottom_margin

        width = max_x - min_x
        height = max_y - min_y

        return {
            "min_x_mm": min_x,
            "max_x_mm": max_x,
            "min_y_mm": min_y,
            "max_y_mm": max_y,
            "width_mm": width,
            "height_mm": height,
            "center_x_mm": min_x + width / 2,
            "center_y_mm": min_y + height / 2,
        }

    def get_usable_area_mils(self) -> dict:
        """Get the usable drawing area in mils (1 mil = 0.0254mm).

        This is the preferred method for schematics using mils as the unit.
        Returns bounds that account for the title block and standard margins.

        Returns:
            Dictionary with all values in mils:
            - min_x: Left boundary in mils
            - max_x: Right boundary in mils
            - min_y: Top boundary in mils
            - max_y: Bottom boundary in mils
            - width: Usable width in mils
            - height: Usable height in mils
            - center_x: Center X coordinate in mils
            - center_y: Center Y coordinate in mils
            - grid: Default grid size (50 mils)

        Example:
            >>> area = sch.get_usable_area_mils()
            >>> print(f"Page: {area['min_x']}-{area['max_x']} x {area['min_y']}-{area['max_y']} mils")
            >>> print(f"Center: ({area['center_x']}, {area['center_y']})")
            >>> # Place at center
            >>> pos = Vector2.from_xy_mils(area['center_x'], area['center_y'])
        """
        # Get mm-based area and convert to mils
        area_mm = self.get_usable_area()
        MM_TO_MIL = 1.0 / 0.0254  # ~39.37 mils per mm

        return {
            "min_x": area_mm["min_x_mm"] * MM_TO_MIL,
            "max_x": area_mm["max_x_mm"] * MM_TO_MIL,
            "min_y": area_mm["min_y_mm"] * MM_TO_MIL,
            "max_y": area_mm["max_y_mm"] * MM_TO_MIL,
            "width": area_mm["width_mm"] * MM_TO_MIL,
            "height": area_mm["height_mm"] * MM_TO_MIL,
            "center_x": area_mm["center_x_mm"] * MM_TO_MIL,
            "center_y": area_mm["center_y_mm"] * MM_TO_MIL,
            "grid": 50,  # Default grid in mils
        }

    def snap_to_grid_mils(
        self,
        x_mils: float,
        y_mils: float,
        grid_mils: float = 50
    ) -> Tuple[float, float]:
        """Snap coordinates to the nearest grid point in mils.

        Args:
            x_mils: X coordinate in mils
            y_mils: Y coordinate in mils
            grid_mils: Grid size in mils (default 50)

        Returns:
            Tuple of (x_mils, y_mils) snapped to grid

        Example:
            >>> x, y = sch.snap_to_grid_mils(5100, 4050)
            >>> print(f"Snapped: ({x}, {y})")  # (5100, 4050)
            >>> pos = Vector2.from_xy_mils(x, y)
        """
        snapped_x = round(x_mils / grid_mils) * grid_mils
        snapped_y = round(y_mils / grid_mils) * grid_mils
        return (snapped_x, snapped_y)

    # =========================================================================
    # Phase 2: Pin-Based Wiring Methods
    # =========================================================================

    def get_pin_position(
        self,
        symbol: "Wrapper",
        pin_id: str
    ) -> Optional[Vector2]:
        """Get the exact position of a pin by name or number.

        This is the recommended way to get pin positions for wiring.
        The returned position is exact and should be used directly
        without modification.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name (e.g., "VCC", "GND") or pin number (e.g., "1", "2")

        Returns:
            Vector2 position of the pin, or None if pin not found

        Example:
            >>> resistor = sch.add_symbol("Device:R", Vector2.from_xy_mm(100, 80))
            >>> pin1_pos = sch.get_pin_position(resistor, "1")
            >>> pin2_pos = sch.get_pin_position(resistor, "2")
            >>> if pin1_pos and pin2_pos:
            ...     print(f"Pin 1 at: ({pin1_pos.x/1e6:.2f}, {pin1_pos.y/1e6:.2f})mm")
        """
        if not hasattr(symbol, 'pins'):
            return None

        for pin in symbol.pins:
            if pin.name == pin_id or pin.number == pin_id:
                return pin.position

        return None

    def wire_pins(
        self,
        symbol1: "Wrapper",
        pin_id1: str,
        symbol2: "Wrapper",
        pin_id2: str
    ) -> Optional["Wrapper"]:
        """Create a wire directly between two pins.

        This is the RECOMMENDED way to wire components together.
        It uses exact pin positions to ensure proper connections.

        Args:
            symbol1: First symbol
            pin_id1: Pin name or number on first symbol
            symbol2: Second symbol
            pin_id2: Pin name or number on second symbol

        Returns:
            The created Wire, or None if either pin was not found

        Example:
            >>> r1 = sch.add_symbol("Device:R", Vector2.from_xy_mm(100, 80))
            >>> r2 = sch.add_symbol("Device:R", Vector2.from_xy_mm(100, 110))
            >>> # Wire R1 pin 2 to R2 pin 1
            >>> wire = sch.wire_pins(r1, "2", r2, "1")
        """
        pos1 = self.get_pin_position(symbol1, pin_id1)
        pos2 = self.get_pin_position(symbol2, pin_id2)

        if pos1 is None or pos2 is None:
            return None

        return self.add_wire(pos1, pos2)

    def wire_from_pin(
        self,
        symbol: "Wrapper",
        pin_id: str,
        end_point_mm: Tuple[float, float]
    ) -> Optional["Wrapper"]:
        """Create a wire from a pin to a coordinate.

        Use this when wiring from a component pin to a junction point
        or other intermediate location.

        Args:
            symbol: The symbol containing the starting pin
            pin_id: Pin name or number
            end_point_mm: Tuple of (x_mm, y_mm) for the wire end

        Returns:
            The created Wire, or None if pin was not found

        Example:
            >>> cap = sch.add_symbol("Device:C", Vector2.from_xy_mm(120, 80))
            >>> # Wire from capacitor pin 1 to a junction point
            >>> wire = sch.wire_from_pin(cap, "1", (100, 80))
        """
        pos = self.get_pin_position(symbol, pin_id)
        if pos is None:
            return None

        end = Vector2.from_xy_mm(end_point_mm[0], end_point_mm[1])
        return self.add_wire(pos, end)

    def wire_to_pin(
        self,
        start_point_mm: Tuple[float, float],
        symbol: "Wrapper",
        pin_id: str
    ) -> Optional["Wrapper"]:
        """Create a wire from a coordinate to a pin.

        Use this when wiring from a junction point or other location
        to a component pin.

        Args:
            start_point_mm: Tuple of (x_mm, y_mm) for the wire start
            symbol: The symbol containing the ending pin
            pin_id: Pin name or number

        Returns:
            The created Wire, or None if pin was not found

        Example:
            >>> cap = sch.add_symbol("Device:C", Vector2.from_xy_mm(120, 80))
            >>> # Wire from a junction point to capacitor pin 2
            >>> wire = sch.wire_to_pin((100, 100), cap, "2")
        """
        pos = self.get_pin_position(symbol, pin_id)
        if pos is None:
            return None

        start = Vector2.from_xy_mm(start_point_mm[0], start_point_mm[1])
        return self.add_wire(start, pos)

    def wire_path(
        self,
        start_pin: Tuple["Wrapper", str],
        waypoints: List[Tuple[float, float]],
        end_pin: Tuple["Wrapper", str]
    ) -> List["Wrapper"]:
        """Create a wire path from one pin through waypoints to another pin.

        This creates multiple connected wire segments from the start pin,
        through each waypoint, to the end pin. This is useful for routing
        wires around obstacles or creating L-shaped or complex paths.

        Args:
            start_pin: Tuple of (symbol, pin_id) for the starting pin
            waypoints: List of (x_mm, y_mm) tuples for intermediate points
            end_pin: Tuple of (symbol, pin_id) for the ending pin

        Returns:
            List of created Wire objects (empty list if pins not found)

        Example:
            >>> r1 = sch.add_symbol("Device:R", Vector2.from_xy_mm(80, 80))
            >>> r2 = sch.add_symbol("Device:R", Vector2.from_xy_mm(120, 100))
            >>> # Wire R1-pin2 -> right -> down -> R2-pin1
            >>> wires = sch.wire_path(
            ...     (r1, "2"),
            ...     [(100, 80), (100, 100)],  # waypoints
            ...     (r2, "1")
            ... )
            >>> print(f"Created {len(wires)} wire segments")
        """
        start_symbol, start_pin_id = start_pin
        end_symbol, end_pin_id = end_pin

        start_pos = self.get_pin_position(start_symbol, start_pin_id)
        end_pos = self.get_pin_position(end_symbol, end_pin_id)

        if start_pos is None or end_pos is None:
            return []

        # Build list of all points: start pin -> waypoints -> end pin
        points = [start_pos]
        for wp in waypoints:
            points.append(Vector2.from_xy_mm(wp[0], wp[1]))
        points.append(end_pos)

        # Create wire segments between consecutive points
        wires = []
        for i in range(len(points) - 1):
            wire = self.add_wire(points[i], points[i + 1])
            wires.append(wire)

        return wires

    def get_unconnected_pins(self) -> List[dict]:
        """Get a list of all unconnected pins in the schematic.

        This is useful for validation after wiring to ensure all
        connections were made properly.

        Returns:
            List of dictionaries with unconnected pin information:
            - net_name: The "unconnected-..." net name
            - symbol_ref: Reference designator (if available)
            - pin_number: Pin number/pad

        Example:
            >>> # After wiring, check for unconnected pins
            >>> unconnected = sch.get_unconnected_pins()
            >>> if unconnected:
            ...     print("WARNING: Unconnected pins found!")
            ...     for pin in unconnected:
            ...         print(f"  {pin['net_name']}")
            ... else:
            ...     print("All pins connected!")
        """
        nets = self.get_nets()
        unconnected = []

        for net in nets.nets:
            if "unconnected" in net.name.lower():
                # Parse the net name: "unconnected-(R1-Pad1)"
                info = {"net_name": net.name}

                # Try to extract symbol ref and pin from name
                import re
                match = re.search(r'\(([^)]+)-Pad(\d+)\)', net.name)
                if match:
                    info["symbol_ref"] = match.group(1)
                    info["pin_number"] = match.group(2)
                else:
                    info["symbol_ref"] = None
                    info["pin_number"] = None

                unconnected.append(info)

        return unconnected

    # =========================================================================
    # Symbol Search Methods
    # =========================================================================

    def get_symbol_by_ref(self, reference: str) -> Optional[Symbol]:
        """Find a symbol by its reference designator.

        Args:
            reference: Reference designator (e.g., "R1", "C2", "U3")

        Returns:
            Symbol object if found, None otherwise

        Example:
            >>> r1 = sch.get_symbol_by_ref("R1")
            >>> if r1:
            ...     print(f"R1 is at {r1.position.x_mm}, {r1.position.y_mm}")
            ...     print(f"R1 value: {r1.value}")
        """
        for symbol in self.get_symbols():
            if symbol.reference == reference:
                return symbol
        return None

    def get_symbols_by_value(self, value: str) -> List[Symbol]:
        """Find all symbols with a specific value.

        Args:
            value: Component value to search for (e.g., "10k", "100nF")

        Returns:
            List of matching Symbol objects

        Example:
            >>> resistors_10k = sch.get_symbols_by_value("10k")
            >>> print(f"Found {len(resistors_10k)} 10k resistors")
        """
        return [s for s in self.get_symbols() if s.value == value]

    def get_symbols_by_lib(self, lib_name: str) -> List[Symbol]:
        """Find all symbols from a specific library.

        Args:
            lib_name: Library name to search for (e.g., "Device", "Transistor_FET")

        Returns:
            List of matching Symbol objects

        Example:
            >>> fets = sch.get_symbols_by_lib("Transistor_FET")
            >>> print(f"Found {len(fets)} FET transistors")
        """
        return [s for s in self.get_symbols() if s.lib_id.library == lib_name]

    def find_symbols(
        self,
        reference: Optional[str] = None,
        value: Optional[str] = None,
        lib_name: Optional[str] = None,
        symbol_name: Optional[str] = None,
    ) -> List[Symbol]:
        """Find symbols matching multiple criteria.

        All provided criteria must match (AND logic). Partial matches
        are supported for all string fields.

        Args:
            reference: Reference designator pattern (e.g., "R" matches R1, R2, etc.)
            value: Value pattern
            lib_name: Library name pattern
            symbol_name: Symbol name pattern

        Returns:
            List of matching Symbol objects

        Example:
            >>> # Find all resistors (reference starts with R)
            >>> resistors = sch.find_symbols(reference="R")
            >>>
            >>> # Find all capacitors with value containing "100"
            >>> caps = sch.find_symbols(reference="C", value="100")
        """
        results = self.get_symbols()

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
    # Position Lookup Methods
    # =========================================================================

    def find_items_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,  # 0.1mm default tolerance
    ) -> List[Wrapper]:
        """Find all items near a given position.

        This searches through symbols, wires, junctions, labels, etc.
        to find items at or near the specified position.

        Args:
            position: Position to search at
            tolerance_nm: Search tolerance in nanometers (default 0.1mm = 100000nm)

        Returns:
            List of items found at the position

        Example:
            >>> pos = Vector2.from_xy_mm(100, 80)
            >>> items = sch.find_items_at_position(pos)
            >>> for item in items:
            ...     print(f"Found: {type(item).__name__}")
        """
        found = []
        target_x = position.x
        target_y = position.y

        def is_near(pos: Vector2) -> bool:
            return (abs(pos.x - target_x) <= tolerance_nm and
                    abs(pos.y - target_y) <= tolerance_nm)

        # Check symbols
        for symbol in self.get_symbols():
            if is_near(symbol.position):
                found.append(symbol)

        # Check wires
        for wire in self.get_wires():
            if is_near(wire.start) or is_near(wire.end):
                found.append(wire)

        # Check junctions
        for junction in self.get_junctions():
            if hasattr(junction, 'position') and is_near(junction.position):
                found.append(junction)

        # Check labels
        for label in self.get_labels():
            if hasattr(label, 'position') and is_near(label.position):
                found.append(label)

        # Check no-connects
        for nc in self.get_no_connects():
            if hasattr(nc, 'position') and is_near(nc.position):
                found.append(nc)

        return found

    def find_pin_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,
    ) -> Optional[Tuple[Symbol, Pin]]:
        """Find a pin at a given position.

        Args:
            position: Position to search at
            tolerance_nm: Search tolerance in nanometers (default 0.1mm)

        Returns:
            Tuple of (Symbol, Pin) if found, None otherwise

        Example:
            >>> pos = Vector2.from_xy_mm(100, 80)
            >>> result = sch.find_pin_at_position(pos)
            >>> if result:
            ...     symbol, pin = result
            ...     print(f"Found {symbol.reference} pin {pin.name}")
        """
        target_x = position.x
        target_y = position.y

        for symbol in self.get_symbols():
            for pin in symbol.pins:
                if (abs(pin.position.x - target_x) <= tolerance_nm and
                    abs(pin.position.y - target_y) <= tolerance_nm):
                    return (symbol, pin)

        return None

    def find_wire_at_position(
        self,
        position: Vector2,
        tolerance_nm: int = 100000,
    ) -> Optional[Wire]:
        """Find a wire that passes through or near a position.

        This checks both wire endpoints and if the position lies
        on the wire segment.

        Args:
            position: Position to search at
            tolerance_nm: Search tolerance in nanometers

        Returns:
            Wire object if found, None otherwise

        Example:
            >>> pos = Vector2.from_xy_mm(100, 80)
            >>> wire = sch.find_wire_at_position(pos)
            >>> if wire:
            ...     print(f"Wire from ({wire.start.x_mm}, {wire.start.y_mm}) to ({wire.end.x_mm}, {wire.end.y_mm})")
        """
        target_x = position.x
        target_y = position.y

        for wire in self.get_wires():
            # Check endpoints
            if (abs(wire.start.x - target_x) <= tolerance_nm and
                abs(wire.start.y - target_y) <= tolerance_nm):
                return wire
            if (abs(wire.end.x - target_x) <= tolerance_nm and
                abs(wire.end.y - target_y) <= tolerance_nm):
                return wire

            # Check if point lies on wire segment (for horizontal/vertical wires)
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
    # Move/Copy/Rotate Methods
    # =========================================================================

    def move_item(
        self,
        item: Wrapper,
        new_position: Vector2,
    ) -> Wrapper:
        """Move an item to a new position.

        Args:
            item: The item to move (symbol, label, junction, etc.)
            new_position: New position for the item

        Returns:
            The updated item

        Note:
            This modifies the item in place and calls update_items.
            For wires, use move_wire() which handles both endpoints.

        Example:
            >>> symbol = sch.get_symbol_by_ref("R1")
            >>> new_pos = Vector2.from_xy_mm(150, 100)
            >>> sch.move_item(symbol, new_pos)
        """
        if hasattr(item, 'position'):
            item.position = new_position
            updated = self.update_items(item)
            return updated[0] if updated else item
        else:
            raise ValueError(f"Item type {type(item).__name__} does not have a position property")

    def move_item_by(
        self,
        item: Wrapper,
        delta_x_mm: float,
        delta_y_mm: float,
    ) -> Wrapper:
        """Move an item by a relative offset.

        Args:
            item: The item to move
            delta_x_mm: X offset in millimeters
            delta_y_mm: Y offset in millimeters

        Returns:
            The updated item

        Example:
            >>> symbol = sch.get_symbol_by_ref("R1")
            >>> # Move 10mm right and 5mm down
            >>> sch.move_item_by(symbol, 10, 5)
        """
        if hasattr(item, 'position'):
            current = item.position
            delta_x_nm = int(delta_x_mm * 1_000_000)
            delta_y_nm = int(delta_y_mm * 1_000_000)
            new_pos = Vector2.from_xy(current.x + delta_x_nm, current.y + delta_y_nm)
            return self.move_item(item, new_pos)
        else:
            raise ValueError(f"Item type {type(item).__name__} does not have a position property")

    def move_wire(
        self,
        wire: Wire,
        delta_x_mm: float,
        delta_y_mm: float,
    ) -> Wire:
        """Move a wire by a relative offset.

        This moves both endpoints of the wire by the same amount.

        Args:
            wire: The wire to move
            delta_x_mm: X offset in millimeters
            delta_y_mm: Y offset in millimeters

        Returns:
            The updated wire

        Example:
            >>> wires = sch.get_wires()
            >>> # Move first wire 10mm right
            >>> sch.move_wire(wires[0], 10, 0)
        """
        delta_x_nm = int(delta_x_mm * 1_000_000)
        delta_y_nm = int(delta_y_mm * 1_000_000)

        new_start = Vector2.from_xy(wire.start.x + delta_x_nm, wire.start.y + delta_y_nm)
        new_end = Vector2.from_xy(wire.end.x + delta_x_nm, wire.end.y + delta_y_nm)

        wire.start = new_start
        wire.end = new_end

        updated = self.update_items(wire)
        return updated[0] if updated else wire

    def rotate_symbol(
        self,
        symbol: Symbol,
        angle_degrees: float,
    ) -> Symbol:
        """Rotate a symbol by the specified angle.

        Args:
            symbol: The symbol to rotate
            angle_degrees: Rotation angle in degrees (positive = CCW)
                          Typical values: 90, 180, 270, or 0

        Returns:
            The updated symbol

        Example:
            >>> symbol = sch.get_symbol_by_ref("R1")
            >>> # Rotate 90 degrees
            >>> sch.rotate_symbol(symbol, 90)
        """
        # Normalize angle to 0-360 range
        new_angle = (symbol.angle + angle_degrees) % 360
        symbol.angle = new_angle
        updated = self.update_items(symbol)
        return updated[0] if updated else symbol

    def mirror_symbol(
        self,
        symbol: Symbol,
        axis: str = "x",
    ) -> Symbol:
        """Mirror a symbol along the specified axis.

        Args:
            symbol: The symbol to mirror
            axis: "x" for horizontal mirror, "y" for vertical mirror

        Returns:
            The updated symbol

        Example:
            >>> symbol = sch.get_symbol_by_ref("Q1")
            >>> # Mirror horizontally
            >>> sch.mirror_symbol(symbol, "x")
        """
        if axis.lower() == "x":
            symbol.mirror_x = not symbol.mirror_x
        elif axis.lower() == "y":
            symbol.mirror_y = not symbol.mirror_y
        else:
            raise ValueError(f"Invalid axis '{axis}'. Use 'x' or 'y'.")

        updated = self.update_items(symbol)
        return updated[0] if updated else symbol

    def copy_symbol(
        self,
        symbol: Symbol,
        new_position: Vector2,
        new_reference: Optional[str] = None,
    ) -> Symbol:
        """Create a copy of a symbol at a new position.

        This creates a new symbol with the same library ID, value,
        and other properties, but at a different position.

        Args:
            symbol: The symbol to copy
            new_position: Position for the new symbol
            new_reference: Optional new reference designator. If not provided,
                          the symbol will get a "?" reference that needs annotation.

        Returns:
            The newly created symbol

        Example:
            >>> r1 = sch.get_symbol_by_ref("R1")
            >>> # Copy R1 to a new position
            >>> r2 = sch.copy_symbol(r1, Vector2.from_xy_mm(150, 80), "R2")
        """
        # Create new symbol with same lib_id
        lib_id_str = f"{symbol.lib_id.library}:{symbol.lib_id.name}"
        new_symbol = self.add_symbol(
            lib_id_str,
            new_position,
            unit=symbol.unit,
            angle=symbol.angle,
            mirror_x=symbol.mirror_x,
            mirror_y=symbol.mirror_y,
        )

        # Copy value if available
        if symbol.value:
            new_symbol.value = symbol.value

        # Set reference if provided
        if new_reference:
            new_symbol.reference = new_reference

        # Update to apply field changes
        updated = self.update_items(new_symbol)
        return updated[0] if updated else new_symbol

    # =========================================================================
    # Net Label Helper Methods
    # =========================================================================

    def add_net_label(
        self,
        net_name: str,
        position: Vector2,
        label_type: str = "local",
    ) -> Wrapper:
        """Add a net label at a position to name a net.

        In KiCad, wires don't have intrinsic net names - nets are named
        by attaching labels. This is the primary way to assign net names.

        Args:
            net_name: Name for the net (e.g., "VCC", "GND", "CLK")
            position: Position for the label (should be on a wire or pin)
            label_type: Type of label:
                - "local": Local to this sheet
                - "global": Visible across all sheets
                - "hierarchical": For sheet pin connections

        Returns:
            The created label object

        Example:
            >>> # Name a wire as "VCC"
            >>> sch.add_net_label("VCC", Vector2.from_xy_mm(100, 50), "global")
            >>>
            >>> # Add local net label
            >>> sch.add_net_label("SW_NODE", Vector2.from_xy_mm(120, 80))
        """
        return self.add_label(net_name, position, label_type)

    def label_pin(
        self,
        symbol: Symbol,
        pin_id: str,
        net_name: str,
        label_type: str = "local",
        offset_mm: Tuple[float, float] = (0, 0),
    ) -> Wrapper:
        """Add a net label at a symbol's pin.

        This is a convenience method that gets the pin position and
        adds a label there, naming the net connected to that pin.

        Args:
            symbol: The symbol containing the pin
            pin_id: Pin name or number
            net_name: Name for the net
            label_type: "local", "global", or "hierarchical"
            offset_mm: Optional (x, y) offset from pin in mm

        Returns:
            The created label object

        Example:
            >>> mosfet = sch.add_symbol("Device:Q_NMOS_GSD", pos)
            >>> # Label the gate pin as "GATE_DRIVE"
            >>> sch.label_pin(mosfet, "G", "GATE_DRIVE")
            >>> # Label the drain as a global net
            >>> sch.label_pin(mosfet, "D", "VIN", label_type="global")
        """
        pin_pos = self.get_pin_position(symbol, pin_id)
        if pin_pos is None:
            raise ValueError(f"Pin '{pin_id}' not found on symbol")

        # Apply offset
        if offset_mm != (0, 0):
            offset_x_nm = int(offset_mm[0] * 1_000_000)
            offset_y_nm = int(offset_mm[1] * 1_000_000)
            label_pos = Vector2.from_xy(pin_pos.x + offset_x_nm, pin_pos.y + offset_y_nm)
        else:
            label_pos = pin_pos

        return self.add_label(net_name, label_pos, label_type)

    def get_net_name(self, item: Wrapper) -> Optional[str]:
        """Get the net name for an item (wire, pin, junction, etc.).

        Args:
            item: The item to query

        Returns:
            Net name if connected, None if not connected or unknown

        Example:
            >>> wire = sch.get_wires()[0]
            >>> net_name = sch.get_net_name(wire)
            >>> print(f"Wire is on net: {net_name}")
        """
        if not hasattr(item, 'id'):
            return None

        try:
            result = self.get_net_for_item(item.id.value)
            if result.is_connected:
                return result.connection.name
        except Exception:
            pass

        return None

    # =========================================================================
    # ERC Analysis Helper Methods
    # =========================================================================

    def analyze_erc_results(self) -> dict:
        """Run ERC and return a detailed analysis of the results.

        This runs ERC and categorizes violations by type, severity,
        and provides human-readable summaries.

        Returns:
            Dictionary with:
            - error_count: Number of errors
            - warning_count: Number of warnings
            - violations: List of violation dictionaries
            - by_type: Violations grouped by error code
            - by_severity: Violations grouped by severity
            - summary: Human-readable summary string

        Example:
            >>> analysis = sch.analyze_erc_results()
            >>> print(analysis['summary'])
            >>> for v in analysis['violations']:
            ...     print(f"{v['severity']}: {v['description']}")
        """
        # Common ERC error code descriptions
        ERC_CODES = {
            "ERCE_DUPLICATE_REFERENCE": "Duplicate reference designator",
            "ERCE_UNANNOTATED": "Symbol not annotated",
            "ERCE_PIN_TO_PIN_ERROR": "Pin-to-pin connection error",
            "ERCE_PIN_NOT_CONNECTED": "Unconnected pin",
            "ERCE_PIN_NOT_DRIVEN": "Pin not driven",
            "ERCE_POWERPIN_NOT_DRIVEN": "Power pin not driven",
            "ERCE_DUPLICATE_PIN": "Duplicate pin in symbol",
            "ERCE_NOCONNECT_CONNECTED": "No-connect marker on connected pin",
            "ERCE_NOCONNECT_NOT_CONNECTED": "No-connect marker not on pin",
            "ERCE_LABEL_NOT_CONNECTED": "Label not connected to anything",
            "ERCE_DIFFERENT_UNIT_FP": "Different footprint for multi-unit symbol",
            "ERCE_DIFFERENT_UNIT_NET": "Different net for multi-unit symbol pins",
            "ERCE_WIRE_DANGLING": "Dangling wire end",
            "ERCE_BUS_ALIAS_CONFLICT": "Bus alias conflict",
            "ERCE_BUS_ENTRY_CONFLICT": "Bus entry conflict",
            "ERCE_DRIVER_CONFLICT": "Multiple drivers conflict",
            "ERCE_GLOBLABEL": "Global label issue",
            "ERCE_SIMILAR_LABELS": "Similar label names (may be typo)",
            "ERCE_MISSING_UNIT": "Missing symbol unit",
            "ERCE_MISSING_INPUT_PIN": "Missing required input pin",
            "ERCE_MISSING_POWER_INPUT_PIN": "Missing power input pin",
            "ERCE_DIFFERENT_UNIT_VALUE": "Different value for multi-unit symbol",
            "ERCE_SIMULATION_MODEL": "Simulation model issue",
            "ERCE_GENERIC_ERROR": "Generic error",
            "ERCE_GENERIC_WARNING": "Generic warning",
        }

        # Severity mapping
        SEVERITY_MAP = {
            1: "error",
            2: "warning",
            3: "excluded",
        }

        result = self.run_erc()

        violations = []
        by_type = {}
        by_severity = {"error": [], "warning": [], "excluded": []}

        for v in result.violations:
            error_code = v.error_code
            severity = SEVERITY_MAP.get(v.severity, "unknown")
            description = v.description or ERC_CODES.get(error_code, error_code)

            violation_info = {
                "id": v.id.value if v.id else None,
                "error_code": error_code,
                "description": description,
                "severity": severity,
                "position_nm": (v.position.x_nm, v.position.y_nm) if v.position else None,
                "position_mm": (v.position.x_nm / 1e6, v.position.y_nm / 1e6) if v.position else None,
                "item_ids": [item.value for item in v.item_ids] if v.item_ids else [],
            }

            violations.append(violation_info)

            # Group by type
            if error_code not in by_type:
                by_type[error_code] = []
            by_type[error_code].append(violation_info)

            # Group by severity
            if severity in by_severity:
                by_severity[severity].append(violation_info)

        # Create summary
        summary_lines = []
        summary_lines.append(f"ERC Results: {result.error_count} errors, {result.warning_count} warnings")

        if by_type:
            summary_lines.append("\nViolations by type:")
            for code, items in sorted(by_type.items()):
                desc = ERC_CODES.get(code, code)
                summary_lines.append(f"  {code}: {len(items)} - {desc}")

        return {
            "error_count": result.error_count,
            "warning_count": result.warning_count,
            "violations": violations,
            "by_type": by_type,
            "by_severity": by_severity,
            "summary": "\n".join(summary_lines),
        }

    def get_erc_error_codes(self) -> dict:
        """Get a dictionary of known ERC error codes and descriptions.

        This is useful for understanding what types of errors KiCad can detect.

        Returns:
            Dictionary mapping error codes to descriptions

        Note:
            ERC settings (configuring which rules are errors/warnings/ignored)
            are stored in the project file and cannot be read/modified via
            the IPC API. To change ERC settings, use the KiCad GUI:
            Inspect -> Electrical Rules Checker -> Settings

        Example:
            >>> codes = sch.get_erc_error_codes()
            >>> for code, desc in codes.items():
            ...     print(f"{code}: {desc}")
        """
        return {
            "ERCE_DUPLICATE_REFERENCE": "Duplicate reference designator",
            "ERCE_UNANNOTATED": "Symbol not annotated (has '?' in reference)",
            "ERCE_PIN_TO_PIN_ERROR": "Invalid pin-to-pin connection (e.g., output to output)",
            "ERCE_PIN_NOT_CONNECTED": "Pin is not connected to any net",
            "ERCE_PIN_NOT_DRIVEN": "Input pin has no driver",
            "ERCE_POWERPIN_NOT_DRIVEN": "Power input pin is not connected to power",
            "ERCE_DUPLICATE_PIN": "Symbol has duplicate pin numbers",
            "ERCE_NOCONNECT_CONNECTED": "No-connect marker placed on pin that is connected",
            "ERCE_NOCONNECT_NOT_CONNECTED": "No-connect marker not placed on a pin",
            "ERCE_LABEL_NOT_CONNECTED": "Label is not connected to any wire or pin",
            "ERCE_DIFFERENT_UNIT_FP": "Multi-unit symbol has different footprints per unit",
            "ERCE_DIFFERENT_UNIT_NET": "Multi-unit symbol common pins on different nets",
            "ERCE_WIRE_DANGLING": "Wire end is not connected to anything",
            "ERCE_BUS_ALIAS_CONFLICT": "Bus alias definition conflicts",
            "ERCE_BUS_ENTRY_CONFLICT": "Bus entry does not match bus members",
            "ERCE_DRIVER_CONFLICT": "Net has multiple conflicting drivers",
            "ERCE_GLOBLABEL": "Global label issue (e.g., only one occurrence)",
            "ERCE_SIMILAR_LABELS": "Labels with similar names (possible typo)",
            "ERCE_MISSING_UNIT": "Multi-unit symbol is missing some units",
            "ERCE_MISSING_INPUT_PIN": "Required input pin is not connected",
            "ERCE_MISSING_POWER_INPUT_PIN": "Power input pin is not connected",
            "ERCE_DIFFERENT_UNIT_VALUE": "Multi-unit symbol has different values per unit",
            "ERCE_SIMULATION_MODEL": "Issue with SPICE simulation model",
        }

    def validate_schematic(self) -> dict:
        """Perform comprehensive validation of the schematic.

        This combines annotation checks and ERC to provide a complete
        validation report.

        Returns:
            Dictionary with:
            - is_valid: True if no errors (warnings allowed)
            - annotation_errors: List of annotation issues
            - erc_errors: List of ERC errors
            - erc_warnings: List of ERC warnings
            - unconnected_pins: List of unconnected pins
            - summary: Human-readable summary

        Example:
            >>> result = sch.validate_schematic()
            >>> if result['is_valid']:
            ...     print("Schematic is valid!")
            ... else:
            ...     print(result['summary'])
        """
        issues = {
            "is_valid": True,
            "annotation_errors": [],
            "erc_errors": [],
            "erc_warnings": [],
            "unconnected_pins": [],
            "summary": "",
        }

        summary_lines = []

        # Check annotation
        try:
            ann_result = self.check_annotation()
            if ann_result.error_count > 0:
                issues["is_valid"] = False
                for err in ann_result.errors:
                    issues["annotation_errors"].append({
                        "type": err.error_type,
                        "message": err.message,
                    })
                summary_lines.append(f"Annotation errors: {ann_result.error_count}")
        except Exception as e:
            summary_lines.append(f"Annotation check failed: {e}")

        # Run ERC
        try:
            erc_analysis = self.analyze_erc_results()
            issues["erc_errors"] = erc_analysis["by_severity"]["error"]
            issues["erc_warnings"] = erc_analysis["by_severity"]["warning"]

            if erc_analysis["error_count"] > 0:
                issues["is_valid"] = False
                summary_lines.append(f"ERC errors: {erc_analysis['error_count']}")

            if erc_analysis["warning_count"] > 0:
                summary_lines.append(f"ERC warnings: {erc_analysis['warning_count']}")

        except Exception as e:
            summary_lines.append(f"ERC check failed: {e}")

        # Check for unconnected pins
        try:
            issues["unconnected_pins"] = self.get_unconnected_pins()
            if issues["unconnected_pins"]:
                summary_lines.append(f"Unconnected pins: {len(issues['unconnected_pins'])}")
        except Exception as e:
            summary_lines.append(f"Unconnected pin check failed: {e}")

        if issues["is_valid"]:
            issues["summary"] = "Schematic validation passed (no errors found)"
        else:
            issues["summary"] = "Validation FAILED:\n" + "\n".join(summary_lines)

        return issues
