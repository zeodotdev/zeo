"""Zeo MCP Server — thin protocol bridge to KiCad's C++ tool executor.

Fetches tool schemas from the C++ app at startup and delegates all tool
execution to C++, getting undo transactions, diff views, and proper
timeouts for free.

Run via:  zeo mcp
"""

import asyncio
import json
import logging
import os
from pathlib import Path
import sys
import time
import urllib.request
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from mcp.types import (
    CallToolResult, GetPromptResult, ImageContent, Prompt, PromptMessage,
    TextContent, Tool,
)

from kipy import KiCad

from .screenshot import take_screenshot

logger = logging.getLogger("zeo-mcp")

_COMPONENT_SEARCH_MCP_URL = "https://pcbparts.dev/mcp"

# ─── Tool usage tracking ────────────────────────────────────────────────────
# Fire-and-forget inserts to Supabase tool_usage table for CC MCP analytics.

_SUPABASE_URL_DEFAULT = "https://sfgfftznmsekkdwtpjwp.supabase.co"
_SUPABASE_ANON_KEY_DEFAULT = "sb_publishable_pxNOOfAZMSWYS8Pa2sQdaw_ZmIAQuME"

_tracking_user_id: str | None = None
_tracking_supabase_url: str | None = None
_tracking_anon_key: str | None = None
_tracking_initialized = False
_tracking_session_path = Path.home() / "Library/Application Support/kicad/agent_session.json"


def _decode_jwt_user_id(token: str) -> str | None:
    """Extract user_id (sub claim) from a Supabase JWT without crypto."""
    import base64
    try:
        payload = token.split(".")[1]
        payload += "=" * (4 - len(payload) % 4)
        return json.loads(base64.urlsafe_b64decode(payload)).get("sub")
    except Exception:
        return None


def _init_tracking():
    """Lazy-init: read Supabase creds from env (or defaults) and user_id from session JWT."""
    global _tracking_user_id, _tracking_supabase_url, _tracking_anon_key, _tracking_initialized
    _tracking_initialized = True

    _tracking_supabase_url = os.environ.get("ZEO_SUPABASE_URL") or _SUPABASE_URL_DEFAULT
    _tracking_anon_key = os.environ.get("ZEO_SUPABASE_ANON_KEY") or _SUPABASE_ANON_KEY_DEFAULT

    try:
        session = json.loads(_tracking_session_path.read_text())
        token = session.get("access_token", "")
        _tracking_user_id = _decode_jwt_user_id(token) if token else None
        if _tracking_user_id:
            logger.info("Tool tracking enabled for user %s", _tracking_user_id[:8])
    except Exception as e:
        logger.debug("Tool tracking disabled: %s", e)


def _post_tool_usage(tool_name: str, success: bool, duration_ms: int):
    """POST a row to Supabase tool_usage. Blocking — run in background thread."""
    if not _tracking_user_id or not _tracking_supabase_url:
        return
    try:
        token = json.loads(_tracking_session_path.read_text()).get("access_token", "")
        if not token:
            return
        payload = json.dumps({
            "user_id": _tracking_user_id,
            "tool_name": tool_name,
            "success": success,
            "duration_ms": duration_ms,
        }).encode()
        req = urllib.request.Request(
            f"{_tracking_supabase_url}/rest/v1/tool_usage",
            data=payload,
            headers={
                "Content-Type": "application/json",
                "apikey": _tracking_anon_key,
                "Authorization": f"Bearer {token}",
                "Prefer": "return=minimal",
            },
        )
        urllib.request.urlopen(req, timeout=5)
    except Exception:
        pass

# Tools proxied directly to pcbparts.dev (component search)
_COMPONENT_SEARCH_TOOLS: set[str] = set()


def _extract_last_json(output: str) -> str:
    """Return the last valid JSON object from a possibly-mixed output string.

    Tool scripts may print debug lines before their JSON result.
    Returns the raw output unchanged if no JSON object is found.
    """
    output = output.strip()
    if not output:
        return output
    try:
        json.loads(output)
        return output
    except json.JSONDecodeError:
        for line in reversed(output.split("\n")):
            line = line.strip()
            if line.startswith("{"):
                try:
                    json.loads(line)
                    return line
                except json.JSONDecodeError:
                    continue
    return output


