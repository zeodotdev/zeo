# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Schematic IPC API tests.

This package contains tests for each schematic module, mirroring the
structure of kipy.schematic.
"""

from kipy.testing.schematic.test_symbols import test_symbols, test_symbol_coordinates
from kipy.testing.schematic.test_wiring import test_wiring, test_wire_coordinates
from kipy.testing.schematic.test_labels import test_labels, test_label_coordinates
from kipy.testing.schematic.test_buses import test_buses
from kipy.testing.schematic.test_graphics import test_graphics
from kipy.testing.schematic.test_selection import test_selection
from kipy.testing.schematic.test_crud import test_crud
from kipy.testing.schematic.test_page import test_page
from kipy.testing.schematic.test_sheets import test_sheets
from kipy.testing.schematic.test_connectivity import test_connectivity
from kipy.testing.schematic.test_erc import test_erc
from kipy.testing.schematic.test_library import test_library
from kipy.testing.schematic.test_document import test_document
from kipy.testing.schematic.test_view import test_view
from kipy.testing.schematic.test_export import test_export
from kipy.testing.schematic.test_simulation import test_simulation
from kipy.testing.schematic.test_transform import test_transform
from kipy.testing.schematic.test_design_blocks import test_design_blocks

__all__ = [
    "test_symbols",
    "test_symbol_coordinates",
    "test_wiring",
    "test_wire_coordinates",
    "test_labels",
    "test_label_coordinates",
    "test_buses",
    "test_graphics",
    "test_selection",
    "test_crud",
    "test_page",
    "test_sheets",
    "test_connectivity",
    "test_erc",
    "test_library",
    "test_document",
    "test_view",
    "test_export",
    "test_simulation",
    "test_transform",
    "test_design_blocks",
]
