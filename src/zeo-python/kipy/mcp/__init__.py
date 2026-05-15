# Zeo MCP Server - exposes KiCad agent tools to Claude Code

import asyncio


def main():
    from .server import run
    asyncio.run(run())
