#!/bin/bash
set -e

# KiCad macOS Build Wrapper using kicad-mac-builder

# --- Configuration ---

# Get absolute path to the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Define paths relative to the script
# Configured based on user workspaces: 
# KiCAD_source_code/kicad-agent/mac_build.sh (SCRIPT_DIR)
# KiCAD_packaging/kicad-mac-builder (BUILDER_DIR)
KICAD_SOURCE_DIR="$SCRIPT_DIR/../" # Go up one level from kicad-agent to get KiCAD_source_code root if that's the intention, 
                                   # OR if this script is INSIDE the source root, just SCRIPT_DIR.
                                   # Looking at the file tree: 
                                   # /Users/gmp/workspaces/KiCAD_agentic/KiCAD_source_code/kicad-agent/mac_build.sh
                                   # /Users/gmp/workspaces/KiCAD_agentic/KiCAD_source_code/ is likely what we want if we want "all applications of kicad_source_code"
                                   # However, strict standard KiCad source structure usually forces the root.
                                   # Let's assume the user wants to build the repo this script is in.
KICAD_SOURCE_DIR="$SCRIPT_DIR"

# Correct path to builder relative to this script
# ../../KiCAD_packaging/kicad-mac-builder
BUILDER_DIR="$(cd "$SCRIPT_DIR/../../KiCAD_packaging/kicad-mac-builder" && pwd)"

echo "Source Dir:  $KICAD_SOURCE_DIR"
echo "Builder Dir: $BUILDER_DIR"

if [ ! -d "$BUILDER_DIR" ]; then
    echo "Error: kicad-mac-builder directory not found at $BUILDER_DIR"
    exit 1
fi

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

echo "Starting KiCad Build..."
cd "$BUILDER_DIR"

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
    if [ -d "$BUILD_OUTPUT_DIR/KiCad.app/Contents/Applications" ]; then
         rm -rf "$BUILD_OUTPUT_DIR/KiCad.app/Contents/Applications"/*.app
    fi
fi

# Use --no-retry-failed-build to fail fast if something is wrong
# Target 'kicad' builds the main suite.
# We pass the local source directory so it builds OUR code.
# -DKICAD_BUNDLE_FILENAME="KiCad_Agentic_dev" renames the output bundle
./build.py \
    --arch=$(uname -m) \
    --kicad-source-dir="$KICAD_SOURCE_DIR" \
    --target kicad \
    --extra-kicad-cmake-args='-DKICAD_BUNDLE_FILENAME="KiCad_Agentic_dev"'

# --- Explicit Install Step ---

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

pushd "$INNER_BUILD_DIR" > /dev/null
echo "Entering $INNER_BUILD_DIR"
# Run install. This installs to kicad-dest (CMAKE_INSTALL_PREFIX is set to it by builder)
make install
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

# --- Artifact Bundling ---

BUILD_OUTPUT_DIR="$BUILDER_DIR/build/kicad-dest"
KICAD_APP_BUNDLE="$BUILD_OUTPUT_DIR/KiCad.app" # CMake often ignores the bundle filename arg for the directory name itself, but let's check.
# If KICAD_BUNDLE_FILENAME is honored for the directory name, we might need to adjust. 
# Usually it makes KiCad.app and renames the DMG. Let's assume KiCad.app for now or check.
if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    # Fallback check if it was named differently
    KICAD_APP_BUNDLE="$BUILD_OUTPUT_DIR/KiCad_Agentic_dev.app"
fi

if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    echo "Error: Could not find main application bundle at $BUILD_OUTPUT_DIR/KiCad.app or KiCad_Agentic_dev.app"
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



echo "Signing bundled applications..."
# Re-sign the bundled applications to ensure validity after copying
# Use ad-hoc signing (-) which is sufficient for local development
find "$DEST_APPS_DIR" -maxdepth 1 -type d -name "*.app" | while read app_path; do
    echo "Signing $app_path..."
    codesign --force --deep --sign - "$app_path"
done

echo "Build and bundling complete."
