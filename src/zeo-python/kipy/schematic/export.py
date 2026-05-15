# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Export operations for netlists, BOM, and plots.
"""

from typing import TYPE_CHECKING, Optional, List, Dict, Union
from enum import IntEnum

from kipy.proto.schematic import schematic_commands_pb2
from kipy.proto.common.commands import editor_commands_pb2
from kipy.wrapper import Wrapper, unwrap

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class NetlistFormat(IntEnum):
    """Netlist output format."""
    KICAD = 1
    SPICE = 2
    ORCAD = 3
    CADSTAR = 4
    PADS = 5


# String-to-enum mapping for NetlistFormat (case-insensitive)
NETLIST_FORMAT_MAP = {
    "kicad": NetlistFormat.KICAD,
    "spice": NetlistFormat.SPICE,
    "orcad": NetlistFormat.ORCAD,
    "cadstar": NetlistFormat.CADSTAR,
    "pads": NetlistFormat.PADS,
}


class PlotFormat(IntEnum):
    """Plot output format."""
    PDF = 1
    SVG = 2
    HPGL = 3
    PS = 4
    DXF = 5
    PNG = 6


# String-to-enum mapping for PlotFormat (case-insensitive)
PLOT_FORMAT_MAP = {
    "pdf": PlotFormat.PDF,
    "svg": PlotFormat.SVG,
    "hpgl": PlotFormat.HPGL,
    "ps": PlotFormat.PS,
    "postscript": PlotFormat.PS,
    "dxf": PlotFormat.DXF,
    "png": PlotFormat.PNG,
}


class ExportOperations:
    """Export operations for netlists, BOM, and plots."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Netlist Export
    # =========================================================================

    def netlist(
        self,
        output_path: str,
        format: Union[NetlistFormat, str] = NetlistFormat.KICAD,
    ) -> Dict:
        """Export netlist to file.

        Args:
            output_path: Path for output file
            format: Netlist format - either NetlistFormat enum or string
                    ("kicad", "spice", "orcad", "cadstar", "pads")

        Returns:
            Dict with success, output_path, error_message

        Example:
            >>> result = sch.export.netlist("/tmp/myschematic.net")
            >>> result = sch.export.netlist("/tmp/test.net", format="kicad")
            >>> result = sch.export.netlist("/tmp/test.cir", format=NetlistFormat.SPICE)
            >>> if result['success']:
            ...     print(f"Netlist exported to: {result['output_path']}")
        """
        # Convert string format to enum if needed
        if isinstance(format, str):
            format_lower = format.lower()
            if format_lower not in NETLIST_FORMAT_MAP:
                raise ValueError(
                    f"Unknown netlist format '{format}'. "
                    f"Valid formats: {list(NETLIST_FORMAT_MAP.keys())}"
                )
            format = NETLIST_FORMAT_MAP[format_lower]

        cmd = schematic_commands_pb2.ExportNetlist()
        cmd.document.CopyFrom(self._doc)
        cmd.output_path = output_path
        cmd.format = format

        response = self._kicad.send(cmd, schematic_commands_pb2.ExportNetlistResponse)

        return {
            'success': response.success,
            'output_path': response.output_path,
            'error_message': response.error_message,
        }

    def spice_netlist(self, output_path: str) -> Dict:
        """Export SPICE netlist.

        Convenience method for netlist(format=NetlistFormat.SPICE).

        Args:
            output_path: Path for output file

        Returns:
            Dict with success, output_path, error_message

        Example:
            >>> result = sch.export.spice_netlist("/tmp/circuit.cir")
        """
        return self.netlist(output_path, NetlistFormat.SPICE)

    # =========================================================================
    # BOM Export
    # =========================================================================

    def bom(
        self,
        output_path: str,
        format: str = "csv",
        fields: Optional[List[str]] = None,
        group_references: bool = True,
    ) -> Dict:
        """Export Bill of Materials.

        Note: Full BOM export is typically done through File > Fabrication
        Outputs > BOM in KiCad. This API provides basic functionality.

        Args:
            output_path: Path for output file
            format: Output format ("csv", "xml", "json")
            fields: List of fields to include (None = default fields)
            group_references: If True, group identical components

        Returns:
            Dict with success, output_path, component_count, error_message

        Example:
            >>> result = sch.export.bom(
            ...     "/tmp/bom.csv",
            ...     fields=["Reference", "Value", "Footprint"]
            ... )
        """
        cmd = schematic_commands_pb2.ExportBOM()
        cmd.document.CopyFrom(self._doc)
        cmd.output_path = output_path
        cmd.format = format
        cmd.group_references = group_references

        if fields is not None:
            for field in fields:
                cmd.fields.append(field)

        response = self._kicad.send(cmd, schematic_commands_pb2.ExportBOMResponse)

        return {
            'success': response.success,
            'output_path': response.output_path,
            'component_count': response.component_count,
            'error_message': response.error_message,
        }

    # =========================================================================
    # Plot Export
    # =========================================================================

    def plot(
        self,
        output_path: str,
        format: Union[PlotFormat, str] = PlotFormat.PDF,
        plot_all_sheets: bool = True,
        black_and_white: bool = False,
        dpi: Optional[int] = None,
    ) -> Dict:
        """Export schematic plot (PDF, SVG, etc.).

        Note: Full plotting is typically done through File > Plot in KiCad.
        This API provides basic functionality.

        Args:
            output_path: Path for output file or directory
            format: Plot format - either PlotFormat enum or string
                    ("pdf", "svg", "hpgl", "ps", "postscript", "dxf", "png")
            plot_all_sheets: If True, plot all sheets; otherwise current only
            black_and_white: If True, use black and white mode
            dpi: DPI for raster formats (PNG)

        Returns:
            Dict with success, output_files, error_message

        Example:
            >>> result = sch.export.plot("/tmp/schematic.pdf")
            >>> result = sch.export.plot("/tmp/schematic.svg", format="svg")
            >>> result = sch.export.plot("/tmp/schematic.pdf", format=PlotFormat.PDF)
            >>> if result['success']:
            ...     for f in result['output_files']:
            ...         print(f"Created: {f}")
        """
        # Convert string format to enum if needed
        if isinstance(format, str):
            format_lower = format.lower()
            if format_lower not in PLOT_FORMAT_MAP:
                raise ValueError(
                    f"Unknown plot format '{format}'. "
                    f"Valid formats: {list(PLOT_FORMAT_MAP.keys())}"
                )
            format = PLOT_FORMAT_MAP[format_lower]

        cmd = schematic_commands_pb2.ExportPlot()
        cmd.document.CopyFrom(self._doc)
        cmd.output_path = output_path
        cmd.format = format
        cmd.plot_all_sheets = plot_all_sheets
        cmd.black_and_white = black_and_white

        if dpi is not None:
            cmd.dpi = dpi

        response = self._kicad.send(cmd, schematic_commands_pb2.ExportPlotResponse)

        return {
            'success': response.success,
            'output_files': list(response.output_files),
            'error_message': response.error_message,
        }

    def pdf(self, output_path: str, **kwargs) -> Dict:
        """Export to PDF.

        Convenience method for plot(format=PlotFormat.PDF).

        Args:
            output_path: Path for output file
            **kwargs: Additional arguments passed to plot()

        Returns:
            Dict with success, output_files, error_message
        """
        return self.plot(output_path, format=PlotFormat.PDF, **kwargs)

    def svg(self, output_path: str, **kwargs) -> Dict:
        """Export to SVG.

        Convenience method for plot(format=PlotFormat.SVG).

        Args:
            output_path: Path for output file
            **kwargs: Additional arguments passed to plot()

        Returns:
            Dict with success, output_files, error_message
        """
        return self.plot(output_path, format=PlotFormat.SVG, **kwargs)

    # =========================================================================
    # String Export/Import Operations
    # =========================================================================

    def to_string(self) -> str:
        """Export the entire schematic document to a string.

        Returns the schematic in KiCad's native file format as a string,
        which can be saved, transmitted, or processed.

        Note:
            The document must be saved to disk before calling this method.
            Use ``sch.save()`` first if you have made unsaved changes.
            This is a requirement of the KiCad IPC API.

        Returns:
            String containing the schematic file contents

        Raises:
            ApiError: If document is not saved to disk

        Example:
            >>> sch.save()  # Ensure saved first
            >>> contents = sch.export.to_string()
            >>> # Save to file manually
            >>> with open("/tmp/backup.kicad_sch", "w") as f:
            ...     f.write(contents)
        """
        cmd = editor_commands_pb2.SaveDocumentToString()
        cmd.document.CopyFrom(self._doc)

        response = self._kicad.send(cmd, editor_commands_pb2.SavedDocumentResponse)
        return response.contents

    def selection_to_string(self) -> Dict:
        """Export the current editor selection to a string.

        Returns the selected items in KiCad's clipboard format,
        which can be used for copy/paste operations.

        Returns:
            Dict with:
                - contents: String with selected items
                - ids: List of KIID strings for selected items

        Example:
            >>> # Select some items first
            >>> sch.selection.add(symbols)
            >>> result = sch.export.selection_to_string()
            >>> print(f"Exported {len(result['ids'])} items")
        """
        cmd = editor_commands_pb2.SaveSelectionToString()

        response = self._kicad.send(cmd, editor_commands_pb2.SavedSelectionResponse)
        return {
            'contents': response.contents,
            'ids': [kiid.value for kiid in response.ids],
        }

    def from_string(self, contents: str) -> List[Wrapper]:
        """Parse and create items from a string.

        Parses the provided string (in KiCad file format) and creates
        the items in the current schematic.

        Args:
            contents: String containing schematic items in KiCad format

        Returns:
            List of created Wrapper objects

        Example:
            >>> # Read from a file
            >>> with open("symbols.kicad_sch") as f:
            ...     contents = f.read()
            >>> items = sch.export.from_string(contents)
            >>> print(f"Created {len(items)} items")

            >>> # Or paste previously copied items
            >>> items = sch.export.from_string(copied_contents)
        """
        cmd = editor_commands_pb2.ParseAndCreateItemsFromString()
        cmd.document.CopyFrom(self._doc)
        cmd.contents = contents

        response = self._kicad.send(cmd, editor_commands_pb2.CreateItemsResponse)

        created = []
        for result in response.created_items:
            try:
                wrapped = unwrap(result.item)
                created.append(wrapped)
            except (ValueError, NotImplementedError):
                pass

        return created
