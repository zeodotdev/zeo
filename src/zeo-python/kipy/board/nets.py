# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Net queries and management operations.
"""

from typing import TYPE_CHECKING, Dict, List, Optional, Sequence, Union, overload

from kipy.board_types import Net
from kipy.project import NetClass
from kipy.proto.board import board_commands_pb2
from kipy.proto.board import board_types_pb2
from kipy.proto.common.commands import project_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


# Friendly names for the computed transmission-line model.
_IMPEDANCE_MODEL_NAMES = {
    board_commands_pb2.IM_NONE: "none",
    board_commands_pb2.IM_MICROSTRIP: "microstrip",
    board_commands_pb2.IM_STRIPLINE: "stripline",
    board_commands_pb2.IM_COPLANAR: "coplanar",
    board_commands_pb2.IM_GROUNDED_COPLANAR: "grounded_coplanar",
    board_commands_pb2.IM_COUPLED_MICROSTRIP: "coupled_microstrip",
    board_commands_pb2.IM_COUPLED_STRIPLINE: "coupled_stripline",
}


class NetOperations:
    """Net queries and management operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_all(
        self, netclass_filter: Optional[Union[str, Sequence[str]]] = None
    ) -> Sequence[Net]:
        """Get all nets on the board.

        Args:
            netclass_filter: Optional net class name(s) to filter by

        Returns:
            Sequence of Net objects
        """
        cmd = board_commands_pb2.GetNets()
        cmd.board.CopyFrom(self._board._doc)

        if isinstance(netclass_filter, str):
            cmd.netclass_filter.append(netclass_filter)
        elif netclass_filter is not None:
            cmd.netclass_filter.extend(netclass_filter)

        return [
            Net(net)
            for net in self._board._kicad.send(cmd, board_commands_pb2.NetsResponse).nets
        ]

    def get_net_impedance(self, net_name: Optional[str] = None) -> dict:
        """Get computed per-net average impedance for routed nets.

        Length-weighted, model-aware (differential pairs report differential impedance,
        coplanar traces their CPW impedance, otherwise single-ended Z0). Mirrors the Net
        Inspector's Average Impedance column.

        Args:
            net_name: Restrict to a single net; None returns all routed nets.

        Returns:
            dict with:
              stackup_has_dk: bool — False means the stackup has no dielectric constants,
                              so impedance can't be computed and 'nets' is empty.
              nets: list of {net_name, net_code, average_impedance_ohms, is_differential,
                            insertion_loss_db, routed_length_nm}
        """
        cmd = board_commands_pb2.GetNetImpedance()
        cmd.board.CopyFrom(self._board._doc)
        if net_name:
            cmd.net_name = net_name

        response = self._board._kicad.send(cmd, board_commands_pb2.NetImpedanceResponse)
        return {
            "stackup_has_dk": response.stackup_has_dk,
            "nets": [
                {
                    "net_name": n.net_name,
                    "net_code": n.net_code,
                    "average_impedance_ohms": n.average_impedance_ohms,
                    "is_differential": n.is_differential,
                    "insertion_loss_db": n.insertion_loss_db,
                    "routed_length_nm": n.routed_length_nm,
                }
                for n in response.nets
            ],
        }

    def get_track_impedance(self, net_name: str) -> dict:
        """Get computed per-track impedance for the segments of one net.

        Args:
            net_name: Required. The net whose track segments to report (bounds output size).

        Returns:
            dict with:
              stackup_has_dk: bool
              tracks: list of {track_id, layer, width_nm, length_nm, single_ended_ohms,
                              differential_ohms, model, insertion_loss_db_per_inch}
        """
        cmd = board_commands_pb2.GetTrackImpedance()
        cmd.board.CopyFrom(self._board._doc)
        cmd.net_name = net_name

        response = self._board._kicad.send(cmd, board_commands_pb2.TrackImpedanceResponse)
        return {
            "stackup_has_dk": response.stackup_has_dk,
            "tracks": [
                {
                    "track_id": t.track_id.value,
                    "layer": board_types_pb2.BoardLayer.Name(t.layer),
                    "width_nm": t.width_nm,
                    "length_nm": t.length_nm,
                    "single_ended_ohms": t.single_ended_ohms,
                    "differential_ohms": t.differential_ohms,
                    "model": _IMPEDANCE_MODEL_NAMES.get(t.model, "none"),
                    "insertion_loss_db_per_inch": t.insertion_loss_db_per_inch,
                }
                for t in response.tracks
            ],
        }

    def get_netclass_for_nets(self, nets: Union[Net, Sequence[Net]]) -> Dict[str, NetClass]:
        """Get the net class for one or more nets.

        Args:
            nets: Single net or sequence of nets

        Returns:
            Dictionary mapping net name to NetClass
        """
        cmd = board_commands_pb2.GetNetClassForNets()
        if isinstance(nets, Net):
            cmd.net.append(nets.proto)
        else:
            cmd.net.extend([net.proto for net in nets])

        response = self._board._kicad.send(cmd, board_commands_pb2.NetClassForNetsResponse)
        return {key: NetClass(value) for key, value in response.classes.items()}

    @overload
    def expand_text_variables(self, text: str) -> str:
        ...

    @overload
    def expand_text_variables(self, text: List[str]) -> List[str]:
        ...

    def expand_text_variables(self, text: Union[str, List[str]]) -> Union[str, List[str]]:
        """Expand text variables in a string or list of strings.

        Args:
            text: String or list of strings containing text variables

        Returns:
            String or list of strings with variables expanded
        """
        cmd = project_commands_pb2.ExpandTextVariables()
        cmd.document.CopyFrom(self._board._doc)
        if isinstance(text, list):
            cmd.text.extend(text)
        else:
            cmd.text.append(text)
        response = self._board._kicad.send(cmd, project_commands_pb2.ExpandTextVariablesResponse)
        return (
            [t for t in response.text]
            if isinstance(text, list)
            else response.text[0]
            if len(response.text) > 0
            else ""
        )
