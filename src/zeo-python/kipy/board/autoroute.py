# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Autoroute operations for PCB boards.

This module provides access to the Freerouting-based maze-search autorouter.
"""

from typing import TYPE_CHECKING, Dict, List, Optional

from kipy.proto.board.board_commands_pb2 import Autoroute, AutorouteResponse

if TYPE_CHECKING:
    from kipy.board.base import Board


class AutorouteOperations:
    """Autorouter operations for PCB boards.

    Provides access to the expansion-room based maze-search autorouter
    with A* pathfinding through convex free-space regions and multi-pass
    rip-up-and-retry.

    Example:
        >>> board = kicad.get_board()
        >>> result = board.autoroute.route_all(max_passes=20, angle=1)
        >>> print(f"Routed {result['routed']}/{result['total']}")
    """

    def __init__(self, board: "Board"):
        self._board = board

    def route_all(
        self,
        max_passes: int = 20,
        angle: int = 1,
        net_codes: Optional[List[int]] = None,
    ) -> Dict[str, int]:
        """Run the autorouter on the board.

        Args:
            max_passes: Maximum number of rip-up-and-retry passes (default 20).
            angle: Angle restriction mode:
                - 0: Free angle routing
                - 1: 45-degree routing (default)
                - 2: 90-degree routing only
            net_codes: List of net codes to route. If None or empty, routes all nets.

        Returns:
            Dict with routing results:
                - total: Total number of connections to route
                - routed: Number of successfully routed connections
                - failed: Number of connections that failed to route
                - passes: Number of passes used
        """
        cmd = Autoroute()
        cmd.board.CopyFrom(self._board.document)
        cmd.max_passes = max_passes
        cmd.angle_restriction = angle
        if net_codes:
            cmd.net_codes.extend(net_codes)

        resp = self._board._kicad.send(cmd, AutorouteResponse)

        return {
            "total": resp.total_connections,
            "routed": resp.routed,
            "failed": resp.failed,
            "passes": resp.passes_used,
        }
