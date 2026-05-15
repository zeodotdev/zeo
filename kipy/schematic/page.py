# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Page settings and title block operations.
"""

from typing import TYPE_CHECKING, Optional, Tuple, Dict

from google.protobuf.empty_pb2 import Empty
from kipy.proto.common.commands import editor_commands_pb2
from kipy.proto.common.types import base_types_pb2
from kipy.proto.schematic import schematic_commands_pb2
from kipy.common_types import TitleBlockInfo, PageInfo

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class PageOperations:
    """Page settings and title block operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Page Settings
    # =========================================================================

    def get_settings(self) -> PageInfo:
        """Get page/paper settings.

        Returns:
            PageInfo with size_type, portrait, width_mm, height_mm

        Example:
            >>> page = sch.page.get_settings()
            >>> print(f"Size: {page.size_type_name}")
            >>> print(f"Dimensions: {page.width_mm}x{page.height_mm}mm")
        """
        cmd = editor_commands_pb2.GetPageSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, base_types_pb2.PageInfo)
        return PageInfo(response)

    def set_settings(
        self,
        size_type: Optional[int] = None,
        portrait: Optional[bool] = None,
        width_mm: Optional[float] = None,
        height_mm: Optional[float] = None,
    ) -> None:
        """Set page/paper settings.

        For standard sizes, use size_type from PageSizeType constants.
        For custom sizes, use size_type=PageSizeType.USER with dimensions.

        Args:
            size_type: Paper size (PageSizeType constant)
            portrait: True for portrait, False for landscape
            width_mm: Custom width (for USER size)
            height_mm: Custom height (for USER size)

        Example:
            >>> from kipy.common_types import PageSizeType
            >>> sch.page.set_settings(size_type=PageSizeType.A4, portrait=False)
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
    # Title Block
    # =========================================================================

    def get_title_block(self) -> TitleBlockInfo:
        """Get title block information.

        Returns:
            TitleBlockInfo with title, date, revision, company, comments

        Example:
            >>> tb = sch.page.get_title_block()
            >>> print(f"Title: {tb.title}")
            >>> print(f"Revision: {tb.revision}")
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
        comments: Optional[Dict[int, str]] = None,
    ) -> None:
        """Set title block information.

        Only provided fields are updated.

        Args:
            title: Document title
            date: Document date
            revision: Revision string
            company: Company name
            comments: Dict mapping comment number (1-9) to text

        Example:
            >>> sch.page.set_title_block(
            ...     title="My Schematic",
            ...     revision="1.0",
            ...     company="ACME Inc",
            ...     comments={1: "Author: John", 2: "Status: Draft"}
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
            for i, text in comments.items():
                if 1 <= i <= 9:
                    setattr(cmd.title_block, f"comment{i}", text)

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Grid Settings
    # =========================================================================

    def get_grid_settings(self) -> dict:
        """Get grid settings.

        Note: The API doesn't expose grid settings, so this returns defaults.

        Returns:
            Dict with size_mm, size_mils, size_nm, is_default

        Example:
            >>> grid = sch.page.get_grid_settings()
            >>> print(f"Grid: {grid['size_mils']} mils")
        """
        DEFAULT_GRID_MILS = 50
        DEFAULT_GRID_MM = 1.27
        DEFAULT_GRID_NM = 1270000

        return {
            'size_mm': DEFAULT_GRID_MM,
            'size_mils': DEFAULT_GRID_MILS,
            'size_nm': DEFAULT_GRID_NM,
            'is_default': True,
        }

    def snap_to_grid(
        self,
        x_mm: float,
        y_mm: float,
        grid_mm: float = 1.27,
    ) -> Tuple[float, float]:
        """Snap coordinates to grid (mm).

        Args:
            x_mm: X coordinate in mm
            y_mm: Y coordinate in mm
            grid_mm: Grid size in mm (default 1.27mm = 50 mils)

        Returns:
            Tuple of (snapped_x, snapped_y)

        Example:
            >>> x, y = sch.page.snap_to_grid(100.5, 75.3)
        """
        snapped_x = round(x_mm / grid_mm) * grid_mm
        snapped_y = round(y_mm / grid_mm) * grid_mm
        return (snapped_x, snapped_y)

    def snap_to_grid_mils(
        self,
        x_mils: float,
        y_mils: float,
        grid_mils: float = 50,
    ) -> Tuple[float, float]:
        """Snap coordinates to grid (mils).

        Args:
            x_mils: X coordinate in mils
            y_mils: Y coordinate in mils
            grid_mils: Grid size in mils (default 50)

        Returns:
            Tuple of (snapped_x, snapped_y)

        Example:
            >>> x, y = sch.page.snap_to_grid_mils(5100, 4050)
        """
        snapped_x = round(x_mils / grid_mils) * grid_mils
        snapped_y = round(y_mils / grid_mils) * grid_mils
        return (snapped_x, snapped_y)

    # =========================================================================
    # Usable Area
    # =========================================================================

    def get_usable_area(self) -> dict:
        """Get usable drawing area (mm).

        Accounts for title block and margins.

        Returns:
            Dict with min_x_mm, max_x_mm, min_y_mm, max_y_mm,
            width_mm, height_mm, center_x_mm, center_y_mm

        Example:
            >>> area = sch.page.get_usable_area()
            >>> print(f"Center: ({area['center_x_mm']}, {area['center_y_mm']})")
        """
        page = self.get_settings()

        LEFT_MARGIN = 20.0
        TOP_MARGIN = 20.0
        RIGHT_MARGIN = 37.0
        BOTTOM_MARGIN = 30.0

        left = LEFT_MARGIN
        top = TOP_MARGIN
        right = page.width_mm - RIGHT_MARGIN
        bottom = page.height_mm - BOTTOM_MARGIN

        return {
            'min_x_mm': left,
            'max_x_mm': right,
            'min_y_mm': top,
            'max_y_mm': bottom,
            'width_mm': right - left,
            'height_mm': bottom - top,
            'center_x_mm': (left + right) / 2,
            'center_y_mm': (top + bottom) / 2,
        }

    def get_usable_area_mils(self) -> dict:
        """Get usable drawing area (mils).

        Returns:
            Dict with min_x, max_x, min_y, max_y,
            width, height, center_x, center_y, grid

        Example:
            >>> area = sch.page.get_usable_area_mils()
            >>> print(f"Width: {area['width']} mils")
        """
        area_mm = self.get_usable_area()
        MM_TO_MIL = 1.0 / 0.0254

        return {
            "min_x": area_mm["min_x_mm"] * MM_TO_MIL,
            "max_x": area_mm["max_x_mm"] * MM_TO_MIL,
            "min_y": area_mm["min_y_mm"] * MM_TO_MIL,
            "max_y": area_mm["max_y_mm"] * MM_TO_MIL,
            "width": area_mm["width_mm"] * MM_TO_MIL,
            "height": area_mm["height_mm"] * MM_TO_MIL,
            "center_x": area_mm["center_x_mm"] * MM_TO_MIL,
            "center_y": area_mm["center_y_mm"] * MM_TO_MIL,
            "grid": 50,
        }

    # =========================================================================
    # Extended Grid Operations
    # =========================================================================

    def set_grid(
        self,
        grid_mm: float = None,
        grid_mils: float = None,
        visible: bool = None,
        snap_to_grid: bool = None,
    ) -> dict:
        """Set the grid size and options.

        Args:
            grid_mm: Grid size in millimeters
            grid_mils: Grid size in mils (1 mil = 0.0254mm)
            visible: Whether grid is visible
            snap_to_grid: Whether snapping is enabled

        Returns:
            Updated grid settings dict

        Example:
            >>> sch.page.set_grid(grid_mils=25)  # 25 mil grid
            >>> sch.page.set_grid(grid_mm=2.54, snap_to_grid=True)
        """
        cmd = schematic_commands_pb2.SetGridSettings()
        cmd.document.CopyFrom(self._doc)

        if grid_mm is not None:
            grid_nm = int(grid_mm * 1_000_000)
            cmd.grid_size_x_nm = grid_nm
            cmd.grid_size_y_nm = grid_nm
        elif grid_mils is not None:
            grid_nm = int(grid_mils * 0.0254 * 1_000_000)
            cmd.grid_size_x_nm = grid_nm
            cmd.grid_size_y_nm = grid_nm

        if visible is not None:
            cmd.grid_visible = visible

        if snap_to_grid is not None:
            cmd.snap_to_grid = snap_to_grid

        self._kicad.send(cmd, Empty)
        return self.get_grid_settings()

    def get_grid_settings(self) -> dict:
        """Get grid settings from KiCad.

        Returns:
            Dict with size_x_nm, size_y_nm, size_mm, visible, snap_to_grid

        Example:
            >>> grid = sch.page.get_grid_settings()
            >>> print(f"Grid: {grid['size_mm']:.2f} mm")
            >>> print(f"Snap enabled: {grid['snap_to_grid']}")
        """
        cmd = schematic_commands_pb2.GetGridSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetGridSettingsResponse)

        settings = response.settings
        size_x_nm = settings.grid_size.x_nm
        size_y_nm = settings.grid_size.y_nm

        return {
            'size_x_nm': size_x_nm,
            'size_y_nm': size_y_nm,
            'size_mm': size_x_nm / 1_000_000,
            'size_mils': size_x_nm / 1_000_000 / 0.0254,
            'visible': settings.grid_visible,
            'snap_to_grid': settings.snap_to_grid,
            'origin_x_nm': settings.grid_origin.x_nm,
            'origin_y_nm': settings.grid_origin.y_nm,
        }

    def get_common_grids(self) -> dict:
        """Get common grid sizes for schematic design.

        Returns:
            Dictionary of grid presets with mm and mils values

        Example:
            >>> grids = sch.page.get_common_grids()
            >>> sch.page.set_grid(grid_mm=grids['fine']['mm'])
        """
        return {
            "coarse": {"mm": 2.54, "mils": 100, "description": "100 mil - coarse placement"},
            "standard": {"mm": 1.27, "mils": 50, "description": "50 mil - standard (default)"},
            "fine": {"mm": 0.635, "mils": 25, "description": "25 mil - fine adjustment"},
            "very_fine": {"mm": 0.254, "mils": 10, "description": "10 mil - precise placement"},
        }

    def snap_point_to_grid(
        self,
        position,
        grid_mm: float = None,
    ):
        """Snap a Vector2 position to the grid.

        Args:
            position: Vector2 position to snap
            grid_mm: Optional grid size (uses current grid if not specified)

        Returns:
            New Vector2 snapped to grid

        Example:
            >>> from kipy.geometry import Vector2
            >>> pos = Vector2.from_xy_mm(100.5, 75.3)
            >>> snapped = sch.page.snap_point_to_grid(pos)
        """
        from kipy.geometry import Vector2

        if grid_mm is None:
            grid_mm = getattr(self, '_grid_mm', 1.27)

        x_mm = position.x / 1_000_000
        y_mm = position.y / 1_000_000

        snapped_x = round(x_mm / grid_mm) * grid_mm
        snapped_y = round(y_mm / grid_mm) * grid_mm

        return Vector2.from_xy_mm(snapped_x, snapped_y)

    def snap_items_to_grid(
        self,
        items,
        grid_mm: float = None,
    ):
        """Snap multiple items to the grid.

        Args:
            items: List of items with position attribute
            grid_mm: Optional grid size

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.get_all()
            >>> sch.page.snap_items_to_grid(symbols)
        """
        if grid_mm is None:
            grid_mm = getattr(self, '_grid_mm', 1.27)

        for item in items:
            if hasattr(item, 'position'):
                item.position = self.snap_point_to_grid(item.position, grid_mm)
            elif hasattr(item, 'start') and hasattr(item, 'end'):
                item.start = self.snap_point_to_grid(item.start, grid_mm)
                item.end = self.snap_point_to_grid(item.end, grid_mm)

        return self._sch.crud.update_items(items)

    def get_grid_positions(
        self,
        start_mm: Tuple[float, float],
        end_mm: Tuple[float, float],
        grid_mm: float = None,
    ):
        """Generate a list of grid-aligned positions within a range.

        Args:
            start_mm: Starting corner (x, y) in mm
            end_mm: Ending corner (x, y) in mm
            grid_mm: Optional grid size

        Returns:
            List of (x_mm, y_mm) tuples at grid intersections

        Example:
            >>> # Get grid points in a 100x50mm area
            >>> points = sch.page.get_grid_positions((100, 50), (200, 100))
            >>> for x, y in points[:10]:
            ...     print(f"  ({x:.1f}, {y:.1f})")
        """
        if grid_mm is None:
            grid_mm = getattr(self, '_grid_mm', 1.27)

        positions = []

        x_start = round(start_mm[0] / grid_mm) * grid_mm
        y_start = round(start_mm[1] / grid_mm) * grid_mm
        x_end = end_mm[0]
        y_end = end_mm[1]

        x = x_start
        while x <= x_end:
            y = y_start
            while y <= y_end:
                positions.append((x, y))
                y += grid_mm
            x += grid_mm

        return positions

    # =========================================================================
    # Layout Helpers
    # =========================================================================

    def calculate_layout_positions(
        self,
        count: int,
        arrangement: str = "horizontal",
        spacing_mm: float = 25.4,
        start_position_mm: Tuple[float, float] = None,
    ):
        """Calculate positions for laying out multiple components.

        Args:
            count: Number of positions needed
            arrangement: "horizontal", "vertical", or "grid"
            spacing_mm: Spacing between positions
            start_position_mm: Starting position (uses page center if None)

        Returns:
            List of (x_mm, y_mm) tuples

        Example:
            >>> # Get positions for 5 components in a row
            >>> positions = sch.page.calculate_layout_positions(5, "horizontal", 30)
            >>> for i, (x, y) in enumerate(positions):
            ...     sch.symbols.add("Device:R", Vector2.from_xy_mm(x, y))
        """
        if start_position_mm is None:
            area = self.get_usable_area()
            start_x = area['center_x_mm']
            start_y = area['center_y_mm']
        else:
            start_x, start_y = start_position_mm

        positions = []

        if arrangement == "horizontal":
            # Center the row
            total_width = (count - 1) * spacing_mm
            start_x -= total_width / 2

            for i in range(count):
                x = start_x + i * spacing_mm
                positions.append(self.snap_to_grid(x, start_y))

        elif arrangement == "vertical":
            # Center the column
            total_height = (count - 1) * spacing_mm
            start_y -= total_height / 2

            for i in range(count):
                y = start_y + i * spacing_mm
                positions.append(self.snap_to_grid(start_x, y))

        elif arrangement == "grid":
            import math
            cols = math.ceil(math.sqrt(count))
            rows = math.ceil(count / cols)

            total_width = (cols - 1) * spacing_mm
            total_height = (rows - 1) * spacing_mm
            grid_start_x = start_x - total_width / 2
            grid_start_y = start_y - total_height / 2

            for i in range(count):
                row = i // cols
                col = i % cols
                x = grid_start_x + col * spacing_mm
                y = grid_start_y + row * spacing_mm
                positions.append(self.snap_to_grid(x, y))

        return positions

    # =========================================================================
    # Editor Preferences
    # =========================================================================

    def get_editor_preferences(self) -> dict:
        """Get editor preferences.

        Returns:
            Dict with display and behavior settings including:
            - show_hidden_pins, show_hidden_fields, show_pin_numbers, show_pin_names
            - show_erc_errors, show_erc_warnings, show_erc_exclusions
            - auto_place_fields, auto_pan, center_on_zoom
            - default_wire_width_nm, default_bus_width_nm, default_junction_size_nm, default_text_size_nm

        Example:
            >>> prefs = sch.page.get_editor_preferences()
            >>> print(f"Show hidden pins: {prefs['show_hidden_pins']}")
            >>> print(f"Default wire width: {prefs['default_wire_width_nm']} nm")
        """
        cmd = schematic_commands_pb2.GetEditorPreferences()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetEditorPreferencesResponse)

        prefs = response.preferences
        return {
            # Display settings
            'show_hidden_pins': prefs.show_hidden_pins if prefs.HasField('show_hidden_pins') else None,
            'show_hidden_fields': prefs.show_hidden_fields if prefs.HasField('show_hidden_fields') else None,
            'show_pin_numbers': prefs.show_pin_numbers if prefs.HasField('show_pin_numbers') else None,
            'show_pin_names': prefs.show_pin_names if prefs.HasField('show_pin_names') else None,
            'show_erc_errors': prefs.show_erc_errors if prefs.HasField('show_erc_errors') else None,
            'show_erc_warnings': prefs.show_erc_warnings if prefs.HasField('show_erc_warnings') else None,
            'show_erc_exclusions': prefs.show_erc_exclusions if prefs.HasField('show_erc_exclusions') else None,
            # Behavior settings
            'auto_place_fields': prefs.auto_place_fields if prefs.HasField('auto_place_fields') else None,
            'auto_pan': prefs.auto_pan if prefs.HasField('auto_pan') else None,
            'auto_pan_acceleration': prefs.auto_pan_acceleration if prefs.HasField('auto_pan_acceleration') else None,
            'center_on_zoom': prefs.center_on_zoom if prefs.HasField('center_on_zoom') else None,
            # Drawing defaults
            'default_wire_width_nm': prefs.default_wire_width_nm if prefs.HasField('default_wire_width_nm') else None,
            'default_bus_width_nm': prefs.default_bus_width_nm if prefs.HasField('default_bus_width_nm') else None,
            'default_junction_size_nm': prefs.default_junction_size_nm if prefs.HasField('default_junction_size_nm') else None,
            'default_text_size_nm': prefs.default_text_size_nm if prefs.HasField('default_text_size_nm') else None,
        }

    def set_editor_preferences(
        self,
        show_hidden_pins: Optional[bool] = None,
        show_hidden_fields: Optional[bool] = None,
        show_pin_numbers: Optional[bool] = None,
        show_pin_names: Optional[bool] = None,
        show_erc_errors: Optional[bool] = None,
        show_erc_warnings: Optional[bool] = None,
        show_erc_exclusions: Optional[bool] = None,
        auto_place_fields: Optional[bool] = None,
        auto_pan: Optional[bool] = None,
        auto_pan_acceleration: Optional[int] = None,
        center_on_zoom: Optional[bool] = None,
        default_wire_width_nm: Optional[int] = None,
        default_bus_width_nm: Optional[int] = None,
        default_junction_size_nm: Optional[int] = None,
        default_text_size_nm: Optional[int] = None,
    ) -> None:
        """Set editor preferences.

        Only provided fields are updated.

        Args:
            show_hidden_pins: Show hidden symbol pins
            show_hidden_fields: Show hidden fields
            show_pin_numbers: Show pin numbers
            show_pin_names: Show pin names
            show_erc_errors: Show ERC error markers
            show_erc_warnings: Show ERC warning markers
            show_erc_exclusions: Show ERC exclusion markers
            auto_place_fields: Auto-place fields when adding symbols
            auto_pan: Enable auto-pan when dragging
            auto_pan_acceleration: Auto-pan acceleration factor
            center_on_zoom: Center view on zoom point
            default_wire_width_nm: Default wire width in nm
            default_bus_width_nm: Default bus width in nm
            default_junction_size_nm: Default junction size in nm
            default_text_size_nm: Default text size in nm

        Example:
            >>> sch.page.set_editor_preferences(
            ...     show_hidden_pins=True,
            ...     auto_place_fields=True,
            ...     default_wire_width_nm=6000
            ... )
        """
        cmd = schematic_commands_pb2.SetEditorPreferences()
        cmd.document.CopyFrom(self._doc)

        if show_hidden_pins is not None:
            cmd.preferences.show_hidden_pins = show_hidden_pins
        if show_hidden_fields is not None:
            cmd.preferences.show_hidden_fields = show_hidden_fields
        if show_pin_numbers is not None:
            cmd.preferences.show_pin_numbers = show_pin_numbers
        if show_pin_names is not None:
            cmd.preferences.show_pin_names = show_pin_names
        if show_erc_errors is not None:
            cmd.preferences.show_erc_errors = show_erc_errors
        if show_erc_warnings is not None:
            cmd.preferences.show_erc_warnings = show_erc_warnings
        if show_erc_exclusions is not None:
            cmd.preferences.show_erc_exclusions = show_erc_exclusions
        if auto_place_fields is not None:
            cmd.preferences.auto_place_fields = auto_place_fields
        if auto_pan is not None:
            cmd.preferences.auto_pan = auto_pan
        if auto_pan_acceleration is not None:
            cmd.preferences.auto_pan_acceleration = auto_pan_acceleration
        if center_on_zoom is not None:
            cmd.preferences.center_on_zoom = center_on_zoom
        if default_wire_width_nm is not None:
            cmd.preferences.default_wire_width_nm = default_wire_width_nm
        if default_bus_width_nm is not None:
            cmd.preferences.default_bus_width_nm = default_bus_width_nm
        if default_junction_size_nm is not None:
            cmd.preferences.default_junction_size_nm = default_junction_size_nm
        if default_text_size_nm is not None:
            cmd.preferences.default_text_size_nm = default_text_size_nm

        self._kicad.send(cmd, Empty)

    def get_design_bounds(self, margin_mm: float = 10):
        """Calculate the bounding box of all design elements.

        Args:
            margin_mm: Optional margin to add around bounds

        Returns:
            Dict with min_x, max_x, min_y, max_y, width, height (all in mm)
            or None if schematic is empty

        Example:
            >>> bounds = sch.page.get_design_bounds()
            >>> if bounds:
            ...     print(f"Design size: {bounds['width']:.1f} x {bounds['height']:.1f} mm")
        """
        symbols = self._sch.crud.get_symbols()
        wires = self._sch.crud.get_wires()
        labels = self._sch.crud.get_labels()

        min_x = float('inf')
        max_x = float('-inf')
        min_y = float('inf')
        max_y = float('-inf')

        found_any = False

        for item in symbols + list(labels):
            if hasattr(item, 'position'):
                found_any = True
                x = item.position.x / 1e6
                y = item.position.y / 1e6
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)

        for wire in wires:
            if hasattr(wire, 'start') and hasattr(wire, 'end'):
                found_any = True
                for pt in [wire.start, wire.end]:
                    x = pt.x / 1e6
                    y = pt.y / 1e6
                    min_x = min(min_x, x)
                    max_x = max(max_x, x)
                    min_y = min(min_y, y)
                    max_y = max(max_y, y)

        if not found_any:
            return None

        return {
            'min_x': min_x - margin_mm,
            'max_x': max_x + margin_mm,
            'min_y': min_y - margin_mm,
            'max_y': max_y + margin_mm,
            'width': max_x - min_x + 2 * margin_mm,
            'height': max_y - min_y + 2 * margin_mm,
            'center_x': (min_x + max_x) / 2,
            'center_y': (min_y + max_y) / 2,
        }

    # =========================================================================
    # Formatting Settings (Project-level settings from Schematic Setup)
    # =========================================================================

    def get_formatting_settings(self) -> dict:
        """Get schematic formatting settings (project-level).

        These are the settings from Schematic Setup -> General -> Formatting.

        Returns:
            Dict with all formatting settings including:
            - text: default_text_size_mils, label_offset_ratio, global_label_margin_ratio
            - symbols: default_line_width_mils, pin_symbol_size_mils
            - connections: junction_size_choice, hop_over_size_choice, connection_grid_mils
            - intersheet_refs: show, list_own_page, format_short, prefix, suffix
            - dashed_lines: dash_ratio, gap_ratio
            - opo: voltage_precision, voltage_range, current_precision, current_range

        Example:
            >>> settings = sch.page.get_formatting_settings()
            >>> print(f"Default text size: {settings['text']['default_text_size_mils']} mils")
            >>> print(f"Junction size: {settings['connections']['junction_size_choice']}")
        """
        cmd = schematic_commands_pb2.GetFormattingSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetFormattingSettingsResponse)

        s = response.settings
        return {
            'text': {
                'default_text_size_mils': s.default_text_size_mils if s.HasField('default_text_size_mils') else None,
                'overbar_offset_ratio': s.overbar_offset_ratio if s.HasField('overbar_offset_ratio') else None,
                'label_offset_ratio': s.label_offset_ratio if s.HasField('label_offset_ratio') else None,
                'global_label_margin_ratio': s.global_label_margin_ratio if s.HasField('global_label_margin_ratio') else None,
            },
            'symbols': {
                'default_line_width_mils': s.default_line_width_mils if s.HasField('default_line_width_mils') else None,
                'pin_symbol_size_mils': s.pin_symbol_size_mils if s.HasField('pin_symbol_size_mils') else None,
            },
            'connections': {
                'junction_size_choice': s.junction_size_choice if s.HasField('junction_size_choice') else None,
                'hop_over_size_choice': s.hop_over_size_choice if s.HasField('hop_over_size_choice') else None,
                'connection_grid_mils': s.connection_grid_mils if s.HasField('connection_grid_mils') else None,
            },
            'intersheet_refs': {
                'show': s.intersheet_refs_show if s.HasField('intersheet_refs_show') else None,
                'list_own_page': s.intersheet_refs_list_own_page if s.HasField('intersheet_refs_list_own_page') else None,
                'format_short': s.intersheet_refs_format_short if s.HasField('intersheet_refs_format_short') else None,
                'prefix': s.intersheet_refs_prefix if s.intersheet_refs_prefix else None,
                'suffix': s.intersheet_refs_suffix if s.intersheet_refs_suffix else None,
            },
            'dashed_lines': {
                'dash_ratio': s.dashed_line_dash_ratio if s.HasField('dashed_line_dash_ratio') else None,
                'gap_ratio': s.dashed_line_gap_ratio if s.HasField('dashed_line_gap_ratio') else None,
            },
            'opo': {
                'voltage_precision': s.opo_voltage_precision if s.HasField('opo_voltage_precision') else None,
                'voltage_range': s.opo_voltage_range if s.opo_voltage_range else None,
                'current_precision': s.opo_current_precision if s.HasField('opo_current_precision') else None,
                'current_range': s.opo_current_range if s.opo_current_range else None,
            },
        }

    def set_formatting_settings(
        self,
        # Text settings
        default_text_size_mils: Optional[int] = None,
        overbar_offset_ratio: Optional[float] = None,
        label_offset_ratio: Optional[float] = None,
        global_label_margin_ratio: Optional[float] = None,
        # Symbol settings
        default_line_width_mils: Optional[int] = None,
        pin_symbol_size_mils: Optional[int] = None,
        # Connection settings
        junction_size_choice: Optional[int] = None,
        hop_over_size_choice: Optional[int] = None,
        connection_grid_mils: Optional[int] = None,
        # Inter-sheet reference settings
        intersheet_refs_show: Optional[bool] = None,
        intersheet_refs_list_own_page: Optional[bool] = None,
        intersheet_refs_format_short: Optional[bool] = None,
        intersheet_refs_prefix: Optional[str] = None,
        intersheet_refs_suffix: Optional[str] = None,
        # Dashed lines settings
        dashed_line_dash_ratio: Optional[float] = None,
        dashed_line_gap_ratio: Optional[float] = None,
        # Operating-point overlay settings
        opo_voltage_precision: Optional[int] = None,
        opo_voltage_range: Optional[str] = None,
        opo_current_precision: Optional[int] = None,
        opo_current_range: Optional[str] = None,
    ) -> None:
        """Set schematic formatting settings (project-level).

        These are the settings from Schematic Setup -> General -> Formatting.
        Only provided fields are updated.

        Args:
            default_text_size_mils: Default text size in mils
            overbar_offset_ratio: Overbar offset as percentage (e.g., 123.0 = 123%)
            label_offset_ratio: Label offset as percentage
            global_label_margin_ratio: Global label margin as percentage
            default_line_width_mils: Default symbol line width in mils
            pin_symbol_size_mils: Pin symbol size in mils
            junction_size_choice: Junction dot size (0=none, 1=smallest, 2=small, etc.)
            hop_over_size_choice: Hop-over size (0=none, 1=smallest, etc.)
            connection_grid_mils: Connection grid size in mils
            intersheet_refs_show: Show inter-sheet references
            intersheet_refs_list_own_page: Show own page in references
            intersheet_refs_format_short: Use abbreviated format (1..3) vs standard (1,2,3)
            intersheet_refs_prefix: Prefix string for references
            intersheet_refs_suffix: Suffix string for references
            dashed_line_dash_ratio: Dash length as ratio of line width
            dashed_line_gap_ratio: Gap length as ratio of line width
            opo_voltage_precision: Voltage significant digits
            opo_voltage_range: Voltage range ("Auto", "V", "mV", etc.)
            opo_current_precision: Current significant digits
            opo_current_range: Current range ("Auto", "A", "mA", etc.)

        Example:
            >>> sch.page.set_formatting_settings(
            ...     default_text_size_mils=50,
            ...     default_line_width_mils=6,
            ...     junction_size_choice=2,  # small
            ...     intersheet_refs_show=True
            ... )
        """
        cmd = schematic_commands_pb2.SetFormattingSettings()
        cmd.document.CopyFrom(self._doc)

        # Text settings
        if default_text_size_mils is not None:
            cmd.settings.default_text_size_mils = default_text_size_mils
        if overbar_offset_ratio is not None:
            cmd.settings.overbar_offset_ratio = overbar_offset_ratio
        if label_offset_ratio is not None:
            cmd.settings.label_offset_ratio = label_offset_ratio
        if global_label_margin_ratio is not None:
            cmd.settings.global_label_margin_ratio = global_label_margin_ratio

        # Symbol settings
        if default_line_width_mils is not None:
            cmd.settings.default_line_width_mils = default_line_width_mils
        if pin_symbol_size_mils is not None:
            cmd.settings.pin_symbol_size_mils = pin_symbol_size_mils

        # Connection settings
        if junction_size_choice is not None:
            cmd.settings.junction_size_choice = junction_size_choice
        if hop_over_size_choice is not None:
            cmd.settings.hop_over_size_choice = hop_over_size_choice
        if connection_grid_mils is not None:
            cmd.settings.connection_grid_mils = connection_grid_mils

        # Inter-sheet reference settings
        if intersheet_refs_show is not None:
            cmd.settings.intersheet_refs_show = intersheet_refs_show
        if intersheet_refs_list_own_page is not None:
            cmd.settings.intersheet_refs_list_own_page = intersheet_refs_list_own_page
        if intersheet_refs_format_short is not None:
            cmd.settings.intersheet_refs_format_short = intersheet_refs_format_short
        if intersheet_refs_prefix is not None:
            cmd.settings.intersheet_refs_prefix = intersheet_refs_prefix
        if intersheet_refs_suffix is not None:
            cmd.settings.intersheet_refs_suffix = intersheet_refs_suffix

        # Dashed lines settings
        if dashed_line_dash_ratio is not None:
            cmd.settings.dashed_line_dash_ratio = dashed_line_dash_ratio
        if dashed_line_gap_ratio is not None:
            cmd.settings.dashed_line_gap_ratio = dashed_line_gap_ratio

        # Operating-point overlay settings
        if opo_voltage_precision is not None:
            cmd.settings.opo_voltage_precision = opo_voltage_precision
        if opo_voltage_range is not None:
            cmd.settings.opo_voltage_range = opo_voltage_range
        if opo_current_precision is not None:
            cmd.settings.opo_current_precision = opo_current_precision
        if opo_current_range is not None:
            cmd.settings.opo_current_range = opo_current_range

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Field Name Templates (Project-level settings from Schematic Setup)
    # =========================================================================

    def get_field_name_templates(self) -> list:
        """Get field name templates (project-level).

        These are the custom field templates from Schematic Setup -> General -> Field Name Templates.

        Returns:
            List of dicts, each with:
            - name: Field name string
            - visible: Default visibility (bool)
            - url: Has URL/browse button (bool)

        Example:
            >>> templates = sch.page.get_field_name_templates()
            >>> for t in templates:
            ...     print(f"{t['name']}: visible={t['visible']}, url={t['url']}")
        """
        cmd = schematic_commands_pb2.GetFieldNameTemplates()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetFieldNameTemplatesResponse)

        result = []
        for tmpl in response.templates:
            result.append({
                'name': tmpl.name,
                'visible': tmpl.visible,
                'url': tmpl.url,
            })
        return result

    def set_field_name_templates(self, templates: list) -> None:
        """Set field name templates (project-level).

        Replaces all existing project-level field name templates.

        Args:
            templates: List of dicts, each with:
                - name (str): Field name (required)
                - visible (bool): Default visibility (default: False)
                - url (bool): Has URL/browse button (default: False)

        Example:
            >>> sch.page.set_field_name_templates([
            ...     {'name': 'Manufacturer', 'visible': True, 'url': False},
            ...     {'name': 'MPN', 'visible': True, 'url': False},
            ...     {'name': 'Datasheet', 'visible': False, 'url': True},
            ... ])
        """
        cmd = schematic_commands_pb2.SetFieldNameTemplates()
        cmd.document.CopyFrom(self._doc)

        for tmpl_dict in templates:
            tmpl = cmd.templates.add()
            tmpl.name = tmpl_dict.get('name', '')
            tmpl.visible = tmpl_dict.get('visible', False)
            tmpl.url = tmpl_dict.get('url', False)

        self._kicad.send(cmd, Empty)

    def add_field_name_template(
        self,
        name: str,
        visible: bool = False,
        url: bool = False,
    ) -> list:
        """Add a field name template (convenience method).

        Adds a new template to the existing list.

        Args:
            name: Field name
            visible: Default visibility
            url: Has URL/browse button

        Returns:
            Updated list of all templates

        Example:
            >>> sch.page.add_field_name_template('Manufacturer', visible=True)
        """
        templates = self.get_field_name_templates()

        # Check if already exists
        for tmpl in templates:
            if tmpl['name'] == name:
                # Update existing
                tmpl['visible'] = visible
                tmpl['url'] = url
                self.set_field_name_templates(templates)
                return self.get_field_name_templates()

        # Add new
        templates.append({'name': name, 'visible': visible, 'url': url})
        self.set_field_name_templates(templates)
        return self.get_field_name_templates()

    def remove_field_name_template(self, name: str) -> list:
        """Remove a field name template by name (convenience method).

        Args:
            name: Field name to remove

        Returns:
            Updated list of all templates

        Example:
            >>> sch.page.remove_field_name_template('OldField')
        """
        templates = self.get_field_name_templates()
        templates = [t for t in templates if t['name'] != name]
        self.set_field_name_templates(templates)
        return self.get_field_name_templates()

    def clear_field_name_templates(self) -> None:
        """Remove all field name templates.

        Example:
            >>> sch.page.clear_field_name_templates()
        """
        self.set_field_name_templates([])

    # =========================================================================
    # Annotation Settings (Project-level settings from Schematic Setup)
    # =========================================================================

    # Symbol unit notation constants
    SYMBOL_UNIT_NOTATION = {
        'A': 0,       # A (no separator, letter)
        '.A': 1,      # .A (dot separator, letter)
        '-A': 2,      # -A (dash separator, letter)
        '_A': 3,      # _A (underscore separator, letter)
        '.1': 4,      # .1 (dot separator, number)
        '-1': 5,      # -1 (dash separator, number)
        '_1': 6,      # _1 (underscore separator, number)
    }

    SYMBOL_UNIT_NOTATION_NAMES = {
        0: 'A',
        1: '.A',
        2: '-A',
        3: '_A',
        4: '.1',
        5: '-1',
        6: '_1',
    }

    # Sort order constants
    SORT_ORDER = {
        'x': 0,       # Sort by X position
        'y': 1,       # Sort by Y position
    }

    SORT_ORDER_NAMES = {
        0: 'x',
        1: 'y',
    }

    # Numbering method constants
    NUMBERING_METHOD = {
        'first_free': 0,      # Use first free number after start_number
        'sheet_x_100': 1,     # First free after sheet number X 100
        'sheet_x_1000': 2,    # First free after sheet number X 1000
    }

    NUMBERING_METHOD_NAMES = {
        0: 'first_free',
        1: 'sheet_x_100',
        2: 'sheet_x_1000',
    }

    def get_annotation_settings(self) -> dict:
        """Get annotation settings (project-level).

        These are the settings from Schematic Setup -> General -> Annotation.

        Returns:
            Dict with annotation settings including:
            - units: symbol_unit_notation (enum value and name)
            - order: sort_order (enum value and name like 'x' or 'y')
            - numbering: method, start_number, allow_reference_reuse

        Example:
            >>> settings = sch.page.get_annotation_settings()
            >>> print(f"Sort order: {settings['order']['name']}")  # 'x' or 'y'
            >>> print(f"Start number: {settings['numbering']['start_number']}")
        """
        cmd = schematic_commands_pb2.GetAnnotationSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetAnnotationSettingsResponse)

        s = response.settings
        notation_val = s.symbol_unit_notation if s.HasField('symbol_unit_notation') else 0
        sort_val = s.sort_order if s.HasField('sort_order') else 0
        method_val = s.numbering_method if s.HasField('numbering_method') else 0

        return {
            'units': {
                'symbol_unit_notation': notation_val,
                'name': self.SYMBOL_UNIT_NOTATION_NAMES.get(notation_val, 'A'),
            },
            'order': {
                'sort_order': sort_val,
                'name': self.SORT_ORDER_NAMES.get(sort_val, 'x'),
            },
            'numbering': {
                'method': method_val,
                'method_name': self.NUMBERING_METHOD_NAMES.get(method_val, 'first_free'),
                'start_number': s.start_number if s.HasField('start_number') else 0,
                'allow_reference_reuse': s.allow_reference_reuse if s.HasField('allow_reference_reuse') else False,
            },
        }

    def set_annotation_settings(
        self,
        # Units section
        symbol_unit_notation: Optional[str] = None,
        # Order section
        sort_order: Optional[str] = None,
        # Numbering section
        numbering_method: Optional[str] = None,
        start_number: Optional[int] = None,
        allow_reference_reuse: Optional[bool] = None,
    ) -> None:
        """Set annotation settings (project-level).

        These are the settings from Schematic Setup -> General -> Annotation.
        Only provided fields are updated.

        Args:
            symbol_unit_notation: Unit notation style ('A', '.A', '-A', '_A', '.1', '-1', '_1')
            sort_order: Sort order for annotation ('x' or 'y')
            numbering_method: Numbering method ('first_free', 'sheet_x_100', 'sheet_x_1000')
            start_number: Starting number for annotation
            allow_reference_reuse: Allow reusing reference designators

        Example:
            >>> sch.page.set_annotation_settings(
            ...     symbol_unit_notation='A',
            ...     sort_order='x',
            ...     numbering_method='first_free',
            ...     start_number=1,
            ...     allow_reference_reuse=True
            ... )
        """
        cmd = schematic_commands_pb2.SetAnnotationSettings()
        cmd.document.CopyFrom(self._doc)

        # Units section
        if symbol_unit_notation is not None:
            notation_val = self.SYMBOL_UNIT_NOTATION.get(symbol_unit_notation, 0)
            cmd.settings.symbol_unit_notation = notation_val

        # Order section
        if sort_order is not None:
            sort_val = self.SORT_ORDER.get(sort_order, 0)
            cmd.settings.sort_order = sort_val

        # Numbering section
        if numbering_method is not None:
            method_val = self.NUMBERING_METHOD.get(numbering_method, 0)
            cmd.settings.numbering_method = method_val

        if start_number is not None:
            cmd.settings.start_number = start_number

        if allow_reference_reuse is not None:
            cmd.settings.allow_reference_reuse = allow_reference_reuse

        self._kicad.send(cmd, Empty)
