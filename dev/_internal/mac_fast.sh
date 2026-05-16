#!/bin/bash
set -e
set -o pipefail

# Zeo macOS Fast Build Script (mac_build.sh --fast)
# Bypasses build.py and runs make directly in the inner build directory.
# Requires a prior full build (mac_build.sh) to set up cmake.
# Typical agent rebuild: 5-15s for one changed .cpp file.

# --- Configuration ---

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

KICAD_SOURCE_DIR="$WORKSPACE_DIR/src/zeo"
KICAD_PYTHON_DIR="$WORKSPACE_DIR/src/zeo-python"
BUILDER_DIR="$WORKSPACE_DIR/packaging/kicad-mac-builder"
LIBRARIES_DIR="$WORKSPACE_DIR/libraries"

INNER_BUILD_DIR="$BUILDER_DIR/build/kicad/src/kicad-build"
INTREE_APP="$INNER_BUILD_DIR/kicad/Zeo.app"
PYTHON_FW_SRC="$BUILDER_DIR/build/python-dest/Library/Frameworks/Python.framework"
INTREE_SITE_PKG="$INTREE_APP/Contents/Frameworks/Python.framework/Versions/3.10/lib/python3.10/site-packages"

NCPU=$(sysctl -n hw.ncpu)

# --- Argument Parsing ---

BUILD_TARGET=""
DO_INSTALL=false
DO_PYTHON=false
DO_QUIT=false
DO_LAUNCH=false
DO_DEBUG=false
DO_LLDB=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Fast incremental build - runs make directly in the inner build tree."
    echo "Requires a prior full build (mac_build.sh)."
    echo ""
    echo "Options:"
    echo "  (default)          Build all targets"
    echo "  --agent            Build agent target only"
    echo "  --target <name>    Build a specific make target"
    echo "  --python           Force reinstall kipy (auto-installed if missing)"
    echo "  --install          Run make install to populate kicad-dest"
    echo "  --quit             Quit running Zeo before build"
    echo "  --launch           Launch Zeo.app after build"
    echo "  --debug            Launch with WXTRACE=KICAD_AGENT (implies --launch)"
    echo "  --lldb             Launch under lldb debugger (implies --launch)"
    echo "  --help             Show this help message"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --agent)
            BUILD_TARGET="agent"
            shift
            ;;
        --target)
            BUILD_TARGET="$2"
            shift 2
            ;;
        --python)
            DO_PYTHON=true
            shift
            ;;
        --install)
            DO_INSTALL=true
            shift
            ;;
        --quit)
            DO_QUIT=true
            shift
            ;;
        --launch)
            DO_LAUNCH=true
            shift
            ;;
        --debug)
            DO_DEBUG=true
            DO_LAUNCH=true
            shift
            ;;
        --lldb)
            DO_LLDB=true
            DO_LAUNCH=true
            shift
            ;;
        --help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# --- Helper Functions ---

log() {
    echo "[FAST] $1"
}

quit_kicad() {
    if ! pgrep -x Zeo > /dev/null 2>&1; then
        return
    fi
    log "Closing any running Zeo instances..."
    osascript -e 'quit app "Zeo"' 2>/dev/null || true
    # Wait for the process to actually exit (up to 10s)
    for i in $(seq 1 20); do
        if ! pgrep -x Zeo > /dev/null 2>&1; then
            return
        fi
        sleep 0.5
    done
    # Still running — force kill
    log "Zeo didn't quit gracefully, force killing..."
    pkill -9 -x Zeo 2>/dev/null || true
    sleep 0.5
}

# --- Validate Prerequisites ---

validate_prerequisites() {
    if [ ! -d "$INNER_BUILD_DIR" ]; then
        echo "Error: Inner build directory not found at:"
        echo "  $INNER_BUILD_DIR"
        echo ""
        echo "Run mac_build.sh first to set up the cmake build tree."
        exit 1
    fi

    if [ ! -f "$INNER_BUILD_DIR/Makefile" ]; then
        echo "Error: No Makefile found in inner build directory."
        echo "Run mac_build.sh first to configure cmake."
        exit 1
    fi

    if [ ! -d "$INTREE_APP" ]; then
        echo "Error: In-tree Zeo.app not found at:"
        echo "  $INTREE_APP"
        echo ""
        echo "Run mac_build.sh first to create the initial build."
        exit 1
    fi
}

