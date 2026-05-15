# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Base Schematic class with core functionality.

This module contains the foundational Schematic class that provides
access to all schematic operations through organized sub-modules.
"""

from typing import TYPE_CHECKING

from kipy.proto.common.types import DocumentSpecifier

if TYPE_CHECKING:
    from kipy.client import KiCadClient
    from kipy.schematic.crud import CRUDOperations
    from kipy.schematic.symbols import SymbolOperations
    from kipy.schematic.wiring import WiringOperations
    from kipy.schematic.labels import LabelOperations
    from kipy.schematic.sheets import SheetOperations
    from kipy.schematic.erc import ERCOperations
    from kipy.schematic.graphics import GraphicsOperations
    from kipy.schematic.page import PageOperations
    from kipy.schematic.library import LibraryOperations
    from kipy.schematic.selection import SelectionOperations
    from kipy.schematic.connectivity import ConnectivityOperations
    from kipy.schematic.transform import TransformOperations
    from kipy.schematic.buses import BusOperations
    from kipy.schematic.view import ViewOperations
    from kipy.schematic.simulation import SimulationOperations
    from kipy.schematic.export import ExportOperations
    from kipy.schematic.design_blocks import DesignBlockOperations
    from kipy.schematic.document import DocumentOperations
    from kipy.schematic.netclass import NetClassOperations
    from kipy.schematic.bus_alias import BusAliasOperations
    from kipy.schematic.multi_board import MultiBoardOperations


class Schematic:
    """Main interface for schematic operations.

    The Schematic class provides access to all schematic functionality
    through organized sub-modules:

    - `crud`: Create, read, update, delete operations
    - `symbols`: Symbol placement, search, move, copy, rotate, duplicate, replace
    - `wiring`: Wire creation, pin-based wiring, auto-routing
    - `labels`: Net labels, power symbols, net naming, net management
    - `sheets`: Hierarchical sheet management
    - `erc`: Electrical rules checking and validation
    - `graphics`: Text, shapes, annotations
    - `page`: Page settings, title block, grid operations
    - `library`: Symbol library management, search
    - `selection`: Editor selection operations
    - `connectivity`: Net and bus queries
    - `transform`: Batch transform operations (move, rotate, mirror, align, distribute)
    - `buses`: Bus definition, routing, and analysis
    - `design_blocks`: Design block management and placement

    Example:
        >>> from kipy import KiCad
        >>> kicad = KiCad()
        >>> sch = kicad.get_schematic()
        >>>
        >>> # Add a symbol
        >>> r1 = sch.symbols.add("Device:R", pos)
        >>>
        >>> # Wire two pins together
        >>> sch.wiring.wire_pins(r1, "1", r2, "2")
        >>>
        >>> # Run ERC
        >>> result = sch.erc.run()
        >>>
        >>> # Save
        >>> sch.save()
    """

    def __init__(self, kicad: "KiCadClient", document: DocumentSpecifier):
        self._kicad = kicad
        self._doc = document

        # Initialize sub-modules lazily
        self._crud = None
        self._symbols = None
        self._wiring = None
        self._labels = None
        self._sheets = None
        self._erc = None
        self._graphics = None
        self._page = None
        self._library = None
        self._selection = None
        self._connectivity = None
        self._transform = None
        self._buses = None
        self._view = None
        self._simulation = None
        self._export = None
        self._design_blocks = None
        self._document_ops = None
        self._netclass = None
        self._bus_alias = None
        self._multi_board = None

    @property
    def client(self) -> "KiCadClient":
        """The underlying KiCad client connection."""
        return self._kicad

    @property
    def document(self) -> DocumentSpecifier:
        """The document specifier for this schematic."""
        return self._doc

    # =========================================================================
    # Sub-module accessors (lazy initialization)
    # =========================================================================

    @property
    def crud(self) -> "CRUDOperations":
        """Create, read, update, delete operations.

        Methods:
            - create_items(items): Create one or more items
            - get_items(types): Get items by type
            - get_by_id(ids): Get items by KIID
            - update_items(items): Update item properties
            - remove_items(items): Delete items
            - begin_commit() / push_commit() / drop_commit(): Transaction control
        """
        if self._crud is None:
            from kipy.schematic.crud import CRUDOperations
            self._crud = CRUDOperations(self)
        return self._crud

    @property
    def symbols(self) -> "SymbolOperations":
        """Symbol operations: add, search, move, copy, rotate.

        Methods:
            - add(lib_id, position, ...): Place a symbol
            - get_all(): Get all symbols
            - get_by_ref(ref): Find by reference designator
            - get_by_value(value): Find by value
            - find(ref, value, lib, name): Multi-criteria search
            - move(symbol, position): Move to new position
            - move_by(symbol, dx, dy): Move by offset
            - rotate(symbol, angle): Rotate symbol
            - mirror(symbol, axis): Mirror symbol
            - copy(symbol, position, ref): Duplicate symbol
        """
        if self._symbols is None:
            from kipy.schematic.symbols import SymbolOperations
            self._symbols = SymbolOperations(self)
        return self._symbols

    @property
    def wiring(self) -> "WiringOperations":
        """Wire and connection operations.

        Methods:
            - add_wire(start, end): Create a wire
            - add_wires(points): Create connected wire segments
            - add_junction(position): Add junction
            - add_no_connect(position): Add no-connect marker
            - wire_pins(s1, p1, s2, p2): Wire between two pins
            - wire_from_pin(symbol, pin, point): Wire from pin to point
            - wire_to_pin(point, symbol, pin): Wire from point to pin
            - wire_path(start, waypoints, end): Multi-segment path
            - auto_wire(s1, p1, s2, p2, style): L-shaped auto-routing
            - get_pin_position(symbol, pin): Get exact pin position
        """
        if self._wiring is None:
            from kipy.schematic.wiring import WiringOperations
            self._wiring = WiringOperations(self)
        return self._wiring

    @property
    def labels(self) -> "LabelOperations":
        """Label and net naming operations.

        Methods:
            - add_local(text, position): Add local label
            - add_global(text, position): Add global label
            - add_hierarchical(text, position): Add hierarchical label
            - add_power(name, position, angle): Add power symbol
            - add_directive(text, position): Add SPICE directive
            - label_pin(symbol, pin, name): Label at a pin
            - get_net_name(item): Get net name for item
        """
        if self._labels is None:
            from kipy.schematic.labels import LabelOperations
            self._labels = LabelOperations(self)
        return self._labels

    @property
    def sheets(self) -> "SheetOperations":
        """Hierarchical sheet operations.

        Methods:
            - create(name, filename, position, size): Create sheet
            - delete(sheet_id): Delete sheet
            - get_hierarchy(): Get hierarchy tree
            - get_current(): Get current sheet
            - navigate_to(sheet_path): Navigate to sheet
            - get_properties(sheet_id): Get sheet properties
            - set_properties(sheet_id, ...): Set sheet properties
            - create_pin(...): Create sheet pin
            - delete_pin(pin_id): Delete sheet pin
            - get_pins(sheet_id): Get sheet pins
            - sync_pins(sheet_id): Sync pins with labels
        """
        if self._sheets is None:
            from kipy.schematic.sheets import SheetOperations
            self._sheets = SheetOperations(self)
        return self._sheets

    @property
    def erc(self) -> "ERCOperations":
        """ERC and validation operations.

        Methods:
            - run(all_tests): Run ERC
            - get_violations(severity): Get violations
            - clear_markers(ids): Clear ERC markers
            - exclude_violation(id): Exclude a violation
            - analyze(): Run ERC with detailed analysis
            - get_error_codes(): Get ERC code descriptions
            - validate(): Comprehensive validation
        """
        if self._erc is None:
            from kipy.schematic.erc import ERCOperations
            self._erc = ERCOperations(self)
        return self._erc

    @property
    def graphics(self) -> "GraphicsOperations":
        """Graphics operations: text, shapes, annotations.

        Methods:
            - add_text(text, position): Add text annotation
            - add_textbox(text, corners): Add text box
            - add_rectangle(corners): Add rectangle
            - add_circle(center, radius): Add circle
            - add_line(start, end): Add graphic line
            - add_arc(start, mid, end): Add arc
            - add_bitmap(path, position, scale): Add image
            - add_table(position, cols, rows): Add table
            - get_text_items(): Get all text
            - get_shapes(): Get all shapes
        """
        if self._graphics is None:
            from kipy.schematic.graphics import GraphicsOperations
            self._graphics = GraphicsOperations(self)
        return self._graphics

    @property
    def page(self) -> "PageOperations":
        """Page settings and title block operations.

        Methods:
            - get_settings(): Get page settings
            - set_settings(size, portrait, ...): Set page settings
            - get_title_block(): Get title block info
            - set_title_block(title, date, ...): Set title block
            - get_usable_area(): Get drawable area (mm)
            - get_usable_area_mils(): Get drawable area (mils)
            - get_grid_settings(): Get grid info
            - snap_to_grid(x, y): Snap to grid (mm)
            - snap_to_grid_mils(x, y): Snap to grid (mils)
        """
        if self._page is None:
            from kipy.schematic.page import PageOperations
            self._page = PageOperations(self)
        return self._page

    @property
    def library(self) -> "LibraryOperations":
        """Symbol library management.

        Methods:
            - get_all(scope): Get configured libraries
            - add(file_path, nickname, scope): Add library
        """
        if self._library is None:
            from kipy.schematic.library import LibraryOperations
            self._library = LibraryOperations(self)
        return self._library

    @property
    def selection(self) -> "SelectionOperations":
        """Editor selection operations.

        Methods:
            - get(types): Get current selection
            - add(items): Add to selection
            - remove(items): Remove from selection
            - clear(): Clear selection
        """
        if self._selection is None:
            from kipy.schematic.selection import SelectionOperations
            self._selection = SelectionOperations(self)
        return self._selection

    @property
    def connectivity(self) -> "ConnectivityOperations":
        """Net and bus connectivity queries.

        Methods:
            - get_nets(): Get all nets
            - get_buses(): Get all buses
            - get_net_for_item(id): Get item's net
            - get_bus_members(name): Get bus members
            - get_net_items(name): Get items on net
            - get_unconnected_pins(): Find floating pins
        """
        if self._connectivity is None:
            from kipy.schematic.connectivity import ConnectivityOperations
            self._connectivity = ConnectivityOperations(self)
        return self._connectivity

    @property
    def transform(self) -> "TransformOperations":
        """Batch transform operations for multiple items.

        Methods:
            - move(items, delta_x_mm, delta_y_mm): Move by offset
            - move_to(items, position, anchor): Move to position
            - rotate(items, angle, center): Rotate items
            - mirror(items, axis, center): Mirror items
            - align(items, alignment, reference): Align items
            - distribute(items, direction, spacing): Distribute evenly
            - snap_to_grid(items, grid_mm): Snap to grid
            - get_bounding_box(items, units): Get bounding box
        """
        if self._transform is None:
            from kipy.schematic.transform import TransformOperations
            self._transform = TransformOperations(self)
        return self._transform

    @property
    def buses(self) -> "BusOperations":
        """Bus definition, routing, and analysis operations.

        Methods:
            - get_all(): Get all buses in schematic
            - get_members(bus_name): Get member nets
            - define_vector_bus(prefix, start, end): Define D[0..7] style bus
            - define_group_bus(name, members): Define {A, B, C} style bus
            - add_bus_line(start, end): Add bus line
            - add_bus_entry(position, direction): Add bus entry
            - add_bus_label(bus_def, position): Add bus label
            - create_bus_tap(bus_pos, wire_end, net): Create entry+wire+label
            - expand_bus_to_pins(bus_def, start, pins): Expand to pin positions
            - analyze_bus_usage(): Analyze bus connections
        """
        if self._buses is None:
            from kipy.schematic.buses import BusOperations
            self._buses = BusOperations(self)
        return self._buses

    @property
    def view(self) -> "ViewOperations":
        """Viewport, zoom, and highlighting operations.

        Methods:
            - get_viewport(): Get current viewport settings
            - set_viewport(center, scale): Set viewport position/zoom
            - zoom_to_fit(): Zoom to fit all content
            - zoom_to_items(items, margin): Zoom to specific items
            - highlight_net(net_name): Highlight a net
            - clear_highlight(): Clear all highlighting
            - cross_probe_to_board(items): Cross-probe to PCB editor
            - get_undo_history(): Get undo/redo history
            - undo(count): Undo operations
            - redo(count): Redo operations
        """
        if self._view is None:
            from kipy.schematic.view import ViewOperations
            self._view = ViewOperations(self)
        return self._view

    @property
    def simulation(self) -> "SimulationOperations":
        """SPICE simulation settings and execution.

        Methods:
            - get_settings(): Get simulation settings
            - set_settings(...): Set simulation settings
            - run(command_override): Run simulation
            - get_results(): Get previous simulation results
        """
        if self._simulation is None:
            from kipy.schematic.simulation import SimulationOperations
            self._simulation = SimulationOperations(self)
        return self._simulation

    @property
    def export(self) -> "ExportOperations":
        """Export operations for netlists, BOM, and plots.

        Methods:
            - netlist(output_path, format): Export netlist
            - spice_netlist(output_path): Export SPICE netlist
            - bom(output_path, format, fields): Export BOM
            - plot(output_path, format): Export plot (PDF, SVG, etc.)
            - pdf(output_path): Export to PDF
            - svg(output_path): Export to SVG
        """
        if self._export is None:
            from kipy.schematic.export import ExportOperations
            self._export = ExportOperations(self)
        return self._export

    @property
    def design_blocks(self) -> "DesignBlockOperations":
        """Design block management operations.

        Methods:
            - get_all(library): Get available design blocks
            - search(query, libraries, max_results): Search design blocks
            - delete(lib_id): Delete a design block
            - get_by_library(): Get blocks organized by library
        """
        if self._design_blocks is None:
            from kipy.schematic.design_blocks import DesignBlockOperations
            self._design_blocks = DesignBlockOperations(self)
        return self._design_blocks

    @property
    def document_ops(self) -> "DocumentOperations":
        """Document management operations.

        Methods:
            - get_open_documents(doc_type): Get all open documents
            - create(path, template): Create new schematic
            - open(path): Open existing schematic
            - save(): Save current schematic
            - save_copy(path): Save copy to new location
            - close(save_changes, force): Close document
            - revert(): Revert to last saved state
        """
        if self._document_ops is None:
            from kipy.schematic.document import DocumentOperations
            self._document_ops = DocumentOperations(self)
        return self._document_ops

    @property
    def netclass(self) -> "NetClassOperations":
        """Net class management operations.

        Methods:
            - get_all(): Get all net classes
            - get(name): Get a specific net class
            - create(name, ...): Create a new net class
            - update(name, ...): Update a net class
            - delete(name): Delete a net class
            - get_assignments(): Get pattern-based net class assignments
            - set_assignments(assignments): Replace all assignments
            - add_assignment(pattern, netclass): Add an assignment
            - remove_assignment(pattern): Remove an assignment
            - assign_net(net_name, netclass): Assign a net to a class
        """
        if self._netclass is None:
            from kipy.schematic.netclass import NetClassOperations
            self._netclass = NetClassOperations(self)
        return self._netclass

    @property
    def bus_alias(self) -> "BusAliasOperations":
        """Bus alias management operations.

        Bus aliases define named groups of signals that can be used together as a bus.

        Methods:
            - get_all(): Get all bus aliases
            - get(name): Get a specific bus alias
            - create(name, members): Create a new bus alias
            - update(name, members): Update a bus alias
            - delete(name): Delete a bus alias
            - set_all(aliases): Replace all bus aliases at once
            - add_member(alias_name, member): Add a member to an alias
            - remove_member(alias_name, member): Remove a member from an alias

        Example:
            >>> sch.bus_alias.create("DATA_BUS", ["D0", "D1", "D2", "D3"])
            >>> sch.bus_alias.create("ADDR_BUS", ["A0", "A1", "A2", "A3"])
        """
        if self._bus_alias is None:
            from kipy.schematic.bus_alias import BusAliasOperations
            self._bus_alias = BusAliasOperations(self)
        return self._bus_alias

    @property
    def multi_board(self) -> "MultiBoardOperations":
        """Multi-board (MBS) inspection operations.

        Valid only when this Schematic represents a `.kicad_mbs` file
        (obtained via ``kicad.get_mbs_schematic()``). Calling on a regular
        schematic returns AS_UNHANDLED from the API server.

        Methods:
            - get_blocks(): list module blocks on the active MBS sheet
            - get_cross_board_nets(): list cross-board nets in the container
            - get_container_info(): container metadata + sub-project list
        """
        if self._multi_board is None:
            from kipy.schematic.multi_board import MultiBoardOperations
            self._multi_board = MultiBoardOperations(self)
        return self._multi_board

    # =========================================================================
    # Core convenience methods (delegated to sub-modules)
    # =========================================================================

    def save(self):
        """Save the schematic to disk."""
        from google.protobuf.empty_pb2 import Empty
        from kipy.proto.common.commands import editor_commands_pb2

        command = editor_commands_pb2.SaveDocument()
        command.document.CopyFrom(self._doc)
        self._kicad.send(command, Empty)

    def refresh(self):
        """Refresh the schematic editor view."""
        from google.protobuf.empty_pb2 import Empty
        from kipy.proto.common.commands import editor_commands_pb2
        from kipy.proto.common.types import FrameType

        command = editor_commands_pb2.RefreshEditor()
        command.frame = FrameType.FT_SCHEMATIC_EDITOR
        self._kicad.send(command, Empty)

    def get_project(self):
        """Get the project associated with this schematic."""
        from kipy.project import Project
        from kipy.proto.common.types import DocumentSpecifier
        # Make a copy of _doc to avoid Project.__init__ mutating our document type
        doc_copy = DocumentSpecifier()
        doc_copy.CopyFrom(self._doc)
        return Project(self._kicad, doc_copy)

    def refresh_document(self, max_retries: int = 3, retry_delay_ms: int = 100) -> bool:
        """Refresh the DocumentSpecifier to handle close/reopen cycles.

        Re-queries GetOpenDocuments to get the current document UUID.
        This should be called after close_editor/open_editor cycles
        or when the document may have been reloaded.

        Args:
            max_retries: Number of times to retry if document not ready (default 3).
            retry_delay_ms: Delay between retries in milliseconds (default 100).

        Returns:
            True if refresh succeeded and document is still open,
            False if no schematic document is open.
        """
        import sys
        import time
        from kipy.proto.common.commands import editor_commands_pb2
        from kipy.proto.common.types import DocumentType

        old_doc_uuid = ""
        if self._doc and hasattr(self._doc, 'sheet_path'):
            sp = self._doc.sheet_path
            if sp and hasattr(sp, 'path') and sp.path:
                old_doc_uuid = sp.path[0].value if sp.path else ""

        # Use the wrapped document's actual type so we re-query against the
        # correct handler (DOCTYPE_MBS_SCHEMATIC on MBS, else DOCTYPE_SCHEMATIC).
        query_type = (
            self._doc.type
            if self._doc and getattr(self._doc, "type", None)
            else DocumentType.DOCTYPE_SCHEMATIC
        )

        for attempt in range(max_retries):
            cmd = editor_commands_pb2.GetOpenDocuments()
            cmd.type = query_type
            response = self._kicad.send(cmd, editor_commands_pb2.GetOpenDocumentsResponse)

            if len(response.documents) > 0:
                new_doc = response.documents[0]
                new_uuid = ""
                if hasattr(new_doc, 'sheet_path'):
                    sp = new_doc.sheet_path
                    if sp and hasattr(sp, 'path') and sp.path:
                        new_uuid = sp.path[0].value if sp.path else ""

                # Check if we got a valid UUID
                if new_uuid and new_uuid != "00000000-0000-0000-0000-000000000000":
                    if old_doc_uuid and old_doc_uuid != new_uuid:
                        print(f"[refresh_document] UUID changed: {old_doc_uuid} -> {new_uuid}", file=sys.stderr)
                    self._doc = new_doc
                    return True
                elif attempt < max_retries - 1:
                    # Got a nil/invalid UUID, retry after delay
                    print(f"[refresh_document] Attempt {attempt + 1}: Got invalid UUID, retrying...", file=sys.stderr)
                    time.sleep(retry_delay_ms / 1000.0)
                    continue
                else:
                    # Last attempt, accept whatever we got
                    print(f"[refresh_document] Warning: Got UUID '{new_uuid}' after {max_retries} attempts", file=sys.stderr)
                    self._doc = new_doc
                    return True

            elif attempt < max_retries - 1:
                print(f"[refresh_document] Attempt {attempt + 1}: No documents, retrying...", file=sys.stderr)
                time.sleep(retry_delay_ms / 1000.0)
            else:
                print(f"[refresh_document] No schematic documents found after {max_retries} attempts", file=sys.stderr)

        return False
