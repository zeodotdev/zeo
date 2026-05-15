# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Board module - backwards compatibility shim.

This module re-exports everything from the new kipy.board package
to maintain backwards compatibility with existing code that imports
from `kipy.board` directly.

For new code, prefer importing directly from the package:
    from kipy.board import Board
    from kipy.board import BoardLayer, BoardLayerClass

The Board class now has a modular structure with sub-modules accessible
as properties:
    board.crud - Create, read, update, delete operations
    board.footprints - Footprint operations
    board.routing - Track and via routing
    board.zones - Copper zone management
    board.layers - Stackup and layer management
    board.nets - Net queries
    board.drc - DRC operations
    board.design_rules - Design rule constraints
    board.selection - Selection operations
    board.page - Page settings and title block
    board.graphics - Shapes, text, dimensions
    board.grid - Grid settings
    board.view - Appearance settings
    board.export - Gerber, drill, string export
    board.document_ops - Save, revert, refresh
    board.sync - Update PCB from schematic
    board.library - Footprint library browsing
    board.connectivity - Ratsnest and connection queries
    board.groups - Group management
"""

# Re-export everything from the new package for backwards compatibility
from kipy.board import (
    # Main class
    Board,
    # Operations classes (for advanced use)
    CRUDOperations,
    FootprintOperations,
    RoutingOperations,
    ZoneOperations,
    LayerOperations,
    NetOperations,
    DRCOperations,
    DesignRulesOperations,
    SelectionOperations,
    PageOperations,
    GraphicsOperations,
    GridOperations,
    ViewOperations,
    ExportOperations,
    DocumentOperations,
    SyncOperations,
    FootprintLibraryOperations,
    ConnectivityOperations,
    GroupOperations,
    # Type wrappers
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
    PCBUpdateChange,
    PCBUpdateResult,
    # Library types
    FootprintInfo,
    FootprintDetails,
    PadInfo,
    # Connectivity types
    RatsnestLine,
    UnroutedNetInfo,
    ItemConnectivity,
    # Group types
    GroupInfo,
    # Enums
    BoardLayerClass,
    BoardLayer,
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