# --- Main ---

START_TIME=$(date +%s)

echo "=============================================="
echo "Zeo Fast Build"
echo "=============================================="
if [ -n "$BUILD_TARGET" ]; then
    echo "Target:   $BUILD_TARGET"
else
    echo "Target:   (all)"
fi
$DO_QUIT    && echo "Quit:     yes"
$DO_PYTHON  && echo "Python:   yes"
$DO_INSTALL && echo "Install:  yes"
$DO_LAUNCH  && echo "Launch:   yes"
$DO_DEBUG   && echo "Debug:    yes"
$DO_LLDB    && echo "LLDB:     yes"
echo "CPUs:     $NCPU"
echo "=============================================="

validate_prerequisites

# Set environment
export PATH="/opt/homebrew/opt/bison/bin:$PATH"
export WX_SKIP_DOXYGEN_VERSION_CHECK=1

# Quit running instance before build
if $DO_QUIT; then
    quit_kicad
fi

# --- Ensure cmake cache has NGSPICE_DLL ---
# Findngspice.cmake sets NGSPICE_DLL as a local variable (not cached), so it
# gets lost. Without it, NGSPICE_DLL_FILE compiles to "eeschema" instead of
# "libngspice.0.dylib", breaking ngspice loading at runtime.
NGSPICE_DLL_PATH="$BUILDER_DIR/build/ngspice-dest/lib/libngspice.0.dylib"
if [ -f "$NGSPICE_DLL_PATH" ] && ! grep -q "^NGSPICE_DLL:" "$INNER_BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    log "Adding NGSPICE_DLL to cmake cache..."
    echo "NGSPICE_DLL:FILEPATH=$NGSPICE_DLL_PATH" >> "$INNER_BUILD_DIR/CMakeCache.txt"
fi

# --- Compile ---

log "Building in $INNER_BUILD_DIR ..."

if [ -n "$BUILD_TARGET" ]; then
    if ! make -C "$INNER_BUILD_DIR" -j"$NCPU" "$BUILD_TARGET"; then
        log "ERROR: Build failed for target '$BUILD_TARGET'"
        exit 1
    fi
else
    if ! make -C "$INNER_BUILD_DIR" -j"$NCPU"; then
        log "ERROR: Build failed"
        exit 1
    fi
fi

log "Build complete."

# --- Ensure crashpad_handler is in the app bundle (required for Sentry) ---
CRASHPAD_HANDLER="$INNER_BUILD_DIR/thirdparty/sentry-native/crashpad_build/handler/crashpad_handler"
if [ -f "$CRASHPAD_HANDLER" ] && [ ! -f "$INTREE_APP/Contents/MacOS/crashpad_handler" ]; then
    log "Copying crashpad_handler into app bundle..."
    cp "$CRASHPAD_HANDLER" "$INTREE_APP/Contents/MacOS/"
fi

# --- Ensure bitmap archive is always available (prevents "?" icons) ---
# The build creates images.tar.gz at resources/ but cmake only copies it into
# SharedSupport during `make install`. Symlink it so icons work in the in-tree app.
INTREE_RESOURCES="$INTREE_APP/Contents/SharedSupport/resources"
BUILD_IMAGES="$INNER_BUILD_DIR/resources/images.tar.gz"
if [ ! -e "$INTREE_RESOURCES/images.tar.gz" ] && [ -f "$BUILD_IMAGES" ]; then
    log "Symlinking bitmap archive into SharedSupport/resources..."
    mkdir -p "$INTREE_RESOURCES"
    ln -s "$BUILD_IMAGES" "$INTREE_RESOURCES/images.tar.gz"
