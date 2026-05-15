#!/bin/bash
set -e
APPS="bitmap2component eeschema gerbview kicad kicad-cli pcb_calculator pcbnew pl_editor"
LAUNCHER="launcher"

# Set LD_LIBRARY_PATH to use bundled libraries
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/x86_64-linux-gnu:$APPDIR/usr/local/lib:$APPDIR/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Set Python environment to use bundled Python
export PYTHONHOME="$APPDIR/usr"
if [ -f "$APPDIR/.env-python" ]; then
    . "$APPDIR/.env-python"
fi
export PYTHONPATH="$APPDIR/usr/lib/python3/dist-packages:$PYTHONPATH"

# Set KiCad resource paths to use bundled resources
export KICAD_STOCK_DATA_HOME="$APPDIR/usr/share/kicad"

# Set versioned library paths so KiCad finds symbols/footprints/3dmodels inside the AppImage.
# The compile-time KICAD_LIBRARY_DATA resolves to /usr/share/kicad which doesn't exist on
# the host. These env vars override the compiled-in paths at runtime.
export KICAD10_SYMBOL_DIR="$APPDIR/usr/share/kicad/symbols"
export KICAD10_FOOTPRINT_DIR="$APPDIR/usr/share/kicad/footprints"
export KICAD10_3DMODEL_DIR="$APPDIR/usr/share/kicad/3dmodels"
export KICAD10_TEMPLATE_DIR="$APPDIR/usr/share/kicad/template"

if [ $# -eq 0 ]; then
    exec "$APPDIR/usr/bin/Zeo" "$@"
else
    # Check for help argument
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        echo "Usage: $0 [COMMAND] [ARGS...]"
        echo ""
        echo "Available commands:"
        for APP in $APPS; do
            echo "  $APP"
        done
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

    # If the first argument is a recognized app, run it with the launcher
    for APP in $APPS; do
        if [ "$1" = "$APP" ]; then
            exec "$APPDIR/usr/bin/$APP" "${@:2}"
        fi
    done

    # If the first argument is not recognized, pass all arguments to the launcher
    exec "$APPDIR/usr/bin/Zeo" "$@"
fi
