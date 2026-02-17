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
│   ├── agent_tools.cpp      # Tool definitions (JSON schemas for LLM)
│   ├── tool_registry.cpp    # Handler registration
│   └── schematic/           # Schematic tool handlers
│       ├── sch_setup_handler.cpp   # sch_setup tool
│       ├── sch_crud_handler.cpp    # CRUD operations
│       └── ...
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
1. **Tool Definition** (`agent_tools.cpp`) - JSON schema for LLM
2. **Tool Handler** (`sch_*_handler.cpp`) - Generates Python code
3. **IPC API** (`api_handler_sch.cpp`) - Handles protobuf commands
4. **Kipy** (`zeo-python/kipy/`) - Python bindings for IPC

For detailed process on adding/updating tools, see `/zeo-python/CLAUDE.md`

## Lint / LSP Errors

Ignore clang LSP diagnostic errors. The LSP cannot resolve CMake include paths, so it reports false positives (e.g. `'nlohmann/json.hpp' file not found`). If the build script succeeds, the code is correct.

## Debugging

Logs: `~/Library/Logs/Zeo/agent-<timestamp>.log` (one per session)

For terminal output: `WXTRACE=1 ./Zeo`
