"""Allow running the MCP server as: python -m kipy.mcp"""

import asyncio

from .server import run

asyncio.run(run())
