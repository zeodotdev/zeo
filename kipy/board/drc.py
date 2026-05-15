# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Design Rule Check (DRC) operations.
"""

from typing import TYPE_CHECKING, List, Optional, Sequence, Tuple

from google.protobuf.empty_pb2 import Empty

from kipy.proto.board import board_commands_pb2

if TYPE_CHECKING:
    from kipy.board.base import Board
    from kipy.board.types import DRCViolation, DRCSettings


class DRCOperations:
    """Design Rule Check operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def run(
        self,
        refill_zones: bool = False,
        report_all_track_errors: bool = False,
        test_footprints: bool = False,
        cancel_after_ms: int = 0,
    ) -> Tuple[int, int, int, bool]:
        """Run Design Rules Check on the board.

        This is a blocking operation that runs DRC and returns when complete.

        Args:
            refill_zones: If True, refill all zones before running DRC
            report_all_track_errors: If True, report all track errors (not just first per track)
            test_footprints: If True, test footprints against library footprints
            cancel_after_ms: If > 0, cancel after this many ms (cooperative;
                may overshoot by one provider's run length).

        Returns:
            Tuple of (error_count, warning_count, exclusion_count, cancelled)
        """
        cmd = board_commands_pb2.RunDRC()
        cmd.board.CopyFrom(self._board._doc)
        cmd.refill_zones = refill_zones
        cmd.report_all_track_errors = report_all_track_errors
        cmd.test_footprints = test_footprints
        cmd.cancel_after_ms = cancel_after_ms
        response = self._board._kicad.send(cmd, board_commands_pb2.RunDRCResponse)
        return (response.error_count, response.warning_count,
                response.exclusion_count, response.cancelled)

    def get_violations(
        self,
        severities: Optional[Sequence[board_commands_pb2.DrcSeverity.ValueType]] = None,
    ) -> List["DRCViolation"]:
        """Get all current DRC violations/markers from the board.

        Args:
            severities: Optional list of severities to filter by

        Returns:
            List of DRCViolation objects
        """
        from kipy.board.types import DRCViolation
        cmd = board_commands_pb2.GetDRCViolations()
        cmd.board.CopyFrom(self._board._doc)
        if severities:
            cmd.severities.extend(severities)
        response = self._board._kicad.send(cmd, board_commands_pb2.DRCViolationsResponse)
        return [DRCViolation(v) for v in response.violations]

    def clear_markers(
        self,
        clear_violations: bool = True,
        clear_exclusions: bool = False,
    ):
        """Clear DRC markers from the board.

        Args:
            clear_violations: If True, clear errors and warnings
            clear_exclusions: If True, clear exclusions (user-marked items to ignore)
        """
        cmd = board_commands_pb2.ClearDRCMarkers()
        cmd.board.CopyFrom(self._board._doc)
        cmd.clear_violations = clear_violations
        cmd.clear_exclusions = clear_exclusions
        self._board._kicad.send(cmd, Empty)

    def get_settings(self) -> "DRCSettings":
        """Get DRC check severity settings.

        Returns:
            DRCSettings object with severity overrides for DRC checks
        """
        from kipy.board.types import DRCSettings
        cmd = board_commands_pb2.GetDRCSettings()
        cmd.board.CopyFrom(self._board._doc)
        response = self._board._kicad.send(cmd, board_commands_pb2.DRCSettingsResponse)
        return DRCSettings(response.settings)

    def set_settings(self, settings: "DRCSettings"):
        """Set DRC check severity settings.

        Args:
            settings: DRCSettings object with severity overrides
        """
        cmd = board_commands_pb2.SetDRCSettings()
        cmd.board.CopyFrom(self._board._doc)
        cmd.settings.CopyFrom(settings.proto)
        self._board._kicad.send(cmd, Empty)
