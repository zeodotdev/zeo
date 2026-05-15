# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Graphics operations: shapes, text, dimensions, bounding boxes.
"""

from typing import TYPE_CHECKING, Dict, Iterable, List, Optional, Sequence, Union, overload

from kipy.geometry import Box2, PolygonWithHoles, Vector2
from kipy.board_types import BoardItem, BoardShape, BoardText, BoardTextBox, Dimension, Pad
from kipy.proto.common.commands.editor_commands_pb2 import (
    GetBoundingBox, GetBoundingBoxResponse, BoundingBoxMode,
    HitTest, HitTestResponse, HitTestResult,
)
from kipy.proto.board import board_commands_pb2, board_types_pb2
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.wrapper import Item

if TYPE_CHECKING:
    from kipy.board.base import Board


class GraphicsOperations:
    """Graphics operations: shapes, text, dimensions, bounding boxes."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_shapes(self) -> Sequence[BoardShape]:
        """Get all graphic shapes on the board."""
        return self._board.crud.get_shapes()

    def get_text(self) -> Sequence[Union[BoardText, BoardTextBox]]:
        """Get all text items on the board."""
        return self._board.crud.get_text()

    def get_dimensions(self) -> Sequence[Dimension]:
        """Get all dimension objects on the board."""
        return self._board.crud.get_dimensions()

    @overload
    def get_bounding_box(
        self, items: BoardItem, include_text: bool = False
    ) -> Optional[Box2]: ...

    @overload
    def get_bounding_box(
        self, items: Sequence[BoardItem], include_text: bool = False
    ) -> List[Optional[Box2]]: ...

    def get_bounding_box(
        self,
        items: Union[BoardItem, Sequence[BoardItem]],
        include_text: bool = False,
    ) -> Union[Optional[Box2], List[Optional[Box2]]]:
        """Get the bounding box for an item or items.

        Args:
            items: Single item or sequence of items
            include_text: If True, include text in bounding box calculation

        Returns:
            Box2 for single item, or list of Box2 for multiple items
        """
        cmd = GetBoundingBox()
        cmd.header.document.CopyFrom(self._board._doc)
        cmd.mode = (
            BoundingBoxMode.BBM_ITEM_AND_CHILD_TEXT
            if include_text
            else BoundingBoxMode.BBM_ITEM_ONLY
        )

        if isinstance(items, BoardItem):
            cmd.items.append(items.id)
        else:
            cmd.items.extend([i.id for i in items])

        response = self._board._kicad.send(cmd, GetBoundingBoxResponse)

        if isinstance(items, BoardItem):
            return Box2.from_proto(response.boxes[0]) if len(response.boxes) == 1 else None

        item_to_bbox = {item.value: bbox for item, bbox in zip(response.items, response.boxes)}
        return [
            Box2.from_proto(box)
            for box in (item_to_bbox.get(item.id.value, None) for item in items)
            if box is not None
        ]

    def hit_test(self, item: Item, position: Vector2, tolerance: int = 0) -> bool:
        """Test if a point hits an item.

        Args:
            item: The item to test
            position: Point to test
            tolerance: Hit tolerance in nanometers

        Returns:
            True if the point hits the item
        """
        cmd = HitTest()
        cmd.header.document.CopyFrom(self._board._doc)
        cmd.id.CopyFrom(item.id)
        cmd.position.CopyFrom(position.proto)
        cmd.tolerance = tolerance
        return self._board._kicad.send(cmd, HitTestResponse).result == HitTestResult.HTR_HIT

    @overload
    def get_pad_shapes_as_polygons(
        self, pads: Pad, layer: BoardLayer.ValueType = BoardLayer.BL_F_Cu
    ) -> Optional[PolygonWithHoles]: ...

    @overload
    def get_pad_shapes_as_polygons(
        self, pads: Sequence[Pad], layer: BoardLayer.ValueType = BoardLayer.BL_F_Cu
    ) -> List[Optional[PolygonWithHoles]]: ...

    def get_pad_shapes_as_polygons(
        self,
        pads: Union[Pad, Sequence[Pad]],
        layer: BoardLayer.ValueType = BoardLayer.BL_F_Cu,
    ) -> Union[Optional[PolygonWithHoles], List[Optional[PolygonWithHoles]]]:
        """Get the polygonal shape of pads on a given layer.

        Args:
            pads: Single pad or sequence of pads
            layer: Board layer to get shape for

        Returns:
            PolygonWithHoles for single pad, or list for multiple pads
        """
        cmd = board_commands_pb2.GetPadShapeAsPolygon()
        cmd.board.CopyFrom(self._board._doc)
        cmd.layer = layer

        if isinstance(pads, Pad):
            cmd.pads.append(pads.id)
        else:
            cmd.pads.extend([pad.id for pad in pads])

        response = self._board._kicad.send(cmd, board_commands_pb2.PadShapeAsPolygonResponse)

        if isinstance(pads, Pad):
            return PolygonWithHoles(response.polygons[0]) if len(response.polygons) == 1 else None

        pad_to_polygon = {pad.value: polygon for pad, polygon in zip(response.pads, response.polygons)}
        return [
            PolygonWithHoles(p)
            for p in (pad_to_polygon.get(pad.id.value, None) for pad in pads)
            if p is not None
        ]

    def check_padstack_presence_on_layers(
        self,
        items: Union[BoardItem, Iterable[BoardItem]],
        layers: Union[board_types_pb2.BoardLayer.ValueType, Iterable[board_types_pb2.BoardLayer.ValueType]],
    ) -> Dict[BoardItem, Dict[board_types_pb2.BoardLayer.ValueType, bool]]:
        """Check if items with padstacks (pads or vias) have content on given layers.

        Args:
            items: The items to check (one or more pads or vias)
            layers: The layer or layers to check for padstack presence

        Returns:
            Dictionary mapping each item to a dictionary of layers and their presence
        """
        cmd = board_commands_pb2.CheckPadstackPresenceOnLayers()
        cmd.board.CopyFrom(self._board._doc)

        items_map = {}

        if isinstance(items, BoardItem):
            cmd.items.append(items.id)
            items_map[items.id.value] = items
        else:
            cmd.items.extend([item.id for item in items])
            items_map.update({item.id.value: item for item in items})

        if isinstance(layers, int):
            cmd.layers.append(layers)
        else:
            cmd.layers.extend(layers)

        response = self._board._kicad.send(cmd, board_commands_pb2.PadstackPresenceResponse)

        result = {}
        for entry in response.entries:
            if entry.item.value not in items_map:
                continue

            item = items_map[entry.item.value]
            layer = entry.layer
            presence = entry.presence is board_commands_pb2.PadstackPresence.PSP_PRESENT

            if item not in result:
                result[item] = {}

            result[item][layer] = presence

        return result
