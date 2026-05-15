# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Schematic operations package.

This package provides a modular interface for schematic operations,
organized into logical sub-modules:

- crud: Create, read, update, delete operations
- symbols: Symbol placement, search, move, copy, rotate, duplicate, replace
- wiring: Wire creation, pin-based wiring, auto-routing
- labels: Net labels, power symbols, net naming, net management
- sheets: Hierarchical sheet management
- erc: Electrical rules checking and validation
- graphics: Text, shapes, annotations
- page: Page settings, title block, grid operations
- library: Symbol library management and search
- selection: Editor selection operations
- connectivity: Net and bus queries
- transform: Batch transform operations (move, rotate, mirror, align, distribute)
- buses: Bus definition, routing, and analysis

Example usage:
    >>> from kipy import KiCad
    >>> kicad = KiCad()
    >>> sch = kicad.get_schematic()
    >>>
    >>> # Add symbols
    >>> r1 = sch.symbols.add("Device:R", pos1)
    >>> r2 = sch.symbols.add("Device:R", pos2)
    >>>
    >>> # Wire them together
    >>> sch.wiring.wire_pins(r1, "2", r2, "1")
    >>>
    >>> # Add power symbols
    >>> gnd = sch.labels.add_power("GND", gnd_pos)
    >>>
    >>> # Run ERC
    >>> result = sch.erc.run()
    >>>
    >>> # Save
    >>> sch.save()
"""

from kipy.schematic.base import Schematic
from kipy.schematic.crud import CRUDOperations
from kipy.schematic.symbols import SymbolOperations
from kipy.schematic.wiring import WiringOperations
from kipy.schematic.labels import LabelOperations
from kipy.schematic.sheets import SheetOperations
from kipy.schematic.erc import ERCOperations
from kipy.schematic.graphics import GraphicsOperations
from kipy.schematic.page import PageOperations
from kipy.schematic.library import LibraryOperations, LibraryInfo, SymbolInfo
from kipy.schematic.selection import SelectionOperations
from kipy.schematic.connectivity import ConnectivityOperations
from kipy.schematic.transform import TransformOperations
from kipy.schematic.buses import BusOperations, BusDefinition
from kipy.schematic import items

__all__ = [
    # Main class
    "Schematic",
    # Operations classes
    "CRUDOperations",
    "SymbolOperations",
    "WiringOperations",
    "LabelOperations",
    "SheetOperations",
    "ERCOperations",
    "GraphicsOperations",
    "PageOperations",
    "LibraryOperations",
    "LibraryInfo",
    "SymbolInfo",
    "SelectionOperations",
    "ConnectivityOperations",
    "TransformOperations",
    "BusOperations",
    "BusDefinition",
    # Items module
    "items",
]