fi

# --- Ensure launch prerequisites (for launch) ---

if $DO_LAUNCH; then
    ZEO_BIN="$INTREE_APP/Contents/MacOS/Zeo"
    if [ ! -f "$ZEO_BIN" ]; then
        log "Zeo binary not found, building kicad target..."
        if ! make -C "$INNER_BUILD_DIR" -j"$NCPU" kicad; then
            log "ERROR: Build failed for kicad target"
            exit 1
        fi
    fi

    # Ensure Python framework is complete (cmake only creates a skeleton)
    INTREE_PYTHON_FW="$INTREE_APP/Contents/Frameworks/Python.framework"
    if [ ! -L "$INTREE_PYTHON_FW/Versions/Current" ]; then
        if [ -d "$PYTHON_FW_SRC" ]; then
            log "Copying Python framework into in-tree bundle..."
            rm -rf "$INTREE_PYTHON_FW"
            mkdir -p "$INTREE_APP/Contents/Frameworks"
            cp -R "$PYTHON_FW_SRC" "$INTREE_PYTHON_FW"
        else
            log "Warning: Python framework not found at $PYTHON_FW_SRC — app may crash"
        fi
    fi

    # Ensure SharedSupport exists (icons, bitmaps, resources)
    # Symlink from the installed bundle so resources stay in sync
    INSTALLED_APP="$BUILDER_DIR/build/kicad-dest/Zeo.app"
    INTREE_SHARED="$INTREE_APP/Contents/SharedSupport"
    if [ ! -e "$INTREE_SHARED" ] && [ -d "$INSTALLED_APP/Contents/SharedSupport" ]; then
        log "Symlinking SharedSupport from installed bundle..."
        ln -s "$INSTALLED_APP/Contents/SharedSupport" "$INTREE_SHARED"
    fi

    # Ensure bitmap archive (images.tar.gz) is available in SharedSupport.
    # The build creates it at resources/images.tar.gz but cmake only copies it
    # into SharedSupport during `make install`. Symlink from the build output
    # so icons load correctly in the in-tree app.
    INTREE_RESOURCES="$INTREE_SHARED/resources"
    BUILD_IMAGES="$INNER_BUILD_DIR/resources/images.tar.gz"
    if [ ! -e "$INTREE_RESOURCES/images.tar.gz" ] && [ -f "$BUILD_IMAGES" ]; then
        log "Symlinking bitmap archive into SharedSupport/resources..."
        mkdir -p "$INTREE_RESOURCES"
        ln -s "$BUILD_IMAGES" "$INTREE_RESOURCES/images.tar.gz"
    fi

    # Ensure KiCad symbol/footprint libraries are installed
    # Uses the same cmake install approach as mac_build.sh.
    # Only runs once — skipped if sym-lib-table already exists in SharedSupport.
    SHARED_SUPPORT="$INTREE_APP/Contents/SharedSupport"
    if [ -L "$SHARED_SUPPORT" ]; then
        SHARED_SUPPORT="$(readlink "$SHARED_SUPPORT")"
    fi

    if [ -d "$LIBRARIES_DIR" ] && [ ! -f "$SHARED_SUPPORT/template/sym-lib-table" ]; then
        log "Installing KiCad libraries into SharedSupport (one-time)..."
        for lib_name in kicad-symbols kicad-footprints; do
            lib_path="$LIBRARIES_DIR/$lib_name"
            if [ -d "$lib_path" ]; then
                log "  $lib_name..."
                build_dir=$(mktemp -d)
                cmake "$lib_path" -DCMAKE_INSTALL_PREFIX="$SHARED_SUPPORT" -B "$build_dir" > /dev/null 2>&1
                make -C "$build_dir" install > /dev/null
                rm -rf "$build_dir"
            fi
        done
        log "Libraries installed."
    fi

    # Symlink templates and 3D models from ExternalProject build outputs
    TEMPLATES_OUTPUT="$BUILDER_DIR/build/templates/src/templates-build/output/template"
    PACKAGES3D_OUTPUT="$BUILDER_DIR/build/packages3d/src/packages3d-build/output/3dmodels"

    if [ -d "$TEMPLATES_OUTPUT" ] && [ ! -e "$SHARED_SUPPORT/template" ]; then
        log "Symlinking templates from ExternalProject output..."
        ln -s "$TEMPLATES_OUTPUT" "$SHARED_SUPPORT/template"
    fi

    if [ -d "$PACKAGES3D_OUTPUT" ] && [ ! -e "$SHARED_SUPPORT/3dmodels" ]; then
        log "Symlinking 3D models from ExternalProject output..."
        ln -s "$PACKAGES3D_OUTPUT" "$SHARED_SUPPORT/3dmodels"
    fi

    # Ensure ngspice libraries and code models are available
    # The cmake build links eeschema_kiface to ngspice-dest via absolute path,
    # but the code model plugins (.cm files) are only copied during make install.
    # Symlink them from ngspice-dest so the in-tree app can find them.
    NGSPICE_DEST="$BUILDER_DIR/build/ngspice-dest/lib"
    INTREE_SIM_DIR="$INTREE_APP/Contents/PlugIns/sim"

    if [ -d "$NGSPICE_DEST/ngspice" ] && [ ! -e "$INTREE_SIM_DIR/ngspice" ]; then
        log "Symlinking ngspice code models into in-tree bundle..."
        mkdir -p "$INTREE_SIM_DIR"
        ln -s "$NGSPICE_DEST/ngspice" "$INTREE_SIM_DIR/ngspice"
    fi

    if [ -f "$NGSPICE_DEST/libngspice.0.dylib" ] && [ ! -e "$INTREE_SIM_DIR/libngspice.0.dylib" ]; then
        log "Symlinking libngspice into in-tree bundle PlugIns/sim..."
        mkdir -p "$INTREE_SIM_DIR"
        ln -s "$NGSPICE_DEST/libngspice.0.dylib" "$INTREE_SIM_DIR/libngspice.0.dylib"
        ln -s "$NGSPICE_DEST/libngspice.dylib" "$INTREE_SIM_DIR/libngspice.dylib"
    fi

    # Copy Freerouting JAR if available (built by mac_build.sh or manually)
    FREEROUTING_JAR="$WORKSPACE_DIR/tools/freerouting/build/libs/freerouting-executable.jar"
    FREEROUTING_DEST="$SHARED_SUPPORT/freerouting"
    if [ -f "$FREEROUTING_JAR" ] && [ ! -f "$FREEROUTING_DEST/freerouting.jar" ]; then
        log "Installing Freerouting JAR into SharedSupport..."
        mkdir -p "$FREEROUTING_DEST"
        cp "$FREEROUTING_JAR" "$FREEROUTING_DEST/freerouting.jar"
    fi
