"""Screenshot tool — export schematic/PCB as PNG via kicad-cli.

Mirrors the C++ screenshot_handler.cpp pipeline:
  kicad-cli SVG export → sips PNG conversion → Pillow crop/resize → base64
"""

from __future__ import annotations

import base64
import logging
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

logger = logging.getLogger("zeo-mcp.screenshot")

# Background colors (from screenshot_handler.cpp)
SCH_BG = (245, 244, 239)
PCB_BG = (0, 16, 35)

# Claude API max image dimension
MAX_IMAGE_DIM = 1568

# Autocrop threshold (for antialiased edges)
CROP_THRESHOLD = 5

# Padding: 5% of content size, minimum 10px
PAD_RATIO = 0.05
PAD_MIN = 10



def find_kicad_cli() -> tuple[str, dict[str, str]]:
    """Locate the kicad-cli binary. Returns (path, env_overrides). Cached after first call."""
    if hasattr(find_kicad_cli, "_cache"):
        return find_kicad_cli._cache

    result = _find_kicad_cli_impl()
    find_kicad_cli._cache = result
    return result


def _find_kicad_cli_impl() -> tuple[str, dict[str, str]]:
    """Locate the kicad-cli binary. Returns (path, env_overrides).

    Search order (mirrors kicad_cli_util.h):
    1. KICAD_CLI_PATH env var
    2. Platform-specific default locations
    3. Fallback: kicad-cli on PATH
    """
    import platform
    env = {}
    is_windows = platform.system() == "Windows"
    cli_name = "kicad-cli.exe" if is_windows else "kicad-cli"

    # 1. Env var
    cli_path = os.environ.get("KICAD_CLI_PATH")
    if cli_path and os.path.isfile(cli_path):
        if not is_windows:
            frameworks = str(Path(cli_path).parent.parent / "Frameworks")
            if os.path.isdir(frameworks):
                env["DYLD_LIBRARY_PATH"] = frameworks
        return cli_path, env

    # 2. Find kicad-cli next to the running kicad process
    if is_windows:
        try:
            import subprocess as _sp
            result = _sp.run(
                ["powershell", "-Command",
                 "Get-Process kicad -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Path"],
                capture_output=True, text=True, timeout=5,
                creationflags=_sp.CREATE_NO_WINDOW,
            )
            kicad_exe = result.stdout.strip()
            if kicad_exe:
                candidate = str(Path(kicad_exe).parent / cli_name)
                if os.path.isfile(candidate):
                    return candidate, env
        except Exception:
            pass

        # Try common install locations
        for prog_dir in [
            os.environ.get("PROGRAMFILES", r"C:\Program Files"),
            os.environ.get("LOCALAPPDATA", ""),
        ]:
            if prog_dir:
                candidate = os.path.join(prog_dir, "Zeo", "bin", cli_name)
                if os.path.isfile(candidate):
                    return candidate, env
    else:
        # macOS: App bundle
        app_cli = "/Applications/Zeo.app/Contents/MacOS/kicad-cli"
        if os.path.isfile(app_cli):
            frameworks = "/Applications/Zeo.app/Contents/Frameworks"
            if os.path.isdir(frameworks):
                env["DYLD_LIBRARY_PATH"] = frameworks
            return app_cli, env

    # 3. PATH fallback
    cli_on_path = shutil.which(cli_name)
    if cli_on_path:
        return cli_on_path, env

    raise FileNotFoundError(
        f"{cli_name} not found. Set KICAD_CLI_PATH or ensure Zeo is installed."
    )


def _is_pcb(file_path: str) -> bool:
    return file_path.endswith(".kicad_pcb")


