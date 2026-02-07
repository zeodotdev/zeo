# Zener

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
./mac_build_fast.sh --launch
```

This builds the agent target in seconds and launches the in-tree Zener.app. Use `--debug` instead of `--launch` to enable agent debug tracing (WXTRACE=KICAD_AGENT).

## Debugging

Logs: `~/Library/Logs/Zener/agent-<timestamp>.log` (one per session)

For terminal output: `WXTRACE=1 ./Zener`
