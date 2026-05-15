# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Symbol library management and search operations.
"""

from typing import TYPE_CHECKING, List, Optional, Dict, Any
from dataclasses import dataclass, field
import os
import re

from kipy.proto.common.commands.editor_commands_pb2 import (
    GetLibraries, GetLibrariesResponse,
    AddLibrary, AddLibraryResponse, AddLibraryStatus, LibraryTableScope,
    RemoveLibrary, RemoveLibraryResponse, RemoveLibraryStatus,
)
from kipy.proto.schematic import schematic_commands_pb2
from kipy.client import ApiError

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


@dataclass
class LibraryInfo:
    """Information about a configured symbol library."""
    nickname: str
    uri: str
    type: str
    description: str = ""
    is_loaded: bool = False
    is_read_only: bool = False
    scope: str = "global"


@dataclass
class LibPinInfo:
    """Pin information from a library symbol."""
    number: str
    name: str
    position_x: int  # nanometers from symbol origin
    position_y: int  # nanometers from symbol origin
    orientation: int  # degrees (0, 90, 180, 270)
    electrical_type: str = ""
    graphical_style: str = ""
    unit: int = 1


@dataclass
class SymbolInfo:
    """Information about a symbol in a library."""
    name: str
    library: str
    lib_id: str  # "library:name" format
    description: str = ""
    keywords: List[str] = field(default_factory=list)
    pin_names: List[str] = field(default_factory=list)
    pins: List[LibPinInfo] = field(default_factory=list)  # Full pin details
    pin_count: int = 0
    unit_count: int = 1
    extends: str = ""  # Parent symbol if derived
    is_power: bool = False
    footprint_filters: List[str] = field(default_factory=list)
    datasheet: str = ""
    body_bbox_min_x_nm: int = 0  # Bounding box (body+pins) relative to origin, in nm
    body_bbox_min_y_nm: int = 0
    body_bbox_max_x_nm: int = 0
    body_bbox_max_y_nm: int = 0


class LibraryOperations:
    """Symbol library management operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    def get_all(self, scope: str = "both") -> List[LibraryInfo]:
        """Get configured symbol libraries.

        Args:
            scope: "global", "project", or "both"

        Returns:
            List of LibraryInfo objects

        Example:
            >>> libs = sch.library.get_all()
            >>> for lib in libs:
            ...     print(f"{lib.nickname}: {lib.uri}")
        """
        cmd = GetLibraries()

        scope_map = {
            "global": LibraryTableScope.LTS_GLOBAL,
            "project": LibraryTableScope.LTS_PROJECT,
            "both": LibraryTableScope.LTS_BOTH,
        }

        if scope.lower() not in scope_map:
            raise ValueError(f"Invalid scope '{scope}'")

        cmd.scope = scope_map[scope.lower()]
        response = self._kicad.send(cmd, GetLibrariesResponse)

        libraries = []
        for lib_proto in response.libraries:
            if lib_proto.scope == LibraryTableScope.LTS_GLOBAL:
                scope_str = "global"
            elif lib_proto.scope == LibraryTableScope.LTS_PROJECT:
                scope_str = "project"
            else:
                scope_str = "unknown"

            libraries.append(LibraryInfo(
                nickname=lib_proto.nickname,
                uri=lib_proto.uri,
                type=lib_proto.type,
                description=lib_proto.description,
                is_loaded=lib_proto.is_loaded,
                is_read_only=lib_proto.is_read_only,
                scope=scope_str,
            ))

        return libraries

    def add(
        self,
        file_path: str,
        nickname: Optional[str] = None,
        scope: str = "global",
    ) -> LibraryInfo:
        """Add a library file to the library table.

        Args:
            file_path: Path to the library file
            nickname: Optional nickname (auto-generated if not provided)
            scope: "global" or "project"

        Returns:
            LibraryInfo for the new library

        Example:
            >>> lib = sch.library.add("/path/to/my_symbols.kicad_sym")
            >>> lib = sch.library.add("/path/to/symbols.kicad_sym", nickname="MyLib")
        """
        cmd = AddLibrary()
        cmd.file_path = file_path

        if nickname:
            cmd.nickname = nickname

        scope_map = {
            "global": LibraryTableScope.LTS_GLOBAL,
            "project": LibraryTableScope.LTS_PROJECT,
        }

        if scope.lower() not in scope_map:
            raise ValueError(f"Invalid scope '{scope}'")

        cmd.scope = scope_map[scope.lower()]
        response = self._kicad.send(cmd, AddLibraryResponse)

        if response.status != AddLibraryStatus.ALS_OK:
            status_messages = {
                AddLibraryStatus.ALS_ALREADY_EXISTS: f"Library '{response.nickname}' exists",
                AddLibraryStatus.ALS_FILE_NOT_FOUND: f"File not found: {file_path}",
                AddLibraryStatus.ALS_INVALID_FORMAT: "Invalid library format",
                AddLibraryStatus.ALS_TABLE_NOT_FOUND: "Library table not found",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to add library: {error_msg}")

        return LibraryInfo(
            nickname=response.nickname,
            uri=file_path,
            type="KiCad",
            scope=scope,
        )

    def remove(self, nickname: str, scope: str = "global") -> None:
        """Remove a library from the library table.

        Args:
            nickname: Library nickname to remove
            scope: "global" or "project"

        Raises:
            ApiError: If the library cannot be removed

        Example:
            >>> sch.library.remove("MyLib")
            >>> sch.library.remove("ProjectLib", scope="project")
        """
        cmd = RemoveLibrary()
        cmd.nickname = nickname

        scope_map = {
            "global": LibraryTableScope.LTS_GLOBAL,
            "project": LibraryTableScope.LTS_PROJECT,
        }

        if scope.lower() not in scope_map:
            raise ValueError(f"Invalid scope '{scope}'")

        cmd.scope = scope_map[scope.lower()]
        response = self._kicad.send(cmd, RemoveLibraryResponse)

        if response.status != RemoveLibraryStatus.RLS_OK:
            status_messages = {
                RemoveLibraryStatus.RLS_NOT_FOUND: f"Library '{nickname}' not found",
                RemoveLibraryStatus.RLS_TABLE_NOT_FOUND: "Library table not found",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to remove library: {error_msg}")

    # =========================================================================
    # API-Based Symbol Query Operations
    # =========================================================================

    def get_library_symbols(self, library_name: str) -> List[str]:
        """Get list of symbol names from a specific library via API.

        This uses the KiCad IPC API to enumerate symbols directly from the
        symbol library table, which is faster and more accurate than parsing
        library files.

        Args:
            library_name: Library nickname (e.g., "Device", "Transistor_FET")

        Returns:
            List of symbol names in the library

        Example:
            >>> symbols = sch.library.get_library_symbols("Device")
            >>> print(f"Found {len(symbols)} symbols")
        """
        cmd = schematic_commands_pb2.GetLibrarySymbols()
        cmd.library_name = library_name
        response = self._kicad.send(cmd, schematic_commands_pb2.GetLibrarySymbolsResponse)
        return list(response.symbol_names)

    def search(
        self,
        query: str,
        libraries: Optional[List[str]] = None,
        max_results: int = 100,
        pattern_type: str = "",
    ) -> List[SymbolInfo]:
        """Search for symbols across libraries via API.

        This uses the KiCad IPC API for searching, which is more accurate
        than local file parsing.

        Args:
            query: Search term (matches name, description, keywords)
            libraries: Optional list of library nicknames to search
            max_results: Maximum results to return (default 100)
            pattern_type: "substring" (default), "wildcard", or "regex"

        Returns:
            List of matching SymbolInfo objects

        Example:
            >>> results = sch.library.search("mosfet")
            >>> for sym in results:
            ...     print(f"{sym.lib_id}: {sym.description}")
        """
        cmd = schematic_commands_pb2.SearchLibrarySymbols()
        cmd.query = query
        cmd.max_results = max_results

        if pattern_type:
            cmd.pattern_type = pattern_type

        if libraries:
            for lib in libraries:
                cmd.libraries.append(lib)

        response = self._kicad.send(cmd, schematic_commands_pb2.SearchLibrarySymbolsResponse)

        results = []
        for sym in response.symbols:
            results.append(SymbolInfo(
                name=sym.name,
                library=sym.lib_id.split(":")[0] if ":" in sym.lib_id else "",
                lib_id=sym.lib_id,
                description=sym.description,
                keywords=sym.keywords.split() if sym.keywords else [],
                pin_names=[p.name for p in sym.pins],
                pins=[LibPinInfo(
                    number=p.number,
                    name=p.name,
                    position_x=p.position.x_nm,
                    position_y=p.position.y_nm,
                    orientation=p.orientation,
                    electrical_type=p.electrical_type,
                    graphical_style=p.graphical_style,
                    unit=p.unit,
                ) for p in sym.pins],
                pin_count=len(sym.pins),
                unit_count=sym.unit_count,
                is_power=sym.is_power,
                footprint_filters=list(sym.footprint_filters),
                datasheet=sym.datasheet,
                body_bbox_min_x_nm=sym.body_bbox_min_x_nm,
                body_bbox_min_y_nm=sym.body_bbox_min_y_nm,
                body_bbox_max_x_nm=sym.body_bbox_max_x_nm,
                body_bbox_max_y_nm=sym.body_bbox_max_y_nm,
            ))

        return results

    def get_symbol_info(self, lib_id: str) -> SymbolInfo:
        """Get detailed information about a specific symbol via API.

        Args:
            lib_id: Full library:symbol identifier (e.g., "Device:R")

        Returns:
            SymbolInfo with detailed information

        Example:
            >>> info = sch.library.get_symbol_info("Device:R")
            >>> print(f"Pins: {info.pin_count}, Units: {info.unit_count}")
        """
        cmd = schematic_commands_pb2.GetSymbolInfo()
        cmd.lib_id = lib_id
        response = self._kicad.send(cmd, schematic_commands_pb2.GetSymbolInfoResponse)

        sym = response.symbol
        return SymbolInfo(
            name=sym.name,
            library=sym.lib_id.split(":")[0] if ":" in sym.lib_id else "",
            lib_id=sym.lib_id,
            description=sym.description,
            keywords=sym.keywords.split() if sym.keywords else [],
            pin_names=[p.name for p in sym.pins],
            pins=[LibPinInfo(
                number=p.number,
                name=p.name,
                position_x=p.position.x_nm,
                position_y=p.position.y_nm,
                orientation=p.orientation,
                electrical_type=p.electrical_type,
                graphical_style=p.graphical_style,
                unit=p.unit,
            ) for p in sym.pins],
            pin_count=len(sym.pins),
            unit_count=sym.unit_count,
            is_power=sym.is_power,
            footprint_filters=list(sym.footprint_filters),
            datasheet=sym.datasheet,
            body_bbox_min_x_nm=sym.body_bbox_min_x_nm,
            body_bbox_min_y_nm=sym.body_bbox_min_y_nm,
            body_bbox_max_x_nm=sym.body_bbox_max_x_nm,
            body_bbox_max_y_nm=sym.body_bbox_max_y_nm,
        )

    def get_pin_position(self, symbol, pin_number: str) -> dict:
        """Get the world position of a pin on a placed symbol.

        This calculates the actual position after applying the symbol's
        position, rotation, and mirror transforms.

        Args:
            symbol: A placed symbol (with 'id' attribute)
            pin_number: Pin number to query

        Returns:
            Dict with 'position' (Vector2) and 'orientation' (degrees)

        Example:
            >>> r1 = sch.symbols.get_by_ref("R1")
            >>> pin_info = sch.library.get_pin_position(r1, "1")
            >>> print(f"Pin 1 at: {pin_info['position']}")
        """
        from kipy.geometry import Vector2

        cmd = schematic_commands_pb2.GetTransformedPinPosition()
        cmd.document.CopyFrom(self._sch._doc)

        if hasattr(symbol, 'id') and hasattr(symbol.id, 'value'):
            cmd.symbol_id.value = symbol.id.value
        elif hasattr(symbol, 'id'):
            cmd.symbol_id.value = str(symbol.id)
        else:
            cmd.symbol_id.value = str(symbol)

        cmd.pin_number = pin_number

        response = self._kicad.send(cmd, schematic_commands_pb2.GetTransformedPinPositionResponse)

        return {
            'position': Vector2(response.position),
            'orientation': response.orientation,
        }

    # =========================================================================
    # Symbol Search Operations (File-based fallback)
    # =========================================================================

    def get_symbols(self, library_name: str) -> List[SymbolInfo]:
        """Get all symbols from a specific library.

        Note: This parses the .kicad_sym file directly since the IPC API
        doesn't provide a symbol enumeration command.

        Args:
            library_name: Library nickname (e.g., "Device", "Transistor_FET")

        Returns:
            List of SymbolInfo objects

        Example:
            >>> symbols = sch.library.get_symbols("Device")
            >>> for sym in symbols:
            ...     print(f"{sym.lib_id}: {sym.description}")
        """
        # Find the library URI
        libraries = self.get_all()
        lib_info = None
        for lib in libraries:
            if lib.nickname == library_name:
                lib_info = lib
                break

        if not lib_info:
            raise ValueError(f"Library '{library_name}' not found")

        return self._parse_library_file(lib_info.uri, library_name)

    def search_symbols(
        self,
        query: str,
        libraries: Optional[List[str]] = None,
        limit: int = 50,
    ) -> List[SymbolInfo]:
        """Search for symbols across libraries.

        Args:
            query: Search term (matches name, description, keywords)
            libraries: Optional list of library nicknames to search
            limit: Maximum results to return (default 50)

        Returns:
            List of matching SymbolInfo objects

        Example:
            >>> results = sch.library.search_symbols("mosfet")
            >>> results = sch.library.search_symbols("capacitor", libraries=["Device"])
        """
        query_lower = query.lower()
        results = []

        # Get libraries to search
        all_libs = self.get_all()
        if libraries:
            libs_to_search = [lib for lib in all_libs if lib.nickname in libraries]
        else:
            libs_to_search = all_libs

        for lib in libs_to_search:
            try:
                symbols = self._parse_library_file(lib.uri, lib.nickname)
                for sym in symbols:
                    # Match against name, description, keywords
                    if (query_lower in sym.name.lower() or
                        query_lower in sym.description.lower() or
                        any(query_lower in kw.lower() for kw in sym.keywords)):
                        results.append(sym)
                        if len(results) >= limit:
                            return results
            except Exception:
                # Skip libraries that can't be parsed
                continue

        return results

    def find_symbol(
        self,
        name: Optional[str] = None,
        description: Optional[str] = None,
        keywords: Optional[List[str]] = None,
        pin_count: Optional[int] = None,
        is_power: Optional[bool] = None,
        libraries: Optional[List[str]] = None,
    ) -> List[SymbolInfo]:
        """Find symbols matching multiple criteria.

        Args:
            name: Pattern to match in symbol name
            description: Pattern to match in description
            keywords: Keywords that must be present
            pin_count: Exact number of pins
            is_power: Filter for power symbols
            libraries: Libraries to search (None for all)

        Returns:
            List of matching SymbolInfo objects

        Example:
            >>> # Find 2-pin resistors
            >>> resistors = sch.library.find_symbol(name="R", pin_count=2)
            >>> # Find power symbols
            >>> power = sch.library.find_symbol(is_power=True)
        """
        all_libs = self.get_all()
        if libraries:
            libs_to_search = [lib for lib in all_libs if lib.nickname in libraries]
        else:
            libs_to_search = all_libs

        results = []

        for lib in libs_to_search:
            try:
                symbols = self._parse_library_file(lib.uri, lib.nickname)
                for sym in symbols:
                    # Apply filters
                    if name and name.lower() not in sym.name.lower():
                        continue
                    if description and description.lower() not in sym.description.lower():
                        continue
                    if keywords:
                        sym_kw_lower = [k.lower() for k in sym.keywords]
                        if not all(kw.lower() in sym_kw_lower for kw in keywords):
                            continue
                    if pin_count is not None and sym.pin_count != pin_count:
                        continue
                    if is_power is not None and sym.is_power != is_power:
                        continue

                    results.append(sym)
            except Exception:
                continue

        return results

    def get_common_symbols(self) -> Dict[str, List[SymbolInfo]]:
        """Get commonly used symbols organized by category.

        Returns:
            Dictionary with categories as keys:
            - resistors: Resistor symbols
            - capacitors: Capacitor symbols
            - inductors: Inductor symbols
            - diodes: Diode symbols
            - transistors: Transistor symbols
            - power: Power symbols (GND, VCC, etc.)
            - connectors: Connector symbols

        Example:
            >>> common = sch.library.get_common_symbols()
            >>> for r in common['resistors'][:5]:
            ...     print(r.lib_id)
        """
        categories = {
            "resistors": [],
            "capacitors": [],
            "inductors": [],
            "diodes": [],
            "transistors": [],
            "power": [],
            "connectors": [],
        }

        # Search in Device library
        try:
            device_symbols = self.get_symbols("Device")
            for sym in device_symbols:
                name_lower = sym.name.lower()
                if name_lower.startswith("r") and not name_lower.startswith("relay"):
                    categories["resistors"].append(sym)
                elif name_lower.startswith("c") and "capacitor" in sym.description.lower():
                    categories["capacitors"].append(sym)
                elif name_lower.startswith("l") and "inductor" in sym.description.lower():
                    categories["inductors"].append(sym)
                elif name_lower.startswith("d") or "diode" in sym.description.lower():
                    categories["diodes"].append(sym)
                elif name_lower.startswith("q") or any(t in name_lower for t in ["mosfet", "bjt", "jfet"]):
                    categories["transistors"].append(sym)
        except Exception:
            pass

        # Search in power library
        try:
            power_symbols = self.get_symbols("power")
            categories["power"] = power_symbols
        except Exception:
            pass

        # Search in Connector library
        try:
            connector_symbols = self.get_symbols("Connector")
            categories["connectors"] = connector_symbols[:20]  # Limit connectors
        except Exception:
            pass

        return categories

    def _parse_library_file(self, uri: str, library_name: str) -> List[SymbolInfo]:
        """Parse a .kicad_sym file to extract symbol information.

        Args:
            uri: Path to the library file
            library_name: Library nickname

        Returns:
            List of SymbolInfo objects
        """
        symbols = []

        # Handle various URI formats
        if uri.startswith("${"):
            # Environment variable path - try to resolve
            # Common: ${KICAD8_SYMBOL_DIR}/Device.kicad_sym
            uri = self._resolve_kicad_path(uri)

        if not uri or not os.path.exists(uri):
            return symbols

        try:
            with open(uri, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception:
            return symbols

        # Parse S-expression format (simplified parsing)
        # Look for (symbol "name" ... ) blocks at the top level
        symbol_pattern = r'\(symbol\s+"([^"]+)"'
        description_pattern = r'\(property\s+"ki_description"\s+"([^"]*)"'
        keywords_pattern = r'\(property\s+"ki_keywords"\s+"([^"]*)"'
        pin_pattern = r'\(pin\s+\w+'
        extends_pattern = r'\(extends\s+"([^"]+)"'

        # Split into symbol blocks
        current_pos = 0
        depth = 0
        in_symbol = False
        symbol_start = 0
        symbol_name = ""

        for match in re.finditer(r'\(symbol\s+"([^"]+)"', content):
            # Find the matching closing paren for this symbol
            symbol_name = match.group(1)

            # Skip internal/derived symbols (those with underscores followed by numbers)
            if "_" in symbol_name and symbol_name.split("_")[-1].isdigit():
                continue

            start = match.start()
            # Count parens to find end of symbol block
            depth = 1
            pos = match.end()
            while depth > 0 and pos < len(content):
                if content[pos] == '(':
                    depth += 1
                elif content[pos] == ')':
                    depth -= 1
                pos += 1

            symbol_block = content[start:pos]

            # Extract info from this symbol block
            desc_match = re.search(description_pattern, symbol_block)
            kw_match = re.search(keywords_pattern, symbol_block)
            pins = re.findall(pin_pattern, symbol_block)
            extends_match = re.search(extends_pattern, symbol_block)

            # Extract pin names
            pin_names = []
            for pin_match in re.finditer(r'\(pin\s+\w+\s+\w+\s+\(at[^)]+\)\s+\(length[^)]+\)\s+\(name\s+"([^"]*)"', symbol_block):
                pin_names.append(pin_match.group(1))

            # Determine if power symbol
            is_power = "power_symbol" in symbol_block or library_name == "power"

            # Count units
            unit_matches = re.findall(r'\(symbol\s+"' + re.escape(symbol_name) + r'_(\d+)_', symbol_block)
            unit_count = len(set(unit_matches)) if unit_matches else 1

            symbols.append(SymbolInfo(
                name=symbol_name,
                library=library_name,
                lib_id=f"{library_name}:{symbol_name}",
                description=desc_match.group(1) if desc_match else "",
                keywords=kw_match.group(1).split() if kw_match else [],
                pin_names=pin_names,
                pin_count=len(pins),
                unit_count=unit_count,
                extends=extends_match.group(1) if extends_match else "",
                is_power=is_power,
            ))

        return symbols

    def _resolve_kicad_path(self, uri: str) -> str:
        """Resolve KiCad environment variable paths.

        Args:
            uri: Path with ${VAR} format

        Returns:
            Resolved path or empty string if not found
        """
        # Extract variable name
        match = re.match(r'\$\{(\w+)\}(.*)', uri)
        if not match:
            return uri

        var_name = match.group(1)
        rest_path = match.group(2)

        # Try to get from environment
        var_value = os.environ.get(var_name)
        if var_value:
            return var_value + rest_path

        # Common KiCad paths (macOS)
        common_paths = {
            "KICAD8_SYMBOL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
            "KICAD7_SYMBOL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
            "KICAD_SYMBOL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
            "KICAD8_3DMODEL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels",
            "KICAD8_FOOTPRINT_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints",
        }

        if var_name in common_paths:
            resolved = common_paths[var_name] + rest_path
            if os.path.exists(resolved):
                return resolved

        # Linux common paths
        linux_paths = {
            "KICAD8_SYMBOL_DIR": "/usr/share/kicad/symbols",
            "KICAD7_SYMBOL_DIR": "/usr/share/kicad/symbols",
            "KICAD_SYMBOL_DIR": "/usr/share/kicad/symbols",
        }

        if var_name in linux_paths:
            resolved = linux_paths[var_name] + rest_path
            if os.path.exists(resolved):
                return resolved

        return ""
