# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
SPICE simulation operations.
"""

from typing import TYPE_CHECKING, Optional, List, Dict

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class SimulationOperations:
    """SPICE simulation settings and execution."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Simulation Settings
    # =========================================================================

    def get_settings(self) -> Dict:
        """Get current simulation settings.

        Returns:
            Dict with spice_command, include_paths, model_overrides, simulator

        Example:
            >>> settings = sch.simulation.get_settings()
            >>> print(f"SPICE command: {settings['spice_command']}")
        """
        cmd = schematic_commands_pb2.GetSimulationSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetSimulationSettingsResponse)

        settings = response.settings
        return {
            'spice_command': settings.spice_command,
            'include_paths': list(settings.include_paths),
            'model_overrides': dict(settings.model_overrides),
            'simulator': settings.simulator if settings.HasField('simulator') else 'ngspice',
        }

    def set_settings(
        self,
        spice_command: Optional[str] = None,
        include_paths: Optional[List[str]] = None,
        model_overrides: Optional[Dict[str, str]] = None,
    ) -> None:
        """Set simulation settings.

        Args:
            spice_command: Default SPICE command (e.g., ".tran 1u 1m")
            include_paths: Paths to search for SPICE models
            model_overrides: Map of symbol names to model names

        Example:
            >>> sch.simulation.set_settings(
            ...     spice_command=".tran 1u 10m",
            ...     include_paths=["/path/to/models"]
            ... )
        """
        cmd = schematic_commands_pb2.SetSimulationSettings()
        cmd.document.CopyFrom(self._doc)

        if spice_command is not None:
            cmd.settings.spice_command = spice_command

        if include_paths is not None:
            for path in include_paths:
                cmd.settings.include_paths.append(path)

        if model_overrides is not None:
            for symbol, model in model_overrides.items():
                cmd.settings.model_overrides[symbol] = model

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Simulation Execution
    # =========================================================================

    def run(self, command_override: Optional[str] = None) -> Dict:
        """Run SPICE simulation.

        Note: This requires the simulator frame to be open in KiCad.
        For full simulation support, use Tools > Simulator in KiCad.

        Args:
            command_override: Override the default SPICE command

        Returns:
            Dict with success, error_message, and traces

        Example:
            >>> result = sch.simulation.run()
            >>> if result['success']:
            ...     for trace in result['traces']:
            ...         print(f"Trace: {trace['name']}")
        """
        cmd = schematic_commands_pb2.RunSimulation()
        cmd.document.CopyFrom(self._doc)

        if command_override is not None:
            cmd.command_override = command_override

        response = self._kicad.send(cmd, schematic_commands_pb2.RunSimulationResponse)

        traces = []
        for trace in response.traces:
            traces.append({
                'name': trace.name,
                'time_values': list(trace.time_values),
                'data_values': list(trace.data_values),
            })

        return {
            'success': response.success,
            'error_message': response.error_message,
            'traces': traces,
        }

    def get_results(self) -> Dict:
        """Get results from previous simulation.

        Returns:
            Dict with has_results and traces

        Example:
            >>> results = sch.simulation.get_results()
            >>> if results['has_results']:
            ...     for trace in results['traces']:
            ...         print(f"Trace: {trace['name']}")
        """
        cmd = schematic_commands_pb2.GetSimulationResults()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetSimulationResultsResponse)

        traces = []
        for trace in response.traces:
            traces.append({
                'name': trace.name,
                'time_values': list(trace.time_values),
                'data_values': list(trace.data_values),
            })

        return {
            'has_results': response.has_results,
            'traces': traces,
        }