def export_svg(
    cli_path: str,
    cli_env: dict[str, str],
    file_path: str,
    output_dir: str,
    *,
    view: str | None = None,
    layers: list[str] | None = None,
    show_zones: bool = True,
    show_vias: bool = True,
    show_pads: bool = True,
    show_tracks: bool = True,
    show_values: bool = True,
    show_references: bool = True,
) -> str:
    """Export schematic/PCB to SVG using kicad-cli. Returns path to SVG."""
    env = {**os.environ, **cli_env}

    if _is_pcb(file_path):
        svg_path = os.path.join(output_dir, "screenshot.svg")
        cmd = [
            cli_path, "pcb", "export", "svg",
            "--exclude-drawing-sheet",
            "--fit-page-to-board",
            "--theme", "_builtin_default",
        ]

        # Layers
        if layers:
            cmd.extend(["--layers", ",".join(layers)])
        elif view == "top":
            cmd.extend(["--layers", "F.Cu,F.Silkscreen,Edge.Cuts"])
        elif view == "bottom":
            cmd.extend(["--layers", "B.Cu,B.Silkscreen,Edge.Cuts"])
            cmd.append("--mirror")
        else:
            cmd.extend([
                "--layers", "F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,Edge.Cuts"
            ])

        # Rendering options (PCB-only)
        if not show_zones:
            cmd.append("--zone-outlines-only")
        if not show_vias:
            cmd.append("--vias-outline-only")
        if not show_pads:
            cmd.append("--pads-outline-only")
        if not show_tracks:
            cmd.append("--tracks-outline-only")
        if not show_values:
            cmd.append("--no-values")
        if not show_references:
            cmd.append("--no-references")

        cmd.extend(["-o", svg_path, file_path])
    else:
        # Schematic — kicad-cli outputs SVG to a directory
        cmd = [
            cli_path, "sch", "export", "svg",
            "--exclude-drawing-sheet",
            "--theme", "_builtin_default",
            "-o", output_dir,
            file_path,
        ]
        # SVG file name is based on the schematic file name
        base = Path(file_path).stem
        svg_path = os.path.join(output_dir, f"{base}.svg")

    logger.info("Running: %s", " ".join(cmd))
    import platform as _plat
    _extra = {}
    if _plat.system() == "Windows":
        _extra["creationflags"] = subprocess.CREATE_NO_WINDOW
    result = subprocess.run(
        cmd, env=env, capture_output=True, text=True, timeout=30, **_extra,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"kicad-cli export failed (exit {result.returncode}): {result.stderr}"
        )

    if not os.path.isfile(svg_path):
        # Check for any SVG in output dir
        svgs = list(Path(output_dir).glob("*.svg"))
        if svgs:
            svg_path = str(svgs[0])
        else:
            raise RuntimeError(
                f"kicad-cli produced no SVG output in {output_dir}"
            )

    return svg_path


