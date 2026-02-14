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

## Lint / LSP Errors

Ignore clang LSP diagnostic errors. The LSP cannot resolve CMake include paths, so it reports false positives (e.g. `'nlohmann/json.hpp' file not found`). If the build script succeeds, the code is correct.

## Debugging

Logs: `~/Library/Logs/Zeo/agent-<timestamp>.log` (one per session)

For terminal output: `WXTRACE=1 ./Zeo`