fi

# --- Install kipy (auto if missing, forced with --python) ---

install_kipy() {
    log "Installing kicad-python into in-tree bundle..."

    # Ensure Python framework exists in in-tree bundle (check for Current symlink,
    # not just the directory, since cmake creates a skeleton with only Versions/3.10/lib)
    INTREE_PYTHON_FW="$INTREE_APP/Contents/Frameworks/Python.framework"
    if [ ! -L "$INTREE_PYTHON_FW/Versions/Current" ]; then
        if [ -d "$PYTHON_FW_SRC" ]; then
            log "Copying Python framework into in-tree bundle (one-time)..."
            rm -rf "$INTREE_PYTHON_FW"
            mkdir -p "$INTREE_APP/Contents/Frameworks"
            cp -R "$PYTHON_FW_SRC" "$INTREE_PYTHON_FW"
        else
            echo "Error: Python framework not found at $PYTHON_FW_SRC"
            echo "Run mac_build.sh first to build the Python framework."
            exit 1
        fi
    fi

    mkdir -p "$INTREE_SITE_PKG"

    if [ ! -d "$KICAD_PYTHON_DIR" ]; then
        echo "Error: kicad-python directory not found at $KICAD_PYTHON_DIR"
        exit 1
    fi

    # Verify protoc is available (required for proto generation)
    if ! command -v protoc &> /dev/null; then
        echo "Error: protoc not found. Install with: brew install protobuf"
        exit 1
    fi

    # Copy proto files from zeo to zeo-python
    log "Copying protobuf files..."
    mkdir -p "$KICAD_PYTHON_DIR/kicad/api/proto"
    cp -r "$KICAD_SOURCE_DIR/api/proto/"* "$KICAD_PYTHON_DIR/kicad/api/proto/"

    # Generate python protobuf bindings (will fail with clear error if protoc/protol missing)
    log "Generating python protobuf bindings..."
    pushd "$KICAD_PYTHON_DIR" > /dev/null
    python3 tools/generate_protos.py
    popd > /dev/null

    # Verify proto generation succeeded
    if [ ! -f "$KICAD_PYTHON_DIR/kipy/proto/common/envelope_pb2.py" ]; then
        log "Error: Proto generation failed - envelope_pb2.py not found"
        exit 1
    fi
    log "Proto generation verified."

    # Generate kicad_api_version.py from git describe
    log "Generating kicad_api_version.py..."
    pushd "$KICAD_PYTHON_DIR/kicad/api/proto" > /dev/null
    GIT_VERSION=$(git describe --long 2>/dev/null || echo "0.0.0-0-unknown")
    popd > /dev/null
    cat > "$KICAD_PYTHON_DIR/kipy/kicad_api_version.py" << 'VERSIONEOF'
# This file is automatically generated, do not modify it
VERSIONEOF
    echo "KICAD_API_VERSION = \"$GIT_VERSION\"" >> "$KICAD_PYTHON_DIR/kipy/kicad_api_version.py"
    log "Generated kicad_api_version.py with version: $GIT_VERSION"

    # Install dependencies (including kiutils which the scripting console needs)
    log "Installing kicad-python dependencies..."
    python3 -m pip install --target "$INTREE_SITE_PKG" --upgrade kiutils 2>/dev/null || true
    python3 -m pip install --target "$INTREE_SITE_PKG" --upgrade --python-version 3.10 --only-binary=:all: "protobuf>=6.33" "pynng>=0.8.0" typing_extensions matplotlib mcp textual 2>/dev/null || true

    # Copy kipy directly (bypasses poetry-core which excludes gitignored generated files)
    # This ensures _pb2.py files and kicad_api_version.py are included
    log "Copying kipy package..."
    rm -rf "$INTREE_SITE_PKG/kipy"
    cp -R "$KICAD_PYTHON_DIR/kipy" "$INTREE_SITE_PKG/"

    # Also install kipy dependencies to the user's Python for CLI tools (zeo shell/tool)
    # These run outside the Zeo bundle using system Python
    log "Installing kipy dependencies to user Python (for zeo CLI tools)..."
    python3 -m pip install --break-system-packages --upgrade "protobuf>=6.33" "pynng>=0.8.0" typing_extensions
    python3 -m pip install --break-system-packages --no-deps -e "$KICAD_PYTHON_DIR"

    log "kicad-python installed."
}

