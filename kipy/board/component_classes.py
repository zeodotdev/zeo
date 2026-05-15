# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Component class operations.
"""

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Optional, List

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Enum mappings
class ComponentClassConditionType:
    REFERENCE = board_commands_pb2.CCCT_REFERENCE
    FOOTPRINT = board_commands_pb2.CCCT_FOOTPRINT
    SIDE = board_commands_pb2.CCCT_SIDE
    ROTATION = board_commands_pb2.CCCT_ROTATION
    FOOTPRINT_FIELD = board_commands_pb2.CCCT_FOOTPRINT_FIELD
    SHEET_NAME = board_commands_pb2.CCCT_SHEET_NAME
    CUSTOM = board_commands_pb2.CCCT_CUSTOM

    _TO_STRING = {
        board_commands_pb2.CCCT_REFERENCE: 'reference',
        board_commands_pb2.CCCT_FOOTPRINT: 'footprint',
        board_commands_pb2.CCCT_SIDE: 'side',
        board_commands_pb2.CCCT_ROTATION: 'rotation',
        board_commands_pb2.CCCT_FOOTPRINT_FIELD: 'footprint_field',
        board_commands_pb2.CCCT_SHEET_NAME: 'sheet_name',
        board_commands_pb2.CCCT_CUSTOM: 'custom',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'reference')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.REFERENCE)


class ComponentClassConditionsOperator:
    """Operator for combining conditions (ALL = AND, ANY = OR)."""
    ALL = board_commands_pb2.CCCO_ALL
    ANY = board_commands_pb2.CCCO_ANY

    _TO_STRING = {
        board_commands_pb2.CCCO_ALL: 'all',
        board_commands_pb2.CCCO_ANY: 'any',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'all')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.ALL)


@dataclass
class ComponentClassCondition:
    """A condition for component class assignment."""

    type: int = ComponentClassConditionType.REFERENCE
    primary_data: str = ""
    secondary_data: str = ""

    @classmethod
    def from_proto(cls, proto) -> "ComponentClassCondition":
        """Create from protobuf message."""
        return cls(
            type=proto.type,
            primary_data=proto.primary_data,
            secondary_data=proto.secondary_data,
        )

    def to_proto(self, proto=None):
        """Convert to protobuf message."""
        if proto is None:
            proto = board_commands_pb2.ComponentClassCondition()
        proto.type = self.type
        proto.primary_data = self.primary_data
        proto.secondary_data = self.secondary_data
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        return {
            "type": ComponentClassConditionType.to_string(self.type),
            "primary_data": self.primary_data,
            "secondary_data": self.secondary_data,
        }


@dataclass
class ComponentClassAssignment:
    """An assignment rule for component classes."""

    component_class: str = ""
    operator: int = ComponentClassConditionsOperator.ALL
    conditions: List[ComponentClassCondition] = field(default_factory=list)

    @classmethod
    def from_proto(cls, proto) -> "ComponentClassAssignment":
        """Create from protobuf message."""
        return cls(
            component_class=proto.component_class,
            operator=proto.operator,
            conditions=[ComponentClassCondition.from_proto(c) for c in proto.conditions],
        )

    def to_proto(self, proto=None):
        """Convert to protobuf message."""
        if proto is None:
            proto = board_commands_pb2.ComponentClassAssignment()
        proto.component_class = self.component_class
        proto.operator = self.operator
        for condition in self.conditions:
            c = proto.conditions.add()
            condition.to_proto(c)
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        return {
            "component_class": self.component_class,
            "operator": ComponentClassConditionsOperator.to_string(self.operator),
            "conditions": [c.to_dict() for c in self.conditions],
        }


@dataclass
class ComponentClassSettings:
    """Complete component class settings."""

    enable_sheet_component_classes: bool = False
    assignments: List[ComponentClassAssignment] = field(default_factory=list)

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.ComponentClassSettings) -> "ComponentClassSettings":
        """Create from protobuf message."""
        return cls(
            enable_sheet_component_classes=proto.enable_sheet_component_classes,
            assignments=[ComponentClassAssignment.from_proto(a) for a in proto.assignments],
        )

    def to_proto(self, proto=None) -> board_commands_pb2.ComponentClassSettings:
        """Convert to protobuf message."""
        if proto is None:
            proto = board_commands_pb2.ComponentClassSettings()
        proto.enable_sheet_component_classes = self.enable_sheet_component_classes
        for assignment in self.assignments:
            a = proto.assignments.add()
            assignment.to_proto(a)
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "enable_sheet_component_classes": self.enable_sheet_component_classes,
            "assignments": [a.to_dict() for a in self.assignments],
        }


class ComponentClassOperations:
    """Component class operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> ComponentClassSettings:
        """Get component class settings.

        Returns:
            ComponentClassSettings with current settings
        """
        cmd = board_commands_pb2.GetComponentClassSettings()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.ComponentClassSettingsResponse)
        return ComponentClassSettings.from_proto(response.settings)

    def set(self, settings: ComponentClassSettings) -> ComponentClassSettings:
        """Set component class settings.

        Args:
            settings: ComponentClassSettings to apply

        Returns:
            Updated ComponentClassSettings
        """
        cmd = board_commands_pb2.SetComponentClassSettings()
        cmd.board.CopyFrom(self._board._doc)
        settings.to_proto(cmd.settings)
        response = self._board._kicad.send(cmd, board_commands_pb2.ComponentClassSettingsResponse)
        return ComponentClassSettings.from_proto(response.settings)
