#!/bin/bash
# appimage_build.sh — unified Zeo Linux build script (Docker-based).
#
# Modes:
#   (default)        Build only inside Docker (no launch, no AppImage)
#   --fast           Alias for default; explicit "dev iteration" intent
#   --launch         Build inside Docker and launch the app
#   --package        Build + create a distributable AppImage
#   --skip-build     Skip build phase (use with --package for AppImage-only)
#
# Shared flags (mirrored from mac_build.sh):
#   --release NAME   Release name (e.g. --release 1.0 → zeo-1.0.AppImage)
#   --quit           Kill any running Zeo before the build
#   --target NAME    Build a specific ninja target (dev mode)
#   --agent          Shortcut for --target agent
#   --python         Rebuild kipy (Python bindings)
#   --debug          Launch with WXTRACE=KICAD_AGENT (implies --launch)
#   --gdb            Launch under gdb (Linux-specific)
#   --lldb           Launch under lldb
#   --force          (--package) force-rebuild all dependencies
#   --help           Show this help
#
# Linux-specific flags:
#   --setup          Force rebuild of the dev Docker image (dev mode)
#   --reconfigure    Force cmake reconfigure on next build (dev mode)
#   --clean          Wipe persistent build directory (dev mode)
#   --light          Build AppImage without 3D packages (--package)
#   --config FILE    Custom AppImage config (--package, default: configs/zeo.json)
#   --build-deps     Build dependency Docker images locally (wx, wxpython)
#
# Internal implementation lives in dev/utils/linux_{fast,appimage}.sh and is
# not intended to be invoked directly.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INTERNAL="$SCRIPT_DIR/utils"

# Defaults
MODE=fast          # fast | skip
PACKAGE=false
RELEASE=""
LAUNCH=false
QUIT=false
TARGET=""
AGENT=false
PYTHON=false
DEBUG=false
GDB=false
LLDB=false
FORCE=false
SETUP=false
RECONFIGURE=false
CLEAN=false
LIGHT=false
CONFIG=""
BUILD_DEPS=false

print_usage() {
    awk '/^[^#]/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --fast)         MODE=fast;          shift ;;
        --skip-build)   MODE=skip;          shift ;;
        --package)      PACKAGE=true;       shift ;;
        --release)      RELEASE="$2";       shift 2 ;;
        --launch)       LAUNCH=true;        shift ;;
        --quit)         QUIT=true;          shift ;;
        --target)       TARGET="$2";        shift 2 ;;
        --agent)        AGENT=true;         shift ;;
        --python)       PYTHON=true;        shift ;;
        --debug)        DEBUG=true; LAUNCH=true; shift ;;
        --gdb)          GDB=true; LAUNCH=true; shift ;;
        --lldb)         LLDB=true; LAUNCH=true; shift ;;
        --force)        FORCE=true;         shift ;;
        --setup)        SETUP=true;         shift ;;
        --reconfigure)  RECONFIGURE=true;   shift ;;
        --clean)        CLEAN=true;         shift ;;
        --light)        LIGHT=true;         shift ;;
        --config)       CONFIG="$2";        shift 2 ;;
        --build-deps)   BUILD_DEPS=true;    shift ;;
        --help|-h)      print_usage; exit 0 ;;
        *)              echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
done

# --- Run ---

run_fast() {
    local args=()
    $LAUNCH || args+=(--build-only)
    $SETUP        && args+=(--setup)
    $AGENT        && args+=(--agent)
    [ -n "$TARGET" ] && args+=(--target "$TARGET")
    $PYTHON       && args+=(--python)
    $QUIT         && args+=(--quit)
    $DEBUG        && args+=(--debug)
    $GDB          && args+=(--gdb)
    $LLDB         && args+=(--lldb)
    $RECONFIGURE  && args+=(--reconfigure)
    $CLEAN        && args+=(--clean)
    "$INTERNAL/linux_fast.sh" "${args[@]}"
}

run_package() {
    local args=()
    [ "$MODE" = skip ] && args+=(--skip-build)
    [ -n "$RELEASE" ]  && args+=(--release "$RELEASE")
    $LIGHT             && args+=(--light)
    [ -n "$CONFIG" ]   && args+=(--config "$CONFIG")
    $FORCE             && args+=(--force)
    $BUILD_DEPS        && args+=(--build-deps)
    "$INTERNAL/linux_appimage.sh" "${args[@]}"
}

if $PACKAGE; then
    run_package
elif [ "$MODE" = skip ]; then
    echo "Error: --skip-build only makes sense with --package."
    exit 1
else
    run_fast
fi
