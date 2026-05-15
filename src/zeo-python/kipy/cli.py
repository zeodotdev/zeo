# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
CLI wrapper utilities for kicad-cli commands.

This module provides utilities for running kicad-cli commands from Python,
enabling export functions like netlist generation, Gerber export, and DRC/ERC.

Example:
    >>> from kipy.cli import get_kicad_cli_path, run_cli
    >>> result = run_cli([get_kicad_cli_path(), "version"])
    >>> if result.success:
    ...     print(result.stdout)
"""

import os
import subprocess
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class CLIResult:
    """Result of a kicad-cli command execution.

    Attributes:
        stdout: Standard output from the command
        stderr: Standard error from the command
        returncode: Process return code (0 = success)
    """
    stdout: str
    stderr: str
    returncode: int

    @property
    def success(self) -> bool:
        """Returns True if the command completed successfully (returncode == 0)."""
        return self.returncode == 0


def get_kicad_cli_path() -> str:
    """Get the path to the kicad-cli executable.

    Search order:
    1. KICAD_CLI environment variable
    2. macOS app bundle (relative to Python.framework)
    3. System PATH

    Returns:
        Path to the kicad-cli executable

    Example:
        >>> import os
        >>> os.environ['KICAD_CLI'] = '/opt/kicad/bin/kicad-cli'
        >>> get_kicad_cli_path()
        '/opt/kicad/bin/kicad-cli'
    """
    import sys

    if 'KICAD_CLI' in os.environ:
        return os.environ['KICAD_CLI']

    # Try to find kicad-cli in macOS app bundle
    # Python is at: .../Zeo.app/Contents/Frameworks/Python.framework/Versions/X.Y/bin/python
    # kicad-cli is at: .../Zeo.app/Contents/MacOS/kicad-cli
    exe_path = sys.executable
    if 'Python.framework' in exe_path:
        # Navigate from Python.framework to MacOS directory
        # Split at Python.framework and go to Contents/MacOS
        parts = exe_path.split('Python.framework')
        if len(parts) >= 1:
            # parts[0] is '.../Zeo.app/Contents/Frameworks/'
            frameworks_dir = parts[0].rstrip('/')
            if frameworks_dir.endswith('/Frameworks'):
                contents_dir = os.path.dirname(frameworks_dir)
                cli_path = os.path.join(contents_dir, 'MacOS', 'kicad-cli')
                if os.path.exists(cli_path):
                    return cli_path

    return "kicad-cli"


def run_cli(
    command: List[str],
    timeout: Optional[int] = None,
    cwd: Optional[str] = None
) -> CLIResult:
    """Execute a kicad-cli command and return the result.

    Args:
        command: Command and arguments as a list of strings
        timeout: Optional timeout in seconds
        cwd: Optional working directory for the command

    Returns:
        CLIResult containing stdout, stderr, and returncode

    Raises:
        subprocess.TimeoutExpired: If the command times out
        FileNotFoundError: If kicad-cli is not found

    Example:
        >>> result = run_cli([get_kicad_cli_path(), "version"])
        >>> if result.success:
        ...     print(f"KiCad version: {result.stdout.strip()}")
    """
    proc = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding='utf-8',
        cwd=cwd
    )

    stdout, stderr = proc.communicate(timeout=timeout)

    return CLIResult(
        stdout=stdout,
        stderr=stderr,
        returncode=proc.returncode
    )
