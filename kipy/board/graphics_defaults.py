# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Graphics defaults operations (text and graphics settings per layer class).
"""

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Optional, List

from kipy.proto.board import board_commands_pb2, board_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Enum mappings
class BoardLayerClass:
    UNKNOWN = board_pb2.BLC_UNKNOWN
    SILKSCREEN = board_pb2.BLC_SILKSCREEN
    COPPER = board_pb2.BLC_COPPER
    EDGES = board_pb2.BLC_EDGES
    COURTYARD = board_pb2.BLC_COURTYARD
    FABRICATION = board_pb2.BLC_FABRICATION
    OTHER = board_pb2.BLC_OTHER

    _TO_STRING = {
        board_pb2.BLC_UNKNOWN: 'unknown',
        board_pb2.BLC_SILKSCREEN: 'silkscreen',
        board_pb2.BLC_COPPER: 'copper',
        board_pb2.BLC_EDGES: 'edges',
        board_pb2.BLC_COURTYARD: 'courtyard',
        board_pb2.BLC_FABRICATION: 'fabrication',
        board_pb2.BLC_OTHER: 'other',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'unknown')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.UNKNOWN)


@dataclass
class LayerGraphicsDefaults:
    """Graphics defaults for a single layer class."""

    layer_class: int = BoardLayerClass.UNKNOWN
    text_size_nm: Optional[int] = None
    text_thickness_nm: Optional[int] = None
    text_italic: Optional[bool] = None
    text_upright: Optional[bool] = None
    line_thickness_nm: Optional[int] = None

    @classmethod
    def from_proto(cls, proto: board_pb2.BoardLayerGraphicsDefaults) -> "LayerGraphicsDefaults":
        """Create from protobuf message."""
        text = proto.text
        return cls(
            layer_class=proto.layer,
            text_size_nm=text.size.x_nm if text.HasField("size") else None,
            text_thickness_nm=text.stroke_width.value_nm if text.HasField("stroke_width") else None,
            text_italic=text.italic if text.HasField("italic") else None,
            text_upright=text.keep_upright if text.HasField("keep_upright") else None,
            line_thickness_nm=proto.line_thickness.value_nm if proto.HasField("line_thickness") else None,
        )

    def to_proto(self, proto: Optional[board_pb2.BoardLayerGraphicsDefaults] = None) -> board_pb2.BoardLayerGraphicsDefaults:
        """Convert to protobuf message."""
        if proto is None:
            proto = board_pb2.BoardLayerGraphicsDefaults()
        proto.layer = self.layer_class
        if self.text_size_nm is not None:
            proto.text.size.x_nm = self.text_size_nm
            proto.text.size.y_nm = self.text_size_nm  # Assuming square
        if self.text_thickness_nm is not None:
            proto.text.stroke_width.value_nm = self.text_thickness_nm
        if self.text_italic is not None:
            proto.text.italic = self.text_italic
        if self.text_upright is not None:
            proto.text.keep_upright = self.text_upright
        if self.line_thickness_nm is not None:
            proto.line_thickness.value_nm = self.line_thickness_nm
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        result = {"layer_class": BoardLayerClass.to_string(self.layer_class)}
        if self.text_size_nm is not None:
            result["text_size_nm"] = self.text_size_nm
        if self.text_thickness_nm is not None:
            result["text_thickness_nm"] = self.text_thickness_nm
        if self.text_italic is not None:
            result["text_italic"] = self.text_italic
        if self.text_upright is not None:
            result["text_upright"] = self.text_upright
        if self.line_thickness_nm is not None:
            result["line_thickness_nm"] = self.line_thickness_nm
        return result


@dataclass
class GraphicsDefaults:
    """Complete graphics defaults for all layer classes."""

    layers: List[LayerGraphicsDefaults] = field(default_factory=list)

    @classmethod
    def from_proto(cls, proto: board_pb2.GraphicsDefaults) -> "GraphicsDefaults":
        """Create from protobuf message."""
        return cls(
            layers=[LayerGraphicsDefaults.from_proto(layer) for layer in proto.layers]
        )

    def to_proto(self, proto: Optional[board_pb2.GraphicsDefaults] = None) -> board_pb2.GraphicsDefaults:
        """Convert to protobuf message."""
        if proto is None:
            proto = board_pb2.GraphicsDefaults()
        for layer in self.layers:
            layer.to_proto(proto.layers.add())
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "layers": [layer.to_dict() for layer in self.layers]
        }

    def get_layer(self, layer_class: int) -> Optional[LayerGraphicsDefaults]:
        """Get defaults for a specific layer class."""
        for layer in self.layers:
            if layer.layer_class == layer_class:
                return layer
        return None

    def set_layer(self, defaults: LayerGraphicsDefaults) -> None:
        """Set or update defaults for a layer class."""
        for i, layer in enumerate(self.layers):
            if layer.layer_class == defaults.layer_class:
                self.layers[i] = defaults
                return
        self.layers.append(defaults)


class GraphicsDefaultsOperations:
    """Graphics defaults operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> GraphicsDefaults:
        """Get graphics defaults for all layer classes.

        Returns:
            GraphicsDefaults with settings for each layer class
        """
        cmd = board_commands_pb2.GetGraphicsDefaults()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.GraphicsDefaultsResponse)
        return GraphicsDefaults.from_proto(response.defaults)

    def set(self, defaults: GraphicsDefaults) -> GraphicsDefaults:
        """Set graphics defaults.

        Args:
            defaults: GraphicsDefaults to apply

        Returns:
            Updated GraphicsDefaults
        """
        cmd = board_commands_pb2.SetGraphicsDefaults()
        cmd.board.CopyFrom(self._board._doc)
        defaults.to_proto(cmd.defaults)
        response = self._board._kicad.send(cmd, board_commands_pb2.GraphicsDefaultsResponse)
        return GraphicsDefaults.from_proto(response.defaults)

    def set_layer(
        self,
        layer_class: int,
        text_size_nm: Optional[int] = None,
        text_thickness_nm: Optional[int] = None,
        text_italic: Optional[bool] = None,
        text_upright: Optional[bool] = None,
        line_thickness_nm: Optional[int] = None,
    ) -> GraphicsDefaults:
        """Set graphics defaults for a specific layer class (read-modify-write).

        Args:
            layer_class: BoardLayerClass value
            text_size_nm: Default text size in nanometers
            text_thickness_nm: Default text stroke width in nanometers
            text_italic: Whether text is italic
            text_upright: Whether text is kept upright
            line_thickness_nm: Default line thickness in nanometers

        Returns:
            Updated GraphicsDefaults
        """
        defaults = self.get()

        layer = defaults.get_layer(layer_class)
        if layer is None:
            layer = LayerGraphicsDefaults(layer_class=layer_class)

        if text_size_nm is not None:
            layer.text_size_nm = text_size_nm
        if text_thickness_nm is not None:
            layer.text_thickness_nm = text_thickness_nm
        if text_italic is not None:
            layer.text_italic = text_italic
        if text_upright is not None:
            layer.text_upright = text_upright
        if line_thickness_nm is not None:
            layer.line_thickness_nm = line_thickness_nm

        defaults.set_layer(layer)
        return self.set(defaults)
