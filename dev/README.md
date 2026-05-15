# Zeo Development Setup

## Repository Structure

Ensure that the repos are in the correct positions relative to one another:

```
zeodotdev/
├── code/
│   ├── zeo/            # Main zeo application
│   └── zeo-python/     # Python API library (kipy)
├── dev/                # Development scripts and docs
├── libraries/
│   ├── kicad-footprint-generator
│   ├── kicad-footprints
│   ├── kicad-library-utils
│   ├── kicad-packages3D
│   ├── kicad-packages3D-source
│   ├── kicad-symbols
│   └── kicad-templates
└── packaging/
    ├── kicad-mac-builder (on gitlab)
    ├── kicad-appimage (on gitlab)
    └── other packaging...
```

## Setting Up API Keys

The KiCad Agent requires API keys to connect to LLM services.

### Quick Setup

```bash
cd code/kicad-agent/agent
cp .env.example .env
# Edit .env and add your API keys
```

### Required Keys

| Key | Required | Description |
|-----|----------|-------------|
| `ANTHROPIC_API_KEY` | Yes | For Claude support (default model) |
| `OPENAI_API_KEY` | No | For GPT-4 support (optional) |

Get your Anthropic API key at: <https://console.anthropic.com/>

## Building

Build scripts are located in this directory (`/dev`).

### macOS Build Scripts

| Script | Description |
|--------|-------------|
| `./mac_build_incr.sh` | **Recommended** - Incremental build, only rebuilds changed components |
| `./mac_build_hard.sh` | Full rebuild of all components (slower, use when needed) |

### Linux Build Script

| Script | Description |
|--------|-------------|
| `./linux_build_fast.sh` | **Recommended** - Auto-setup, build, and launch in Docker |

```bash
cd dev
./linux_build_fast.sh              # Build + launch (auto-builds Docker image on first run)
./linux_build_fast.sh --build-only # Build without launching
./linux_build_fast.sh --setup      # Force rebuild of the dev Docker image
./linux_build_fast.sh --python     # Force reinstall of kipy (kicad-python)
./linux_build_fast.sh --gdb        # Build + launch under gdb
./linux_build_fast.sh --help       # Show all options
```

Requires Docker. The dev Docker image is automatically built on first run and
rebuilt when the inline Dockerfile changes. kipy (kicad-python) packages are
installed to a persistent host-mounted directory so they survive container restarts.

### Quick Build

```bash
cd dev
./mac_build_incr.sh
```

### Incremental Build Options

```bash
./mac_build_incr.sh --help        # Show all options
./mac_build_incr.sh --force       # Force rebuild even if no changes
./mac_build_incr.sh --skip-libs   # Skip library installation
```
