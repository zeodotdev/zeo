# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Board type wrappers and data classes.

This module contains wrapper classes for board-related protobuf messages.
"""

from typing import List, Optional

from kipy.geometry import Vector2
from kipy.common_types import Color, TextAttributes
from kipy.wrapper import Wrapper
from kipy.proto.common.types import KIID
from kipy.proto.board import board_pb2, board_commands_pb2
from kipy.proto.board.board_types_pb2 import BoardLayer


class BoardLayerGraphicsDefaults(Wrapper):
    """Default properties for graphic items on a given layer class."""

    def __init__(self, proto: Optional[board_pb2.BoardLayerGraphicsDefaults] = None):
        self._proto = board_pb2.BoardLayerGraphicsDefaults()
        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def layer(self) -> board_pb2.BoardLayerClass.ValueType:
        """The layer class these defaults apply to."""
        return self._proto.layer

    @layer.setter
    def layer(self, value: board_pb2.BoardLayerClass.ValueType):
        self._proto.layer = value

    @property
    def line_thickness(self) -> int:
        """Default line thickness in nanometers."""
        return self._proto.line_thickness.value_nm

    @line_thickness.setter
    def line_thickness(self, value: int):
        self._proto.line_thickness.value_nm = value

    @property
    def text(self) -> TextAttributes:
        """Default text attributes."""
        return TextAttributes(self._proto.text)


class BoardStackupDielectricProperties(Wrapper):
    """Properties of a dielectric sub-layer."""

    def __init__(self, proto: Optional[board_pb2.BoardStackupDielectricProperties] = None):
        self._proto = board_pb2.BoardStackupDielectricProperties()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def epsilon_r(self) -> float:
        return self._proto.epsilon_r

    @epsilon_r.setter
    def epsilon_r(self, value: float):
        self._proto.epsilon_r = value

    @property
    def loss_tangent(self) -> float:
        return self._proto.loss_tangent

    @loss_tangent.setter
    def loss_tangent(self, value: float):
        self._proto.loss_tangent = value

    @property
    def material_name(self) -> str:
        return self._proto.material_name

    @material_name.setter
    def material_name(self, name: str):
        self._proto.material_name = name

    @property
    def thickness(self) -> int:
        return self._proto.thickness.value_nm

    @thickness.setter
    def thickness(self, thickness: int):
        self._proto.thickness.value_nm = thickness


class BoardStackupDielectricLayer(Wrapper):
    """A dielectric layer which may have multiple sub-layers."""

    def __init__(self, proto: Optional[board_pb2.BoardStackupDielectricLayer] = None):
        self._proto = board_pb2.BoardStackupDielectricLayer()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def layers(self) -> List[BoardStackupDielectricProperties]:
        return [BoardStackupDielectricProperties(layer) for layer in self._proto.layer]


class BoardStackupLayer(Wrapper):
    """A layer in the board stackup."""

    def __init__(self, proto: Optional[board_pb2.BoardStackupLayer] = None):
        self._proto = board_pb2.BoardStackupLayer()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return (
            f"BoardStackupLayer(layer={BoardLayer.Name(self.layer)}, user_name={self.user_name},"
            f"thickness={self.thickness}, enabled={self.enabled})"
        )

    @property
    def thickness(self) -> int:
        """Total thickness in nanometers."""
        return self._proto.thickness.value_nm

    @thickness.setter
    def thickness(self, value: int):
        self._proto.thickness.value_nm = value

    @property
    def layer(self) -> BoardLayer.ValueType:
        """Board layer, or BL_UNDEFINED for dielectric layers."""
        return self._proto.layer

    @layer.setter
    def layer(self, value: BoardLayer.ValueType):
        self._proto.layer = value

    @property
    def enabled(self) -> bool:
        return self._proto.enabled

    @enabled.setter
    def enabled(self, value: bool):
        self._proto.enabled = value

    @property
    def type(self) -> board_pb2.BoardStackupLayerType.ValueType:
        return self._proto.type

    @type.setter
    def type(self, value: board_pb2.BoardStackupLayerType.ValueType):
        self._proto.type = value

    @property
    def dielectric(self) -> BoardStackupDielectricLayer:
        return BoardStackupDielectricLayer(self._proto.dielectric)

    @property
    def color(self) -> Color:
        return Color(self._proto.color)

    @color.setter
    def color(self, value: Color):
        self._proto.color.CopyFrom(value.proto)

    @property
    def material_name(self) -> str:
        return self._proto.material_name

    @material_name.setter
    def material_name(self, value: str):
        self._proto.material_name = value

    @property
    def user_name(self) -> str:
        """User-customized layer name."""
        return self._proto.user_name

    @user_name.setter
    def user_name(self, value: str):
        self._proto.user_name = value


class BoardStackup(Wrapper):
    """Board stackup configuration."""

    def __init__(self, proto: Optional[board_pb2.BoardStackup] = None):
        self._proto = board_pb2.BoardStackup()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return f"BoardStackup(layers={len(self.layers)}, impedance_controlled={self.impedance_controlled})"

    @property
    def layers(self) -> List[BoardStackupLayer]:
        """Stackup layers, from top to bottom."""
        return [BoardStackupLayer(layer) for layer in self._proto.layers]

    @property
    def impedance_controlled(self) -> bool:
        """Whether impedance control is enabled."""
        return self._proto.impedance.is_controlled

    @impedance_controlled.setter
    def impedance_controlled(self, value: bool):
        self._proto.impedance.is_controlled = value

    @property
    def finish_type(self) -> str:
        """Board finish type name (e.g., 'HASL', 'ENIG', etc.)."""
        return self._proto.finish.type_name

    @finish_type.setter
    def finish_type(self, value: str):
        self._proto.finish.type_name = value

    @property
    def has_edge_plating(self) -> bool:
        """Whether the board has edge plating."""
        return self._proto.edge.plating.has_edge_plating

    @has_edge_plating.setter
    def has_edge_plating(self, value: bool):
        self._proto.edge.plating.has_edge_plating = value

    @property
    def edge_connector(self) -> str:
        """Edge connector constraints: 'none', 'in_use', or 'bevelled'."""
        constraints = self._proto.edge.connector.constraints
        if constraints == board_pb2.EdgeConnectorConstraints.ECC_IN_USE:
            return "in_use"
        elif constraints == board_pb2.EdgeConnectorConstraints.ECC_BEVELLED:
            return "bevelled"
        return "none"

    @edge_connector.setter
    def edge_connector(self, value: str):
        """Set edge connector constraints: 'none', 'in_use', or 'bevelled'."""
        value_lower = value.lower()
        if value_lower == "in_use" or value_lower == "yes":
            self._proto.edge.connector.constraints = board_pb2.EdgeConnectorConstraints.ECC_IN_USE
        elif value_lower == "bevelled":
            self._proto.edge.connector.constraints = board_pb2.EdgeConnectorConstraints.ECC_BEVELLED
        else:
            self._proto.edge.connector.constraints = board_pb2.EdgeConnectorConstraints.ECC_NONE

    def get_layer_by_board_layer(self, board_layer: BoardLayer.ValueType) -> Optional[BoardStackupLayer]:
        """Get a stackup layer by its board layer enum.

        Args:
            board_layer: The BoardLayer enum value (e.g., BoardLayer.BL_F_Cu)

        Returns:
            The BoardStackupLayer if found, None otherwise
        """
        for layer in self._proto.layers:
            if layer.layer == board_layer:
                return BoardStackupLayer(layer)
        return None

    def get_dielectric_layers(self) -> List[BoardStackupLayer]:
        """Get all dielectric layers in the stackup."""
        return [
            BoardStackupLayer(layer)
            for layer in self._proto.layers
            if layer.type == board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC
        ]

    def calculate_board_thickness(self) -> int:
        """Calculate total board thickness from stackup layers (in nanometers)."""
        total = 0
        for layer in self._proto.layers:
            if layer.type == board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC:
                # Dielectric layers may have multiple sub-layers
                for sub in layer.dielectric.layer:
                    total += sub.thickness.value_nm
            else:
                total += layer.thickness.value_nm
        return total


class BoardDesignRules(Wrapper):
    """Board design rules and constraints."""

    def __init__(self, proto: Optional[board_commands_pb2.BoardDesignRules] = None):
        self._proto = board_commands_pb2.BoardDesignRules()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return f"BoardDesignRules(min_clearance={self.min_clearance}, min_track_width={self.min_track_width})"

    # Copper clearances
    @property
    def min_clearance(self) -> int:
        """Minimum copper-to-copper clearance (nm)."""
        return self._proto.min_clearance

    @min_clearance.setter
    def min_clearance(self, value: int):
        self._proto.min_clearance = value

    @property
    def min_track_width(self) -> int:
        """Minimum track width (nm)."""
        return self._proto.min_track_width

    @min_track_width.setter
    def min_track_width(self, value: int):
        self._proto.min_track_width = value

    @property
    def min_connection(self) -> int:
        """Minimum zone connection width (nm)."""
        return self._proto.min_connection

    @min_connection.setter
    def min_connection(self, value: int):
        self._proto.min_connection = value

    # Via constraints
    @property
    def min_via_diameter(self) -> int:
        """Minimum via pad diameter (nm)."""
        return self._proto.min_via_diameter

    @min_via_diameter.setter
    def min_via_diameter(self, value: int):
        self._proto.min_via_diameter = value

    @property
    def min_via_drill(self) -> int:
        """Minimum via drill diameter (nm)."""
        return self._proto.min_via_drill

    @min_via_drill.setter
    def min_via_drill(self, value: int):
        self._proto.min_via_drill = value

    @property
    def min_via_annular_width(self) -> int:
        """Minimum via annular ring (nm)."""
        return self._proto.min_via_annular_width

    @min_via_annular_width.setter
    def min_via_annular_width(self, value: int):
        self._proto.min_via_annular_width = value

    # Microvia constraints
    @property
    def min_microvia_diameter(self) -> int:
        return self._proto.min_microvia_diameter

    @min_microvia_diameter.setter
    def min_microvia_diameter(self, value: int):
        self._proto.min_microvia_diameter = value

    @property
    def min_microvia_drill(self) -> int:
        return self._proto.min_microvia_drill

    @min_microvia_drill.setter
    def min_microvia_drill(self, value: int):
        self._proto.min_microvia_drill = value

    # Hole constraints
    @property
    def min_through_hole(self) -> int:
        return self._proto.min_through_hole

    @min_through_hole.setter
    def min_through_hole(self, value: int):
        self._proto.min_through_hole = value

    @property
    def min_hole_to_hole(self) -> int:
        return self._proto.min_hole_to_hole

    @min_hole_to_hole.setter
    def min_hole_to_hole(self, value: int):
        self._proto.min_hole_to_hole = value

    @property
    def hole_to_copper_clearance(self) -> int:
        return self._proto.hole_to_copper_clearance

    @hole_to_copper_clearance.setter
    def hole_to_copper_clearance(self, value: int):
        self._proto.hole_to_copper_clearance = value

    # Silkscreen
    @property
    def min_silk_clearance(self) -> int:
        return self._proto.min_silk_clearance

    @min_silk_clearance.setter
    def min_silk_clearance(self, value: int):
        self._proto.min_silk_clearance = value

    @property
    def min_silk_text_height(self) -> int:
        return self._proto.min_silk_text_height

    @min_silk_text_height.setter
    def min_silk_text_height(self, value: int):
        self._proto.min_silk_text_height = value

    @property
    def min_silk_text_thickness(self) -> int:
        return self._proto.min_silk_text_thickness

    @min_silk_text_thickness.setter
    def min_silk_text_thickness(self, value: int):
        self._proto.min_silk_text_thickness = value

    # Board edge
    @property
    def copper_edge_clearance(self) -> int:
        return self._proto.copper_edge_clearance

    @copper_edge_clearance.setter
    def copper_edge_clearance(self, value: int):
        self._proto.copper_edge_clearance = value

    # Solder mask
    @property
    def solder_mask_expansion(self) -> int:
        return self._proto.solder_mask_expansion

    @solder_mask_expansion.setter
    def solder_mask_expansion(self, value: int):
        self._proto.solder_mask_expansion = value

    @property
    def solder_mask_min_width(self) -> int:
        return self._proto.solder_mask_min_width

    @solder_mask_min_width.setter
    def solder_mask_min_width(self, value: int):
        self._proto.solder_mask_min_width = value

    @property
    def solder_mask_to_copper_clearance(self) -> int:
        return self._proto.solder_mask_to_copper_clearance

    @solder_mask_to_copper_clearance.setter
    def solder_mask_to_copper_clearance(self, value: int):
        self._proto.solder_mask_to_copper_clearance = value

    @property
    def allow_soldermask_bridges_in_fps(self) -> bool:
        """Allow bridged solder mask apertures between pads within footprints."""
        return self._proto.allow_soldermask_bridges_in_fps

    @allow_soldermask_bridges_in_fps.setter
    def allow_soldermask_bridges_in_fps(self, value: bool):
        self._proto.allow_soldermask_bridges_in_fps = value

    @property
    def tent_vias_front(self) -> bool:
        """Tent vias on front side (cover with solder mask)."""
        return self._proto.tent_vias_front

    @tent_vias_front.setter
    def tent_vias_front(self, value: bool):
        self._proto.tent_vias_front = value

    @property
    def tent_vias_back(self) -> bool:
        """Tent vias on back side (cover with solder mask)."""
        return self._proto.tent_vias_back

    @tent_vias_back.setter
    def tent_vias_back(self, value: bool):
        self._proto.tent_vias_back = value

    # Solder paste
    @property
    def solder_paste_margin(self) -> int:
        return self._proto.solder_paste_margin

    @solder_paste_margin.setter
    def solder_paste_margin(self, value: int):
        self._proto.solder_paste_margin = value

    @property
    def solder_paste_margin_ratio(self) -> float:
        return self._proto.solder_paste_margin_ratio

    @solder_paste_margin_ratio.setter
    def solder_paste_margin_ratio(self, value: float):
        self._proto.solder_paste_margin_ratio = value

    # Thermals
    @property
    def min_resolved_spokes(self) -> int:
        return self._proto.min_resolved_spokes

    @min_resolved_spokes.setter
    def min_resolved_spokes(self, value: int):
        self._proto.min_resolved_spokes = value


class DRCViolation(Wrapper):
    """A single DRC violation/marker."""

    def __init__(self, proto: Optional[board_commands_pb2.DRCViolation] = None):
        self._proto = board_commands_pb2.DRCViolation()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        msg = self.message[:50] + "..." if len(self.message) > 50 else self.message
        return f"DRCViolation(type={self.error_type}, message={msg})"

    @property
    def id(self) -> KIID:
        return self._proto.id

    @property
    def severity(self) -> board_commands_pb2.DrcSeverity.ValueType:
        return self._proto.severity

    @property
    def error_code(self) -> int:
        return self._proto.error_code

    @property
    def error_type(self) -> str:
        return self._proto.error_type

    @property
    def error_type_code(self) -> str:
        """Stable, machine-friendly identifier for the violation type
        (e.g. 'annular_width', 'cross_board_net_mismatch'). Use this for
        structural assertions in tests instead of error_type (human text)
        or error_code (numeric, may shift across releases)."""
        return self._proto.error_type_code

    @property
    def message(self) -> str:
        return self._proto.message

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @property
    def item_ids(self) -> List[KIID]:
        return list(self._proto.items)


class DRCCheckSeverity(Wrapper):
    """DRC check severity setting for a single check."""

    def __init__(self, proto: Optional[board_commands_pb2.DRCCheckSeverity] = None):
        self._proto = board_commands_pb2.DRCCheckSeverity()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def check_name(self) -> str:
        return self._proto.check_name

    @check_name.setter
    def check_name(self, value: str):
        self._proto.check_name = value

    @property
    def severity(self) -> board_commands_pb2.DrcSeverity.ValueType:
        return self._proto.severity

    @severity.setter
    def severity(self, value: board_commands_pb2.DrcSeverity.ValueType):
        self._proto.severity = value


class DRCSettings(Wrapper):
    """DRC settings configuration."""

    def __init__(self, proto: Optional[board_commands_pb2.DRCSettingsData] = None):
        self._proto = board_commands_pb2.DRCSettingsData()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def check_severities(self) -> List[DRCCheckSeverity]:
        return [DRCCheckSeverity(cs) for cs in self._proto.check_severities]

    def set_check_severity(self, check_name: str, severity: board_commands_pb2.DrcSeverity.ValueType):
        """Set the severity for a specific DRC check."""
        for cs in self._proto.check_severities:
            if cs.check_name == check_name:
                cs.severity = severity
                return
        new_check = self._proto.check_severities.add()
        new_check.check_name = check_name
        new_check.severity = severity

    def get_check_severity(self, check_name: str) -> Optional[board_commands_pb2.DrcSeverity.ValueType]:
        """Get the severity for a specific DRC check."""
        for cs in self._proto.check_severities:
            if cs.check_name == check_name:
                return cs.severity
        return None


class PCBGridSettings(Wrapper):
    """PCB grid settings."""

    def __init__(self, proto: Optional[board_commands_pb2.PCBGridSettings] = None):
        self._proto = board_commands_pb2.PCBGridSettings()
        if proto:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return (
            f"PCBGridSettings(size_x={self.grid_size_x_nm}, size_y={self.grid_size_y_nm}, "
            f"visible={self.show_grid})"
        )

    @property
    def grid_size_x_nm(self) -> int:
        return self._proto.grid_size_x_nm

    @grid_size_x_nm.setter
    def grid_size_x_nm(self, value: int):
        self._proto.grid_size_x_nm = value

    @property
    def grid_size_y_nm(self) -> int:
        return self._proto.grid_size_y_nm

    @grid_size_y_nm.setter
    def grid_size_y_nm(self, value: int):
        self._proto.grid_size_y_nm = value

    @property
    def show_grid(self) -> bool:
        return self._proto.show_grid

    @show_grid.setter
    def show_grid(self, value: bool):
        self._proto.show_grid = value

    @property
    def style(self) -> board_commands_pb2.GridStyle.ValueType:
        return self._proto.style

    @style.setter
    def style(self, value: board_commands_pb2.GridStyle.ValueType):
        self._proto.style = value
