# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Base Board class with core functionality.

This module contains the foundational Board class that provides
access to all board operations through organized sub-modules.
"""

from typing import TYPE_CHECKING

from kipy.proto.common.types import DocumentSpecifier

if TYPE_CHECKING:
    from kipy.client import KiCadClient
    from kipy.board.crud import CRUDOperations
    from kipy.board.footprints import FootprintOperations
    from kipy.board.routing import RoutingOperations
    from kipy.board.zones import ZoneOperations
    from kipy.board.layers import LayerOperations
    from kipy.board.nets import NetOperations
    from kipy.board.drc import DRCOperations
    from kipy.board.design_rules import DesignRulesOperations
    from kipy.board.selection import SelectionOperations
    from kipy.board.page import PageOperations
    from kipy.board.graphics import GraphicsOperations
    from kipy.board.grid import GridOperations
    from kipy.board.view import ViewOperations
    from kipy.board.export import ExportOperations
    from kipy.board.document import DocumentOperations
    from kipy.board.sync import SyncOperations
    from kipy.board.library import FootprintLibraryOperations
    from kipy.board.connectivity import ConnectivityOperations
    from kipy.board.groups import GroupOperations
    from kipy.board.autoroute import AutorouteOperations
    from kipy.board.teardrops import TeardropOperations
    from kipy.board.custom_rules import CustomRulesOperations
    from kipy.board.dimension_defaults import DimensionDefaultsOperations
    from kipy.board.zone_defaults import ZoneDefaultsOperations
    from kipy.board.tuning import TuningOperations
    from kipy.board.component_classes import ComponentClassOperations
    from kipy.board.graphics_defaults import GraphicsDefaultsOperations


class Board:
    """Main interface for board operations.

    The Board class provides access to all board functionality
    through organized sub-modules:

    - `crud`: Create, read, update, delete operations
    - `footprints`: Footprint placement, search, move, rotate
    - `routing`: Track and via creation, routing helpers
    - `zones`: Copper zone and rule area management
    - `layers`: Stackup, enabled/visible layer management
    - `nets`: Net queries and net class management
    - `drc`: Design rule checking, settings, violations
    - `design_rules`: Board design constraints (clearances, widths, etc.)
    - `selection`: Editor selection operations
    - `page`: Page settings, title block
    - `graphics`: Shapes, text, dimensions
    - `grid`: Grid settings
    - `view`: Appearance settings, active layer
    - `export`: Gerber, drill, DRC reports
    - `document_ops`: Save, open, close, revert
    - `sync`: Update PCB from schematic
    - `library`: Footprint library browsing and search
    - `connectivity`: Ratsnest and unrouted connection queries
    - `groups`: Group management operations
    - `autoroute`: Autorouter for automatic trace routing

    Example:
        >>> from kipy import KiCad
        >>> kicad = KiCad()
        >>> board = kicad.get_board()
        >>>
        >>> # Route a track
        >>> board.routing.route([start, end], width=250000, layer=BoardLayer.BL_F_Cu)
        >>>
        >>> # Run DRC
        >>> errors, warnings, _ = board.drc.run()
        >>>
        >>> # Save
        >>> board.save()
    """

    def __init__(self, kicad: "KiCadClient", document: DocumentSpecifier):
        self._kicad = kicad
        self._doc = document

        # Initialize sub-modules lazily
        self._crud = None
        self._footprints = None
        self._routing = None
        self._zones = None
        self._layers = None
        self._nets = None
        self._drc = None
        self._design_rules = None
        self._selection = None
        self._page = None
        self._graphics = None
        self._grid = None
        self._view = None
        self._export = None
        self._document_ops = None
        self._sync = None
        self._library = None
        self._connectivity = None
        self._groups = None
        self._autoroute = None
        self._teardrops = None
        self._custom_rules = None
        self._dimension_defaults = None
        self._zone_defaults = None
        self._tuning = None
        self._component_classes = None
        self._graphics_defaults = None

    def __repr__(self) -> str:
        return f"Board(filename={self.name})"

    @property
    def client(self) -> "KiCadClient":
        """The underlying KiCad client connection."""
        return self._kicad

    @property
    def document(self) -> DocumentSpecifier:
        """The document specifier for this board."""
        return self._doc

    @property
    def name(self) -> str:
        """The file name of the board."""
        return self._doc.board_filename

    # =========================================================================
    # Sub-module accessors (lazy initialization)
    # =========================================================================

    @property
    def crud(self) -> "CRUDOperations":
        """Create, read, update, delete operations.

        Methods:
            - create_items(items): Create one or more items
            - get_items(types): Get items by type
            - get_footprints(): Get all footprints
            - get_tracks(): Get all tracks
            - get_vias(): Get all vias
            - get_pads(): Get all pads
            - get_zones(): Get all zones
            - get_shapes(): Get all graphic shapes
            - get_text(): Get all text items
            - get_dimensions(): Get all dimensions
            - update_items(items): Update item properties
            - remove_items(items): Delete items
            - remove_items_by_id(ids): Delete items by KIID
            - begin_commit() / push_commit() / drop_commit(): Transaction control
        """
        if self._crud is None:
            from kipy.board.crud import CRUDOperations
            self._crud = CRUDOperations(self)
        return self._crud

    @property
    def footprints(self) -> "FootprintOperations":
        """Footprint operations: place, search, move, rotate.

        Methods:
            - place(lib_id, position, layer, orientation, ...): Place footprint
            - get_all(): Get all footprints
            - get_by_reference(ref): Find by reference designator
            - get_by_value(value): Find by value
            - move(footprint, position): Move footprint
            - rotate(footprint, angle): Rotate footprint
            - flip(footprint): Flip to other side
        """
        if self._footprints is None:
            from kipy.board.footprints import FootprintOperations
            self._footprints = FootprintOperations(self)
        return self._footprints

    @property
    def routing(self) -> "RoutingOperations":
        """Track and via routing operations.

        Methods:
            - route(points, width, layer, net): Create connected track segments
            - add_track(start, end, width, layer, net): Create single track
            - add_via(position, diameter, drill, net, type): Add via
            - get_tracks(): Get all tracks
            - get_vias(): Get all vias
        """
        if self._routing is None:
            from kipy.board.routing import RoutingOperations
            self._routing = RoutingOperations(self)
        return self._routing

    @property
    def zones(self) -> "ZoneOperations":
        """Zone management operations.

        Methods:
            - add(outline, layers, net, name, priority): Create zone
            - get_all(): Get all zones
            - refill(block): Refill all zones
            - get_by_net(net_name): Get zones by net
        """
        if self._zones is None:
            from kipy.board.zones import ZoneOperations
            self._zones = ZoneOperations(self)
        return self._zones

    @property
    def layers(self) -> "LayerOperations":
        """Layer and stackup operations.

        Methods:
            - get_stackup(): Get board stackup
            - update_stackup(stackup): Update stackup
            - get_copper_layer_count(): Get number of copper layers
            - get_enabled_layers(): Get enabled layers
            - set_enabled_layers(count, layers): Set enabled layers
            - get_visible_layers(): Get visible layers
            - set_visible_layers(layers): Set visible layers
            - get_active_layer(): Get active layer
            - set_active_layer(layer): Set active layer
            - get_graphics_defaults(): Get graphics defaults per layer class
            - set_graphics_defaults(defaults): Set graphics defaults
        """
        if self._layers is None:
            from kipy.board.layers import LayerOperations
            self._layers = LayerOperations(self)
        return self._layers

    @property
    def nets(self) -> "NetOperations":
        """Net queries and management.

        Methods:
            - get_all(netclass_filter): Get all nets
            - get_netclass_for_nets(nets): Get net class for nets
            - expand_text_variables(text): Expand text variables
        """
        if self._nets is None:
            from kipy.board.nets import NetOperations
            self._nets = NetOperations(self)
        return self._nets

    @property
    def drc(self) -> "DRCOperations":
        """Design Rule Check operations.

        Methods:
            - run(refill_zones, report_all_track_errors, test_footprints): Run DRC
            - get_violations(severities): Get current DRC violations
            - clear_markers(clear_violations, clear_exclusions): Clear DRC markers
            - get_settings(): Get DRC check severity settings
            - set_settings(settings): Set DRC check severity settings
        """
        if self._drc is None:
            from kipy.board.drc import DRCOperations
            self._drc = DRCOperations(self)
        return self._drc

    @property
    def design_rules(self) -> "DesignRulesOperations":
        """Board design constraints operations.

        Methods:
            - get(): Get all design rules
            - set(rules): Set design rules
            - get_clearance(): Get minimum clearance
            - set_clearance(value): Set minimum clearance
            - (and similar for other rules...)
        """
        if self._design_rules is None:
            from kipy.board.design_rules import DesignRulesOperations
            self._design_rules = DesignRulesOperations(self)
        return self._design_rules

    @property
    def selection(self) -> "SelectionOperations":
        """Editor selection operations.

        Methods:
            - get(types): Get current selection
            - add(items): Add to selection
            - remove(items): Remove from selection
            - clear(): Clear selection
            - get_as_string(): Get selection as S-expression string
        """
        if self._selection is None:
            from kipy.board.selection import SelectionOperations
            self._selection = SelectionOperations(self)
        return self._selection

    @property
    def page(self) -> "PageOperations":
        """Page settings and title block operations.

        Methods:
            - get_title_block(): Get title block info
            - get_origin(type): Get board origin
            - set_origin(type, position): Set board origin
        """
        if self._page is None:
            from kipy.board.page import PageOperations
            self._page = PageOperations(self)
        return self._page

    @property
    def graphics(self) -> "GraphicsOperations":
        """Graphics operations: shapes, text, dimensions.

        Methods:
            - get_shapes(): Get all graphic shapes
            - get_text(): Get all text items
            - get_dimensions(): Get all dimensions
            - get_bounding_box(items, include_text): Get item bounds
            - hit_test(item, position, tolerance): Test point hit
        """
        if self._graphics is None:
            from kipy.board.graphics import GraphicsOperations
            self._graphics = GraphicsOperations(self)
        return self._graphics

    @property
    def grid(self) -> "GridOperations":
        """Grid settings operations.

        Methods:
            - get_settings(): Get grid size, visibility, style
            - set_settings(size_x, size_y, show, style): Set grid settings
        """
        if self._grid is None:
            from kipy.board.grid import GridOperations
            self._grid = GridOperations(self)
        return self._grid

    @property
    def view(self) -> "ViewOperations":
        """View and appearance operations.

        Methods:
            - get_appearance_settings(): Get editor appearance settings
            - set_appearance_settings(settings): Set appearance settings
            - interactive_move(items): Start interactive move
            - show_diff_overlay(bounding_box): Show diff overlay
        """
        if self._view is None:
            from kipy.board.view import ViewOperations
            self._view = ViewOperations(self)
        return self._view

    @property
    def export(self) -> "ExportOperations":
        """Export operations for manufacturing files.

        Methods:
            - run_drc_cli(output_path, format, units): Run DRC via CLI
            - generate_gerbers(output_dir, layers, ...): Generate Gerbers
            - generate_drill_files(output_dir, format, units, ...): Generate drill files
            - get_as_string(): Get board as S-expression string
            - from_string(contents): Create items from S-expression string
        """
        if self._export is None:
            from kipy.board.export import ExportOperations
            self._export = ExportOperations(self)
        return self._export

    @property
    def document_ops(self) -> "DocumentOperations":
        """Document management operations.

        Methods:
            - save(): Save board
            - save_as(filename, overwrite, include_project): Save copy
            - revert(): Revert to saved state
            - refresh(): Refresh editor view
        """
        if self._document_ops is None:
            from kipy.board.document import DocumentOperations
            self._document_ops = DocumentOperations(self)
        return self._document_ops

    @property
    def sync(self) -> "SyncOperations":
        """Schematic-to-PCB synchronization operations.

        Methods:
            - update_from_schematic(...): Update PCB from schematic
            - preview_update(...): Preview changes without applying
        """
        if self._sync is None:
            from kipy.board.sync import SyncOperations
            self._sync = SyncOperations(self)
        return self._sync

    @property
    def library(self) -> "FootprintLibraryOperations":
        """Footprint library browsing and search operations.

        Methods:
            - get_footprints(library_name): List footprints in a library
            - search(query, libraries, max_results): Search for footprints
            - get_info(lib_id): Get detailed footprint information
        """
        if self._library is None:
            from kipy.board.library import FootprintLibraryOperations
            self._library = FootprintLibraryOperations(self)
        return self._library

    @property
    def connectivity(self) -> "ConnectivityOperations":
        """Ratsnest and connectivity query operations.

        Methods:
            - get_ratsnest(net_codes, include_zones): Get unrouted connections
            - get_unrouted_nets(): Get nets with incomplete routing
            - get_status(item_ids): Get connectivity status for items
            - is_routing_complete(): Check if all nets are routed
            - get_routing_progress(): Get overall routing percentage
        """
        if self._connectivity is None:
            from kipy.board.connectivity import ConnectivityOperations
            self._connectivity = ConnectivityOperations(self)
        return self._connectivity

    @property
    def groups(self) -> "GroupOperations":
        """Group management operations.

        Methods:
            - get_all(): Get all groups
            - get_by_name(name): Find group by name
            - create(name, items): Create a new group
            - delete(group, ungroup_members): Delete a group
            - add_items(group, items): Add items to a group
            - remove_items(group, items): Remove items from a group
            - get_members(group): Get items in a group
        """
        if self._groups is None:
            from kipy.board.groups import GroupOperations
            self._groups = GroupOperations(self)
        return self._groups

    @property
    def autoroute(self) -> "AutorouteOperations":
        """Autorouter operations.

        Methods:
            - route_all(max_passes, angle, net_codes): Run the autorouter

        Example:
            >>> result = board.autoroute.route_all(max_passes=20, angle=1)
            >>> print(f"Routed {result['routed']}/{result['total']}")
        """
        if self._autoroute is None:
            from kipy.board.autoroute import AutorouteOperations
            self._autoroute = AutorouteOperations(self)
        return self._autoroute

    @property
    def teardrops(self) -> "TeardropOperations":
        """Teardrop settings operations.

        Methods:
            - get(): Get teardrop settings
            - set(...): Set teardrop settings (partial update)

        Example:
            >>> from kipy.board.teardrops import TeardropParameters
            >>> settings = board.teardrops.get()
            >>> board.teardrops.set(
            ...     target_vias=True,
            ...     round_shapes=TeardropParameters(best_length_ratio=0.5, curved_edges=True)
            ... )
        """
        if self._teardrops is None:
            from kipy.board.teardrops import TeardropOperations
            self._teardrops = TeardropOperations(self)
        return self._teardrops

    @property
    def custom_rules(self) -> "CustomRulesOperations":
        """Custom design rules (DRC rules language) operations.

        Methods:
            - get(): Get custom rules text
            - set(rules_text): Set custom rules text

        Example:
            >>> rules = board.custom_rules.get()
            >>> board.custom_rules.set("(rule HV_clearance ...)")
        """
        if self._custom_rules is None:
            from kipy.board.custom_rules import CustomRulesOperations
            self._custom_rules = CustomRulesOperations(self)
        return self._custom_rules

    @property
    def dimension_defaults(self) -> "DimensionDefaultsOperations":
        """Dimension annotation default settings operations.

        Methods:
            - get(): Get dimension defaults
            - set(...): Set dimension defaults (partial update)

        Example:
            >>> defaults = board.dimension_defaults.get()
            >>> board.dimension_defaults.set(units_mode=DimensionUnitsMode.INCHES)
        """
        if self._dimension_defaults is None:
            from kipy.board.dimension_defaults import DimensionDefaultsOperations
            self._dimension_defaults = DimensionDefaultsOperations(self)
        return self._dimension_defaults

    @property
    def zone_defaults(self) -> "ZoneDefaultsOperations":
        """Zone default settings operations.

        Methods:
            - get(): Get zone defaults
            - set(...): Set zone defaults (partial update)

        Example:
            >>> defaults = board.zone_defaults.get()
            >>> board.zone_defaults.set(clearance_nm=200000, pad_connection=ZonePadConnection.THERMAL)
        """
        if self._zone_defaults is None:
            from kipy.board.zone_defaults import ZoneDefaultsOperations
            self._zone_defaults = ZoneDefaultsOperations(self)
        return self._zone_defaults

    @property
    def tuning(self) -> "TuningOperations":
        """Length tuning pattern and profile operations.

        Methods:
            - get_pattern_settings(): Get meander pattern settings
            - set_pattern_settings(...): Set meander pattern settings
            - get_profiles(): Get all tuning profiles
            - set_profiles(profiles): Set (replace) all tuning profiles

        Example:
            >>> settings = board.tuning.get_pattern_settings()
            >>> profiles = board.tuning.get_profiles()
        """
        if self._tuning is None:
            from kipy.board.tuning import TuningOperations
            self._tuning = TuningOperations(self)
        return self._tuning

    @property
    def component_classes(self) -> "ComponentClassOperations":
        """Component class settings operations.

        Methods:
            - get(): Get component class settings
            - set(settings): Set component class settings

        Example:
            >>> settings = board.component_classes.get()
            >>> settings.enable_sheet_class_generation = True
            >>> board.component_classes.set(settings)
        """
        if self._component_classes is None:
            from kipy.board.component_classes import ComponentClassOperations
            self._component_classes = ComponentClassOperations(self)
        return self._component_classes

    @property
    def graphics_defaults(self) -> "GraphicsDefaultsOperations":
        """Graphics defaults per layer class operations.

        Methods:
            - get(): Get graphics defaults for all layer classes
            - set(defaults): Set graphics defaults
            - set_layer(...): Set defaults for a specific layer class

        Example:
            >>> defaults = board.graphics_defaults.get()
            >>> board.graphics_defaults.set_layer(
            ...     layer_class=BoardLayerClass.SILKSCREEN,
            ...     text_size_nm=1000000
            ... )
        """
        if self._graphics_defaults is None:
            from kipy.board.graphics_defaults import GraphicsDefaultsOperations
            self._graphics_defaults = GraphicsDefaultsOperations(self)
        return self._graphics_defaults

    # =========================================================================
    # Backwards compatibility methods (delegated to sub-modules)
    # These maintain API compatibility with the original monolithic Board class
    # =========================================================================

    # Document operations
    def save(self):
        """Save the board to disk."""
        self.document_ops.save()

    def save_as(self, filename: str, overwrite: bool = False, include_project: bool = True):
        """Save the board to a new file."""
        self.document_ops.save_as(filename, overwrite, include_project)

    def revert(self):
        """Revert the board to the last saved state."""
        self.document_ops.revert()

    def refresh(self):
        """Refresh the board editor view."""
        self.document_ops.refresh()

    def get_project(self):
        """Get the project associated with this board."""
        from kipy.project import Project
        from kipy.proto.common.types import DocumentSpecifier
        # Make a copy of _doc to avoid Project.__init__ mutating our document type
        doc_copy = DocumentSpecifier()
        doc_copy.CopyFrom(self._doc)
        return Project(self._kicad, doc_copy)

    # CRUD operations
    def begin_commit(self):
        """Begin a commit transaction."""
        return self.crud.begin_commit()

    def push_commit(self, commit, message: str = ""):
        """Push a commit transaction."""
        self.crud.push_commit(commit, message)

    def drop_commit(self, commit):
        """Drop/cancel a commit transaction."""
        self.crud.drop_commit(commit)

    def create_items(self, items):
        """Create one or more items on the board."""
        return self.crud.create_items(items)

    def get_items(self, types):
        """Retrieve items from the board by type."""
        return self.crud.get_items(types)

    def update_items(self, items):
        """Update item properties on the board."""
        return self.crud.update_items(items)

    def remove_items(self, items):
        """Delete items from the board."""
        self.crud.remove_items(items)

    def remove_items_by_id(self, items):
        """Delete items from the board by ID."""
        self.crud.remove_items_by_id(items)

    def get_tracks(self):
        """Get all tracks on the board."""
        return self.crud.get_tracks()

    def get_vias(self):
        """Get all vias on the board."""
        return self.crud.get_vias()

    def get_pads(self):
        """Get all pads on the board."""
        return self.crud.get_pads()

    def get_footprints(self):
        """Get all footprints on the board."""
        return self.crud.get_footprints()

    def get_shapes(self):
        """Get all graphic shapes on the board."""
        return self.crud.get_shapes()

    def get_dimensions(self):
        """Get all dimensions on the board."""
        return self.crud.get_dimensions()

    def get_text(self):
        """Get all text items on the board."""
        return self.crud.get_text()

    def get_zones(self):
        """Get all zones on the board."""
        return self.crud.get_zones()

    # Net operations
    def get_nets(self, netclass_filter=None):
        """Get all nets on the board."""
        return self.nets.get_all(netclass_filter)

    def get_netclass_for_nets(self, nets):
        """Get the net class for one or more nets."""
        return self.nets.get_netclass_for_nets(nets)

    def expand_text_variables(self, text):
        """Expand text variables in a string or list of strings."""
        return self.nets.expand_text_variables(text)

    # Selection operations
    def get_selection(self, types=None):
        """Get the current selection."""
        return self.selection.get(types)

    def add_to_selection(self, items):
        """Add items to the selection."""
        return self.selection.add(items)

    def remove_from_selection(self, items):
        """Remove items from the selection."""
        return self.selection.remove(items)

    def clear_selection(self):
        """Clear the selection."""
        self.selection.clear()

    def get_selection_as_string(self):
        """Get the selection as a KiCad S-expression string."""
        return self.selection.get_as_string()

    # Layer operations
    def get_stackup(self):
        """Get the board stackup."""
        return self.layers.get_stackup()

    def update_stackup(self, stackup):
        """Update the board stackup."""
        return self.layers.update_stackup(stackup)

    def get_copper_layer_count(self):
        """Get the number of copper layers."""
        return self.layers.get_copper_layer_count()

    def get_enabled_layers(self):
        """Get the enabled layers."""
        return self.layers.get_enabled_layers()

    def set_enabled_layers(self, copper_layer_count, layers):
        """Set the enabled layers."""
        return self.layers.set_enabled_layers(copper_layer_count, layers)

    def get_visible_layers(self):
        """Get the visible layers."""
        return self.layers.get_visible_layers()

    def set_visible_layers(self, layers):
        """Set the visible layers."""
        self.layers.set_visible_layers(layers)

    def get_active_layer(self):
        """Get the active layer."""
        return self.layers.get_active_layer()

    def set_active_layer(self, layer):
        """Set the active layer."""
        self.layers.set_active_layer(layer)

    def get_graphics_defaults(self):
        """Get graphics defaults per layer class."""
        return self.layers.get_graphics_defaults()

    def set_graphics_defaults(self, defaults):
        """Set graphics defaults per layer class."""
        return self.layers.set_graphics_defaults(defaults)

    # Page operations
    def get_title_block_info(self):
        """Get the title block information."""
        return self.page.get_title_block()

    def get_origin(self, origin_type):
        """Get the board origin."""
        return self.page.get_origin(origin_type)

    def set_origin(self, origin_type, origin):
        """Set the board origin."""
        self.page.set_origin(origin_type, origin)

    # Graphics operations
    def get_item_bounding_box(self, items, include_text: bool = False):
        """Get the bounding box for an item or items."""
        return self.graphics.get_bounding_box(items, include_text)

    def get_pad_shapes_as_polygons(self, pads, layer=None):
        """Get the polygonal shape of pads."""
        from kipy.proto.board.board_types_pb2 import BoardLayer
        if layer is None:
            layer = BoardLayer.BL_F_Cu
        return self.graphics.get_pad_shapes_as_polygons(pads, layer)

    def hit_test(self, item, position, tolerance: int = 0):
        """Perform a hit test on an item."""
        return self.graphics.hit_test(item, position, tolerance)

    # Connector-pad set (multi-board metadata on BOARD::m_connectorPads)
    def get_connector_pads(self):
        """Return the list of pad UUIDs marked as connector pads on this board.

        Connector pads are pads that connect to other sub-projects in a
        multi-board container. The returned list contains UUID strings.
        """
        from kipy.proto.board import board_commands_pb2
        cmd = board_commands_pb2.GetConnectorPads()
        cmd.board.CopyFrom(self._doc)
        response = self._kicad.send(cmd, board_commands_pb2.ConnectorPadsResponse)
        return [u.value for u in response.pad_uuids]

    def update_connector_pad_set(self, add=None, remove=None):
        """Add and/or remove pad UUIDs from the connector-pad set.

        Args:
            add: iterable of pad UUID strings to mark as connector pads.
            remove: iterable of pad UUID strings to unmark.

        Returns:
            (added, removed) — counts of pads whose state actually changed.
            Idempotent ops (already in the requested state) are not counted.
        """
        from kipy.proto.board import board_commands_pb2
        from kipy.proto.common.types import base_types_pb2

        cmd = board_commands_pb2.UpdateConnectorPadSet()
        cmd.board.CopyFrom(self._doc)

        for u in (add or []):
            kiid = base_types_pb2.KIID()
            kiid.value = str(u)
            cmd.add.append(kiid)

        for u in (remove or []):
            kiid = base_types_pb2.KIID()
            kiid.value = str(u)
            cmd.remove.append(kiid)

        response = self._kicad.send(cmd, board_commands_pb2.UpdateConnectorPadSetResponse)
        return (response.added, response.removed)

    # View operations
    def get_editor_appearance_settings(self):
        """Get the board editor appearance settings."""
        return self.view.get_appearance_settings()

    def set_editor_appearance_settings(self, settings):
        """Set the board editor appearance settings."""
        self.view.set_appearance_settings(settings)

    def interactive_move(self, items):
        """Initiate an interactive move operation."""
        self.view.interactive_move(items)

    def test_diff_view(self, bounding_box=None):
        """Show a diff overlay for testing."""
        self.view.show_diff_overlay(bounding_box)

    # DRC operations
    def run_drc_ipc(self, refill_zones: bool = False, report_all_track_errors: bool = False,
                    test_footprints: bool = False):
        """Run DRC via IPC API."""
        return self.drc.run(refill_zones, report_all_track_errors, test_footprints)

    def get_drc_violations(self, severities=None):
        """Get current DRC violations."""
        return self.drc.get_violations(severities)

    def clear_drc_markers(self, clear_violations: bool = True, clear_exclusions: bool = False):
        """Clear DRC markers."""
        self.drc.clear_markers(clear_violations, clear_exclusions)

    def get_drc_settings(self):
        """Get DRC check severity settings."""
        return self.drc.get_settings()

    def set_drc_settings(self, settings):
        """Set DRC check severity settings."""
        self.drc.set_settings(settings)

    # Design rules operations
    def get_design_rules(self):
        """Get design rules."""
        return self.design_rules.get()

    def set_design_rules(self, rules):
        """Set design rules."""
        return self.design_rules.set(rules)

    # Grid operations
    def get_grid_settings(self):
        """Get grid settings."""
        return self.grid.get_settings()

    def set_grid_settings(self, grid_size_x_nm=None, grid_size_y_nm=None,
                          show_grid=None, style=None):
        """Set grid settings."""
        self.grid.set_settings(grid_size_x_nm, grid_size_y_nm, show_grid, style)

    # Zone operations
    def refill_zones(self, block=True, max_poll_seconds: float = 30.0,
                     poll_interval_seconds: float = 0.5):
        """Refill all zones on the board."""
        self.zones.refill(block, max_poll_seconds, poll_interval_seconds)

    def add_zone(self, outline, layers, net=None, name: str = "", priority: int = 0):
        """Create a copper zone."""
        return self.zones.add(outline, layers, net, name, priority)

    # Routing operations
    def route_track(self, points, width, layer=None, net=None):
        """Create a track path connecting multiple points."""
        return self.routing.route(points, width, layer, net)

    def add_via(self, position, diameter, drill, net=None, via_type=None):
        """Add a via at the given position."""
        return self.routing.add_via(position, diameter, drill, net, via_type)

    # Export operations
    def get_as_string(self):
        """Get the board as a KiCad S-expression string."""
        return self.export.get_as_string()

    def from_string(self, contents):
        """Create board items from a KiCad S-expression string."""
        return self.export.from_string(contents)

    def run_drc(self, output_path, format: str = "report", units: str = "mm"):
        """Run DRC via kicad-cli."""
        return self.export.run_drc_cli(output_path, format, units)

    def generate_gerbers(self, output_dir, layers=None, no_x2: bool = False,
                         use_drill_origin: bool = True, subtract_soldermask: bool = False):
        """Generate Gerber files via kicad-cli."""
        return self.export.generate_gerbers(output_dir, layers, no_x2,
                                           use_drill_origin, subtract_soldermask)

    def generate_drill_files(self, output_dir, format: str = "excellon", units: str = "mm",
                             generate_map: bool = False, map_format: str = "pdf",
                             use_drill_origin: bool = True):
        """Generate drill files via kicad-cli."""
        return self.export.generate_drill_files(output_dir, format, units,
                                                generate_map, map_format, use_drill_origin)

    # Additional operations
    def check_padstack_presence_on_layers(self, items, layers):
        """Check if items with padstacks have content on given layers."""
        return self.graphics.check_padstack_presence_on_layers(items, layers)

    # Sync operations
    def update_from_schematic(self, dry_run: bool = False, **kwargs):
        """Update PCB from the associated schematic."""
        return self.sync.update_from_schematic(dry_run=dry_run, **kwargs)
