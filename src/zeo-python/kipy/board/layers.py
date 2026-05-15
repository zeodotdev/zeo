# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Layer and stackup operations.
"""

from typing import TYPE_CHECKING, Dict, List, Sequence

from google.protobuf.empty_pb2 import Empty

from kipy.proto.board import board_pb2, board_commands_pb2, board_types_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board
    from kipy.board.types import BoardStackup, BoardStackupLayer, BoardLayerGraphicsDefaults


class LayerOperations:
    """Layer and stackup operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_stackup(self) -> "BoardStackup":
        """Get the board stackup configuration."""
        from kipy.board.types import BoardStackup
        cmd = board_commands_pb2.GetBoardStackup()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardStackupResponse)
        return BoardStackup(response.stackup)

    def update_stackup(self, stackup: "BoardStackup") -> "BoardStackup":
        """Update the board stackup configuration.

        Args:
            stackup: BoardStackup object with the desired configuration

        Returns:
            The normalized/updated BoardStackup
        """
        from kipy.board.types import BoardStackup
        cmd = board_commands_pb2.UpdateBoardStackup()
        cmd.board.CopyFrom(self._board._doc)
        cmd.stackup.CopyFrom(stackup.proto)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardStackupResponse)
        return BoardStackup(response.stackup)

    def get_copper_layer_count(self) -> int:
        """Get the number of copper layers on the board."""
        cmd = board_commands_pb2.GetBoardEnabledLayers()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardEnabledLayersResponse)
        return response.copper_layer_count

    def get_enabled_layers(self) -> List[board_types_pb2.BoardLayer.ValueType]:
        """Get all enabled layers on the board."""
        cmd = board_commands_pb2.GetBoardEnabledLayers()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardEnabledLayersResponse)
        return list(response.layers)

    def set_enabled_layers(
        self,
        copper_layer_count: int,
        layers: Sequence[board_types_pb2.BoardLayer.ValueType],
    ) -> List[board_types_pb2.BoardLayer.ValueType]:
        """Set the copper layer count and enabled non-copper layers.

        WARNING: Content on removed layers is deleted. Cannot be undone.

        Args:
            copper_layer_count: Number of copper layers (must be even and >= 2)
            layers: Non-copper layers to enable

        Returns:
            Updated list of enabled layers
        """
        cmd = board_commands_pb2.SetBoardEnabledLayers()
        cmd.board.CopyFrom(self._board._doc)
        cmd.copper_layer_count = copper_layer_count
        cmd.layers.extend(layers)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardEnabledLayersResponse)
        return list(response.layers)

    def get_visible_layers(self) -> Sequence[board_types_pb2.BoardLayer.ValueType]:
        """Get visible layers in the editor."""
        cmd = board_commands_pb2.GetVisibleLayers()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardLayers)
        return response.layers

    def set_visible_layers(self, layers: Sequence[board_types_pb2.BoardLayer.ValueType]):
        """Set visible layers in the editor."""
        cmd = board_commands_pb2.SetVisibleLayers()
        cmd.board.CopyFrom(self._board._doc)
        cmd.layers.extend(layers)
        self._board._kicad.send(cmd, Empty)

    def get_active_layer(self) -> board_types_pb2.BoardLayer.ValueType:
        """Get the currently active layer in the editor."""
        cmd = board_commands_pb2.GetActiveLayer()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardLayerResponse)
        return response.layer

    def set_active_layer(self, layer: board_types_pb2.BoardLayer.ValueType):
        """Set the active layer in the editor."""
        cmd = board_commands_pb2.SetActiveLayer()
        cmd.board.CopyFrom(self._board._doc)
        cmd.layer = layer
        self._board._kicad.send(cmd, Empty)

    def get_graphics_defaults(self) -> Dict[int, "BoardLayerGraphicsDefaults"]:
        """Get default graphics properties for each layer class."""
        from kipy.board.types import BoardLayerGraphicsDefaults
        cmd = board_commands_pb2.GetGraphicsDefaults()
        cmd.board.CopyFrom(self._board._doc)
        reply = self._board._kicad.send(cmd, board_commands_pb2.GraphicsDefaultsResponse)
        return {
            board_pb2.BoardLayerClass.BLC_SILKSCREEN:  BoardLayerGraphicsDefaults(reply.defaults.layers[0]),
            board_pb2.BoardLayerClass.BLC_COPPER:      BoardLayerGraphicsDefaults(reply.defaults.layers[1]),
            board_pb2.BoardLayerClass.BLC_EDGES:       BoardLayerGraphicsDefaults(reply.defaults.layers[2]),
            board_pb2.BoardLayerClass.BLC_COURTYARD:   BoardLayerGraphicsDefaults(reply.defaults.layers[3]),
            board_pb2.BoardLayerClass.BLC_FABRICATION: BoardLayerGraphicsDefaults(reply.defaults.layers[4]),
            board_pb2.BoardLayerClass.BLC_OTHER:       BoardLayerGraphicsDefaults(reply.defaults.layers[5])
        }

    def set_graphics_defaults(
        self,
        defaults: Dict[int, "BoardLayerGraphicsDefaults"],
    ) -> Dict[int, "BoardLayerGraphicsDefaults"]:
        """Set default graphics properties for layer classes.

        Args:
            defaults: Dictionary mapping BoardLayerClass to BoardLayerGraphicsDefaults

        Returns:
            The updated graphics defaults
        """
        from kipy.board.types import BoardLayerGraphicsDefaults
        cmd = board_commands_pb2.SetGraphicsDefaults()
        cmd.board.CopyFrom(self._board._doc)
        for layer_class, layer_defaults in defaults.items():
            layer_proto = cmd.defaults.layers.add()
            layer_proto.CopyFrom(layer_defaults.proto)
        response = self._board._kicad.send(cmd, board_commands_pb2.GraphicsDefaultsResponse)
        return {
            board_pb2.BoardLayerClass.BLC_SILKSCREEN:  BoardLayerGraphicsDefaults(response.defaults.layers[0]),
            board_pb2.BoardLayerClass.BLC_COPPER:      BoardLayerGraphicsDefaults(response.defaults.layers[1]),
            board_pb2.BoardLayerClass.BLC_EDGES:       BoardLayerGraphicsDefaults(response.defaults.layers[2]),
            board_pb2.BoardLayerClass.BLC_COURTYARD:   BoardLayerGraphicsDefaults(response.defaults.layers[3]),
            board_pb2.BoardLayerClass.BLC_FABRICATION: BoardLayerGraphicsDefaults(response.defaults.layers[4]),
            board_pb2.BoardLayerClass.BLC_OTHER:       BoardLayerGraphicsDefaults(response.defaults.layers[5])
        }

    def get_layers_info(self) -> List[dict]:
        """Get detailed information about all enabled board layers.

        Returns:
            List of dicts with layer info: layer (enum), name, user_name, type, enabled, visible

        Example:
            >>> layers = board.layers.get_layers_info()
            >>> for layer in layers:
            ...     print(f"{layer['name']}: {layer['type']}")
        """
        cmd = board_commands_pb2.GetBoardLayersInfo()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.BoardLayersInfoResponse)

        # Map layer type enum to string
        type_names = {
            board_commands_pb2.BoardLayerType.BLT_UNDEFINED: "undefined",
            board_commands_pb2.BoardLayerType.BLT_SIGNAL: "signal",
            board_commands_pb2.BoardLayerType.BLT_POWER: "power",
            board_commands_pb2.BoardLayerType.BLT_MIXED: "mixed",
            board_commands_pb2.BoardLayerType.BLT_JUMPER: "jumper",
            board_commands_pb2.BoardLayerType.BLT_AUX: "auxiliary",
            board_commands_pb2.BoardLayerType.BLT_FRONT: "front",
            board_commands_pb2.BoardLayerType.BLT_BACK: "back",
        }

        return [
            {
                "layer": info.layer,
                "layer_name": board_types_pb2.BoardLayer.Name(info.layer),
                "name": info.name,
                "user_name": info.user_name,
                "type": type_names.get(info.type, "unknown"),
                "enabled": info.enabled,
                "visible": info.visible,
            }
            for info in response.layers
        ]

    def set_layer_name(self, layer: board_types_pb2.BoardLayer.ValueType, name: str) -> None:
        """Set a custom name for a layer.

        Args:
            layer: The BoardLayer enum value (e.g., BoardLayer.BL_F_Cu)
            name: The new custom name (empty string to reset to default)

        Example:
            >>> from kipy.proto.board.board_types_pb2 import BoardLayer
            >>> board.layers.set_layer_name(BoardLayer.BL_F_Cu, "Top Signal")
        """
        cmd = board_commands_pb2.SetLayerName()
        cmd.board.CopyFrom(self._board._doc)
        cmd.layer = layer
        cmd.name = name
        self._board._kicad.send(cmd, Empty)

    def set_layer_type(self, layer: board_types_pb2.BoardLayer.ValueType, layer_type: str) -> None:
        """Set the type for a copper layer.

        Args:
            layer: The BoardLayer enum value (must be a copper layer)
            layer_type: One of: "signal", "power", "mixed", "jumper"

        Example:
            >>> from kipy.proto.board.board_types_pb2 import BoardLayer
            >>> board.layers.set_layer_type(BoardLayer.BL_In1_Cu, "power")
        """
        type_map = {
            "signal": board_commands_pb2.BoardLayerType.BLT_SIGNAL,
            "power": board_commands_pb2.BoardLayerType.BLT_POWER,
            "mixed": board_commands_pb2.BoardLayerType.BLT_MIXED,
            "jumper": board_commands_pb2.BoardLayerType.BLT_JUMPER,
        }

        if layer_type.lower() not in type_map:
            raise ValueError(f"Invalid layer type: {layer_type}. Must be one of: {', '.join(type_map.keys())}")

        cmd = board_commands_pb2.SetLayerType()
        cmd.board.CopyFrom(self._board._doc)
        cmd.layer = layer
        cmd.type = type_map[layer_type.lower()]
        self._board._kicad.send(cmd, Empty)

    def get_zone_hatch_offsets(self) -> List[dict]:
        """Get zone hatch fill offsets for all copper layers.

        Returns:
            List of dicts with layer and offset: {'layer': BoardLayer, 'layer_name': str, 'offset_x': int, 'offset_y': int}
            Offsets are in nanometers.

        Example:
            >>> offsets = board.layers.get_zone_hatch_offsets()
            >>> for offset in offsets:
            ...     print(f"{offset['layer_name']}: ({offset['offset_x']}, {offset['offset_y']})")
        """
        cmd = board_commands_pb2.GetZoneHatchOffsets()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.ZoneHatchOffsetsResponse)

        return [
            {
                "layer": offset.layer,
                "layer_name": board_types_pb2.BoardLayer.Name(offset.layer),
                "offset_x": offset.offset.x_nm,
                "offset_y": offset.offset.y_nm,
            }
            for offset in response.layer_offsets
        ]

    def set_zone_hatch_offsets(self, layer_offsets: List[dict]) -> List[dict]:
        """Set zone hatch fill offsets for specific copper layers.

        Args:
            layer_offsets: List of dicts with 'layer' (BoardLayer enum or name str) and 'offset_x', 'offset_y' (in nanometers)

        Returns:
            Updated list of all zone hatch offsets

        Example:
            >>> from kipy.proto.board.board_types_pb2 import BoardLayer
            >>> board.layers.set_zone_hatch_offsets([
            ...     {'layer': BoardLayer.BL_F_Cu, 'offset_x': 100000, 'offset_y': 50000},
            ...     {'layer': 'BL_B_Cu', 'offset_x': 0, 'offset_y': 0},
            ... ])
        """
        cmd = board_commands_pb2.SetZoneHatchOffsets()
        cmd.board.CopyFrom(self._board._doc)

        for offset in layer_offsets:
            proto_offset = cmd.layer_offsets.add()

            # Handle layer as enum or string
            layer = offset['layer']
            if isinstance(layer, str):
                layer = board_types_pb2.BoardLayer.Value(layer)
            proto_offset.layer = layer

            proto_offset.offset.x_nm = offset.get('offset_x', 0)
            proto_offset.offset.y_nm = offset.get('offset_y', 0)

        response = self._board._kicad.send(cmd, board_commands_pb2.ZoneHatchOffsetsResponse)

        return [
            {
                "layer": off.layer,
                "layer_name": board_types_pb2.BoardLayer.Name(off.layer),
                "offset_x": off.offset.x_nm,
                "offset_y": off.offset.y_nm,
            }
            for off in response.layer_offsets
        ]