class ConnectionManager:
    """Manages the KiCad connection and tool execution."""

    def __init__(self, kicad: KiCad, tool_list: list[Tool],
                 cpp_tool_apps: dict[str, str], cpp_tool_names: set[str]):
        self.kicad = kicad
        self.tool_list = tool_list
        # Map of tool_name -> app ("sch"/"pcb") from C++ manifest, for editor auto-launch
        self._cpp_tool_apps = cpp_tool_apps
        # All tool names from C++ manifest (including those without app)
        self._cpp_tool_names = cpp_tool_names

    def launch_editor(self, doc_type: str) -> None:
        """Launch an editor via the project manager's KIWAY (non-standalone mode)."""
        self.kicad.launch_editor(doc_type)

    def has_tool(self, name: str) -> bool:
        """Check if a tool name is known to the C++ executor."""
        return name in self._cpp_tool_names

    def _ensure_editor(self, name: str) -> None:
        """Auto-launch the editor needed by a tool. No-op if already open.

        Raises on failure.
        """
        app = self._cpp_tool_apps.get(name)
        if app is None:
            return

        doc_type = {"sch": "schematic", "pcb": "pcb"}.get(app)
        if doc_type is None:
            return

        self.launch_editor(doc_type)

    def execute_tool(self, name: str, arguments: dict) -> str:
        """Execute a tool via the C++ app's embedded Python executor."""
        self._ensure_editor(name)

        result = self.kicad.execute_tool(
            tool_name=name,
            tool_args_json=json.dumps(arguments),
        )

        if not result["success"]:
            return json.dumps({
                "status": "error",
                "message": result["error_message"],
            })

        output = result["result_json"].strip()
        if not output or output == "(no output)":
            return json.dumps({"status": "success", "message": "Tool completed with no output"})

        return _extract_last_json(output)

    def take_screenshot(self, **kwargs):
        """Take a screenshot."""
        return take_screenshot(self.kicad, **kwargs)


def _fetch_component_search_tools() -> list[Tool]:
    """Fetch component search tool schemas from pcbparts.dev MCP endpoint."""
    payload = json.dumps({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/list",
        "params": {},
    }).encode()

    req = urllib.request.Request(
        _COMPONENT_SEARCH_MCP_URL,
        data=payload,
        headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"},
    )

    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            body = resp.read().decode()
    except Exception as e:
        logger.warning("Failed to fetch component search schemas: %s", e)
        return []

    # Response may be SSE (lines starting with "data:") or plain JSON
    result_json = None
    for line in body.split("\n"):
        line = line.strip()
        if line.startswith("data:"):
            line = line[5:].strip()
        if not line:
            continue
        try:
            parsed = json.loads(line)
            if "result" in parsed:
                result_json = parsed
                break
        except json.JSONDecodeError:
            continue

    if not result_json:
        logger.warning("No valid response from component search MCP")
        return []

    tools = []
    for t in result_json.get("result", {}).get("tools", []):
        name = t.get("name", "")
        _COMPONENT_SEARCH_TOOLS.add(name)
        tools.append(Tool(
            name=name,
            description=t.get("description", ""),
            inputSchema=t.get("inputSchema", {"type": "object", "properties": {}}),
        ))

    return tools


def _call_component_search(tool_name: str, arguments: dict) -> str:
    """Proxy a tool call to pcbparts.dev MCP endpoint."""
    payload = json.dumps({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": tool_name,
            "arguments": arguments,
        },
    }).encode()

    req = urllib.request.Request(
        _COMPONENT_SEARCH_MCP_URL,
        data=payload,
        headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"},
    )

    with urllib.request.urlopen(req, timeout=30) as resp:
        body = resp.read().decode()

    # Parse SSE or plain JSON response
    for line in body.split("\n"):
        line = line.strip()
        if line.startswith("data:"):
            line = line[5:].strip()
        if not line:
            continue
        try:
            parsed = json.loads(line)
            if "result" in parsed:
                content_list = parsed["result"].get("content", [])
                texts = [c.get("text", "") for c in content_list if c.get("type") == "text"]
                return "\n".join(texts) if texts else json.dumps(parsed["result"])
        except json.JSONDecodeError:
            continue

    return body


