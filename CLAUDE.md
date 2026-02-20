# Zeo

AI Agent extension for KiCad (open-source EDA software).

## Tech Stack

C++17, wxWidgets, libcurl, nlohmann/json

## Project Structure

```
agent/                  # AI Agent UI and LLM integration
├── agent.cpp           # Entry point (KIFACE factory)
├── agent_frame.cpp     # Main window, chat UI, event handling
├── agent_llm_client.cpp # LLM API client (OpenAI/Anthropic)
├── chat_history.cpp    # JSON persistence
├── core/
│   └── chat_controller.cpp  # Chat state machine
├── tools/
│   ├── tool_schemas.cpp         # Tool definitions (JSON schemas sent to LLM)
│   ├── tool_registry.cpp        # Singleton dispatcher (tool name → handler map)
│   ├── tool_handler.h           # Base interface for all tool handlers
│   ├── handlers/                # Tool handler implementations
│   │   ├── python_tool_handler.cpp  # Loads .py scripts, builds IPC commands (30 tools)
│   │   ├── pcb_autoroute_handler.cpp # Async Freerouting integration
│   │   └── screenshot_handler.cpp   # CLI screenshot capture
│   ├── python/                  # Python scripts (loaded at runtime)
│   │   ├── common/              # Shared utilities (preamble.py, bbox.py)
│   │   ├── schematic/           # sch_*.py (add, update, delete, connect_net, etc.)
│   │   └── pcb/                 # pcb_*.py (add, route, place, export, etc.)
│   └── util/                    # C++ helpers (file_writer, sch_parser, sexpr_util)
├── auth/               # Supabase auth + keychain storage
├── bridge/             # JS↔C++ webview message router
└── view/               # Markdown→HTML, templates, file attachments

api/proto/              # Protobuf definitions for IPC API
├── schematic/          # Schematic commands
└── common/             # Shared types

eeschema/api/           # Schematic editor API handlers
├── api_handler_sch.cpp # IPC command implementations
└── api_handler_sch.h

pcbnew/                 # PCB editor
eeschema/               # Schematic editor
kicad/                  # Project manager
common/                 # Shared libraries
```

## Agent Tools Architecture

Tools follow a layered architecture:
1. **Tool Schema** (`tool_schemas.cpp`) - JSON definitions sent to LLM
2. **Tool Registry** (`tool_registry.cpp`) - Dispatches tool calls to handlers via name → handler map
3. **Tool Handler** (`python_tool_handler.cpp`) - Loads `.py` scripts and builds IPC commands
4. **IPC API** (`api_handler_sch.cpp`) - Handles protobuf commands
5. **Kipy** (`zeo-python/kipy/`) - Python bindings for IPC

For detailed process on adding/updating tools, see `/zeo-python/CLAUDE.md`

## Lint / LSP Errors

Ignore clang LSP diagnostic errors. The LSP cannot resolve CMake include paths, so it reports false positives (e.g. `'nlohmann/json.hpp' file not found`). If the build script succeeds, the code is correct.

## Debugging

Logs: `~/Library/Logs/Zeo/agent-<timestamp>.log` (one per session)

For terminal output: `WXTRACE=1 ./Zeo`
