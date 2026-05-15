# Zeo Python (kipy)

Python bindings for KiCad's IPC API, used by the Zeo AI Agent.

## Tech Stack

Python 3.9+, protobuf, grpcio

## Project Structure

```
kipy/
├── proto/                  # Generated protobuf Python classes
│   ├── common/             # Shared types (Vector2, KIID, etc.)
│   ├── schematic/          # Schematic commands & types
│   └── board/              # PCB commands & types
├── schematic/              # Schematic operations API
│   ├── page.py             # Page, title block, grid, formatting settings
│   ├── symbols.py          # Symbol placement & manipulation
│   ├── wiring.py           # Wire creation & routing
│   ├── erc.py              # ERC settings & validation
│   └── ...
├── board/                  # PCB operations API
└── common_types.py         # Wrapper classes (PageInfo, TitleBlockInfo, etc.)

tools/
└── generate_protos.py      # Regenerate Python from .proto files
```

## Adding/Updating IPC Features (e.g., sch_setup)

When adding new settings or features to tools like `sch_setup`, follow this process:

### 1. Update Protobuf Definitions

Edit the proto file in the main Zeo repo:
```
/zeo/api/proto/schematic/schematic_commands.proto
```

Add new message types or fields. Example for FormattingSettings:
```protobuf
message FormattingSettings
{
  optional int32 default_text_size_mils = 1;
  optional double label_offset_ratio = 3;
  // ... more fields
}

message GetFormattingSettings { ... }
message GetFormattingSettingsResponse { ... }
message SetFormattingSettings { ... }
```

### 2. Implement API Handlers in Eeschema

Edit in `/zeo/eeschema/api/`:
- `api_handler_sch.h` - Add handler declarations and forward declarations
- `api_handler_sch.cpp` - Implement handlers, register them in constructor

The handlers read/write from `SCHEMATIC_SETTINGS` (project-level) or `EESCHEMA_SETTINGS` (app-level).

### 3. Regenerate Python Protobuf Bindings

```bash
cd /zeo-python

# Copy updated proto file
cp /zeo/api/proto/schematic/schematic_commands.proto kipy/proto/schematic/

# Regenerate Python classes
protoc --python_out=kipy/proto \
  --proto_path=/zeo/api/proto \
  /zeo/api/proto/schematic/schematic_commands.proto

# Fix imports with protoletariat
protol --dont-create-package --in-place --exclude-google-imports \
  --python-out kipy/proto \
  protoc --proto-path /zeo/api/proto \
  /zeo/api/proto/schematic/schematic_commands.proto
```

### 4. Add Kipy Wrapper Methods

Edit the relevant module in `/zeo-python/kipy/schematic/` (e.g., `page.py`):

```python
def get_formatting_settings(self) -> dict:
    """Get schematic formatting settings."""
    cmd = schematic_commands_pb2.GetFormattingSettings()
    cmd.document.CopyFrom(self._doc)
    response = self._kicad.send(cmd, schematic_commands_pb2.GetFormattingSettingsResponse)
    # Convert response to dict
    return { ... }

def set_formatting_settings(self, **kwargs) -> None:
    """Set schematic formatting settings."""
    cmd = schematic_commands_pb2.SetFormattingSettings()
    cmd.document.CopyFrom(self._doc)
    # Set fields from kwargs
    self._kicad.send(cmd, Empty)
```

### 5. Create/Update Agent Tool Handler

In `/zeo/agent/tools/schematic/`:
- Create `sch_setup_handler.h` and `sch_setup_handler.cpp`
- Handler generates Python code that calls kipy methods
- Register in `tool_registry.cpp`
- Add source to `CMakeLists.txt`

### 6. Define Tool Schema in agent_tools.cpp

Add tool definition in `/zeo/agent/tools/agent_tools.cpp`:
```cpp
LLM_TOOL schSetup;
schSetup.name = "sch_setup";
schSetup.description = "...";
schSetup.input_schema = { /* JSON schema */ };
tools.push_back(schSetup);
```

### 7. Build and Test

```bash
cd /zeodotdev/dev
./mac_build_fast.sh --install
```

Test by:
1. Opening Zeo and a schematic
2. Asking the agent to read/modify settings
3. Verifying changes in Schematic Setup dialog

## Key Files Reference

| Component | Location |
|-----------|----------|
| Proto definitions | `/zeo/api/proto/schematic/schematic_commands.proto` |
| API handler | `/zeo/eeschema/api/api_handler_sch.cpp` |
| Settings storage | `/zeo/eeschema/schematic_settings.h` |
| Kipy page module | `/zeo-python/kipy/schematic/page.py` |
| Agent tool handler | `/zeo/agent/tools/schematic/sch_setup_handler.cpp` |
| Tool definitions | `/zeo/agent/tools/agent_tools.cpp` |
| Tool registry | `/zeo/agent/tools/tool_registry.cpp` |

## MCP Server (`kipy.mcp`)

The MCP server exposes KiCad tools to Claude Code and other MCP clients.

- **Tool manifest**: `code/zeo/agent/tools/python/tool_manifest.json` — source of truth for all MCP tool schemas
- **Three tool categories**:
  - Python-scripted (app: `"sch"` / `"pcb"`) — run via IPC against the editor
  - Agent C++ handlers (app: `"agent"`) — route via `MAIL_MCP_EXECUTE_AGENT_TOOL` → `FRAME_AGENT` → `TOOL_REGISTRY::Execute()`
  - Direct (no app) — `check_status`, `launch_editor`, `screenshot`
- **Tool scripts**: `code/zeo/agent/tools/python/schematic/` and `pcb/`
- **Preamble** (`preamble.py`): shared constants (DEFAULT_FOOTPRINTS etc.), prepended to all tool scripts at runtime
- `launch_editor` is synchronous — blocks until editor is open
- After changing kipy Python code, must `/mcp restart` in Claude Code (C++ changes only need a rebuild)

## Common Patterns

### Converting Units
- Settings stored in IU (internal units) internally
- Proto uses mils for user-facing values
- Use `schIUScale.MilsToIU()` and divide by `schIUScale.MilsToIU(1)` for conversion

### Optional Fields in Proto
Use `has_field_name()` to check if optional field was set:
```cpp
if (settings.has_default_text_size_mils())
    schSettings.m_DefaultTextSize = schIUScale.MilsToIU(settings.default_text_size_mils());
```

### Wrapper Objects in Kipy
Some kipy methods return wrapper objects (PageInfo, TitleBlockInfo) that need attribute extraction for JSON serialization, while others return dicts directly.
