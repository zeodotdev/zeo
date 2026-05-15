# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Net queries and management operations.
"""

from typing import TYPE_CHECKING, Dict, List, Optional, Sequence, Union, overload

from kipy.board_types import Net
from kipy.project import NetClass
from kipy.proto.board import board_commands_pb2
from kipy.proto.common.commands import project_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board


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
