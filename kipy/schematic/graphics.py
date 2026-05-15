# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Graphics operations: text, shapes, annotations.
"""

from typing import TYPE_CHECKING, Optional, List, cast

from kipy.schematic_types import SchematicText, SchematicGraphicShape, SchematicTextBox
from kipy.geometry import Vector2
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class GraphicsOperations:
    """Graphics operations: text, shapes, annotations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Text Operations
    # =========================================================================

    def add_text(self, text: str, position: Vector2) -> SchematicText:
        """Add a text annotation.

        Args:
            text: The text content
            position: Position for the text

        Returns:
            The created SchematicText object

        Example:
            >>> text = sch.graphics.add_text("Note: Check values", pos)
        """
        text_item = SchematicText.create(text, position)
        created = self._sch.crud.create_items(text_item)
        if created:
            return cast(SchematicText, created[0])
        return text_item

    def add_textbox(
        self,
        text: str,
        top_left: Vector2,
        bottom_right: Vector2,
    ) -> SchematicTextBox:
        """Add a text box.

        Args:
            text: The text content
            top_left: Top-left corner
            bottom_right: Bottom-right corner

        Returns:
            The created SchematicTextBox object

        Example:
            >>> textbox = sch.graphics.add_textbox(
            ...     "Design Notes\\nRevision 1.0",
            ...     Vector2.from_xy_mm(10, 10),
            ...     Vector2.from_xy_mm(50, 30)
            ... )
        """
        textbox = SchematicTextBox.create(text, top_left, bottom_right)
        created = self._sch.crud.create_items(textbox)
        if created:
            return cast(SchematicTextBox, created[0])
        return textbox

    def get_text_items(self) -> List[Wrapper]:
        """Get all text and textbox items."""
        return list(self._sch.crud.get_text_items())

    # =========================================================================
    # Shape Operations
    # =========================================================================

    def add_rectangle(
        self,
        top_left: Vector2,
        bottom_right: Vector2,
        stroke_width: int = 0,
        filled: bool = False,
    ) -> SchematicGraphicShape:
        """Add a rectangle shape.

        Args:
            top_left: Top-left corner
            bottom_right: Bottom-right corner
            stroke_width: Line width in internal units (0 = default)
            filled: Whether to fill the shape

        Returns:
            The created shape

        Example:
            >>> rect = sch.graphics.add_rectangle(
            ...     Vector2.from_xy_mm(10, 10),
            ...     Vector2.from_xy_mm(50, 30),
            ...     filled=True
            ... )
        """
        shape = SchematicGraphicShape.create_rectangle(
            top_left, bottom_right, stroke_width, filled
        )
        created = self._sch.crud.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_circle(
        self,
        center: Vector2,
        radius: int,
        stroke_width: int = 0,
        filled: bool = False,
    ) -> SchematicGraphicShape:
        """Add a circle shape.

        Args:
            center: Center point
            radius: Radius in internal units
            stroke_width: Line width (0 = default)
            filled: Whether to fill

        Returns:
            The created shape

        Example:
            >>> circle = sch.graphics.add_circle(
            ...     Vector2.from_xy_mm(50, 50),
            ...     radius=5_000_000,  # 5mm
            ...     filled=True
            ... )
        """
        radius_point = Vector2.from_xy(center.x + radius, center.y)
        shape = SchematicGraphicShape.create_circle(
            center, radius_point, stroke_width, filled
        )
        created = self._sch.crud.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_line(
        self,
        start: Vector2,
        end: Vector2,
        stroke_width: int = 0,
    ) -> SchematicGraphicShape:
        """Add a graphic line (not an electrical wire).

        Args:
            start: Start point
            end: End point
            stroke_width: Line width (0 = default)

        Returns:
            The created shape

        Note:
            Use sch.wiring.add_wire() for electrical connections.
        """
        shape = SchematicGraphicShape.create_line(start, end, stroke_width)
        created = self._sch.crud.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def add_arc(
        self,
        start: Vector2,
        mid: Vector2,
        end: Vector2,
        stroke_width: int = 0,
    ) -> SchematicGraphicShape:
        """Add an arc shape.

        Args:
            start: Start point
            mid: Mid point (defines curvature)
            end: End point
            stroke_width: Line width (0 = default)

        Returns:
            The created shape
        """
        shape = SchematicGraphicShape.create_arc(start, mid, end, stroke_width)
        created = self._sch.crud.create_items(shape)
        if created:
            return cast(SchematicGraphicShape, created[0])
        return shape

    def get_shapes(self) -> List[SchematicGraphicShape]:
        """Get all graphic shapes."""
        return self._sch.crud.get_graphic_shapes()

    # =========================================================================
    # Bitmap Operations
    # =========================================================================

    def add_bitmap(
        self,
        image_path: str,
        position: Vector2,
        scale: float = 1.0,
    ) -> Optional[Wrapper]:
        """Add a bitmap image.

        Args:
            image_path: Path to image file (PNG recommended)
            position: Position for image center
            scale: Scale factor (1.0 = original size)

        Returns:
            The created bitmap, or None if failed
        """
        from kipy.schematic_types import Bitmap as BitmapType

        if BitmapType is None:
            raise NotImplementedError("Bitmap support requires proto regeneration")

        bitmap = BitmapType.create(position, image_path, scale)
        created = self._sch.crud.create_items(bitmap)
        return created[0] if created else None

    def get_bitmaps(self) -> List[Wrapper]:
        """Get all bitmap images."""
        return list(self._sch.crud.get_bitmaps())

    # =========================================================================
    # Table Operations
    # =========================================================================

    def add_table(
        self,
        position: Vector2,
        columns: int,
        rows: int,
        col_width: int = 0,
        row_height: int = 0,
        header_on: bool = False,
    ) -> Optional[Wrapper]:
        """Add a table.

        Args:
            position: Position for the table
            columns: Number of columns
            rows: Number of rows
            col_width: Default column width (0 = default)
            row_height: Default row height (0 = default)
            header_on: First row is header

        Returns:
            The created table, or None if failed
        """
        from kipy.schematic_types import Table as TableType

        if TableType is None:
            raise NotImplementedError("Table support requires proto regeneration")

        table = TableType.create(position, columns, rows, col_width, row_height, header_on)
        created = self._sch.crud.create_items(table)
        return created[0] if created else None

    def get_tables(self) -> List[Wrapper]:
        """Get all tables."""
        return list(self._sch.crud.get_tables())

    # =========================================================================
    # Group Operations
    # =========================================================================

    def create_group(self, name: str, items: List[Wrapper]) -> Optional[Wrapper]:
        """Create a group from items.

        Args:
            name: Group name
            items: Items to include

        Returns:
            The created group, or None if failed
        """
        from kipy.schematic_types import SchematicGroup

        if SchematicGroup is None:
            raise NotImplementedError("Group support requires proto regeneration")

        member_ids = [item.id.value for item in items if hasattr(item, 'id')]
        group = SchematicGroup.create(name, member_ids)
        created = self._sch.crud.create_items(group)
        return created[0] if created else None

    def get_groups(self) -> List[Wrapper]:
        """Get all groups."""
        return list(self._sch.crud.get_groups())

    def get_textboxes(self) -> List[SchematicTextBox]:
        """Get all text boxes.

        Returns:
            List of SchematicTextBox objects

        Example:
            >>> textboxes = sch.graphics.get_textboxes()
            >>> for tb in textboxes:
            ...     print(f"TextBox: {tb.text[:20]}...")
        """
        from kipy.proto.common.types import KiCadObjectType
        items = self._sch.crud.get_items(KiCadObjectType.KOT_SCH_TEXTBOX)
        return [cast(SchematicTextBox, i) for i in items]

    # =========================================================================
    # Update/Delete Operations
    # =========================================================================

    def update_graphic(self, item: Wrapper) -> Wrapper:
        """Update a graphic item's properties.

        Works for text, textboxes, shapes, bitmaps, tables, and groups.

        Args:
            item: Graphic item with updated properties

        Returns:
            Updated item

        Example:
            >>> text = sch.graphics.get_text_items()[0]
            >>> text.text = "Updated note"
            >>> sch.graphics.update_graphic(text)
        """
        updated = self._sch.crud.update_items(item)
        return updated[0] if updated else item

    def update_graphics(self, items: List[Wrapper]) -> List[Wrapper]:
        """Update multiple graphic items' properties.

        Args:
            items: List of graphic items with updated properties

        Returns:
            List of updated items
        """
        if items:
            return self._sch.crud.update_items(items)
        return []

    def delete_graphic(self, item: Wrapper) -> None:
        """Delete a graphic item.

        Works for text, textboxes, shapes, bitmaps, tables, and groups.

        Args:
            item: Graphic item to delete

        Example:
            >>> shapes = sch.graphics.get_shapes()
            >>> sch.graphics.delete_graphic(shapes[0])
        """
        self._sch.crud.remove_items(item)

    def delete_graphics(self, items: List[Wrapper]) -> None:
        """Delete multiple graphic items.

        Args:
            items: List of graphic items to delete

        Example:
            >>> shapes = sch.graphics.get_shapes()
            >>> sch.graphics.delete_graphics(shapes)
        """
        if items:
            self._sch.crud.remove_items(items)
