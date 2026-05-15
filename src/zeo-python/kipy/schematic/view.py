# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Viewport and view control operations.
"""

from typing import TYPE_CHECKING, Optional, List, Dict

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2
from kipy.geometry import Vector2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class ViewOperations:
    """Viewport control and zoom operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Viewport Operations
    # =========================================================================

    def get_viewport(self) -> Dict:
        """Get current viewport settings.

        Returns:
            Dict with center (Vector2), scale (float), and visible_area

        Example:
            >>> viewport = sch.view.get_viewport()
            >>> print(f"Center: {viewport['center']}")
            >>> print(f"Scale: {viewport['scale']}")
        """
        cmd = schematic_commands_pb2.GetViewport()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetViewportResponse)

        viewport = response.viewport

        # Build visible_area dict with defensive checks for unset fields
        visible_area = {}
        if viewport.HasField('visible_area'):
            va = viewport.visible_area
            if va.HasField('position'):
                visible_area['x'] = va.position.x_nm
                visible_area['y'] = va.position.y_nm
            else:
                visible_area['x'] = 0
                visible_area['y'] = 0
            if va.HasField('size'):
                visible_area['width'] = va.size.x_nm
                visible_area['height'] = va.size.y_nm
            else:
                visible_area['width'] = 0
                visible_area['height'] = 0
        else:
            visible_area = {'x': 0, 'y': 0, 'width': 0, 'height': 0}

        return {
            'center': Vector2(viewport.center) if viewport.HasField('center') else Vector2.from_xy(0, 0),
            'scale': viewport.scale,
            'visible_area': visible_area
        }

    def set_viewport(
        self,
        center: Optional[Vector2] = None,
        scale: Optional[float] = None,
    ) -> None:
        """Set viewport position and/or zoom level.

        Args:
            center: New center position (Vector2)
            scale: New zoom scale (higher = more zoomed in)

        Example:
            >>> from kipy.geometry import Vector2
            >>> sch.view.set_viewport(center=Vector2.from_xy_mm(100, 50), scale=1.5)
        """
        cmd = schematic_commands_pb2.SetViewport()
        cmd.document.CopyFrom(self._doc)

        if center is not None:
            cmd.center.x_nm = center.x
            cmd.center.y_nm = center.y

        if scale is not None:
            cmd.scale = scale

        self._kicad.send(cmd, Empty)

    def zoom_to_fit(self) -> None:
        """Zoom to fit all schematic content.

        Example:
            >>> sch.view.zoom_to_fit()
        """
        cmd = schematic_commands_pb2.ZoomToFit()
        cmd.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)

    def zoom_to_items(
        self,
        items: List,
        margin_ratio: float = 0.1,
    ) -> None:
        """Zoom to fit specific items with optional margin.

        Args:
            items: List of items (must have 'id' attribute)
            margin_ratio: Margin as ratio of bounds (default 0.1 = 10%)

        Example:
            >>> symbols = sch.symbols.get_by_ref("U1")
            >>> sch.view.zoom_to_items([symbols], margin_ratio=0.2)
        """
        cmd = schematic_commands_pb2.ZoomToItems()
        cmd.document.CopyFrom(self._doc)

        for item in items:
            if hasattr(item, 'id') and hasattr(item.id, 'value'):
                kiid = cmd.item_ids.add()
                kiid.value = item.id.value

        cmd.margin_ratio = margin_ratio
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Highlighting Operations
    # =========================================================================

    def highlight_net(self, net_name: str) -> None:
        """Highlight all items connected to a specific net.

        Args:
            net_name: Name of the net to highlight

        Example:
            >>> sch.view.highlight_net("VCC")
        """
        cmd = schematic_commands_pb2.HighlightNet()
        cmd.document.CopyFrom(self._doc)
        cmd.net_name = net_name
        self._kicad.send(cmd, Empty)

    def clear_highlight(self) -> None:
        """Clear all net highlighting.

        Example:
            >>> sch.view.clear_highlight()
        """
        cmd = schematic_commands_pb2.ClearHighlight()
        cmd.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Cross-Probe Operations
    # =========================================================================

    def cross_probe_to_board(
        self,
        items: List,
        select_in_board: bool = True,
        center_view: bool = True,
    ) -> Dict:
        """Cross-probe items to the PCB editor.

        Args:
            items: List of items to cross-probe (usually symbols)
            select_in_board: If True, select corresponding items in board
            center_view: If True, center board view on items

        Returns:
            Dict with 'board_found' (bool) and 'items_found' (int)

        Example:
            >>> symbol = sch.symbols.get_by_ref("U1")
            >>> result = sch.view.cross_probe_to_board([symbol])
            >>> if result['board_found']:
            ...     print(f"Found {result['items_found']} items in board")
        """
        cmd = schematic_commands_pb2.CrossProbeToBoard()
        cmd.document.CopyFrom(self._doc)

        for item in items:
            if hasattr(item, 'id') and hasattr(item.id, 'value'):
                kiid = cmd.item_ids.add()
                kiid.value = item.id.value

        cmd.select_in_board = select_in_board
        cmd.center_view = center_view

        response = self._kicad.send(cmd, schematic_commands_pb2.CrossProbeResponse)

        return {
            'board_found': response.board_found,
            'items_found': response.items_found,
        }

    def cross_probe_from_board(
        self,
        references: Optional[List[str]] = None,
        net_names: Optional[List[str]] = None,
        select_items: bool = True,
        highlight_nets: bool = False,
        center_view: bool = True,
        clear_existing_selection: bool = True,
    ) -> Dict:
        """Receive cross-probe from the PCB editor to schematic.

        Finds symbols by reference designator and/or highlights nets.
        This is typically called in response to selections made in the PCB editor.

        Args:
            references: List of reference designators to find (e.g., ["R1", "U1"])
            net_names: List of net names to highlight (e.g., ["VCC", "GND"])
            select_items: If True, select the found symbols
            highlight_nets: If True, highlight the specified nets
            center_view: If True, center schematic view on first found symbol
            clear_existing_selection: If True, clear selection before selecting

        Returns:
            Dict with:
                - 'symbols_found': Number of symbols found by reference
                - 'nets_found': Number of nets found
                - 'found_symbol_ids': List of KIID strings for found symbols

        Example:
            >>> # Find and select U1 and R1 from board selection
            >>> result = sch.view.cross_probe_from_board(
            ...     references=["U1", "R1"],
            ...     select_items=True,
            ...     center_view=True
            ... )
            >>> print(f"Found {result['symbols_found']} symbols")

            >>> # Highlight VCC net from board
            >>> result = sch.view.cross_probe_from_board(
            ...     net_names=["VCC"],
            ...     highlight_nets=True
            ... )
        """
        cmd = schematic_commands_pb2.CrossProbeFromBoard()
        cmd.document.CopyFrom(self._doc)

        if references:
            cmd.references.extend(references)

        if net_names:
            cmd.net_names.extend(net_names)

        cmd.select_items = select_items
        cmd.highlight_nets = highlight_nets
        cmd.center_view = center_view
        cmd.clear_existing_selection = clear_existing_selection

        response = self._kicad.send(
            cmd, schematic_commands_pb2.CrossProbeFromBoardResponse
        )

        return {
            'symbols_found': response.symbols_found,
            'nets_found': response.nets_found,
            'found_symbol_ids': [kiid.value for kiid in response.found_symbol_ids],
        }

    # =========================================================================
    # Undo/Redo Operations
    # =========================================================================

    def get_undo_history(self) -> Dict:
        """Get undo/redo history.

        Returns:
            Dict with 'undo_stack' and 'redo_stack' lists

        Example:
            >>> history = sch.view.get_undo_history()
            >>> print(f"Can undo {len(history['undo_stack'])} actions")
        """
        cmd = schematic_commands_pb2.GetUndoHistory()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetUndoHistoryResponse)

        undo_stack = [
            {'description': info.description, 'item_count': info.item_count}
            for info in response.undo_stack
        ]
        redo_stack = [
            {'description': info.description, 'item_count': info.item_count}
            for info in response.redo_stack
        ]

        return {
            'undo_stack': undo_stack,
            'redo_stack': redo_stack,
        }

    def undo(self, count: int = 1) -> int:
        """Undo one or more actions.

        Args:
            count: Number of actions to undo

        Returns:
            Number of actions actually undone

        Example:
            >>> undone = sch.view.undo(3)
            >>> print(f"Undid {undone} actions")
        """
        cmd = schematic_commands_pb2.Undo()
        cmd.document.CopyFrom(self._doc)
        cmd.count = count
        response = self._kicad.send(cmd, schematic_commands_pb2.UndoResponse)
        return response.steps_undone

    def redo(self, count: int = 1) -> int:
        """Redo one or more actions.

        Args:
            count: Number of actions to redo

        Returns:
            Number of actions actually redone

        Example:
            >>> redone = sch.view.redo(2)
            >>> print(f"Redid {redone} actions")
        """
        cmd = schematic_commands_pb2.Redo()
        cmd.document.CopyFrom(self._doc)
        cmd.count = count
        response = self._kicad.send(cmd, schematic_commands_pb2.RedoResponse)
        return response.steps_redone
