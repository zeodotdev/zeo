#!/bin/bash
# Zeo AppImage wrapper script
# Launches Zeo (KiCad Agentic Edition) with Agent and Terminal support
set -e

# Standard KiCad apps plus Zeo-specific additions
APPS="bitmap2component eeschema gerbview kicad kicad-cli pcb_calculator pcbnew pl_editor agent terminal"
LAUNCHER="launcher"

# Set LD_LIBRARY_PATH to use bundled libraries
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/x86_64-linux-gnu:$APPDIR/usr/local/lib:$APPDIR/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Set Python environment to use bundled Python
export PYTHONHOME="$APPDIR/usr"
if [ -f "$APPDIR/.env-python" ]; then
    . "$APPDIR/.env-python"
fi
export PYTHONPATH="$APPDIR/usr/lib/python3/dist-packages:$APPDIR/usr/local/lib/python3/dist-packages:$PYTHONPATH"

# Set KiCad resource paths to use bundled resources
export KICAD_STOCK_DATA_HOME="$APPDIR/usr/share/kicad"

# Set versioned library paths so KiCad finds symbols/footprints/3dmodels inside the AppImage.
# The compile-time KICAD_LIBRARY_DATA resolves to /usr/share/kicad which doesn't exist on
# the host. These env vars override the compiled-in paths at runtime.
export KICAD10_SYMBOL_DIR="$APPDIR/usr/share/kicad/symbols"
export KICAD10_FOOTPRINT_DIR="$APPDIR/usr/share/kicad/footprints"
export KICAD10_3DMODEL_DIR="$APPDIR/usr/share/kicad/3dmodels"
export KICAD10_TEMPLATE_DIR="$APPDIR/usr/share/kicad/template"

# Freerouting path (Java autorouter)
if [ -f "$APPDIR/usr/share/kicad/freerouting/freerouting.jar" ]; then
    export KICAD_FREEROUTING_JAR="$APPDIR/usr/share/kicad/freerouting/freerouting.jar"
fi

# Main executable (renamed to Zeo)
ZEO_BIN="$APPDIR/usr/bin/Zeo"
KICAD_BIN="$APPDIR/usr/bin/Zeo"

# Determine which main binary to use
if [ -x "$ZEO_BIN" ]; then
    MAIN_BIN="$ZEO_BIN"
elif [ -x "$KICAD_BIN" ]; then
    MAIN_BIN="$KICAD_BIN"
else
    echo "Error: Neither zeo nor kicad binary found in $APPDIR/usr/bin" >&2
    exit 1
fi

if [ $# -eq 0 ]; then
    exec "$MAIN_BIN" "$@"
else
    # Check for help argument
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        echo "Zeo - KiCad Agentic Edition"
        echo ""
        echo "Usage: $0 [COMMAND] [ARGS...]"
        echo ""
        echo "Available commands:"
        for APP in $APPS; do
            echo "  $APP"
        done
        echo ""
        echo "Zeo-specific commands:"
        echo "  agent       Launch the AI-powered design assistant"
        echo "  terminal    Launch the Python scripting terminal"
        echo ""
        echo "AppImage built-in arguments:"
        echo "  --appimage-extract          Extract the contents of the AppImage"
        echo "  --appimage-extract-and-run  Temporarily extract content from embedded"
        echo "                              filesystem and run main binary then clean up"
        echo "  --appimage-help             Display this help message"
        echo "  --appimage-mount            Mount the AppImage and print the mount point"
        echo "  --appimage-offset           Print the offset of the AppImage"
        echo "  --appimage-version          Print the version of the AppImage runtime"
        exit 0
    fi

    # Handle agent and terminal specially (they may be in different locations)
    case "$1" in
        agent)
            shift
            if [ -x "$APPDIR/usr/bin/agent" ]; then
                exec "$APPDIR/usr/bin/agent" "$@"
            elif [ -f "$APPDIR/usr/lib/kicad/plugins/_agent.kiface" ]; then
                # Load agent kiface through main kicad binary
                exec "$MAIN_BIN" --agent "$@"
            else
                echo "Error: Agent not found in this build" >&2
                exit 1
            fi
            ;;
        terminal)
            shift
            if [ -x "$APPDIR/usr/bin/terminal" ]; then
                exec "$APPDIR/usr/bin/terminal" "$@"
            elif [ -f "$APPDIR/usr/lib/kicad/plugins/_terminal.kiface" ]; then
                # Load terminal kiface through main kicad binary
                exec "$MAIN_BIN" --terminal "$@"
            else
                echo "Error: Terminal not found in this build" >&2
                exit 1
            fi
            ;;
    esac

    # If the first argument is a recognized app, run it
    for APP in $APPS; do
        if [ "$1" = "$APP" ]; then
            exec "$APPDIR/usr/bin/$APP" "${@:2}"
        fi
    done

    # If the first argument is not recognized, pass all arguments to the launcher
    exec "$MAIN_BIN" "$@"
fi