def convert_svg_to_png(svg_path: str, png_path: str, cli_path: str = "") -> str:
    """Convert SVG to PNG. Uses sips on macOS, cairosvg on other platforms.

    Args:
        cli_path: Path to kicad-cli, used on Windows to locate cairo-2.dll.
    """
    import platform
    is_windows = platform.system() == "Windows"

    if platform.system() == "Darwin":
        result = subprocess.run(
            ["sips", "-s", "format", "png", "-Z", "4096", svg_path, "--out", png_path],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            raise RuntimeError(f"sips conversion failed: {result.stderr}")
        return png_path

    # Windows/Linux: use cairosvg (KiCad ships cairo-2.dll on Windows)
    if is_windows and cli_path:
        bin_dir = str(Path(cli_path).parent)
        cairo_dll = os.path.join(bin_dir, "cairo-2.dll")
        if os.path.isfile(cairo_dll):
            if bin_dir not in os.environ.get("PATH", ""):
                os.environ["PATH"] = bin_dir + ";" + os.environ.get("PATH", "")
            # Pre-load cairo DLL so cairocffi finds it
            import ctypes
            try:
                ctypes.CDLL(cairo_dll)
            except OSError:
                pass

    # Redirect stdout to devnull during cairosvg — the MCP server uses stdio
    # and any stray output from cairo would corrupt the protocol / cause hangs.
    import io
    old_stdout = sys.stdout
    sys.stdout = io.StringIO()
    try:
        import cairosvg
        cairosvg.svg2png(url=svg_path, write_to=png_path, output_width=4096)
    finally:
        sys.stdout = old_stdout
    return png_path


def crop_and_resize(png_path: str, is_pcb: bool) -> bytes:
    """Crop to content, add padding, resize, and return PNG bytes.

    Uses Pillow (mirrors wxImage logic in screenshot_handler.cpp).
    """
    from PIL import Image

    img = Image.open(png_path).convert("RGBA")
    bg_color = PCB_BG if is_pcb else SCH_BG

    # Composite onto solid background to handle transparency
    bg = Image.new("RGBA", img.size, (*bg_color, 255))
    composited = Image.alpha_composite(bg, img).convert("RGB")

    # Find content bounding box (non-background pixels)
    pixels = composited.load()
    w, h = composited.size
    min_x, min_y, max_x, max_y = w, h, 0, 0

    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            dr = abs(r - bg_color[0])
            dg = abs(g - bg_color[1])
            db = abs(b - bg_color[2])
            if dr > CROP_THRESHOLD or dg > CROP_THRESHOLD or db > CROP_THRESHOLD:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)

    if max_x <= min_x or max_y <= min_y:
        # No content found — return full image
        logger.warning("No content detected in screenshot, returning full image")
    else:
        # Add padding
        content_w = max_x - min_x
        content_h = max_y - min_y
        pad_x = max(PAD_MIN, int(content_w * PAD_RATIO))
        pad_y = max(PAD_MIN, int(content_h * PAD_RATIO))

        crop_x1 = max(0, min_x - pad_x)
        crop_y1 = max(0, min_y - pad_y)
        crop_x2 = min(w, max_x + pad_x)
        crop_y2 = min(h, max_y + pad_y)

        composited = composited.crop((crop_x1, crop_y1, crop_x2, crop_y2))

    # Resize to fit within MAX_IMAGE_DIM
    w, h = composited.size
    if w > MAX_IMAGE_DIM or h > MAX_IMAGE_DIM:
        scale = MAX_IMAGE_DIM / max(w, h)
        new_w = int(w * scale)
        new_h = int(h * scale)
        composited = composited.resize((new_w, new_h), Image.LANCZOS)

    # Export to PNG bytes
    import io
    buf = io.BytesIO()
    composited.save(buf, format="PNG")
    return buf.getvalue()


def _save_temp_copy(kicad, tmpdir: str) -> tuple[str | None, bool]:
    """Save a temp copy of the currently open editor's document.

    Returns (file_path, is_pcb) or (None, False) if no editor is open.
    This ensures screenshots reflect the latest in-memory state.
    """
    # Try schematic first
    try:
        sch = kicad.get_schematic()
        temp_path = os.path.join(tmpdir, "screenshot.kicad_sch")
        sch.document_ops.save_copy(temp_path)
        logger.info("Saved temp schematic copy to %s", temp_path)
        return temp_path, False
    except Exception as e:
        logger.debug("No schematic to save: %s", e)

    # Try PCB
    try:
        board = kicad.get_board()
        temp_path = os.path.join(tmpdir, "screenshot.kicad_pcb")
        board.document_ops.save_copy(temp_path)
        logger.info("Saved temp PCB copy to %s", temp_path)
        return temp_path, True
    except Exception as e:
        logger.debug("No PCB to save: %s", e)

    return None, False