def _load_addendum() -> str:
    """Load the MCP-specific addendum markdown (cached after first read)."""
    if not hasattr(_load_addendum, "_cache"):
        path = Path(__file__).parent / "addendum.md"
        _load_addendum._cache = path.read_text()
    return _load_addendum._cache


def _load_instructions(kicad: KiCad) -> str:
    """Load core instructions via IPC from C++ app, append MCP addendum."""
    if not hasattr(_load_instructions, "_cache"):
        try:
            core = kicad.get_instructions()
            _load_instructions._cache = core + "\n" + _load_addendum()
            logger.info("Loaded instructions via IPC (%d bytes core + addendum)",
                        len(core))
        except Exception as e:
            logger.error("Failed to fetch instructions via IPC: %s", e)
            _load_instructions._cache = _load_addendum()
    return _load_instructions._cache


def _save_document(kicad: KiCad, doc_type: str) -> dict:
    """Save the schematic and/or PCB document to disk.

    Args:
        kicad: Active KiCad connection.
        doc_type: "schematic", "pcb", or "all".

    Returns:
        dict with status and details of what was saved.
    """
    saved = []
    errors = []

    if doc_type in ("schematic", "all"):
        try:
            sch = kicad.get_schematic()
            sch.document_ops.save()
            saved.append("schematic")
        except Exception as e:
            if doc_type == "schematic":
                errors.append(f"schematic: {e}")
            # If "all", silently skip if editor isn't open

    if doc_type in ("pcb", "all"):
        try:
            board = kicad.get_board()
            board.save()
            saved.append("pcb")
        except Exception as e:
            if doc_type == "pcb":
                errors.append(f"pcb: {e}")

    if errors:
        return {"status": "error", "message": "; ".join(errors)}

    if not saved:
        return {"status": "error", "message": "No open editors found to save"}

    return {"status": "success", "saved": saved}


def _create_server(conn: ConnectionManager) -> Server:
    """Create and configure the MCP server with all tool handlers."""
    server = Server("zeo", instructions=_load_instructions(conn.kicad))
    tool_list = conn.tool_list

    @server.list_prompts()
    async def handle_list_prompts() -> list[Prompt]:
        return [
            Prompt(
                name="zeo-instructions",
                description="KiCad schematic and PCB design instructions for the Zeo MCP tools",
            )
        ]

    @server.get_prompt()
    async def handle_get_prompt(
        name: str, arguments: dict[str, str] | None
    ) -> GetPromptResult:
        if name != "zeo-instructions":
            raise ValueError(f"Unknown prompt: {name}")
        return GetPromptResult(
            messages=[
                PromptMessage(
                    role="user",
                    content=TextContent(type="text", text=_load_instructions(conn.kicad)),
                )
            ]
        )

    @server.list_tools()
    async def handle_list_tools():
        return tool_list

    @server.call_tool()
    async def handle_call_tool(name: str, arguments: dict) -> CallToolResult:
        if not _tracking_initialized:
            _init_tracking()

        start = time.monotonic()
        try:
            result = await _handle_call_tool_inner(name, arguments)
        except Exception as e:
            result = CallToolResult(
                content=[TextContent(
                    type="text",
                    text=json.dumps({"status": "error", "message": str(e)}),
                )],
                isError=True,
            )
        duration_ms = int((time.monotonic() - start) * 1000)

        is_error = result.isError if result.isError is not None else False
        asyncio.create_task(asyncio.to_thread(
            _post_tool_usage, name, not is_error, duration_ms,
        ))

        return result

    async def _handle_call_tool_inner(name: str, arguments: dict) -> CallToolResult:
        # launch_editor opens an editor via the project manager's KIWAY
        if name == "launch_editor":
            doc_type = arguments.get("doc_type", "")
            if doc_type not in ("schematic", "pcb"):
                raise ValueError("doc_type must be 'schematic' or 'pcb'")
            await asyncio.to_thread(conn.launch_editor, doc_type)
            return CallToolResult(
                content=[TextContent(type="text", text=f"{doc_type} editor is open.")]
            )

        # save_document saves the schematic/PCB to disk
        if name == "save_document":
            doc_type = arguments.get("doc_type", "all")
            if doc_type not in ("schematic", "pcb", "all"):
                raise ValueError("doc_type must be 'schematic', 'pcb', or 'all'")
            result = await asyncio.to_thread(_save_document, conn.kicad, doc_type)
            is_error = result.get("status") == "error"
            return CallToolResult(
                content=[TextContent(type="text", text=json.dumps(result))],
                isError=is_error,
            )

        # screenshot goes through C++ handler which returns JSON with image data
        if name == "screenshot":
            result_str = await asyncio.to_thread(conn.execute_tool, name, arguments)
            result_json = json.loads(result_str)
            img = result_json.get("image", {})
            b64_data = img.get("base64", "")
            if b64_data:
                content = [ImageContent(type="image", data=b64_data, mimeType=img.get("media_type", "image/png"))]
                text = result_json.get("text", "")
                if text:
                    content.insert(0, TextContent(type="text", text=text))
                return CallToolResult(content=content)
            else:
                return CallToolResult(
                    content=[TextContent(type="text", text=result_str)],
                    isError="error" in result_str.lower(),
                )

        # Component search tools go directly to pcbparts.dev
        if name in _COMPONENT_SEARCH_TOOLS:
            result = await asyncio.to_thread(
                _call_component_search, name, arguments,
            )
            return CallToolResult(
                content=[TextContent(type="text", text=result)]
            )

        # All other tools go through the C++ executor
        if not conn.has_tool(name):
            raise ValueError(f"Unknown tool: {name}")

        result = await asyncio.to_thread(conn.execute_tool, name, arguments)
        return CallToolResult(
            content=[TextContent(type="text", text=result)]
        )

    return server


