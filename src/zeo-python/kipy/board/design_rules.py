# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Board design constraints operations.
"""

from typing import TYPE_CHECKING

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board
    from kipy.board.types import BoardDesignRules


class DesignRulesOperations:
    """Board design constraints operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get(self) -> "BoardDesignRules":
        """Get all design rules/constraints for the board.

        Returns:
            BoardDesignRules object with all design constraints
        """
        from kipy.board.types import BoardDesignRules
        cmd = board_commands_pb2.GetDesignRules()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.DesignRulesResponse)
        return BoardDesignRules(response.rules)

    def set(self, rules: "BoardDesignRules") -> "BoardDesignRules":
        """Set the design rules/constraints for the board.

        Args:
            rules: BoardDesignRules object with the desired constraints

        Returns:
            The updated BoardDesignRules (may differ if values were clamped)
        """
        from kipy.board.types import BoardDesignRules
        cmd = board_commands_pb2.SetDesignRules()
        cmd.board.CopyFrom(self._board._doc)
        cmd.rules.CopyFrom(rules.proto)
        response = self._board._kicad.send(cmd, board_commands_pb2.DesignRulesResponse)
        return BoardDesignRules(response.rules)

    # Convenience properties for common rules

    def get_min_clearance(self) -> int:
        """Get minimum copper-to-copper clearance in nanometers."""
        return self.get().min_clearance

    def set_min_clearance(self, value: int):
        """Set minimum copper-to-copper clearance in nanometers."""
        rules = self.get()
        rules.min_clearance = value
        self.set(rules)

    def get_min_track_width(self) -> int:
        """Get minimum track width in nanometers."""
        return self.get().min_track_width

    def set_min_track_width(self, value: int):
        """Set minimum track width in nanometers."""
        rules = self.get()
        rules.min_track_width = value
        self.set(rules)

    def get_min_via_diameter(self) -> int:
        """Get minimum via pad diameter in nanometers."""
        return self.get().min_via_diameter

    def set_min_via_diameter(self, value: int):
        """Set minimum via pad diameter in nanometers."""
        rules = self.get()
        rules.min_via_diameter = value
        self.set(rules)

    def get_min_via_drill(self) -> int:
        """Get minimum via drill diameter in nanometers."""
        return self.get().min_via_drill

    def set_min_via_drill(self, value: int):
        """Set minimum via drill diameter in nanometers."""
        rules = self.get()
        rules.min_via_drill = value
        self.set(rules)

    def get_copper_edge_clearance(self) -> int:
        """Get minimum copper-to-edge clearance in nanometers."""
        return self.get().copper_edge_clearance

    def set_copper_edge_clearance(self, value: int):
        """Set minimum copper-to-edge clearance in nanometers."""
        rules = self.get()
        rules.copper_edge_clearance = value
        self.set(rules)

    # Signal-integrity analysis parameters (impedance / transmission-line calculation).
    # Board-wide inputs distinct from the per-dielectric Dk/Df and per-layer thickness in
    # the stackup. See Board Setup -> Board Stackup -> Impedance.

    def get_signal_integrity_params(self) -> dict:
        """Get the board's signal-integrity analysis parameters.

        Returns:
            dict with reference_frequency_hz, dk_measurement_frequency_hz,
            conductor_resistivity_ohm_m, conductor_roughness_m
        """
        cmd = board_commands_pb2.GetSignalIntegrityParams()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(
            cmd, board_commands_pb2.SignalIntegrityParamsResponse)
        p = response.params
        return {
            "reference_frequency_hz": p.reference_frequency_hz,
            "dk_measurement_frequency_hz": p.dk_measurement_frequency_hz,
            "conductor_resistivity_ohm_m": p.conductor_resistivity_ohm_m,
            "conductor_roughness_m": p.conductor_roughness_m,
        }

    def set_signal_integrity_params(
        self,
        reference_frequency_hz: "float | None" = None,
        dk_measurement_frequency_hz: "float | None" = None,
        conductor_resistivity_ohm_m: "float | None" = None,
        conductor_roughness_m: "float | None" = None,
    ) -> dict:
        """Set signal-integrity analysis parameters. Only provided values are updated.

        Returns:
            dict of the full updated parameter set
        """
        cmd = board_commands_pb2.SetSignalIntegrityParams()
        cmd.board.CopyFrom(self._board._doc)

        if reference_frequency_hz is not None:
            cmd.params.reference_frequency_hz = reference_frequency_hz
        if dk_measurement_frequency_hz is not None:
            cmd.params.dk_measurement_frequency_hz = dk_measurement_frequency_hz
        if conductor_resistivity_ohm_m is not None:
            cmd.params.conductor_resistivity_ohm_m = conductor_resistivity_ohm_m
        if conductor_roughness_m is not None:
            cmd.params.conductor_roughness_m = conductor_roughness_m

        response = self._board._kicad.send(
            cmd, board_commands_pb2.SignalIntegrityParamsResponse)
        p = response.params
        return {
            "reference_frequency_hz": p.reference_frequency_hz,
            "dk_measurement_frequency_hz": p.dk_measurement_frequency_hz,
            "conductor_resistivity_ohm_m": p.conductor_resistivity_ohm_m,
            "conductor_roughness_m": p.conductor_roughness_m,
        }
