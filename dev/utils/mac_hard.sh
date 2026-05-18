#!/bin/bash
set -e

# Zeo macOS Full Build Script (mac_build.sh)
# Performs a complete rebuild of all components
# For incremental builds, use mac_build.sh --fast instead

# --- Configuration ---

# Get absolute path to the directory containing this script (dev folder)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Base workspace directory
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Define paths relative to workspace
KICAD_SOURCE_DIR="$WORKSPACE_DIR/src/zeo"
BUILDER_DIR="$WORKSPACE_DIR/packaging/kicad-mac-builder"
LIBRARIES_DIR="$WORKSPACE_DIR/libraries"
KICAD_PYTHON_DIR="$WORKSPACE_DIR/src/zeo-python"

# --- Argument Parsing ---

DEBUG_AGENT=false
AUTO_LAUNCH=false
FORCE_BUILD=false
LLDB_DEBUG=false
RELEASE_BUILD=false
VERBOSE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --release  Release build (clean version string, production URLs)"
    echo "  --force    Clear CMake cache and force full reconfigure"
    echo "  --launch   Auto-close running instance and launch app after build"
    echo "  --debug    Enable agent debug tracing (use with --launch)"
    echo "  --lldb     Launch under lldb debugger for crash analysis (use with --launch)"
    echo "  --verbose  Show build output in terminal (default: log only)"
    echo "  --help     Show this help message"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            RELEASE_BUILD=true
            shift
            ;;
        --force)
            FORCE_BUILD=true
            shift
            ;;
        --launch)
            AUTO_LAUNCH=true
            shift
            ;;
        --debug)
            DEBUG_AGENT=true
            shift
            ;;
        --lldb)
            LLDB_DEBUG=true
            shift
            ;;
        --verbose)
            VERBOSE=true
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

# Build log — overwritten each run; lives in dev/log/ (gitignored)
BUILD_LOG="$WORKSPACE_DIR/dev/log/build.log"
mkdir -p "$(dirname "$BUILD_LOG")"
echo "Build started: $(date)" > "$BUILD_LOG"
if [ "$VERBOSE" = true ]; then
    exec > >(tee -a "$BUILD_LOG") 2>&1
else
    echo "Build log: $BUILD_LOG"
    exec > "$BUILD_LOG" 2>&1
fi

echo "=============================================="
echo "Zeo Build Script"
echo "=============================================="
echo "Workspace:     $WORKSPACE_DIR"
echo "Source Dir:    $KICAD_SOURCE_DIR"
echo "Builder Dir:   $BUILDER_DIR"
echo "Libraries Dir: $LIBRARIES_DIR"
echo "Python Dir:    $KICAD_PYTHON_DIR"
if $RELEASE_BUILD; then
    echo "Mode:          RELEASE (clean version, production URLs)"
fi
if $FORCE_BUILD; then
    echo "Mode:          FORCE (clearing CMake cache)"
fi
echo "=============================================="

if [ ! -d "$KICAD_SOURCE_DIR" ]; then
    echo "Error: Zeo source directory not found at $KICAD_SOURCE_DIR"
    exit 1
fi

if [ ! -d "$BUILDER_DIR" ]; then
    echo "Error: kicad-mac-builder directory not found at $BUILDER_DIR"
    exit 1
fi

if [ ! -d "$LIBRARIES_DIR" ]; then
    echo "Warning: Libraries directory not found at $LIBRARIES_DIR"
    echo "Global libraries (symbols, footprints, 3D models) will not be available."
fi

# --- Helper Functions ---

# Quit running Zeo instances
quit_kicad() {
    echo "[BUILD] Closing any running KiCad instances..."
    osascript -e 'quit app "Zeo"' 2>/dev/null || true
}