if $DO_PYTHON; then
    # Explicit --python: force reinstall (to pick up changes)
    install_kipy
    # Kill the MCP server so Claude Code auto-restarts it with fresh modules
    pkill -f "kipy.mcp" 2>/dev/null && log "Killed MCP server (will auto-restart)." || true
elif [ ! -d "$INTREE_SITE_PKG/kipy" ]; then
    # Auto-install if kipy is missing
    install_kipy
fi

# --- Optional: make install ---

if $DO_INSTALL; then
    # Remove stale kicad-dest to avoid conflicts (missing Python.framework, etc).
    KICAD_DEST="$BUILDER_DIR/build/kicad-dest"
    if [ -d "$KICAD_DEST" ]; then
        log "Removing stale kicad-dest..."
        rm -rf "$KICAD_DEST"
    fi

    # CMake's file(INSTALL TYPE DIRECTORY) follows the ngspice symlink (creating
    # a real directory at the dest) then tries to duplicate the symlink itself,
    # which fails. Temporarily remove it from the source build tree.
    SRC_NGSPICE="$INNER_BUILD_DIR/kicad/Zeo.app/Contents/PlugIns/sim/ngspice"
    NGSPICE_TARGET=""
    if [ -L "$SRC_NGSPICE" ]; then
        NGSPICE_TARGET=$(readlink "$SRC_NGSPICE")
        log "Temporarily removing ngspice symlink for install..."
        rm "$SRC_NGSPICE"
    fi

    # Use cmake --install (not make install) to run ONLY the install scripts
    # without re-building. This prevents the build step from seeing the
    # temporarily-removed ngspice symlink.
    log "Running cmake --install..."
    cmake --install "$INNER_BUILD_DIR"

    # Restore the symlink in the source build tree
    if [ -n "$NGSPICE_TARGET" ]; then
        ln -s "$NGSPICE_TARGET" "$SRC_NGSPICE"
    fi

    log "Install complete."

    # Install KiCad libraries to kicad-dest SharedSupport
    KICAD_DEST="$BUILDER_DIR/build/kicad-dest"
    DEST_SHARED_SUPPORT="$KICAD_DEST/Zeo.app/Contents/SharedSupport"

    if [ -d "$LIBRARIES_DIR" ] && [ ! -f "$DEST_SHARED_SUPPORT/template/sym-lib-table" ]; then
        log "Installing KiCad libraries into kicad-dest SharedSupport..."
        for lib_name in kicad-symbols kicad-footprints; do
            lib_path="$LIBRARIES_DIR/$lib_name"
            if [ -d "$lib_path" ]; then
                log "  $lib_name..."
                build_dir=$(mktemp -d)
                cmake "$lib_path" -DCMAKE_INSTALL_PREFIX="$DEST_SHARED_SUPPORT" -B "$build_dir" > /dev/null 2>&1
                make -C "$build_dir" install > /dev/null
                rm -rf "$build_dir"
            fi
        done
        log "Libraries installed to kicad-dest."
    fi

    # Copy Freerouting JAR to kicad-dest if available
    FREEROUTING_JAR="$WORKSPACE_DIR/tools/freerouting/build/libs/freerouting-executable.jar"
    DEST_FREEROUTING="$DEST_SHARED_SUPPORT/freerouting"
    if [ -f "$FREEROUTING_JAR" ] && [ ! -f "$DEST_FREEROUTING/freerouting.jar" ]; then
        log "Installing Freerouting JAR into kicad-dest SharedSupport..."
        mkdir -p "$DEST_FREEROUTING"
        cp "$FREEROUTING_JAR" "$DEST_FREEROUTING/freerouting.jar"
    fi
fi

# --- Launch ---

if $DO_LAUNCH; then
    ZEO_BIN="$INTREE_APP/Contents/MacOS/Zeo"

    if $DO_LLDB; then
        log "Launching under lldb debugger..."
        echo "When the app crashes, use 'bt' for backtrace, 'bt all' for all threads"
        lldb -o "run" "$ZEO_BIN"
    elif $DO_DEBUG; then
        log "Launching with agent debug tracing (WXTRACE=KICAD_AGENT)..."
        WXTRACE=KICAD_AGENT "$ZEO_BIN"
    else
        log "Launching Zeo (hot-reload enabled)..."
        export AGENT_PYTHON_DIR="$KICAD_SOURCE_DIR/agent/tools/python"
        nohup "$ZEO_BIN" > /dev/null 2>&1 &
    fi
fi

# --- Timing ---

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
log "Done in ${ELAPSED}s."
echo ""
echo "AGENT_PYTHON_DIR=\"$KICAD_SOURCE_DIR/agent/tools/python\" \"$INTREE_APP/Contents/MacOS/Zeo\""
