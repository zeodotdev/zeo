# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Ratsnest and connectivity query operations.
"""

from typing import TYPE_CHECKING, List, Optional
from dataclasses import dataclass, field

from kipy.proto.board import board_commands_pb2
from kipy.proto.common.types import KIID
from kipy.geometry import Vector2

if TYPE_CHECKING:
    from kipy.board.base import Board


@dataclass
class RatsnestLine:
    """An unrouted connection (ratsnest line) between two pads/vias."""
    net_code: int
    net_name: str
    pad1_id: KIID
    pad2_id: KIID
    start: Vector2
    end: Vector2
    length: int  # Manhattan distance in nm


@dataclass
class UnroutedNetInfo:
    """Information about a net with incomplete routing."""
    net_code: int
    net_name: str
    total_pads: int
    routed_connections: int
    unrouted_connections: int
    is_complete: bool


@dataclass
class ItemConnectivity:
    """Connectivity status for a single item."""
    item_id: KIID
    net_code: int
    net_name: str
    connected_items: List[KIID] = field(default_factory=list)
    unconnected_items: List[KIID] = field(default_factory=list)


class ConnectivityOperations:
    """Ratsnest and connectivity query operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_ratsnest(
        self,
        net_codes: Optional[List[int]] = None,
        include_zones: bool = False,
    ) -> List[RatsnestLine]:
        """Get unrouted connections (ratsnest lines).

        Args:
            net_codes: Filter by specific nets (None = all nets)
            include_zones: Include zone-to-zone connections

        Returns:
            List of RatsnestLine objects representing unrouted connections

        Example:
            >>> ratsnest = board.connectivity.get_ratsnest()
            >>> print(f"Total unrouted: {len(ratsnest)}")
            >>> for line in ratsnest:
            ...     print(f"{line.net_name}: {line.length/1e6:.2f}mm")
        """
        cmd = board_commands_pb2.GetRatsnest()
        cmd.board.CopyFrom(self._board._doc)
        cmd.include_zones = include_zones

        if net_codes:
            cmd.net_codes.extend(net_codes)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetRatsnestResponse
        )

        result = []
        for line_proto in response.lines:
            result.append(RatsnestLine(
                net_code=line_proto.net_code,
                net_name=line_proto.net_name,
                pad1_id=line_proto.pad1_id,
                pad2_id=line_proto.pad2_id,
                start=Vector2(line_proto.start.x_nm, line_proto.start.y_nm),
                end=Vector2(line_proto.end.x_nm, line_proto.end.y_nm),
                length=line_proto.length,
            ))

        return result

    def get_unrouted_nets(self) -> List[UnroutedNetInfo]:
        """Get all nets with incomplete routing.

        Returns:
            List of UnroutedNetInfo objects for nets needing routing

        Example:
            >>> unrouted = board.connectivity.get_unrouted_nets()
            >>> for net in unrouted:
            ...     print(f"{net.net_name}: {net.unrouted_connections} unrouted")
        """
        cmd = board_commands_pb2.GetUnroutedNets()
        cmd.board.CopyFrom(self._board._doc)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetUnroutedNetsResponse
        )

        result = []
        for net_proto in response.nets:
            result.append(UnroutedNetInfo(
                net_code=net_proto.net_code,
                net_name=net_proto.net_name,
                total_pads=net_proto.total_pads,
                routed_connections=net_proto.routed_connections,
                unrouted_connections=net_proto.unrouted_connections,
                is_complete=net_proto.is_complete,
            ))

        return result

    def get_status(self, item_ids: List[KIID]) -> List[ItemConnectivity]:
        """Get connectivity status for specific items.

        Args:
            item_ids: List of item IDs (pads, vias) to check

        Returns:
            List of ItemConnectivity objects with connection details

        Example:
            >>> pad = board.footprints.get_by_reference("U1").pads[0]
            >>> status = board.connectivity.get_status([pad.id])
            >>> print(f"Connected to {len(status[0].connected_items)} items")
            >>> print(f"Unconnected: {len(status[0].unconnected_items)} items")
        """
        cmd = board_commands_pb2.GetConnectivityStatus()
        cmd.board.CopyFrom(self._board._doc)

        for item_id in item_ids:
            cmd.item_ids.append(item_id)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetConnectivityStatusResponse
        )

        result = []
        for item_proto in response.items:
            result.append(ItemConnectivity(
                item_id=item_proto.item_id,
                net_code=item_proto.net_code,
                net_name=item_proto.net_name,
                connected_items=list(item_proto.connected_items),
                unconnected_items=list(item_proto.unconnected_items),
            ))

        return result

    def is_routing_complete(self) -> bool:
        """Check if all nets are fully routed.

        Returns:
            True if no unrouted connections exist
        """
        unrouted = self.get_unrouted_nets()
        return len(unrouted) == 0

    def get_routing_progress(self) -> tuple:
        """Get overall routing progress.

        Returns:
            Tuple of (routed_connections, total_connections, percentage)

        Example:
            >>> routed, total, pct = board.connectivity.get_routing_progress()
            >>> print(f"Routing: {pct:.1f}% complete ({routed}/{total})")
        """
        unrouted_nets = self.get_unrouted_nets()

        total_unrouted = sum(n.unrouted_connections for n in unrouted_nets)
        total_routed = sum(n.routed_connections for n in unrouted_nets)
        total = total_routed + total_unrouted

        if total == 0:
            return (0, 0, 100.0)

        percentage = (total_routed / total) * 100
        return (total_routed, total, percentage)

    def get_unrouted_for_net(self, net_name: str) -> List[RatsnestLine]:
        """Get unrouted connections for a specific net.

        Args:
            net_name: Name of the net

        Returns:
            List of RatsnestLine objects for the net
        """
        # Get net code from name
        nets = self._board.nets.get_all()
        net_code = None

        for net in nets:
            if net.name == net_name:
                net_code = net.code
                break

        if net_code is None:
            return []

        return self.get_ratsnest(net_codes=[net_code])
