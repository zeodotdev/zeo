# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Design block operations for the schematic editor.
"""

from typing import TYPE_CHECKING, List, Optional
from dataclasses import dataclass, field

from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic
    from kipy.geometry import Vector2


@dataclass
class DesignBlockInfo:
    """Information about a design block."""
    lib_id: str  # "Library:BlockName" format
    name: str
    library_nickname: str
    description: str = ""
    keywords: List[str] = field(default_factory=list)


class DesignBlockOperations:
    """Design block management operations.

    Design blocks are reusable schematic snippets that can be saved and
    placed multiple times across projects.

    Example:
        >>> # Get all available design blocks
        >>> blocks = sch.design_blocks.get_all()
        >>> for block in blocks:
        ...     print(f"{block.lib_id}: {block.description}")
        >>>
        >>> # Search for a specific block
        >>> results = sch.design_blocks.search("power supply")
        >>> for block in results:
        ...     print(block.name)
    """

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    def get_all(self, library_nickname: Optional[str] = None) -> List[DesignBlockInfo]:
        """Get available design blocks.

        Args:
            library_nickname: Optional library to filter (None for all libraries)

        Returns:
            List of DesignBlockInfo objects

        Example:
            >>> all_blocks = sch.design_blocks.get_all()
            >>> my_lib_blocks = sch.design_blocks.get_all("MyDesignBlocks")
        """
        cmd = schematic_commands_pb2.GetDesignBlocks()
        if library_nickname:
            cmd.library_nickname = library_nickname

        response = self._kicad.send(cmd, schematic_commands_pb2.GetDesignBlocksResponse)

        return [
            DesignBlockInfo(
                lib_id=block.lib_id,
                name=block.name,
                library_nickname=block.library_nickname,
                description=block.description,
                keywords=block.keywords.split() if block.keywords else [],
            )
            for block in response.design_blocks
        ]

    def search(
        self,
        query: str,
        libraries: Optional[List[str]] = None,
        max_results: int = 100,
    ) -> List[DesignBlockInfo]:
        """Search for design blocks.

        Args:
            query: Search term (matches name, description, keywords)
            libraries: Optional list of library nicknames to search
            max_results: Maximum results to return (default 100, 0 = no limit)

        Returns:
            List of matching DesignBlockInfo objects

        Example:
            >>> results = sch.design_blocks.search("amplifier")
            >>> results = sch.design_blocks.search("filter", libraries=["Analog"])
        """
        cmd = schematic_commands_pb2.SearchDesignBlocks()
        cmd.query = query
        cmd.max_results = max_results

        if libraries:
            for lib in libraries:
                cmd.libraries.append(lib)

        response = self._kicad.send(cmd, schematic_commands_pb2.SearchDesignBlocksResponse)

        return [
            DesignBlockInfo(
                lib_id=block.lib_id,
                name=block.name,
                library_nickname=block.library_nickname,
                description=block.description,
                keywords=block.keywords.split() if block.keywords else [],
            )
            for block in response.design_blocks
        ]

    def delete(self, lib_id: str) -> None:
        """Delete a design block.

        Args:
            lib_id: Full library:block identifier (e.g., "MyLib:PowerSupply")

        Raises:
            ApiError: If the block cannot be deleted

        Example:
            >>> sch.design_blocks.delete("MyLib:OldBlock")
        """
        from kipy.client import ApiError

        cmd = schematic_commands_pb2.DeleteDesignBlock()
        cmd.lib_id = lib_id

        response = self._kicad.send(cmd, schematic_commands_pb2.DeleteDesignBlockResponse)

        if not response.success:
            raise ApiError(f"Failed to delete design block: {response.error_message}")

    def get_by_library(self) -> dict:
        """Get design blocks organized by library.

        Returns:
            Dictionary mapping library nicknames to lists of DesignBlockInfo

        Example:
            >>> by_lib = sch.design_blocks.get_by_library()
            >>> for lib, blocks in by_lib.items():
            ...     print(f"{lib}: {len(blocks)} blocks")
        """
        all_blocks = self.get_all()
        by_library = {}

        for block in all_blocks:
            if block.library_nickname not in by_library:
                by_library[block.library_nickname] = []
            by_library[block.library_nickname].append(block)

        return by_library

    def save_selection(
        self,
        library_nickname: str,
        name: str,
        description: str = "",
        keywords: str = "",
    ) -> str:
        """Save the current selection as a design block.

        Note: This operation requires user interaction in KiCad and may
        not work in all scenarios via the API.

        Args:
            library_nickname: Target library name
            name: Name for the design block
            description: Optional description
            keywords: Optional keywords (space-separated)

        Returns:
            The lib_id of the created design block

        Raises:
            ApiError: If the operation fails

        Example:
            >>> lib_id = sch.design_blocks.save_selection("MyLib", "PowerSection")
        """
        from kipy.client import ApiError

        cmd = schematic_commands_pb2.SaveSelectionAsDesignBlock()
        cmd.document.CopyFrom(self._doc)
        cmd.library_nickname = library_nickname
        cmd.name = name
        cmd.description = description
        cmd.keywords = keywords

        response = self._kicad.send(cmd, schematic_commands_pb2.SaveDesignBlockResponse)

        if not response.success:
            raise ApiError(f"Failed to save selection as design block: {response.error_message}")

        return response.lib_id

    def save_sheet(
        self,
        library_nickname: str,
        name: str,
        description: str = "",
        keywords: str = "",
    ) -> str:
        """Save the current sheet as a design block.

        Note: This operation requires user interaction in KiCad and may
        not work in all scenarios via the API.

        Args:
            library_nickname: Target library name
            name: Name for the design block
            description: Optional description
            keywords: Optional keywords (space-separated)

        Returns:
            The lib_id of the created design block

        Raises:
            ApiError: If the operation fails

        Example:
            >>> lib_id = sch.design_blocks.save_sheet("MyLib", "EntireSheet")
        """
        from kipy.client import ApiError

        cmd = schematic_commands_pb2.SaveSheetAsDesignBlock()
        cmd.document.CopyFrom(self._doc)
        cmd.library_nickname = library_nickname
        cmd.name = name
        cmd.description = description
        cmd.keywords = keywords

        response = self._kicad.send(cmd, schematic_commands_pb2.SaveDesignBlockResponse)

        if not response.success:
            raise ApiError(f"Failed to save sheet as design block: {response.error_message}")

        return response.lib_id

    def place(
        self,
        lib_id: str,
        position: "Vector2",
    ) -> List[str]:
        """Place a design block into the schematic.

        Note: This operation requires user interaction in KiCad and may
        not work in all scenarios via the API.

        Args:
            lib_id: Full library:block identifier (e.g., "MyLib:PowerSupply")
            position: Position to place the block (Vector2)

        Returns:
            List of KIIDs for the created items

        Raises:
            ApiError: If the operation fails

        Example:
            >>> from kipy.geometry import Vector2
            >>> ids = sch.design_blocks.place("MyLib:PowerSupply", Vector2.from_xy_mm(50, 100))
        """
        from kipy.client import ApiError

        cmd = schematic_commands_pb2.PlaceDesignBlock()
        cmd.document.CopyFrom(self._doc)
        cmd.lib_id = lib_id
        cmd.position.x_nm = position.x
        cmd.position.y_nm = position.y

        response = self._kicad.send(cmd, schematic_commands_pb2.PlaceDesignBlockResponse)

        if not response.success:
            raise ApiError(f"Failed to place design block: {response.error_message}")

        return [item_id.value for item_id in response.created_item_ids]