# Replace ngspice symlink with real directory in the build tree.
# The build tree's Zeo.app has ngspice as a symlink to ngspice-dest/lib/ngspice,
# but eeschema's install rule copies ngspice as a real directory first. When kicad's
# install rule then tries to copy the symlink over the real directory, CMake fails.
# Replacing the symlink with a real copy prevents this conflict.
fix_ngspice_symlink() {
    local ngspice_link="$BUILDER_DIR/build/kicad/src/kicad-build/kicad/Zeo.app/Contents/PlugIns/sim/ngspice"
    if [ -L "$ngspice_link" ]; then
        local target
        target=$(readlink "$ngspice_link")
        if [ -d "$target" ]; then
            echo "Replacing ngspice symlink with real directory for clean install..."
            rm "$ngspice_link"
            cp -R "$target" "$ngspice_link"
        fi
    fi
}

# Remove dangling SharedSupport symlink in the build tree.
# The cleanup above removes kicad-dest/Zeo.app, but the build tree may still
# have SharedSupport as a symlink pointing into that (now deleted) location.
# cmake -E make_directory fails through a dangling symlink, so remove it and
# let the build recreate it as a real directory.
fix_sharedsupport_symlink() {
    local ss_link="$BUILDER_DIR/build/kicad/src/kicad-build/kicad/Zeo.app/Contents/SharedSupport"
    if [ -L "$ss_link" ] && [ ! -d "$ss_link" ]; then
        echo "Removing dangling SharedSupport symlink in build tree..."
        rm "$ss_link"
    fi
}

# --- Dependency Check ---

export PATH="/opt/homebrew/opt/bison/bin:$PATH"
export WX_SKIP_DOXYGEN_VERSION_CHECK=1

REQUIRED_BREW_PACKAGES=(
    "automake" "libtool" "bison" "opencascade" "swig" "glew" "glm" "boost"
    "harfbuzz" "cairo" "doxygen" "gettext" "wget" "libgit2" "openssl"
    "unixodbc" "ninja" "protobuf" "nng" "zstd" "libomp"
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_BREW_PACKAGES[@]}"; do
    if ! brew list "$pkg" &> /dev/null; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo "Error: The following required brew packages are missing:"
    printf "  %s\n" "${MISSING_PACKAGES[@]}"
    echo "Please install them via brew install ..."
    exit 1
fi

# --- Build Execution ---

if $AUTO_LAUNCH; then
    quit_kicad
fi

echo ""
echo "Starting Zeo Build..."
cd "$BUILDER_DIR"

# Force full reconfigure by clearing CMake cache
if $FORCE_BUILD; then
    echo "Force build requested, clearing CMake cache..."
    rm -rf "$BUILDER_DIR/build/kicad"
    echo "CMake cache cleared."
fi

# Force incremental build by removing stamp files
# This ensures that 'make' and 'make install' are re-run inside the ExternalProject
STAMP_DIR="build/kicad/src/kicad-stamp"
if [ -d "$STAMP_DIR" ]; then
    echo "Removed stamp files to force incremental build..."
    rm -f "$STAMP_DIR/kicad-build"
    rm -f "$STAMP_DIR/kicad-install"
fi

