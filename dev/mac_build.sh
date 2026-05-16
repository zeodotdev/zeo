#!/bin/bash
# mac_build.sh — unified Zeo macOS build script.
#
# Modes:
#   (default)        Full hard build + install (one-time setup / clean rebuild)
#   --fast           Incremental build (requires a prior hard build)
#   --package        Also create a distributable .dmg after the build
#   --skip-build     Skip build phase entirely (use with --package for DMG-only)
#
# Flags (cross-platform, mirrored in appimage_build.sh / win_build.ps1):
#   --release NAME   Release name (e.g. --release 1.0 → zeo-1.0.dmg)
#   --launch         Launch Zeo.app after build
#   --quit           Kill any running Zeo before the build
#   --target NAME    Build a specific make target (implies --fast)
#   --agent          Shortcut for --target agent --fast
#   --python         Rebuild kipy (Python bindings)
#   --install        Run make install to populate kicad-dest (fast mode)
#   --debug          Launch with WXTRACE=KICAD_AGENT (implies --launch)
#   --lldb           Launch under lldb (implies --launch)
#   --force          Clear cmake cache, full reconfigure
#   --verbose        Show build output in terminal (hard mode)
#   --sign IDENTITY  Code signing identity (--package)
#   --notarize       Apple notarization (--package, requires --sign)
#   --help           Show this help
#
# Internal implementation lives in dev/utils/mac_{hard,fast,dmg}.sh and is
# not intended to be invoked directly.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INTERNAL="$SCRIPT_DIR/utils"

# Defaults
MODE=hard          # hard | fast | skip
PACKAGE=false
RELEASE=""
LAUNCH=false
QUIT=false
TARGET=""
AGENT=false
PYTHON=false
INSTALL=false
DEBUG=false
LLDB=false
FORCE=false
VERBOSE=false
SIGN=""
NOTARIZE=false

print_usage() {
    # Print the header comment block (lines 2 through first non-comment line)
    awk '/^[^#]/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --fast)        MODE=fast;          shift ;;
        --skip-build)  MODE=skip;          shift ;;
        --package)     PACKAGE=true;       shift ;;
        --release)     RELEASE="$2";       shift 2 ;;
        --launch)      LAUNCH=true;        shift ;;
        --quit)        QUIT=true;          shift ;;
        --target)      TARGET="$2"; MODE=fast; shift 2 ;;
        --agent)       AGENT=true; MODE=fast; shift ;;
        --python)      PYTHON=true;        shift ;;
        --install)     INSTALL=true;       shift ;;
        --debug)       DEBUG=true; LAUNCH=true; shift ;;
        --lldb)        LLDB=true; LAUNCH=true; shift ;;
        --force)       FORCE=true;         shift ;;
        --verbose)     VERBOSE=true;       shift ;;
        --sign)        SIGN="$2";          shift 2 ;;
        --notarize)    NOTARIZE=true;      shift ;;
        --help|-h)     print_usage; exit 0 ;;
        *)             echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
done

# --- Build phase ---

run_hard() {
    local args=()
    $FORCE   && args+=(--force)
    $LAUNCH  && args+=(--launch)
    $DEBUG   && args+=(--debug)
    $LLDB    && args+=(--lldb)
    $VERBOSE && args+=(--verbose)
    # Release tag is only meaningful via --package's path; in hard mode itself
    # we still pass --release to enable production URLs / clean version string.
    [ -n "$RELEASE" ] && args+=(--release)
    "$INTERNAL/mac_hard.sh" "${args[@]}"
}

run_fast() {
    local args=()
    $AGENT   && args+=(--agent)
    [ -n "$TARGET" ] && args+=(--target "$TARGET")
    $PYTHON  && args+=(--python)
    $INSTALL && args+=(--install)
    $QUIT    && args+=(--quit)
    $LAUNCH  && args+=(--launch)
    $DEBUG   && args+=(--debug)
    $LLDB    && args+=(--lldb)
    "$INTERNAL/mac_fast.sh" "${args[@]}"
}

run_package() {
    local args=(--skip-build)
    [ -n "$RELEASE" ] && args+=(--release "$RELEASE")
    [ -n "$SIGN" ]    && args+=(--sign "$SIGN")
    $NOTARIZE && args+=(--notarize)
    "$INTERNAL/mac_dmg.sh" "${args[@]}"
}

case $MODE in
    hard) run_hard ;;
    fast) run_fast ;;
    skip)
        if ! $PACKAGE; then
            echo "Error: --skip-build only makes sense with --package."
            exit 1
        fi
        ;;
esac

if $PACKAGE; then
    run_package
fi
