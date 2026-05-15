# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Convenience imports for schematic item types.

This module provides a convenient way to import all schematic item types
from a single location:

    from kipy.schematic.items import Wire, Junction, Symbol, ...

.. versionadded:: 0.4.0
"""

from kipy.schematic_types import (
    Wire,
    LocalLabel,
    GlobalLabel,
    HierarchicalLabel,
    DirectiveLabel,
    Pin,
    Field,
    Symbol,
    SheetPin,
    Sheet,
    SchematicText,
    SchematicGraphicShape,
    SchematicTextBox,
)

# Also expose the layer constants
from kipy.schematic_types import SchematicLayer

# Try to import Junction and NoConnect if they exist
try:
    from kipy.schematic_types import Junction
except ImportError:
    Junction = None

try:
    from kipy.schematic_types import NoConnect
except ImportError:
    NoConnect = None

try:
    from kipy.schematic_types import Bus
except ImportError:
    Bus = None

try:
    from kipy.schematic_types import BusEntry
except ImportError:
    BusEntry = None

try:
    from kipy.schematic_types import Table
except ImportError:
    Table = None

try:
    from kipy.schematic_types import SchematicGroup
except ImportError:
    SchematicGroup = None

try:
    from kipy.schematic_types import Bitmap
except ImportError:
    Bitmap = None

__all__ = [
    # Core items
    "Wire",
    "LocalLabel",
    "GlobalLabel",
    "HierarchicalLabel",
    "DirectiveLabel",
    "Pin",
    "Field",
    "Symbol",
    "SheetPin",
    "Sheet",
    "SchematicText",
    "SchematicGraphicShape",
    "SchematicTextBox",
    "SchematicLayer",
    # Optional items (may be None if not available)
    "Junction",
    "NoConnect",
    "Bus",
    "BusEntry",
    "Table",
    "SchematicGroup",
    "Bitmap",
]