async def run() -> None:
    """Main entry point — connect to Zeo and start the MCP server."""
    logging.basicConfig(
        level=logging.INFO,
        handlers=[
            logging.StreamHandler(sys.stderr),
            logging.FileHandler(os.path.join(os.path.expanduser("~"), "zeo_mcp.log")),
        ],
    )

    logger.info("Connecting to Zeo...")
    kicad = None

    try:
        kicad = KiCad(timeout_ms=5000)
        version = kicad.get_version()
        logger.info("Connected to Zeo (KiCad %s)", version)
    except Exception as e:
        logger.error("Could not connect to Zeo: %s\nMake sure Zeo is running.", e)
        sys.exit(1)

    # Fetch tool schemas from C++ app
    cpp_tool_apps: dict[str, str] = {}
    cpp_tool_names: set[str] = set()

    try:
        cpp_schemas = kicad.get_tool_schemas()
        logger.info("Fetched %d tool schemas from C++ app", len(cpp_schemas))
    except Exception as e:
        logger.error("Failed to fetch tool schemas from C++ app: %s", e)
        sys.exit(1)

    # Build MCP tool list and app lookup from C++ manifest
    tool_list = []
    for t in cpp_schemas:
        tool_list.append(
            Tool(
                name=t["name"],
                description=t["description"],
                inputSchema=t["input_schema"],
            )
        )
        cpp_tool_names.add(t["name"])
        if t.get("app"):
            cpp_tool_apps[t["name"]] = t["app"]

    # Fetch component search tools from pcbparts.dev
    cs_tools = _fetch_component_search_tools()
    if cs_tools:
        tool_list.extend(cs_tools)
        logger.info("Added %d component search tools from pcbparts.dev", len(cs_tools))

    # MCP-only tools (not in C++ manifest)
    tool_list.append(Tool(
        name="save_document",
        description=(
            "Save the current schematic and/or PCB to disk. Use this to commit your "
            "changes after making edits. This writes the in-memory state to the "
            ".kicad_sch / .kicad_pcb files on disk."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "doc_type": {
                    "type": "string",
                    "enum": ["schematic", "pcb", "all"],
                    "description": (
                        "Which document to save: 'schematic', 'pcb', or 'all' "
                        "(saves both if open). Default: 'all'"
                    ),
                },
            },
            "required": [],
        },
    ))

    conn = ConnectionManager(kicad, tool_list, cpp_tool_apps, cpp_tool_names)
    server = _create_server(conn)

    logger.info("Starting MCP server with %d tools...", len(tool_list))

    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options(),
        )
