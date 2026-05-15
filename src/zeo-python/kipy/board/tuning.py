# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Length tuning pattern and tuning profile operations.
"""

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Optional, List

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Enum mappings
class MeanderCornerStyle:
    ROUND = board_commands_pb2.MCS_ROUND
    CHAMFER = board_commands_pb2.MCS_CHAMFER

    _TO_STRING = {
        board_commands_pb2.MCS_ROUND: 'round',
        board_commands_pb2.MCS_CHAMFER: 'chamfer',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'round')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.ROUND)


class TuningProfileType:
    SINGLE = board_commands_pb2.TPT_SINGLE
    DIFFERENTIAL = board_commands_pb2.TPT_DIFFERENTIAL

    _TO_STRING = {
        board_commands_pb2.TPT_SINGLE: 'single',
        board_commands_pb2.TPT_DIFFERENTIAL: 'differential',
    }
    _FROM_STRING = {v: k for k, v in _TO_STRING.items()}

    @classmethod
    def to_string(cls, value: int) -> str:
        return cls._TO_STRING.get(value, 'single')

    @classmethod
    def from_string(cls, value: str) -> int:
        return cls._FROM_STRING.get(value, cls.SINGLE)


@dataclass
class MeanderPatternSettings:
    """Settings for a meander pattern type."""

    min_amplitude_nm: Optional[int] = None
    max_amplitude_nm: Optional[int] = None
    spacing_nm: Optional[int] = None
    corner_style: Optional[int] = None
    corner_radius_percent: Optional[int] = None
    single_sided: Optional[bool] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.MeanderPatternSettings) -> "MeanderPatternSettings":
        """Create from protobuf message."""
        return cls(
            min_amplitude_nm=proto.min_amplitude_nm if proto.HasField("min_amplitude_nm") else None,
            max_amplitude_nm=proto.max_amplitude_nm if proto.HasField("max_amplitude_nm") else None,
            spacing_nm=proto.spacing_nm if proto.HasField("spacing_nm") else None,
            corner_style=proto.corner_style if proto.HasField("corner_style") else None,
            corner_radius_percent=proto.corner_radius_percent if proto.HasField("corner_radius_percent") else None,
            single_sided=proto.single_sided if proto.HasField("single_sided") else None,
        )

    def to_proto(self, proto: Optional[board_commands_pb2.MeanderPatternSettings] = None) -> board_commands_pb2.MeanderPatternSettings:
        """Convert to protobuf message."""
        if proto is None:
            proto = board_commands_pb2.MeanderPatternSettings()
        if self.min_amplitude_nm is not None:
            proto.min_amplitude_nm = self.min_amplitude_nm
        if self.max_amplitude_nm is not None:
            proto.max_amplitude_nm = self.max_amplitude_nm
        if self.spacing_nm is not None:
            proto.spacing_nm = self.spacing_nm
        if self.corner_style is not None:
            proto.corner_style = self.corner_style
        if self.corner_radius_percent is not None:
            proto.corner_radius_percent = self.corner_radius_percent
        if self.single_sided is not None:
            proto.single_sided = self.single_sided
        return proto

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        result = {}
        if self.min_amplitude_nm is not None:
            result["min_amplitude_nm"] = self.min_amplitude_nm
        if self.max_amplitude_nm is not None:
            result["max_amplitude_nm"] = self.max_amplitude_nm
        if self.spacing_nm is not None:
            result["spacing_nm"] = self.spacing_nm
        if self.corner_style is not None:
            result["corner_style"] = MeanderCornerStyle.to_string(self.corner_style)
        if self.corner_radius_percent is not None:
            result["corner_radius_percent"] = self.corner_radius_percent
        if self.single_sided is not None:
            result["single_sided"] = self.single_sided
        return result


@dataclass
class LengthTuningPatternSettings:
    """Complete length tuning pattern settings."""

    single_track: Optional[MeanderPatternSettings] = None
    diff_pair: Optional[MeanderPatternSettings] = None
    diff_pair_skew: Optional[MeanderPatternSettings] = None

    @classmethod
    def from_proto(cls, proto: board_commands_pb2.LengthTuningPatternSettings) -> "LengthTuningPatternSettings":
        """Create from protobuf message."""
        return cls(
            single_track=MeanderPatternSettings.from_proto(proto.single_track),
            diff_pair=MeanderPatternSettings.from_proto(proto.diff_pair),
            diff_pair_skew=MeanderPatternSettings.from_proto(proto.diff_pair_skew),
        )

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "single_track": self.single_track.to_dict() if self.single_track else {},
            "diff_pair": self.diff_pair.to_dict() if self.diff_pair else {},
            "diff_pair_skew": self.diff_pair_skew.to_dict() if self.diff_pair_skew else {},
        }


@dataclass
class TuningProfileTrackEntry:
    """Track entry in a tuning profile."""

    signal_layer: int = 0
    top_reference_layer: int = 0
    bottom_reference_layer: int = 0
    width_nm: int = 0
    diff_pair_gap_nm: int = 0
    delay_ps_per_mm: float = 0.0
    enable_time_domain: bool = False

    @classmethod
    def from_proto(cls, proto) -> "TuningProfileTrackEntry":
        """Create from protobuf message."""
        return cls(
            signal_layer=proto.signal_layer.layer_id,
            top_reference_layer=proto.top_reference_layer.layer_id,
            bottom_reference_layer=proto.bottom_reference_layer.layer_id,
            width_nm=proto.width_nm,
            diff_pair_gap_nm=proto.diff_pair_gap_nm,
            delay_ps_per_mm=proto.delay_ps_per_mm,
            enable_time_domain=proto.enable_time_domain,
        )

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "signal_layer": self.signal_layer,
            "top_reference_layer": self.top_reference_layer,
            "bottom_reference_layer": self.bottom_reference_layer,
            "width_nm": self.width_nm,
            "diff_pair_gap_nm": self.diff_pair_gap_nm,
            "delay_ps_per_mm": self.delay_ps_per_mm,
            "enable_time_domain": self.enable_time_domain,
        }


@dataclass
class TuningProfileViaOverride:
    """Via override in a tuning profile."""

    signal_layer_from: int = 0
    signal_layer_to: int = 0
    via_layer_from: int = 0
    via_layer_to: int = 0
    delay_ps: float = 0.0

    @classmethod
    def from_proto(cls, proto) -> "TuningProfileViaOverride":
        """Create from protobuf message."""
        return cls(
            signal_layer_from=proto.signal_layer_from.layer_id,
            signal_layer_to=proto.signal_layer_to.layer_id,
            via_layer_from=proto.via_layer_from.layer_id,
            via_layer_to=proto.via_layer_to.layer_id,
            delay_ps=proto.delay_ps,
        )

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "signal_layer_from": self.signal_layer_from,
            "signal_layer_to": self.signal_layer_to,
            "via_layer_from": self.via_layer_from,
            "via_layer_to": self.via_layer_to,
            "delay_ps": self.delay_ps,
        }


@dataclass
class TuningProfile:
    """A tuning profile for length/timing matching."""

    name: str = ""
    type: int = TuningProfileType.SINGLE
    target_impedance_ohms: float = 0.0
    enable_time_domain_tuning: bool = False
    via_propagation_delay_ps: float = 0.0
    track_entries: List[TuningProfileTrackEntry] = field(default_factory=list)
    via_overrides: List[TuningProfileViaOverride] = field(default_factory=list)

    @classmethod
    def from_proto(cls, proto) -> "TuningProfile":
        """Create from protobuf message."""
        return cls(
            name=proto.name,
            type=proto.type,
            target_impedance_ohms=proto.target_impedance_ohms,
            enable_time_domain_tuning=proto.enable_time_domain_tuning,
            via_propagation_delay_ps=proto.via_propagation_delay_ps,
            track_entries=[TuningProfileTrackEntry.from_proto(e) for e in proto.track_entries],
            via_overrides=[TuningProfileViaOverride.from_proto(o) for o in proto.via_overrides],
        )

    def to_dict(self) -> dict:
        """Convert to dictionary with string enum names."""
        return {
            "name": self.name,
            "type": TuningProfileType.to_string(self.type),
            "target_impedance_ohms": self.target_impedance_ohms,
            "enable_time_domain_tuning": self.enable_time_domain_tuning,
            "via_propagation_delay_ps": self.via_propagation_delay_ps,
            "track_entries": [e.to_dict() for e in self.track_entries],
            "via_overrides": [o.to_dict() for o in self.via_overrides],
        }


class TuningOperations:
    """Length tuning pattern and tuning profile operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_pattern_settings(self) -> LengthTuningPatternSettings:
        """Get length tuning pattern settings.

        Returns:
            LengthTuningPatternSettings with current settings
        """
        cmd = board_commands_pb2.GetLengthTuningPatternSettings()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.LengthTuningPatternSettingsResponse)
        return LengthTuningPatternSettings.from_proto(response.settings)

    def set_pattern_settings(
        self,
        single_track: Optional[MeanderPatternSettings] = None,
        diff_pair: Optional[MeanderPatternSettings] = None,
        diff_pair_skew: Optional[MeanderPatternSettings] = None,
    ) -> LengthTuningPatternSettings:
        """Set length tuning pattern settings (partial update).

        Args:
            single_track: Settings for single track tuning
            diff_pair: Settings for differential pair tuning
            diff_pair_skew: Settings for differential pair skew tuning

        Returns:
            Updated LengthTuningPatternSettings
        """
        cmd = board_commands_pb2.SetLengthTuningPatternSettings()
        cmd.board.CopyFrom(self._board._doc)

        if single_track is not None:
            single_track.to_proto(cmd.single_track)
        if diff_pair is not None:
            diff_pair.to_proto(cmd.diff_pair)
        if diff_pair_skew is not None:
            diff_pair_skew.to_proto(cmd.diff_pair_skew)

        response = self._board._kicad.send(cmd, board_commands_pb2.LengthTuningPatternSettingsResponse)
        return LengthTuningPatternSettings.from_proto(response.settings)

    def get_profiles(self) -> List[TuningProfile]:
        """Get all tuning profiles.

        Returns:
            List of TuningProfile objects
        """
        cmd = board_commands_pb2.GetTuningProfiles()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.TuningProfilesResponse)
        return [TuningProfile.from_proto(p) for p in response.profiles.profiles]

    def set_profiles(self, profiles: List[TuningProfile]) -> List[TuningProfile]:
        """Set (replace) all tuning profiles.

        Args:
            profiles: List of TuningProfile objects

        Returns:
            Updated list of TuningProfile objects
        """
        cmd = board_commands_pb2.SetTuningProfiles()
        cmd.board.CopyFrom(self._board._doc)

        for profile in profiles:
            p = cmd.profiles.profiles.add()
            p.name = profile.name
            p.type = profile.type
            p.target_impedance_ohms = profile.target_impedance_ohms
            p.enable_time_domain_tuning = profile.enable_time_domain_tuning
            p.via_propagation_delay_ps = profile.via_propagation_delay_ps

            for entry in profile.track_entries:
                e = p.track_entries.add()
                e.signal_layer.layer_id = entry.signal_layer
                e.top_reference_layer.layer_id = entry.top_reference_layer
                e.bottom_reference_layer.layer_id = entry.bottom_reference_layer
                e.width_nm = entry.width_nm
                e.diff_pair_gap_nm = entry.diff_pair_gap_nm
                e.delay_ps_per_mm = entry.delay_ps_per_mm
                e.enable_time_domain = entry.enable_time_domain

            for override in profile.via_overrides:
                o = p.via_overrides.add()
                o.signal_layer_from.layer_id = override.signal_layer_from
                o.signal_layer_to.layer_id = override.signal_layer_to
                o.via_layer_from.layer_id = override.via_layer_from
                o.via_layer_to.layer_id = override.via_layer_to
                o.delay_ps = override.delay_ps

        response = self._board._kicad.send(cmd, board_commands_pb2.TuningProfilesResponse)
        return [TuningProfile.from_proto(p) for p in response.profiles.profiles]
