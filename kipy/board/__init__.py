# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Board operations package.

This package provides a modular interface for PCB board operations,
organized into logical sub-modules:

- crud: Create, read, update, delete operations
- footprints: Footprint placement, search, move, rotate
- routing: Track and via creation, routing helpers
- zones: Copper zone and rule area management
- layers: Stackup, enabled/visible layer management
- nets: Net queries and net class management
- drc: Design rule checking, settings, violations
- design_rules: Board design constraints
- selection: Editor selection operations
- page: Page settings, title block, origin
- graphics: Shapes, text, dimensions, bounding boxes
- grid: Grid settings
- view: Appearance settings, active layer
- export: Gerber, drill, DRC reports
- document: Save, open, close, revert
- sync: Update PCB from schematic
- library: Footprint library browsing and search
- connectivity: Ratsnest and unrouted connection queries
- groups: Group management operations

Example usage:
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

from kipy.board.base import Board
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
from kipy.board.library import FootprintLibraryOperations, FootprintInfo, FootprintDetails, PadInfo
from kipy.board.connectivity import ConnectivityOperations, RatsnestLine, UnroutedNetInfo, ItemConnectivity
from kipy.board.groups import GroupOperations, GroupInfo

# Types
from kipy.board.types import (
    BoardLayerGraphicsDefaults,
    BoardStackup,
    BoardStackupLayer,
    BoardStackupDielectricLayer,
    BoardStackupDielectricProperties,
    BoardDesignRules,
    DRCViolation,
    DRCCheckSeverity,
    DRCSettings,
    PCBGridSettings,
)
from kipy.board.sync import PCBUpdateChange, PCBUpdateResult

# Re-export enums from proto for convenience
from kipy.proto.board.board_pb2 import BoardLayerClass
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.proto.board.board_commands_pb2 import (
    BoardOriginType,
    GridStyle,
    DrcSeverity,
)

__all__ = [
    # Main class
    "Board",
    # Operations classes
    "CRUDOperations",
    "FootprintOperations",
    "RoutingOperations",
    "ZoneOperations",
    "LayerOperations",
    "NetOperations",
    "DRCOperations",
    "DesignRulesOperations",
    "SelectionOperations",
    "PageOperations",
    "GraphicsOperations",
    "GridOperations",
    "ViewOperations",
    "ExportOperations",
    "DocumentOperations",
    "SyncOperations",
    "FootprintLibraryOperations",
    "ConnectivityOperations",
    "GroupOperations",
    # Type wrappers
    "BoardLayerGraphicsDefaults",
    "BoardStackup",
    "BoardStackupLayer",
    "BoardStackupDielectricLayer",
    "BoardStackupDielectricProperties",
    "BoardDesignRules",
    "DRCViolation",
    "DRCCheckSeverity",
    "DRCSettings",
    "PCBGridSettings",
    "PCBUpdateChange",
    "PCBUpdateResult",
    # Library types
    "FootprintInfo",
    "FootprintDetails",
    "PadInfo",
    # Connectivity types
    "RatsnestLine",
    "UnroutedNetInfo",
    "ItemConnectivity",
    # Group types
    "GroupInfo",
    # Enums
    "BoardLayerClass",
    "BoardLayer",
    "BoardOriginType",
    "GridStyle",
    "DrcSeverity",
]
