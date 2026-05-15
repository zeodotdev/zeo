# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Hierarchical sheet operations.
"""

from typing import TYPE_CHECKING, Optional

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2
from kipy.geometry import Vector2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class SheetOperations:
    """Hierarchical sheet operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Sheet Hierarchy Navigation
    # =========================================================================

    def get_hierarchy(self):
        """Get the full sheet hierarchy tree.

        Returns:
            SheetHierarchyNode: Root node of the hierarchy

        Example:
            >>> hierarchy = sch.sheets.get_hierarchy()
            >>> print(f"Root: {hierarchy.name}")
            >>> for child in hierarchy.children:
            ...     print(f"  Sheet: {child.name}")
        """
        cmd = schematic_commands_pb2.GetSheetHierarchy()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetSheetHierarchyResponse)
        return response.root

    def get_current(self):
        """Get information about the current sheet.

        Returns:
            Tuple of (SheetPath, Sheet)

        Example:
            >>> path, info = sch.sheets.get_current()
        """
        cmd = schematic_commands_pb2.GetCurrentSheet()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetCurrentSheetResponse)
        return (response.current_sheet, response.sheet_info)

    def navigate_to(self, sheet_path):
        """Navigate to a specific sheet.

        Args:
            sheet_path: SheetPath proto message

        Example:
            >>> hierarchy = sch.sheets.get_hierarchy()
            >>> if hierarchy.children:
            ...     sch.sheets.navigate_to(hierarchy.children[0].path)
        """
        cmd = schematic_commands_pb2.NavigateToSheet()
        cmd.document.CopyFrom(self._doc)
        cmd.target_sheet.CopyFrom(sheet_path)
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Sheet CRUD Operations
    # =========================================================================

    def create(
        self,
        name: str,
        filename: str,
        position: Vector2,
        size: Vector2,
        create_file: bool = True,
        parent_sheet_path: Optional[str] = None,
    ):
        """Create a new hierarchical sheet.

        Args:
            name: Display name for the sheet
            filename: Filename (e.g., "subsheet.kicad_sch")
            position: Position of sheet symbol on parent
            size: Size of sheet symbol
            create_file: If True (default), also create the .kicad_sch file.
                        Set to False only if the file already exists.
            parent_sheet_path: Human-readable path of the parent sheet
                (e.g., "/" for root, "/Power Supply/"). If None, uses the
                current agent target sheet.

        Returns:
            CreateSheetResponse with:
            - sheet_id: KIID of the created sheet
            - new_sheet_path: SheetPath to the new sheet

        Note:
            If create_file is False and the file doesn't exist, KiCad will
            show an error when trying to load the schematic. Always use
            create_file=True unless referencing an existing schematic file.

        Example:
            >>> response = sch.sheets.create(
            ...     name="Power Supply",
            ...     filename="power.kicad_sch",
            ...     position=Vector2.from_xy_mm(50, 50),
            ...     size=Vector2.from_xy_mm(30, 20),
            ...     parent_sheet_path="/",
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
        if parent_sheet_path is not None:
            cmd.parent_sheet_path = parent_sheet_path
        return self._kicad.send(cmd, schematic_commands_pb2.CreateSheetResponse)

    def delete(self, sheet_id: str, delete_file: bool = False):
        """Delete a hierarchical sheet.

        Args:
            sheet_id: KIID of the sheet
            delete_file: If True, also delete the .kicad_sch file
        """
        cmd = schematic_commands_pb2.DeleteSheet()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        cmd.delete_file = delete_file
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Sheet Properties
    # =========================================================================

    def get_properties(self, sheet_id: str):
        """Get properties of a sheet.

        Args:
            sheet_id: KIID of the sheet

        Returns:
            GetSheetPropertiesResponse
        """
        cmd = schematic_commands_pb2.GetSheetProperties()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetSheetPropertiesResponse)

    def set_properties(
        self,
        sheet_id: str,
        name: Optional[str] = None,
        filename: Optional[str] = None,
        page_number: Optional[str] = None,
    ):
        """Set properties of a sheet.

        Args:
            sheet_id: KIID of the sheet
            name: New display name (optional)
            filename: New filename (optional)
            page_number: New page number (optional)
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
    # Sheet Pin Operations
    # =========================================================================

    def create_pin(
        self,
        sheet_id: str,
        name: str,
        position: Vector2,
        shape: int = 0,
        side: int = 0,
    ):
        """Create a pin on a hierarchical sheet.

        Args:
            sheet_id: KIID of the sheet
            name: Pin name
            position: Position on the sheet edge
            shape: LabelShape enum value
            side: SheetPinSide (0=LEFT, 1=RIGHT, 2=TOP, 3=BOTTOM)

        Returns:
            CreateSheetPinResponse with pin_id
        """
        cmd = schematic_commands_pb2.CreateSheetPin()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        cmd.name = name
        cmd.position.x_nm = position.x
        cmd.position.y_nm = position.y
        cmd.shape = shape
        cmd.side = side
        return self._kicad.send(cmd, schematic_commands_pb2.CreateSheetPinResponse)

    def delete_pin(self, pin_id: str):
        """Delete a sheet pin.

        Args:
            pin_id: KIID of the pin
        """
        cmd = schematic_commands_pb2.DeleteSheetPin()
        cmd.document.CopyFrom(self._doc)
        cmd.pin_id.value = pin_id
        self._kicad.send(cmd, Empty)

    def get_pins(self, sheet_id: str):
        """Get all pins on a sheet.

        Args:
            sheet_id: KIID of the sheet

        Returns:
            GetSheetPinsResponse with list of pins
        """
        cmd = schematic_commands_pb2.GetSheetPins()
        cmd.document.CopyFrom(self._doc)
        cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetSheetPinsResponse)

    def sync_pins(self, sheet_id: Optional[str] = None):
        """Synchronize sheet pins with hierarchical labels.

        Args:
            sheet_id: KIID of sheet to sync (or None for all)

        Returns:
            SyncSheetPinsResponse with counts
        """
        cmd = schematic_commands_pb2.SyncSheetPins()
        cmd.document.CopyFrom(self._doc)
        if sheet_id is not None:
            cmd.sheet_id.value = sheet_id
        return self._kicad.send(cmd, schematic_commands_pb2.SyncSheetPinsResponse)
