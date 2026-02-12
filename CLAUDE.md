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
│   ├── chat_controller.cpp  # Chat state machine
│   └── agent_tools.cpp      # Tool definitions & execution
├── auth/               # Supabase auth + keychain storage
├── bridge/             # JS↔C++ webview message router
└── view/               # Markdown→HTML, templates, file attachments

pcbnew/                 # PCB editor
eeschema/               # Schematic editor
kicad/                  # Project manager
common/                 # Shared libraries
```

## Build

CMake 3.22+. Targets: `agent` (standalone), `agent_kiface` (KiCad plugin).

### Validating Changes

After making code changes, run the fast build script to compile and launch:

```bash
cd /Users/jared/Documents/kicadpp/dev
./mac_build_fast.sh
```

This builds the agent target in seconds. Do NOT pass `--launch` — the user will launch manually. Use `--debug` to enable agent debug tracing (WXTRACE=KICAD_AGENT). Use `--all` when changes touch eeschema/pcbnew/common. Use `--install` to deploy to kicad-dest (the app the user runs).

## Lint / LSP Errors

Ignore clang LSP diagnostic errors. The LSP cannot resolve CMake include paths, so it reports false positives (e.g. `'nlohmann/json.hpp' file not found`). If the build script succeeds, the code is correct.

## Debugging

Logs: `~/Library/Logs/Zeo/agent-<timestamp>.log` (one per session)

For terminal output: `WXTRACE=1 ./Zeo`
