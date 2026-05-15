# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Transform operations: move, rotate, mirror, align, distribute.

Provides batch operations for transforming schematic items.
"""

from typing import TYPE_CHECKING, List, Optional, Union, Tuple
from enum import Enum

from kipy.geometry import Vector2
from kipy.wrapper import Wrapper
from kipy.schematic_types import Symbol, Wire
from kipy.proto.common.commands import editor_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class AlignmentType(Enum):
    """Alignment type for align operations."""
    LEFT = "left"
    RIGHT = "right"
    TOP = "top"
    BOTTOM = "bottom"
    CENTER_H = "center_h"  # Horizontal center
    CENTER_V = "center_v"  # Vertical center


class DistributeType(Enum):
    """Distribution type for distribute operations."""
    HORIZONTAL = "horizontal"
    VERTICAL = "vertical"
    GRID = "grid"


class TransformOperations:
    """Batch transform operations for schematic items."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    # =========================================================================
    # Move Operations
    # =========================================================================

    def move(
        self,
        items: Union[Wrapper, List[Wrapper]],
        delta_x_mm: float = 0,
        delta_y_mm: float = 0,
    ) -> List[Wrapper]:
        """Move items by a relative offset.

        Args:
            items: Item or list of items to move
            delta_x_mm: X offset in millimeters
            delta_y_mm: Y offset in millimeters

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.transform.move(symbols, delta_x_mm=10, delta_y_mm=5)
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return []

        delta_x_nm = int(delta_x_mm * 1_000_000)
        delta_y_nm = int(delta_y_mm * 1_000_000)

        for item in items:
            if hasattr(item, 'position'):
                current = item.position
                item.position = Vector2.from_xy(
                    current.x + delta_x_nm,
                    current.y + delta_y_nm
                )
            elif hasattr(item, 'start') and hasattr(item, 'end'):
                # Wire-like items
                item.start = Vector2.from_xy(
                    item.start.x + delta_x_nm,
                    item.start.y + delta_y_nm
                )
                item.end = Vector2.from_xy(
                    item.end.x + delta_x_nm,
                    item.end.y + delta_y_nm
                )

        result = self._sch.crud.update_items(items)
        self._sch.save()
        return result

    def move_to(
        self,
        items: Union[Wrapper, List[Wrapper]],
        position: Vector2,
        anchor: str = "center",
    ) -> List[Wrapper]:
        """Move items to an absolute position.

        Args:
            items: Item or list of items to move
            position: Target position
            anchor: Anchor point - "center", "top_left", "bottom_right"

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.transform.move_to(symbols, Vector2.from_xy_mm(100, 80))
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return []

        # Calculate bounding box
        bbox = self._get_bounding_box(items)
        if not bbox:
            return []

        # Calculate offset based on anchor
        if anchor == "center":
            current_x = (bbox["min_x"] + bbox["max_x"]) / 2
            current_y = (bbox["min_y"] + bbox["max_y"]) / 2
        elif anchor == "top_left":
            current_x = bbox["min_x"]
            current_y = bbox["min_y"]
        elif anchor == "bottom_right":
            current_x = bbox["max_x"]
            current_y = bbox["max_y"]
        else:
            current_x = (bbox["min_x"] + bbox["max_x"]) / 2
            current_y = (bbox["min_y"] + bbox["max_y"]) / 2

        delta_x_nm = position.x - int(current_x)
        delta_y_nm = position.y - int(current_y)

        return self.move(items, delta_x_nm / 1e6, delta_y_nm / 1e6)

    # =========================================================================
    # Rotate Operations
    # =========================================================================

    def rotate(
        self,
        items: Union[Wrapper, List[Wrapper]],
        angle_degrees: float,
        center: Optional[Vector2] = None,
    ) -> List[Wrapper]:
        """Rotate items around a center point.

        Args:
            items: Item or list of items to rotate
            angle_degrees: Rotation angle (positive = CCW)
            center: Center of rotation (None = center of items)

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.transform.rotate(symbols, 90)
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return []

        # For symbols, just rotate the symbol orientation
        # For a true rotation around a point, we'd need to move + rotate
        import math

        if center is None:
            bbox = self._get_bounding_box(items)
            if bbox:
                center = Vector2.from_xy(
                    int((bbox["min_x"] + bbox["max_x"]) / 2),
                    int((bbox["min_y"] + bbox["max_y"]) / 2)
                )
            else:
                center = Vector2.from_xy(0, 0)

        angle_rad = math.radians(angle_degrees)
        cos_a = math.cos(angle_rad)
        sin_a = math.sin(angle_rad)

        for item in items:
            if isinstance(item, Symbol):
                # Rotate position around center
                if center:
                    dx = item.position.x - center.x
                    dy = item.position.y - center.y
                    new_x = center.x + dx * cos_a - dy * sin_a
                    new_y = center.y + dx * sin_a + dy * cos_a
                    item.position = Vector2.from_xy(int(new_x), int(new_y))

                # Update symbol orientation
                item.angle = (item.angle + angle_degrees) % 360

            elif hasattr(item, 'start') and hasattr(item, 'end'):
                # Rotate wire endpoints
                for attr in ['start', 'end']:
                    pos = getattr(item, attr)
                    dx = pos.x - center.x
                    dy = pos.y - center.y
                    new_x = center.x + dx * cos_a - dy * sin_a
                    new_y = center.y + dx * sin_a + dy * cos_a
                    setattr(item, attr, Vector2.from_xy(int(new_x), int(new_y)))

            elif hasattr(item, 'position'):
                # Rotate position
                dx = item.position.x - center.x
                dy = item.position.y - center.y
                new_x = center.x + dx * cos_a - dy * sin_a
                new_y = center.y + dx * sin_a + dy * cos_a
                item.position = Vector2.from_xy(int(new_x), int(new_y))

        result = self._sch.crud.update_items(items)
        self._sch.save()
        return result

    # =========================================================================
    # Mirror Operations
    # =========================================================================

    def mirror(
        self,
        items: Union[Wrapper, List[Wrapper]],
        axis: str = "x",
        center: Optional[Vector2] = None,
    ) -> List[Wrapper]:
        """Mirror items along an axis.

        Args:
            items: Item or list of items to mirror
            axis: "x" (horizontal flip) or "y" (vertical flip)
            center: Center of mirror (None = center of items)

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="Q")
            >>> sch.transform.mirror(symbols, axis="x")
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return []

        if center is None:
            bbox = self._get_bounding_box(items)
            if bbox:
                center = Vector2.from_xy(
                    int((bbox["min_x"] + bbox["max_x"]) / 2),
                    int((bbox["min_y"] + bbox["max_y"]) / 2)
                )
            else:
                center = Vector2.from_xy(0, 0)

        for item in items:
            if isinstance(item, Symbol):
                if axis.lower() == "x":
                    # Mirror across vertical axis (flip horizontally)
                    item.mirror_x = not item.mirror_x
                    # Also mirror position
                    new_x = 2 * center.x - item.position.x
                    item.position = Vector2.from_xy(new_x, item.position.y)
                elif axis.lower() == "y":
                    # Mirror across horizontal axis (flip vertically)
                    item.mirror_y = not item.mirror_y
                    new_y = 2 * center.y - item.position.y
                    item.position = Vector2.from_xy(item.position.x, new_y)

            elif hasattr(item, 'start') and hasattr(item, 'end'):
                # Mirror wire endpoints
                if axis.lower() == "x":
                    item.start = Vector2.from_xy(2 * center.x - item.start.x, item.start.y)
                    item.end = Vector2.from_xy(2 * center.x - item.end.x, item.end.y)
                elif axis.lower() == "y":
                    item.start = Vector2.from_xy(item.start.x, 2 * center.y - item.start.y)
                    item.end = Vector2.from_xy(item.end.x, 2 * center.y - item.end.y)

            elif hasattr(item, 'position'):
                if axis.lower() == "x":
                    new_x = 2 * center.x - item.position.x
                    item.position = Vector2.from_xy(new_x, item.position.y)
                elif axis.lower() == "y":
                    new_y = 2 * center.y - item.position.y
                    item.position = Vector2.from_xy(item.position.x, new_y)

        return self._sch.crud.update_items(items)

    # =========================================================================
    # Align Operations
    # =========================================================================

    def align(
        self,
        items: Union[Wrapper, List[Wrapper]],
        alignment: Union[str, AlignmentType],
        reference: Optional[Wrapper] = None,
    ) -> List[Wrapper]:
        """Align items to a common edge or center.

        Args:
            items: Items to align
            alignment: "left", "right", "top", "bottom", "center_h", "center_v"
            reference: Reference item (None = use first item or bounding box)

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.transform.align(symbols, "left")
        """
        if isinstance(items, Wrapper):
            items = [items]

        if len(items) < 2:
            return items

        if isinstance(alignment, str):
            alignment = AlignmentType(alignment.lower())

        # Determine reference coordinate
        if reference:
            ref_bbox = self._get_bounding_box([reference])
        else:
            ref_bbox = self._get_bounding_box([items[0]])

        if not ref_bbox:
            return items

        # Calculate target coordinate
        if alignment == AlignmentType.LEFT:
            target_x = ref_bbox["min_x"]
        elif alignment == AlignmentType.RIGHT:
            target_x = ref_bbox["max_x"]
        elif alignment == AlignmentType.TOP:
            target_y = ref_bbox["min_y"]
        elif alignment == AlignmentType.BOTTOM:
            target_y = ref_bbox["max_y"]
        elif alignment == AlignmentType.CENTER_H:
            target_x = (ref_bbox["min_x"] + ref_bbox["max_x"]) / 2
        elif alignment == AlignmentType.CENTER_V:
            target_y = (ref_bbox["min_y"] + ref_bbox["max_y"]) / 2

        # Align each item
        for item in items:
            item_bbox = self._get_bounding_box([item])
            if not item_bbox:
                continue

            if alignment in [AlignmentType.LEFT, AlignmentType.RIGHT, AlignmentType.CENTER_H]:
                if alignment == AlignmentType.LEFT:
                    delta_x = target_x - item_bbox["min_x"]
                elif alignment == AlignmentType.RIGHT:
                    delta_x = target_x - item_bbox["max_x"]
                else:  # CENTER_H
                    item_center_x = (item_bbox["min_x"] + item_bbox["max_x"]) / 2
                    delta_x = target_x - item_center_x

                if hasattr(item, 'position'):
                    item.position = Vector2.from_xy(
                        item.position.x + int(delta_x),
                        item.position.y
                    )
                elif hasattr(item, 'start'):
                    item.start = Vector2.from_xy(item.start.x + int(delta_x), item.start.y)
                    item.end = Vector2.from_xy(item.end.x + int(delta_x), item.end.y)

            else:  # TOP, BOTTOM, CENTER_V
                if alignment == AlignmentType.TOP:
                    delta_y = target_y - item_bbox["min_y"]
                elif alignment == AlignmentType.BOTTOM:
                    delta_y = target_y - item_bbox["max_y"]
                else:  # CENTER_V
                    item_center_y = (item_bbox["min_y"] + item_bbox["max_y"]) / 2
                    delta_y = target_y - item_center_y

                if hasattr(item, 'position'):
                    item.position = Vector2.from_xy(
                        item.position.x,
                        item.position.y + int(delta_y)
                    )
                elif hasattr(item, 'start'):
                    item.start = Vector2.from_xy(item.start.x, item.start.y + int(delta_y))
                    item.end = Vector2.from_xy(item.end.x, item.end.y + int(delta_y))

        result = self._sch.crud.update_items(items)
        self._sch.save()
        return result

    # =========================================================================
    # Distribute Operations
    # =========================================================================

    def distribute(
        self,
        items: Union[Wrapper, List[Wrapper]],
        direction: Union[str, DistributeType],
        spacing_mm: Optional[float] = None,
    ) -> List[Wrapper]:
        """Distribute items evenly.

        Args:
            items: Items to distribute (must be 3+ for even distribution)
            direction: "horizontal", "vertical", or "grid"
            spacing_mm: Optional fixed spacing (None = distribute evenly)

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.find(reference="R")
            >>> sch.transform.distribute(symbols, "horizontal", spacing_mm=20)
        """
        if isinstance(items, Wrapper):
            items = [items]

        if len(items) < 2:
            return items

        if isinstance(direction, str):
            direction = DistributeType(direction.lower())

        # Sort items by position
        items_with_pos = []
        for item in items:
            if hasattr(item, 'position'):
                items_with_pos.append((item, item.position))
            elif hasattr(item, 'start'):
                # Use midpoint for wires
                mid_x = (item.start.x + item.end.x) // 2
                mid_y = (item.start.y + item.end.y) // 2
                items_with_pos.append((item, Vector2.from_xy(mid_x, mid_y)))

        if len(items_with_pos) < 2:
            return items

        if direction == DistributeType.HORIZONTAL:
            items_with_pos.sort(key=lambda x: x[1].x)
            positions = [p.x for _, p in items_with_pos]
        elif direction == DistributeType.VERTICAL:
            items_with_pos.sort(key=lambda x: x[1].y)
            positions = [p.y for _, p in items_with_pos]
        else:  # GRID - not fully implemented
            return self._distribute_grid(items, spacing_mm)

        # Calculate new positions
        if spacing_mm is not None:
            spacing_nm = int(spacing_mm * 1_000_000)
            new_positions = [positions[0]]
            for i in range(1, len(positions)):
                new_positions.append(new_positions[i-1] + spacing_nm)
        else:
            # Distribute evenly between first and last
            start = positions[0]
            end = positions[-1]
            count = len(positions)
            spacing = (end - start) / (count - 1) if count > 1 else 0
            new_positions = [start + int(i * spacing) for i in range(count)]

        # Apply new positions
        for i, (item, _) in enumerate(items_with_pos):
            if hasattr(item, 'position'):
                if direction == DistributeType.HORIZONTAL:
                    item.position = Vector2.from_xy(new_positions[i], item.position.y)
                else:
                    item.position = Vector2.from_xy(item.position.x, new_positions[i])
            elif hasattr(item, 'start'):
                if direction == DistributeType.HORIZONTAL:
                    delta = new_positions[i] - (item.start.x + item.end.x) // 2
                    item.start = Vector2.from_xy(item.start.x + delta, item.start.y)
                    item.end = Vector2.from_xy(item.end.x + delta, item.end.y)
                else:
                    delta = new_positions[i] - (item.start.y + item.end.y) // 2
                    item.start = Vector2.from_xy(item.start.x, item.start.y + delta)
                    item.end = Vector2.from_xy(item.end.x, item.end.y + delta)

        result = self._sch.crud.update_items([item for item, _ in items_with_pos])
        self._sch.save()
        return result

    def _distribute_grid(
        self,
        items: List[Wrapper],
        spacing_mm: Optional[float],
    ) -> List[Wrapper]:
        """Distribute items in a grid pattern."""
        import math

        if not items:
            return items

        # Calculate grid dimensions
        count = len(items)
        cols = math.ceil(math.sqrt(count))
        rows = math.ceil(count / cols)

        # Get first item position as anchor
        first_pos = None
        for item in items:
            if hasattr(item, 'position'):
                first_pos = item.position
                break

        if not first_pos:
            return items

        spacing_nm = int((spacing_mm or 25.4) * 1_000_000)  # Default 1 inch

        for i, item in enumerate(items):
            row = i // cols
            col = i % cols

            new_x = first_pos.x + col * spacing_nm
            new_y = first_pos.y + row * spacing_nm

            if hasattr(item, 'position'):
                item.position = Vector2.from_xy(new_x, new_y)

        return self._sch.crud.update_items(items)

    # =========================================================================
    # Snap to Grid
    # =========================================================================

    def snap_to_grid(
        self,
        items: Union[Wrapper, List[Wrapper]],
        grid_mm: float = 1.27,
    ) -> List[Wrapper]:
        """Snap items to the nearest grid point.

        Args:
            items: Items to snap
            grid_mm: Grid size in millimeters (default 1.27mm = 50 mils)

        Returns:
            List of updated items

        Example:
            >>> symbols = sch.symbols.get_all()
            >>> sch.transform.snap_to_grid(symbols)
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return []

        grid_nm = int(grid_mm * 1_000_000)

        for item in items:
            if hasattr(item, 'position'):
                x = round(item.position.x / grid_nm) * grid_nm
                y = round(item.position.y / grid_nm) * grid_nm
                item.position = Vector2.from_xy(x, y)
            elif hasattr(item, 'start') and hasattr(item, 'end'):
                sx = round(item.start.x / grid_nm) * grid_nm
                sy = round(item.start.y / grid_nm) * grid_nm
                ex = round(item.end.x / grid_nm) * grid_nm
                ey = round(item.end.y / grid_nm) * grid_nm
                item.start = Vector2.from_xy(sx, sy)
                item.end = Vector2.from_xy(ex, ey)

        return self._sch.crud.update_items(items)

    # =========================================================================
    # Helper Methods
    # =========================================================================

    def _get_bounding_box(self, items: List[Wrapper]) -> Optional[dict]:
        """Calculate bounding box of items in nanometers."""
        if not items:
            return None

        min_x = float('inf')
        max_x = float('-inf')
        min_y = float('inf')
        max_y = float('-inf')

        has_positions = False

        for item in items:
            if hasattr(item, 'position'):
                has_positions = True
                min_x = min(min_x, item.position.x)
                max_x = max(max_x, item.position.x)
                min_y = min(min_y, item.position.y)
                max_y = max(max_y, item.position.y)
            elif hasattr(item, 'start') and hasattr(item, 'end'):
                has_positions = True
                min_x = min(min_x, item.start.x, item.end.x)
                max_x = max(max_x, item.start.x, item.end.x)
                min_y = min(min_y, item.start.y, item.end.y)
                max_y = max(max_y, item.start.y, item.end.y)

        if not has_positions:
            return None

        return {
            "min_x": min_x,
            "max_x": max_x,
            "min_y": min_y,
            "max_y": max_y,
            "width": max_x - min_x,
            "height": max_y - min_y,
            "center_x": (min_x + max_x) / 2,
            "center_y": (min_y + max_y) / 2,
        }

    def get_bounding_box(
        self,
        items: Union[Wrapper, List[Wrapper]],
        units: str = "mm",
        use_ipc: bool = True,
        include_text: bool = True,
    ) -> Optional[dict]:
        """Get bounding box of items.

        Uses the IPC API (GetBoundingBox) for accurate bounding box
        calculation including all visual extents.

        Args:
            items: Item or list of items
            units: "nm" (nanometers) or "mm" (millimeters)
            use_ipc: If True, use IPC command; if False, use local calculation
            include_text: If False, exclude child text fields (reference, value)
                from the bounding box. Only affects symbols via IPC.

        Returns:
            Dictionary with min_x, max_x, min_y, max_y, width, height, center_x, center_y
            or None if no valid positions found

        Example:
            >>> symbols = sch.symbols.get_all()
            >>> bbox = sch.transform.get_bounding_box(symbols, units="mm")
            >>> print(f"Design size: {bbox['width']:.1f} x {bbox['height']:.1f} mm")
        """
        if isinstance(items, Wrapper):
            items = [items]

        if not items:
            return None

        bbox = None

        if use_ipc:
            bbox = self._get_bounding_box_ipc(items, include_text=include_text)

        # Fallback to local calculation
        if bbox is None:
            bbox = self._get_bounding_box(items)

        if not bbox:
            return None

        if units.lower() == "mm":
            return {k: v / 1e6 for k, v in bbox.items()}
        return bbox

    def _get_bounding_box_ipc(self, items: List[Wrapper], include_text: bool = True) -> Optional[dict]:
        """Get bounding box using IPC GetBoundingBox command."""
        if not items:
            return None

        cmd = editor_commands_pb2.GetBoundingBox()
        cmd.header.document.CopyFrom(self._sch._doc)

        if include_text:
            cmd.mode = editor_commands_pb2.BBM_ITEM_AND_CHILD_TEXT
        else:
            cmd.mode = editor_commands_pb2.BBM_ITEM_ONLY

        for item in items:
            if hasattr(item, 'id'):
                kiid = cmd.items.add()
                kiid.value = item.id.value

        if len(cmd.items) == 0:
            return None

        try:
            response = self._sch._kicad.send(
                cmd, editor_commands_pb2.GetBoundingBoxResponse
            )

            if not response.boxes:
                return None

            # Combine all bounding boxes
            min_x = float('inf')
            max_x = float('-inf')
            min_y = float('inf')
            max_y = float('-inf')

            for box in response.boxes:
                # Box2 has position (top-left) and size
                box_min_x = box.position.x_nm
                box_min_y = box.position.y_nm
                box_max_x = box_min_x + box.size.x_nm
                box_max_y = box_min_y + box.size.y_nm

                min_x = min(min_x, box_min_x)
                max_x = max(max_x, box_max_x)
                min_y = min(min_y, box_min_y)
                max_y = max(max_y, box_max_y)

            return {
                "min_x": min_x,
                "max_x": max_x,
                "min_y": min_y,
                "max_y": max_y,
                "width": max_x - min_x,
                "height": max_y - min_y,
                "center_x": (min_x + max_x) / 2,
                "center_y": (min_y + max_y) / 2,
            }
        except Exception:
            return None