def _discover_file_path(kicad) -> str | None:
    """Discover the file path of the currently open editor.

    Strategy:
    1. Check if schematic/board editor is open via API
    2. Find the editor process's cwd (= project directory)
    3. Locate .kicad_sch or .kicad_pcb files in that directory
    """
    import glob as glob_mod
    import platform

    editor_type = None  # "sch" or "pcb"

    try:
        kicad.get_schematic()
        editor_type = "sch"
    except Exception:
        pass

    if not editor_type:
        try:
            kicad.get_board()
            editor_type = "pcb"
        except Exception:
            pass

    if not editor_type:
        return None

    # Find the editor process cwd on macOS/Linux
    if platform.system() in ("Darwin", "Linux"):
        try:
            proc_name = "eeschema" if editor_type == "sch" else "pcbnew"
            result = subprocess.run(
                ["pgrep", "-f", proc_name],
                capture_output=True, text=True, timeout=5,
            )
            pids = result.stdout.strip().split("\n")
            for pid in pids:
                if not pid.strip():
                    continue
                # Get cwd via lsof
                lsof = subprocess.run(
                    ["lsof", "-p", pid.strip(), "-Fn"],
                    capture_output=True, text=True, timeout=5,
                )
                lines = lsof.stdout.split("\n")
                for i, line in enumerate(lines):
                    if line == "fcwd" and i + 1 < len(lines):
                        cwd = lines[i + 1].lstrip("n")
                        ext = "*.kicad_sch" if editor_type == "sch" else "*.kicad_pcb"
                        matches = glob_mod.glob(os.path.join(cwd, ext))
                        if matches:
                            logger.info("Discovered file path: %s", matches[0])
                            return matches[0]
        except Exception as e:
            logger.debug("Process discovery failed: %s", e)

    return None


def take_screenshot(
    kicad,
    *,
    file_path: str | None = None,
    view: str | None = None,
    layers: list[str] | None = None,
    show_zones: bool = True,
    show_vias: bool = True,
    show_pads: bool = True,
    show_tracks: bool = True,
    show_values: bool = True,
    show_references: bool = True,
) -> tuple[str, str]:
    """Take a screenshot and return (base64_data, mime_type).

    If file_path is not provided, screenshots the currently open editor.
    """
    import time as _time
    _t0 = _time.monotonic()
    def _elapsed():
        return f"{_time.monotonic() - _t0:.1f}s"

    logger.info("[screenshot] start")
    cli_path, cli_env = find_kicad_cli()
    logger.info("[screenshot] found kicad-cli (%s) %s", cli_path, _elapsed())

    with tempfile.TemporaryDirectory(prefix="zeo-screenshot-") as tmpdir:
        # If no file_path, save a temp copy from the open editor's memory
        is_pcb_file = False
        if not file_path:
            file_path, is_pcb_file = _save_temp_copy(kicad, tmpdir)
            logger.info("[screenshot] save_temp_copy done (%s) %s", file_path, _elapsed())
        else:
            is_pcb_file = _is_pcb(file_path)

        if not file_path:
            raise RuntimeError(
                "No file_path provided and no open editor found. "
                "Open a schematic or PCB editor in Zeo first."
            )

        # Step 1: Export SVG
        svg_path = export_svg(
            cli_path, cli_env, file_path, tmpdir,
            view=view,
            layers=layers,
            show_zones=show_zones,
            show_vias=show_vias,
            show_pads=show_pads,
            show_tracks=show_tracks,
            show_values=show_values,
            show_references=show_references,
        )
        logger.info("[screenshot] SVG exported (%s) %s", svg_path, _elapsed())

        # Step 2: Convert SVG to PNG
        png_path = os.path.join(tmpdir, "screenshot.png")
        convert_svg_to_png(svg_path, png_path, cli_path=cli_path)
        logger.info("[screenshot] PNG converted %s", _elapsed())

        # Step 3: Crop and resize
        png_bytes = crop_and_resize(png_path, is_pcb_file)
        logger.info("[screenshot] crop/resize done (%d bytes) %s", len(png_bytes), _elapsed())

    # Step 4: Base64 encode
    b64_data = base64.b64encode(png_bytes).decode("ascii")
    logger.info("[screenshot] complete %s", _elapsed())
    return b64_data, "image/png"