# Clean up previous artifacts that cause install conflicts
BUILD_OUTPUT_DIR="$BUILDER_DIR/build/kicad-dest"
if [ -d "$BUILD_OUTPUT_DIR" ]; then
    echo "Cleaning up previous build artifacts from destination..."
    # Clean generic .app bundles from staging area
    rm -rf "$BUILD_OUTPUT_DIR"/*.app
    rm -rf "$BUILD_OUTPUT_DIR"/*.kiface

    # Also clean them from inside the bundle to ensure fresh copy
    if [ -d "$BUILD_OUTPUT_DIR/Zeo.app/Contents/Applications" ]; then
         rm -rf "$BUILD_OUTPUT_DIR/Zeo.app/Contents/Applications"/*.app
    fi
fi

# Fix symlinks before build.py triggers install
fix_ngspice_symlink
fix_sharedsupport_symlink

# Use --no-retry-failed-build to fail fast if something is wrong
# Target 'kicad' builds the main suite.
# We pass the local source directory so it builds OUR code.
# -DKICAD_BUNDLE_FILENAME="Zeo" renames the output bundle
EXTRA_CMAKE_ARGS='-DKICAD_BUNDLE_FILENAME="Zeo" -DKICAD_SCRIPTING=ON -DKICAD_SCRIPTING_MODULES=ON -DKICAD_SCRIPTING_WXPYTHON=ON -DKICAD_IPC_API=ON'

# Sentry crash reporting — flags are injected into the inner CMake cache after
# build.py runs (the --extra-kicad-cmake-args quoting mangles URLs).
KICAD_SENTRY_DSN="https://e79a97d4575365eec6ee3eed0a4281ce@o4511067051524096.ingest.us.sentry.io/4511067066138624"
SENTRY_ENABLED=true
echo "Sentry:        ENABLED"

if $RELEASE_BUILD; then
    EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DZEO_RELEASE=ON"
fi

./build.py \
    --arch=$(uname -m) \
    --kicad-source-dir="$KICAD_SOURCE_DIR" \
    --target kicad \
    --extra-kicad-cmake-args="$EXTRA_CMAKE_ARGS"

# --- Explicit Install Step ---

# --- Inject Sentry into inner CMake cache ---
# build.py's --extra-kicad-cmake-args mangles URLs, so we set these directly
# in the inner CMake cache and re-run cmake to pick them up.
INNER_BUILD_DIR="$(cd "$BUILDER_DIR/build/kicad/src/kicad-build" && pwd)"

if $SENTRY_ENABLED && [ -d "$INNER_BUILD_DIR" ]; then
    echo "Injecting Sentry config into inner CMake cache..."
    # Edit the cache directly, then reconfigure in-place
    sed -i '' 's/^KICAD_USE_SENTRY:BOOL=OFF/KICAD_USE_SENTRY:BOOL=ON/' "$INNER_BUILD_DIR/CMakeCache.txt"
    if ! grep -q "^KICAD_SENTRY_DSN:" "$INNER_BUILD_DIR/CMakeCache.txt"; then
        echo "KICAD_SENTRY_DSN:STRING=$KICAD_SENTRY_DSN" >> "$INNER_BUILD_DIR/CMakeCache.txt"
    fi
    # Reconfigure from the build dir to pick up the new flags
    pushd "$INNER_BUILD_DIR" > /dev/null
    cmake .
    popd > /dev/null
    # Rebuild with Sentry enabled
    make -C "$INNER_BUILD_DIR" -j$(sysctl -n hw.ncpu)

    # Copy crashpad_handler into the app bundle (required for Sentry to send events)
    CRASHPAD_HANDLER="$INNER_BUILD_DIR/thirdparty/sentry-native/crashpad_build/handler/crashpad_handler"
    if [ -f "$CRASHPAD_HANDLER" ]; then
        echo "Copying crashpad_handler into app bundle..."
        cp "$CRASHPAD_HANDLER" "$INNER_BUILD_DIR/kicad/Zeo.app/Contents/MacOS/"
    fi
fi


echo "Performing explicit make install..."
# build.py might not trigger a full install or specific targets might be skipped in wrapping.
# We go directly to the inner build directory and install everything.
# We go directly to the inner build directory and install everything.
# Ensure absolute path
INNER_BUILD_DIR="$(cd "$BUILDER_DIR/build/kicad/src/kicad-build" && pwd)"
echo "Inner Build Dir: $INNER_BUILD_DIR"

if [ ! -d "$INNER_BUILD_DIR" ]; then
    echo "Error: Inner build directory not found at $INNER_BUILD_DIR"
    exit 1
fi

# Clean up conflicting artifacts (symlinks or old dirs) in destination before explicit install
# This prevents "File exists" errors if build.py created symlinks (e.g. Agent.app -> ...)
# This is CRITICAL because build.py re-populates these, causing the subsequent make install to fail.
DEST_DIR="$BUILDER_DIR/build/kicad-dest"
if [ -d "$DEST_DIR" ]; then
    echo "Cleaning destination for explicit install..."
    rm -rf "$DEST_DIR/agent.app"
    rm -rf "$DEST_DIR/Agent.app"
    rm -rf "$DEST_DIR/gerbview.app"
    rm -rf "$DEST_DIR/GerbView.app"
    rm -rf "$DEST_DIR/terminal.app"
    rm -rf "$DEST_DIR/Terminal.app"
fi

# Fix symlinks again in case build.py recreated them
fix_ngspice_symlink
fix_sharedsupport_symlink

pushd "$INNER_BUILD_DIR" > /dev/null
echo "Entering $INNER_BUILD_DIR"
# Run install. COPYFILE_DISABLE prevents macOS from copying immutable
# com.apple.provenance xattrs that cause cmake file(INSTALL) to fail.
COPYFILE_DISABLE=1 make install
popd > /dev/null

# Verify validation
echo "Verifying explicit install..."
if [ ! -d "$DEST_DIR/agent.app" ] && [ ! -d "$DEST_DIR/Agent.app" ]; then
    echo "Error: agent.app failed to install to $DEST_DIR"
    exit 1
fi
if [ -z "$(ls -A "$DEST_DIR/agent.app" 2>/dev/null)" ] && [ -z "$(ls -A "$DEST_DIR/Agent.app" 2>/dev/null)" ]; then
    echo "Error: agent.app installed but is empty!"
    exit 1
fi
echo "Explicit install verification passed."

# Copy crashpad_handler into installed bundle (for DMG packaging)
if $SENTRY_ENABLED; then
    CRASHPAD_HANDLER="$INNER_BUILD_DIR/thirdparty/sentry-native/crashpad_build/handler/crashpad_handler"
    if [ -f "$CRASHPAD_HANDLER" ]; then
        echo "Copying crashpad_handler into installed bundle..."
        cp "$CRASHPAD_HANDLER" "$DEST_DIR/Zeo.app/Contents/MacOS/"
    fi

    # Upload debug symbols (dSYMs) to Sentry for crash symbolication
    if command -v sentry-cli >/dev/null 2>&1; then
        echo "Uploading debug symbols to Sentry..."
        sentry-cli debug-files upload \
            --org moonshine-e2 \
            --project zeo \
            "$INNER_BUILD_DIR/kicad/Zeo.app" \
            "$INNER_BUILD_DIR/common/" \
            "$INNER_BUILD_DIR/eeschema/" \
            "$INNER_BUILD_DIR/pcbnew/" \
            "$INNER_BUILD_DIR/3d-viewer/" \
            --include-sources \
            --log-level=warn || echo "Warning: Sentry debug symbol upload failed (non-fatal)"
    else
        echo "Warning: sentry-cli not found — skipping debug symbol upload."
        echo "  Install with: brew install getsentry/tools/sentry-cli"
    fi
fi

# --- Java Check (required for Freerouting) ---

if ! command -v java >/dev/null 2>&1; then
    echo "Error: Java is required to build Freerouting but was not found."
    echo ""
    echo "Please install Java from Adoptium Temurin:"
    echo "  https://adoptium.net/temurin/releases/?version=25&os=any&arch=any"
    echo ""
    exit 1
fi

# --- Freerouting Build ---
# Build the Freerouting autorouter JAR and install into SharedSupport

FREEROUTING_DIR="$WORKSPACE_DIR/tools/freerouting"
if [ -d "$FREEROUTING_DIR" ]; then
    echo "Building Freerouting autorouter..."

    SHARED_SUPPORT="$DEST_DIR/Zeo.app/Contents/SharedSupport"
    mkdir -p "$SHARED_SUPPORT/freerouting"

    pushd "$FREEROUTING_DIR" > /dev/null
    ./gradlew executableJar --no-daemon
    popd > /dev/null

    FREEROUTING_JAR="$FREEROUTING_DIR/build/libs/freerouting-executable.jar"
    if [ -f "$FREEROUTING_JAR" ]; then
        cp "$FREEROUTING_JAR" "$SHARED_SUPPORT/freerouting/freerouting.jar"
        echo "Freerouting JAR installed to SharedSupport/freerouting/"
    else
        echo "Warning: Freerouting JAR not found at $FREEROUTING_JAR"
    fi
else
    echo "Skipping Freerouting build (tools/freerouting directory not found)."
fi

# --- KiCad Libraries Build ---
# Build and install global libraries (symbols, footprints, 3D models, templates)
# These are built from the local libraries directory and installed into SharedSupport

if [ -d "$LIBRARIES_DIR" ]; then
    SHARED_SUPPORT="$DEST_DIR/Zeo.app/Contents/SharedSupport"
    mkdir -p "$SHARED_SUPPORT"

    # Function to build and install a library
    build_library() {
        local lib_name="$1"
        local lib_path="$LIBRARIES_DIR/$lib_name"

        if [ ! -d "$lib_path" ]; then
            echo "Warning: $lib_name not found at $lib_path, skipping..."
            return 0
        fi

        echo "Building $lib_name..."
        local build_dir=$(mktemp -d)

        pushd "$build_dir" > /dev/null
        if ! cmake "$lib_path" -DCMAKE_INSTALL_PREFIX="$SHARED_SUPPORT"; then
            echo "Error: cmake failed for $lib_name"
            popd > /dev/null
            rm -rf "$build_dir"
            return 1
        fi
        if ! make install; then
            echo "Error: make install failed for $lib_name"
            popd > /dev/null
            rm -rf "$build_dir"
            return 1
        fi
        popd > /dev/null

        rm -rf "$build_dir"
        echo "$lib_name installed."
    }

    echo "Installing KiCad libraries to SharedSupport..."

    # Build each library
    build_library "kicad-symbols"
    build_library "kicad-footprints"
    build_library "kicad-packages3D"
    build_library "kicad-templates"

    # Verify installation
    echo "Verifying library installation..."
    LIB_CHECK_FAILED=0

    if [ ! -d "$SHARED_SUPPORT/symbols" ] || [ -z "$(ls -A "$SHARED_SUPPORT/symbols" 2>/dev/null)" ]; then
        echo "Warning: Symbol libraries not found or empty"
        LIB_CHECK_FAILED=1
    fi

    if [ ! -d "$SHARED_SUPPORT/footprints" ] || [ -z "$(ls -A "$SHARED_SUPPORT/footprints" 2>/dev/null)" ]; then
        echo "Warning: Footprint libraries not found or empty"
        LIB_CHECK_FAILED=1
    fi

    if [ ! -d "$SHARED_SUPPORT/3dmodels" ] || [ -z "$(ls -A "$SHARED_SUPPORT/3dmodels" 2>/dev/null)" ]; then
        echo "Warning: 3D model libraries not found or empty"
        LIB_CHECK_FAILED=1
    fi

    if [ ! -d "$SHARED_SUPPORT/template" ] || [ -z "$(ls -A "$SHARED_SUPPORT/template" 2>/dev/null)" ]; then
        echo "Warning: Templates not found or empty"
        LIB_CHECK_FAILED=1
    fi

    if [ $LIB_CHECK_FAILED -eq 0 ]; then
        echo "Library installation verified successfully."
    else
        echo "Warning: Some libraries may not have installed correctly."
    fi
else
    echo "Skipping library installation (libraries directory not found)."
fi

# --- Artifact Bundling ---

BUILD_OUTPUT_DIR="$BUILDER_DIR/build/kicad-dest"
KICAD_APP_BUNDLE="$BUILD_OUTPUT_DIR/Zeo.app" # CMake often ignores the bundle filename arg for the directory name itself, but let's check.
# If KICAD_BUNDLE_FILENAME is honored for the directory name, we might need to adjust.
# Usually it makes Zeo.app and renames the DMG. Let's assume Zeo.app for now or check.
if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    echo "Error: Could not find main application bundle at $BUILD_OUTPUT_DIR/Zeo.app"
    exit 1
fi

DEST_APPS_DIR="$KICAD_APP_BUNDLE/Contents/Applications"
DEST_FRAMEWORKS_DIR="$KICAD_APP_BUNDLE/Contents/Frameworks"

mkdir -p "$DEST_APPS_DIR"
mkdir -p "$DEST_FRAMEWORKS_DIR"

echo "Scanning for additional applications to bundle..."

# Find all .app folders in build output that are NOT the main bundle
# We look in kicad-dest
# Find all .app folders in build output that are NOT the main bundle
# We look in kicad-dest
# Use -type d to avoid copying symlinks (which might be broken or circular when moved)
find "$BUILD_OUTPUT_DIR" -maxdepth 1 -type d -name "*.app" | while read app_path; do
    app_name=$(basename "$app_path")

    # Skip the main bundle itself to avoid recursion
    if [[ "$app_path" == "$KICAD_APP_BUNDLE" ]]; then
        continue
    fi

    echo "Bundling $app_name..."
    # Remove existing to ensure clean copy
    rm -rf "$DEST_APPS_DIR/$app_name"
    cp -R "$app_path" "$DEST_APPS_DIR/"
done

DEST_PLUGINS_DIR="$KICAD_APP_BUNDLE/Contents/PlugIns"
mkdir -p "$DEST_PLUGINS_DIR"

echo "Scanning for additional .kiface files..."
find "$BUILD_OUTPUT_DIR" -maxdepth 1 -name "*.kiface" | while read kiface_path; do
    kiface_name=$(basename "$kiface_path")
    echo "Bundling $kiface_name..."
    cp "$kiface_path" "$DEST_PLUGINS_DIR/"
done

echo "Fixing up _agent.kiface dependencies..."
AGENT_KIFACE="$DEST_PLUGINS_DIR/_agent.kiface"
if [ -f "$AGENT_KIFACE" ]; then
    otool -L "$AGENT_KIFACE" | grep "\t/" | while read -r line; do
        # Extract path (first token)
        libpath=$(echo "$line" | awk '{print $1}')
        libname=$(basename "$libpath")

        # Don't touch system libs (/usr/lib, /System/Library)
        if [[ "$libpath" == /usr/lib* ]] || [[ "$libpath" == /System/Library* ]]; then
            continue
        fi

        echo "Changing $libpath to @rpath/$libname"
        install_name_tool -change "$libpath" "@rpath/$libname" "$AGENT_KIFACE"
    done
fi

echo "Fixing up _terminal.kiface dependencies..."
TERMINAL_KIFACE="$DEST_PLUGINS_DIR/_terminal.kiface"
if [ -f "$TERMINAL_KIFACE" ]; then
    otool -L "$TERMINAL_KIFACE" | grep "\t/" | while read -r line; do
        # Extract path (first token)
        libpath=$(echo "$line" | awk '{print $1}')
        libname=$(basename "$libpath")

        # Don't touch system libs (/usr/lib, /System/Library)
        if [[ "$libpath" == /usr/lib* ]] || [[ "$libpath" == /System/Library* ]]; then
            continue
        fi

        echo "Changing $libpath to @rpath/$libname"
        install_name_tool -change "$libpath" "@rpath/$libname" "$TERMINAL_KIFACE"
    done
fi

echo "Fixing up _vcs.kiface dependencies..."
VCS_KIFACE="$DEST_PLUGINS_DIR/_vcs.kiface"
if [ -f "$VCS_KIFACE" ]; then
    otool -L "$VCS_KIFACE" | grep "\t/" | while read -r line; do
        # Extract path (first token)
        libpath=$(echo "$line" | awk '{print $1}')
        libname=$(basename "$libpath")

        # Don't touch system libs (/usr/lib, /System/Library)
        if [[ "$libpath" == /usr/lib* ]] || [[ "$libpath" == /System/Library* ]]; then
            continue
        fi

        echo "Changing $libpath to @rpath/$libname"
        install_name_tool -change "$libpath" "@rpath/$libname" "$VCS_KIFACE"
    done
fi

echo "Fixing ngspice library symlink..."
FRAMEWORKS_DIR="$KICAD_APP_BUNDLE/Contents/Frameworks"
if [ -f "$FRAMEWORKS_DIR/libngspice.0.dylib" ] && [ ! -e "$FRAMEWORKS_DIR/libngspice.dylib" ]; then
    ln -sf libngspice.0.dylib "$FRAMEWORKS_DIR/libngspice.dylib"
    echo "Created libngspice.dylib symlink"
fi

echo "Fixing ngspice PlugIns symlinks..."
SIM_PLUGINS_DIR="$KICAD_APP_BUNDLE/Contents/PlugIns/sim"
if [ -d "$SIM_PLUGINS_DIR" ]; then
    # Replace absolute symlinks with actual files for code signing compatibility
    for symlink in "$SIM_PLUGINS_DIR"/*.dylib; do
        if [ -L "$symlink" ]; then
            target=$(readlink "$symlink")
            # Check if it's an absolute path (outside bundle)
            if [[ "$target" == /* ]] && [[ "$target" != "$KICAD_APP_BUNDLE"* ]]; then
                echo "Resolving external symlink: $symlink -> $target"
                if [ -f "$target" ]; then
                    rm "$symlink"
                    cp "$target" "$symlink"
                    # Fix the install name to use @rpath
                    libname=$(basename "$symlink")
                    install_name_tool -id "@rpath/$libname" "$symlink" 2>/dev/null || true
                    echo "Replaced with actual file: $symlink"
                else
                    echo "Warning: symlink target not found: $target"
                fi
            fi
        fi
    done

    # Recreate the relative libngspice.dylib symlink if needed
    if [ -f "$SIM_PLUGINS_DIR/libngspice.0.dylib" ] && [ ! -e "$SIM_PLUGINS_DIR/libngspice.dylib" ]; then
        ln -sf libngspice.0.dylib "$SIM_PLUGINS_DIR/libngspice.dylib"
        echo "Created libngspice.dylib symlink in PlugIns/sim"
    fi
fi

echo "Bundling kiutils..."
# Install kiutils into the bundle's site-packages
# Path determined from previous exploration: Python.framework/Versions/Current/lib/python3.10/site-packages
# We use the system pip/python to install into the target directory.
# 'Current' symlink should be reliable given the framework structure.

PYTHON_SITE_PACKAGES="$KICAD_APP_BUNDLE/Contents/Frameworks/Python.framework/Versions/Current/lib/python3.10/site-packages"

if [ -d "$PYTHON_SITE_PACKAGES" ]; then
    echo "Installing kiutils to $PYTHON_SITE_PACKAGES"
    # Use pip3, assume available since we checked for python deps? Or just use python3 -m pip
    # ignoring installed build dependencies to avoid messing with system, just target install
    python3 -m pip install --target "$PYTHON_SITE_PACKAGES" kiutils
else
    echo "Warning: Python site-packages directory not found at:"
    echo "  $PYTHON_SITE_PACKAGES"
    echo "Skipping kiutils bundling."
fi


echo "Building and bundling kicad-python..."

if [ -d "$KICAD_PYTHON_DIR" ]; then
    echo "Processing kicad-python at $KICAD_PYTHON_DIR"

    # Verify protoc is available (required for proto generation)
    if ! command -v protoc &> /dev/null; then
        echo "Error: protoc not found. Install with: brew install protobuf"
        exit 1
    fi

    # Copy protos from kicad-agent (source) to kicad-python
    echo "Copying protobuf files..."
    mkdir -p "$KICAD_PYTHON_DIR/kicad/api/proto"
    cp -r "$KICAD_SOURCE_DIR/api/proto/"* "$KICAD_PYTHON_DIR/kicad/api/proto/"

    # host build deps
    # Note: protoletariat claims to require protobuf<6, but actually works with protobuf 6+
    # Install protobuf first, then protoletariat with --no-deps to bypass the incorrect constraint
    echo "Installing build dependencies..."
    python3 -m pip install --break-system-packages --upgrade "protobuf>=6.33" mypy-protobuf
    python3 -m pip install --break-system-packages --no-deps protoletariat

    # Run proto generation (will fail with clear error if protoc/protol missing)
    echo "Generating python protobuf bindings..."
    pushd "$KICAD_PYTHON_DIR" > /dev/null
    python3 tools/generate_protos.py
    popd > /dev/null

    # Verify proto generation succeeded
    if [ ! -f "$KICAD_PYTHON_DIR/kipy/proto/common/envelope_pb2.py" ]; then
        echo "Error: Proto generation failed - envelope_pb2.py not found"
        exit 1
    fi
    echo "Proto generation verified."

    # Generate kicad_api_version.py from git describe
    echo "Generating kicad_api_version.py..."
    pushd "$KICAD_PYTHON_DIR/kicad/api/proto" > /dev/null
    GIT_VERSION=$(git describe --long 2>/dev/null || echo "0.0.0-0-unknown")
    popd > /dev/null
    cat > "$KICAD_PYTHON_DIR/kipy/kicad_api_version.py" << EOF
# This file is automatically generated, do not modify it
KICAD_API_VERSION = "$GIT_VERSION"
EOF
    echo "Generated kicad_api_version.py with version: $GIT_VERSION"

    # Install into bundle
    if [ -d "$PYTHON_SITE_PACKAGES" ]; then
        echo "Installing kicad-python dependencies to $PYTHON_SITE_PACKAGES"
        # Step 1: Install dependencies with binary wheels for cp310
        python3 -m pip install --target "$PYTHON_SITE_PACKAGES" --upgrade --python-version 3.10 --only-binary=:all: "protobuf>=6.33" "pynng>=0.8.0" typing_extensions matplotlib mcp "Pillow>=10.0" textual

        echo "Installing kicad-python package..."
        # Step 2: Copy kipy directly (bypasses poetry-core which excludes gitignored generated files)
        # This ensures _pb2.py files and kicad_api_version.py are included
        rm -rf "$PYTHON_SITE_PACKAGES/kipy"
        cp -R "$KICAD_PYTHON_DIR/kipy" "$PYTHON_SITE_PACKAGES/"
        echo "kipy copied to $PYTHON_SITE_PACKAGES/kipy"

        # Fix permissions so all users can read the bundled Python packages.
        # Without this, apps installed by one user won't work for other users.
        echo "Fixing permissions on bundled Python packages..."
        find "$PYTHON_SITE_PACKAGES" -type d -exec chmod 755 {} \;
        find "$PYTHON_SITE_PACKAGES" -type f -exec chmod 644 {} \;
    else
         echo "Warning: Python site-packages directory not found, skipping kicad-python install."
    fi

    # Also install kipy dependencies to the user's Python for development convenience.
    # Note: DMG users don't need this - the zeo CLI uses the bundled Python when
    # running from the app bundle. This is only for developers running from build dir.
    echo "Installing kipy dependencies to user Python (for development)..."
    python3 -m pip install --break-system-packages --upgrade "protobuf>=6.33" "pynng>=0.8.0" typing_extensions
    python3 -m pip install --break-system-packages --no-deps -e "$KICAD_PYTHON_DIR"

else
    echo "Warning: kicad-python directory not found at $KICAD_PYTHON_DIR"
fi


echo "Signing bundled applications..."
# Re-sign the bundled applications to ensure validity after copying
# Use ad-hoc signing (-) which is sufficient for local development
find "$DEST_APPS_DIR" -maxdepth 1 -type d -name "*.app" | while read app_path; do
    echo "Signing $app_path..."
    codesign --force --deep --sign - "$app_path"
done

echo "Signing main bundle..."
codesign --force --deep --sign - "$KICAD_APP_BUNDLE"

# --- Build Complete ---

echo ""
echo "=============================================="
echo "HARD BUILD COMPLETE"
echo "=============================================="
echo ""
echo "Bundle:"
echo "  $KICAD_APP_BUNDLE"
echo ""
echo "Common commands:"
echo "  open \"$KICAD_APP_BUNDLE\""
echo "  WXTRACE=KICAD_AGENT \"$KICAD_APP_BUNDLE/Contents/MacOS/Zeo\"        # agent debug tracing"
echo "  lldb -o run \"$KICAD_APP_BUNDLE/Contents/MacOS/Zeo\"                # under lldb"
echo ""
echo "For incremental rebuilds, use: ./dev/mac_build.sh --fast --install --python"
echo "=============================================="

# Auto-launch Zeo (only if --launch or --debug flag is set)
if $AUTO_LAUNCH; then
    if $LLDB_DEBUG; then
        echo "[BUILD] Launching under lldb debugger..."
        echo "When the app crashes, use 'bt' for backtrace, 'bt all' for all threads"
        lldb -o "run" "$KICAD_APP_BUNDLE/Contents/MacOS/Zeo"
    elif $DEBUG_AGENT; then
        echo "[BUILD] Launching with agent debug tracing enabled (WXTRACE=KICAD_AGENT)..."
        WXTRACE=KICAD_AGENT "$KICAD_APP_BUNDLE/Contents/MacOS/Zeo"
    else
        open "$KICAD_APP_BUNDLE"
    fi
fi