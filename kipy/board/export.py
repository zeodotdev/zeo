# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Export operations for manufacturing files.
"""

from typing import TYPE_CHECKING, List, Optional, Sequence, Tuple

from kipy.proto.common.commands import editor_commands_pb2
from kipy.board_types import unwrap
from kipy.wrapper import Wrapper

if TYPE_CHECKING:
    from kipy.board.base import Board


class ExportOperations:
    """Export operations for manufacturing files."""

    def __init__(self, board: "Board"):
        self._board = board

    def get_as_string(self) -> str:
        """Get the board as a KiCad S-expression string."""
        cmd = editor_commands_pb2.SaveDocumentToString()
        cmd.document.CopyFrom(self._board._doc)
        return self._board._kicad.send(cmd, editor_commands_pb2.SavedDocumentResponse).contents

    def from_string(self, contents: str) -> List[Wrapper]:
        """Create board items from a KiCad S-expression string.

        This parses KiCad board/footprint data and creates the items on the board.
        Useful for copy/paste workflows or importing from templates.

        Args:
            contents: KiCad S-expression formatted string

        Returns:
            List of created items
        """
        cmd = editor_commands_pb2.ParseAndCreateItemsFromString()
        cmd.header.document.CopyFrom(self._board._doc)
        cmd.contents = contents
        response = self._board._kicad.send(cmd, editor_commands_pb2.CreateItemsResponse)

        created = []
        for result in response.created_items:
            if result.status.code == 1:  # ISC_OK
                try:
                    created.append(unwrap(result.item))
                except (ValueError, NotImplementedError):
                    pass
        return created

    def run_drc_cli(
        self,
        output_path: str,
        format: str = "report",
        units: str = "mm",
    ) -> Tuple[str, int]:
        """Run Design Rules Check via kicad-cli.

        Args:
            output_path: Path for the DRC report file
            format: Output format - "report" (text) or "json"
            units: Units for coordinates - "mm", "in", or "mils"

        Returns:
            Tuple of (report_path, violation_count)
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError

        valid_formats = ["report", "json"]
        if format not in valid_formats:
            raise ValueError(f"Invalid format '{format}'. Valid options: {valid_formats}")

        valid_units = ["mm", "in", "mils"]
        if units not in valid_units:
            raise ValueError(f"Invalid units '{units}'. Valid options: {valid_units}")

        command = [
            get_kicad_cli_path(),
            "pcb", "drc",
            f"--format={format}",
            f"--units={units}",
            "-o", output_path,
            self._board._doc.board_filename,
        ]

        result = run_cli(command)

        if result.returncode < 0:
            raise CLIError(
                f"DRC failed to run: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return (output_path, result.returncode)

    def generate_gerbers(
        self,
        output_dir: str,
        layers: Optional[Sequence[str]] = None,
        no_x2: bool = False,
        use_drill_origin: bool = True,
        subtract_soldermask: bool = False,
        use_board_plot_params: bool = True,
    ) -> List[str]:
        """Export Gerber files via kicad-cli.

        Args:
            output_dir: Directory for output files
            layers: List of layer names (e.g., ["F.Cu", "B.Cu"]).
                    If None and use_board_plot_params is True, uses stored board settings.
            no_x2: Disable X2 extensions
            use_drill_origin: Use drill/place file origin
            subtract_soldermask: Subtract soldermask from silkscreen
            use_board_plot_params: Use stored board plot settings when layers not specified

        Returns:
            List of generated file paths
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError
        import re

        # Use the new 'gerbers' command (KiCad 9.0+)
        command = [
            get_kicad_cli_path(),
            "pcb", "export", "gerbers",
            "-o", output_dir,
        ]

        if no_x2:
            command.append("--no-x2")
        if use_drill_origin:
            command.append("--use-drill-file-origin")
        if subtract_soldermask:
            command.append("--subtract-soldermask")
        if layers:
            command.extend(["--layers", ",".join(layers)])
        elif use_board_plot_params:
            command.append("--board-plot-params")

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"Gerber export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        files = []
        for match in re.finditer(r"Plotted to ['\"]?([^'\"]+)['\"]?", result.stdout):
            files.append(match.group(1))

        return files

    def generate_drill_files(
        self,
        output_dir: str,
        format: str = "excellon",
        units: str = "mm",
        generate_map: bool = False,
        map_format: str = "pdf",
        use_drill_origin: bool = True,
    ) -> List[str]:
        """Export drill files via kicad-cli.

        Args:
            output_dir: Directory for output files
            format: Drill format - "excellon" or "gerber"
            units: Units - "mm" or "in"
            generate_map: Generate drill map file
            map_format: Map format - "pdf", "ps", "svg", "dxf", "gerber"
            use_drill_origin: Use drill/place file origin

        Returns:
            List of generated file paths
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError
        import re

        valid_formats = ["excellon", "gerber"]
        if format not in valid_formats:
            raise ValueError(f"Invalid format '{format}'. Valid options: {valid_formats}")

        valid_units = ["mm", "in"]
        if units not in valid_units:
            raise ValueError(f"Invalid units '{units}'. Valid options: {valid_units}")

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "drill",
            f"--format={format}",
            f"-u", units,
            "-o", output_dir,
        ]

        if use_drill_origin:
            command.append("--drill-origin=plot")

        if generate_map:
            command.append("--generate-map")
            command.extend(["--map-format", map_format])

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"Drill export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        files = []
        for match in re.finditer(r"Created file ['\"]?([^'\"]+)['\"]?", result.stdout):
            files.append(match.group(1))

        return files

    def generate_pos(
        self,
        output_path: str,
        side: str = "both",
        format: str = "csv",
        units: str = "mm",
        use_drill_origin: bool = True,
        smd_only: bool = False,
        exclude_th: bool = False,
        exclude_dnp: bool = True,
        bottom_negate_x: bool = False,
    ) -> str:
        """Export Pick & Place / Component Position file via kicad-cli.

        Generates a file listing component positions for assembly machines.

        Args:
            output_path: Path for the output file
            side: Which side(s) - "top", "bottom", or "both"
            format: Output format - "ascii" or "csv"
            units: Coordinate units - "mm" or "in"
            use_drill_origin: Use drill/place file origin
            smd_only: Only include SMD components
            exclude_th: Exclude through-hole footprints
            exclude_dnp: Exclude DNP (Do Not Populate) components
            bottom_negate_x: Negate X coordinates for bottom side

        Returns:
            Path to generated file
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError

        valid_sides = ["top", "bottom", "both"]
        if side not in valid_sides:
            raise ValueError(f"Invalid side '{side}'. Valid options: {valid_sides}")

        valid_formats = ["ascii", "csv"]
        if format not in valid_formats:
            raise ValueError(f"Invalid format '{format}'. Valid options: {valid_formats}")

        valid_units = ["mm", "in"]
        if units not in valid_units:
            raise ValueError(f"Invalid units '{units}'. Valid options: {valid_units}")

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "pos",
            "-o", output_path,
            f"--side={side}",
            f"--format={format}",
            f"--units={units}",
        ]

        if use_drill_origin:
            command.append("--use-drill-file-origin")
        if smd_only:
            command.append("--smd-only")
        if exclude_th:
            command.append("--exclude-fp-th")
        if exclude_dnp:
            command.append("--exclude-dnp")
        if bottom_negate_x:
            command.append("--bottom-negate-x")

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"Position file export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return output_path

    def generate_step(
        self,
        output_path: str,
        use_drill_origin: bool = False,
        use_grid_origin: bool = False,
        board_only: bool = False,
        include_tracks: bool = False,
        include_zones: bool = False,
        exclude_dnp: bool = True,
        no_unspecified: bool = True,
        subst_models: bool = False,
        min_distance: Optional[float] = None,
        user_origin: Optional[Tuple[float, float]] = None,
        force: bool = True,
    ) -> str:
        """Export 3D STEP model via kicad-cli.

        Generates a STEP file for mechanical CAD integration.

        Args:
            output_path: Path for the output .step file
            use_drill_origin: Use drill/place file origin
            use_grid_origin: Use grid origin
            board_only: Export board outline only (no components)
            include_tracks: Include copper tracks in model
            include_zones: Include copper zones in model
            exclude_dnp: Exclude DNP components
            no_unspecified: Ignore models with unspecified paths
            subst_models: Substitute STEP models with footprint
            min_distance: Minimum distance between points (mm)
            user_origin: Custom origin as (x, y) tuple in mm
            force: Overwrite existing output file

        Returns:
            Path to generated STEP file
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "step",
            "-o", output_path,
        ]

        if use_drill_origin:
            command.append("--drill-origin")
        if use_grid_origin:
            command.append("--grid-origin")
        if board_only:
            command.append("--board-only")
        if include_tracks:
            command.append("--include-tracks")
        if include_zones:
            command.append("--include-zones")
        if exclude_dnp:
            command.append("--no-dnp")
        if no_unspecified:
            command.append("--no-unspecified")
        if subst_models:
            command.append("--subst-models")
        if min_distance is not None:
            command.extend(["--min-distance", str(min_distance)])
        if user_origin is not None:
            command.extend(["--user-origin", f"{user_origin[0]},{user_origin[1]}"])
        if force:
            command.append("--force")

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"STEP export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return output_path

    def generate_svg(
        self,
        output_dir: str,
        layers: Optional[Sequence[str]] = None,
        mirror: bool = False,
        negative: bool = False,
        black_and_white: bool = False,
        theme: Optional[str] = None,
        page_size_mode: int = 0,
        drill_shape_opt: int = 2,
        single_file: bool = False,
    ) -> List[str]:
        """Export SVG files via kicad-cli.

        Args:
            output_dir: Directory for output files (or full path if single_file=True)
            layers: List of layer names (e.g., ["F.Cu", "B.Cu"]).
                    If None, exports all layers.
            mirror: Mirror the board
            negative: Plot as negative
            black_and_white: Black and white only
            theme: Color theme name
            page_size_mode: 0=page with frame, 1=current page size, 2=board area only
            drill_shape_opt: 0=no shape, 1=small, 2=actual
            single_file: If True, output all layers to a single file

        Returns:
            List of generated file paths
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError
        import os
        import glob

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "svg",
            "-o", output_dir,
        ]

        if layers:
            command.extend(["--layers", ",".join(layers)])
        if mirror:
            command.append("--mirror")
        if negative:
            command.append("--negative")
        if black_and_white:
            command.append("--black-and-white")
        if theme:
            command.extend(["--theme", theme])
        command.extend(["--page-size-mode", str(page_size_mode)])
        command.extend(["--drill-shape-opt", str(drill_shape_opt)])
        if single_file:
            command.append("--mode-single")
        else:
            command.append("--mode-multi")

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"SVG export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        if single_file:
            return [output_dir] if os.path.exists(output_dir) else []

        return sorted(glob.glob(os.path.join(output_dir, "*.svg")))

    def generate_pdf(
        self,
        output_dir: str,
        layers: Optional[Sequence[str]] = None,
        mirror: bool = False,
        negative: bool = False,
        black_and_white: bool = False,
        theme: Optional[str] = None,
        drill_shape_opt: int = 2,
        separate_files: bool = False,
    ) -> List[str]:
        """Export PDF files via kicad-cli.

        Args:
            output_dir: Directory for output files (or full path for single file)
            layers: List of layer names (e.g., ["F.Cu", "B.Cu"]).
                    If None, exports all layers.
            mirror: Mirror the board
            negative: Plot as negative
            black_and_white: Black and white only
            theme: Color theme name
            drill_shape_opt: 0=no shape, 1=small, 2=actual
            separate_files: If True, plot layers to separate PDF files

        Returns:
            List of generated file paths
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError
        import os
        import glob

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "pdf",
            "-o", output_dir,
        ]

        if layers:
            command.extend(["--layers", ",".join(layers)])
        if mirror:
            command.append("--mirror")
        if negative:
            command.append("--negative")
        if black_and_white:
            command.append("--black-and-white")
        if theme:
            command.extend(["--theme", theme])
        command.extend(["--drill-shape-opt", str(drill_shape_opt)])
        if separate_files:
            command.append("--mode-separate")
        else:
            command.append("--mode-separate")

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"PDF export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return sorted(glob.glob(os.path.join(output_dir, "*.pdf")))

    def generate_dxf(
        self,
        output_dir: str,
        layers: Optional[Sequence[str]] = None,
        use_contours: bool = False,
        use_drill_origin: bool = False,
        output_units: str = "mm",
        drill_shape_opt: int = 2,
    ) -> List[str]:
        """Export DXF files via kicad-cli.

        Args:
            output_dir: Directory for output files
            layers: List of layer names (e.g., ["F.Cu", "Edge.Cuts"]).
                    If None, exports all layers.
            use_contours: Plot graphic items using their contours
            use_drill_origin: Use drill/place file origin
            output_units: Output units - "mm" or "in"
            drill_shape_opt: 0=no shape, 1=small, 2=actual

        Returns:
            List of generated file paths
        """
        from kipy.cli import get_kicad_cli_path, run_cli
        from kipy.errors import CLIError
        import os
        import glob

        valid_units = ["mm", "in"]
        if output_units not in valid_units:
            raise ValueError(f"Invalid output_units '{output_units}'. Valid: {valid_units}")

        command = [
            get_kicad_cli_path(),
            "pcb", "export", "dxf",
            "-o", output_dir,
        ]

        if layers:
            command.extend(["--layers", ",".join(layers)])
        if use_contours:
            command.append("--use-contours")
        if use_drill_origin:
            command.append("--use-drill-origin")
        command.extend(["--output-units", output_units])
        command.extend(["--drill-shape-opt", str(drill_shape_opt)])

        command.append(self._board._doc.board_filename)

        result = run_cli(command)
        if not result.success:
            raise CLIError(
                f"DXF export failed: {result.stderr}",
                result.returncode,
                result.stdout,
                result.stderr
            )

        return sorted(glob.glob(os.path.join(output_dir, "*.dxf")))
