# Zeo

Zeo is a fork of [KiCad](https://www.kicad.org/) with an integrated AI agent sidebar for schematic and PCB design.

## What's New

- **Agent Sidebar** (`agent/`) - AI assistant that can read, create, and modify schematics and PCBs through 40+ tool handlers
- **Version Control** (`vcs/`) - Git-based version control integration for project history
- **Terminal** (`terminal/`) - Integrated terminal for development and agent use

## Building

See the [KiCad Developer Documentation](https://dev-docs.kicad.org/en/build/) for base build instructions.

### Zeo Python (Required for Agent)

The AI agent requires the `kipy` Python package to communicate with KiCad via IPC. After building Zeo, install the Python bindings:

```sh
cd ../zeo-python
pip install -e .
```

This installs the `kipy` package in editable mode. The agent will use this to execute tool commands against the running KiCad instance.

**Requirements:**
- Python 3.9+
- protobuf and pynng packages (installed automatically with pip)

**Verify installation:**
```sh
python3 -c "import kipy; print('kipy installed successfully')"
```

## MCP Server (Claude Code Integration)

Zeo includes an MCP (Model Context Protocol) server that gives [Claude Code](https://docs.anthropic.com/en/docs/claude-code/overview) direct access to the schematic and PCB editors — the same 46 tools available in Zeo's built-in agent sidebar.

### Setup

**macOS:**

```bash
claude mcp add zeo \
  -s user \
  -- /Applications/Zeo.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
  -m kipy.mcp
```

**Windows:**

```bash
claude mcp add zeo -s user -- "C:\Program Files\Zeo\0.1\bin\zeo" mcp
```

**Linux:**

```bash
claude mcp add zeo \
  -s user \
  -- python3 -m kipy.mcp
```

This adds the server at user scope (`-s user`), so it's available in all projects.

### Usage

Make sure Zeo is running with a project open, then use Claude Code as normal. It will call `check_status` to verify the editor state and use tools like `sch_add`, `pcb_place`, `pcb_autoroute`, `screenshot`, and more.

For full documentation, see [docs.zeocad.com](https://docs.zeocad.com/docs/features/agent/integrations/claude-code).

## License

Zeo is licensed under the [GNU General Public License v3.0](LICENSE) (or later), the same license as KiCad. See [LICENSE.README](LICENSE.README) for details on third-party components.

## Upstream KiCad

This project is based on KiCad, a free and open-source EDA suite. For more about the upstream project:

- [KiCad Website](https://www.kicad.org/)
- [KiCad GitLab](https://gitlab.com/kicad/code/kicad)
- [KiCad Forum](https://forum.kicad.info/)
