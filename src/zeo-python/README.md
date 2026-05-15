# Zeo Python

`zeo-python` (package name `kipy`) provides Python bindings for the [Zeo](https://zeo.dev) IPC API. This library makes it possible to develop scripts and tools that interact with a running Zeo session.

Zeo is a fork of [KiCad](https://www.kicad.org/) with an integrated AI agent for schematic and PCB design. The IPC API enables programmatic control of Zeo's editors — the AI agent uses it internally, and you can use it to build your own automation.

## Requirements

- **Zeo 9.0+** (or KiCad 9.0+) running with the API server enabled in Preferences > Plugins
- **Python 3.10+**
- `protobuf` and `pynng` packages (installed automatically)

> Note: The IPC API requires communication with a running instance of Zeo. It is not possible to use `kipy` to manipulate design files without Zeo running.

## Installation

Install from source:

```sh
cd zeo-python
pip install -e .
```

Verify:

```sh
python3 -c "import kipy; print('kipy installed successfully')"
```

## Getting Started

Launch Zeo, make sure the API server is enabled in Preferences > Plugins, and then:

```sh
python3 ./examples/hello.py
```

This should print out the version of Zeo you have connected to.

## Building from Source

For instructions on building and generating protobuf bindings, see `COMPILING.md`.

This library builds against the API definitions (`.proto` files) in the main Zeo repository. After updating protos, regenerate the Python bindings:

```sh
python3 tools/generate_protos.py
```

You can use the method `KiCad.check_version` to make sure you are using a compatible version of `kipy` for your installed version of Zeo.

## Documentation

Documentation is generated from the `docs` directory and the docstrings in the source code.

## MCP Server

`kipy` includes an MCP server that exposes Zeo's tools to Claude Code and other MCP-compatible clients. This is how external AI agents can control Zeo programmatically — schematic editing, PCB layout, screenshots, and more.

Add it to Claude Code:

```sh
claude mcp add zeo -- python3 -m kipy.mcp
```

## Examples

Check out the `examples/` directory for scripts that may serve as a starting point. Some are snippets that can be run directly from a terminal, and some are action plugins that can be loaded into the PCB editor.

## Contributing

Issues and pull requests are welcome on [GitHub](https://github.com/zeodotdev/zeo-python).