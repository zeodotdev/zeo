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
├── ui/                 # History panel, change approval dialogs
└── rendering/          # Markdown→HTML, templates

pcbnew/                 # PCB editor
eeschema/               # Schematic editor
kicad/                  # Project manager
common/                 # Shared libraries
```

## Build

CMake 3.22+. Targets: `agent` (standalone), `agent_kiface` (KiCad plugin).
