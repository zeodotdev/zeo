# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Footprint library browsing and search operations.
"""

from typing import TYPE_CHECKING, List, Optional
from dataclasses import dataclass, field

from kipy.proto.board import board_commands_pb2
from kipy.geometry import Vector2

if TYPE_CHECKING:
    from kipy.board.base import Board


@dataclass
class FootprintInfo:
    """Information about a footprint in a library."""
    name: str
    library: str
    lib_id: str  # "library:name" format
    description: str = ""
    keywords: str = ""
    pad_count: int = 0
    unique_pad_count: int = 0


@dataclass
class PadInfo:
    """Information about a pad in a footprint."""
    number: str
    position: Vector2
    size: Vector2
    shape: int = 0
    net_name: str = ""


@dataclass
class FootprintDetails:
    """Detailed information about a footprint including pads."""
    info: FootprintInfo
    pads: List[PadInfo] = field(default_factory=list)
    bounding_box: Optional[tuple] = None  # (min_x, min_y, max_x, max_y)
    doc_link: str = ""


class FootprintLibraryOperations:
    """Footprint library browsing and search operations."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_footprints(self, library_name: str) -> List[FootprintInfo]:
        """Get all footprints in a library.

        Args:
            library_name: Library nickname (e.g., "Resistor_SMD")

        Returns:
            List of FootprintInfo objects

        Example:
            >>> fps = board.library.get_footprints("Resistor_SMD")
            >>> for fp in fps:
            ...     print(f"{fp.lib_id}: {fp.description}")
        """
        cmd = board_commands_pb2.GetLibraryFootprints()
        cmd.library_name = library_name

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetLibraryFootprintsResponse
        )

        result = []
        for fp_proto in response.footprints:
            # Parse lib_id to get library name
            lib_id = fp_proto.lib_id
            library = library_name
            if ":" in lib_id:
                library = lib_id.split(":", 1)[0]

            result.append(FootprintInfo(
                name=fp_proto.name,
                library=library,
                lib_id=fp_proto.lib_id,
                description=fp_proto.description,
                keywords=fp_proto.keywords,
                pad_count=fp_proto.pad_count,
                unique_pad_count=fp_proto.unique_pad_count,
            ))

        return result

    def search(
        self,
        query: str,
        libraries: Optional[List[str]] = None,
        max_results: int = 100,
    ) -> List[FootprintInfo]:
        """Search for footprints across libraries.

        Args:
            query: Search term (matches name, description, keywords)
            libraries: List of library names to search (None = all)
            max_results: Maximum results to return (0 = unlimited)

        Returns:
            List of matching FootprintInfo objects

        Example:
            >>> results = board.library.search("0402")
            >>> for fp in results:
            ...     print(f"{fp.lib_id}")
        """
        cmd = board_commands_pb2.SearchLibraryFootprints()
        cmd.query = query
        cmd.max_results = max_results

        if libraries:
            cmd.libraries.extend(libraries)

        response = self._board._kicad.send(
            cmd, board_commands_pb2.SearchLibraryFootprintsResponse
        )

        result = []
        for fp_proto in response.results:
            lib_id = fp_proto.lib_id
            library = ""
            if ":" in lib_id:
                library = lib_id.split(":", 1)[0]

            result.append(FootprintInfo(
                name=fp_proto.name,
                library=library,
                lib_id=fp_proto.lib_id,
                description=fp_proto.description,
                keywords=fp_proto.keywords,
                pad_count=fp_proto.pad_count,
                unique_pad_count=fp_proto.unique_pad_count,
            ))

        return result

    def get_info(self, lib_id: str) -> FootprintDetails:
        """Get detailed information about a footprint.

        Args:
            lib_id: Full library ID in "Library:Footprint" format

        Returns:
            FootprintDetails with full footprint information including pads

        Example:
            >>> details = board.library.get_info("Resistor_SMD:R_0402_1005Metric")
            >>> print(f"Pads: {len(details.pads)}")
            >>> for pad in details.pads:
            ...     print(f"  {pad.number}: {pad.position}")
        """
        cmd = board_commands_pb2.GetFootprintInfo()
        cmd.lib_id = lib_id

        response = self._board._kicad.send(
            cmd, board_commands_pb2.GetFootprintInfoResponse
        )

        # Parse library from lib_id
        library = ""
        if ":" in lib_id:
            library = lib_id.split(":", 1)[0]

        info = FootprintInfo(
            name=response.info.name,
            library=library,
            lib_id=response.info.lib_id,
            description=response.info.description,
            keywords=response.info.keywords,
            pad_count=response.info.pad_count,
            unique_pad_count=response.info.unique_pad_count,
        )

        pads = []
        for pad_proto in response.pads:
            pads.append(PadInfo(
                number=pad_proto.number,
                position=Vector2(pad_proto.position.x_nm, pad_proto.position.y_nm),
                size=Vector2(pad_proto.size.x_nm, pad_proto.size.y_nm),
                shape=pad_proto.shape,
                net_name=pad_proto.net_name,
            ))

        # Parse bounding box if present
        bbox = None
        if response.HasField("bounding_box"):
            bb = response.bounding_box
            bbox = (bb.position.x_nm, bb.position.y_nm,
                    bb.position.x_nm + bb.size.x_nm,
                    bb.position.y_nm + bb.size.y_nm)

        return FootprintDetails(
            info=info,
            pads=pads,
            bounding_box=bbox,
            doc_link=response.doc_link if response.doc_link else "",
        )

    def find_by_pad_count(
        self,
        pad_count: int,
        libraries: Optional[List[str]] = None,
    ) -> List[FootprintInfo]:
        """Find footprints with a specific pad count.

        Args:
            pad_count: Number of pads to match
            libraries: Libraries to search (None = all)

        Returns:
            List of matching FootprintInfo objects
        """
        # Search with empty query to get all, then filter
        all_fps = self.search("", libraries, max_results=0)
        return [fp for fp in all_fps if fp.pad_count == pad_count]
