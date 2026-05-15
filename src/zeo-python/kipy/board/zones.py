# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Zone management operations.
"""

from time import sleep
from typing import TYPE_CHECKING, List, Optional, Sequence, Union, cast

from google.protobuf.empty_pb2 import Empty

from kipy.geometry import PolygonWithHoles
from kipy.board_types import Zone, Net
from kipy.proto.board import board_commands_pb2, board_types_pb2
from kipy.proto.common.commands import Ping
from kipy.proto.common.envelope_pb2 import ApiStatusCode
from kipy.client import ApiError

if TYPE_CHECKING:
    from kipy.board.base import Board


class ZoneOperations:
    """Zone management operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def add(
        self,
        outline: PolygonWithHoles,
        layers: Union[board_types_pb2.BoardLayer.ValueType, Sequence[board_types_pb2.BoardLayer.ValueType]],
        net: Optional[str] = None,
        name: str = "",
        priority: int = 0,
    ) -> Zone:
        """Create a copper zone with the given outline.

        Args:
            outline: Zone outline as a PolygonWithHoles
            layers: Layer or list of layers for the zone
            net: Optional net name for the zone (required for copper zones)
            name: Optional zone name
            priority: Zone fill priority (higher = filled first)

        Returns:
            The created Zone object
        """
        zone = Zone()
        zone.outline = outline

        if isinstance(layers, int):
            zone.layers = [layers]
        else:
            zone.layers = list(layers)

        if net:
            net_obj = Net()
            net_obj.name = net
            zone.net = net_obj

        if name:
            zone.name = name

        zone.priority = priority

        created = self._board.crud.create_items(zone)
        if created:
            return cast(Zone, created[0])
        return zone

    def get_all(self) -> Sequence[Zone]:
        """Get all zones (including rule areas) on the board."""
        return self._board.crud.get_zones()

    def get_by_net(self, net_name: str) -> List[Zone]:
        """Get zones by net name.

        Args:
            net_name: Name of the net

        Returns:
            List of zones on the specified net
        """
        return [z for z in self.get_all() if z.net and z.net.name == net_name]

    def refill(
        self,
        block: bool = True,
        max_poll_seconds: float = 30.0,
        poll_interval_seconds: float = 0.5,
    ):
        """Refill all zones on the board.

        Args:
            block: If True, wait for refill to complete
            max_poll_seconds: Maximum time to wait for refill
            poll_interval_seconds: Polling interval
        """
        cmd = board_commands_pb2.RefillZones()
        cmd.board.CopyFrom(self._board._doc)
        self._board._kicad.send(cmd, Empty)

        if not block:
            return

        # Zone fill is blocking - poll until complete
        sleeps = 0
        while sleeps < max_poll_seconds:
            sleep(poll_interval_seconds)
            try:
                self._board._kicad.send(Ping(), Empty)
            except IOError:
                continue
            except ApiError as e:
                if e.code == ApiStatusCode.AS_BUSY:
                    continue
                else:
                    raise e
            break
