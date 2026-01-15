#!/bin/bash
set -e

# KiCad macOS Build Wrapper using kicad-mac-builder

# --- Configuration ---

# Get absolute path to the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Define paths relative to the script
# Configured based on user workspaces:
# code/kicad-agent/mac_build.sh (SCRIPT_DIR)
# packaging/kicad-mac-builder (BUILDER_DIR)
# libraries/kicad-symbols, kicad-footprints, etc. (LIBRARIES_DIR)
KICAD_SOURCE_DIR="$SCRIPT_DIR"

# Correct path to builder relative to this script
# ../../packaging/kicad-mac-builder
BUILDER_DIR="$(cd "$SCRIPT_DIR/../../packaging/kicad-mac-builder" && pwd)"

# Path to KiCad libraries (symbols, footprints, 3D models, templates)
# ../../libraries
LIBRARIES_DIR="$(cd "$SCRIPT_DIR/../../libraries" && pwd)"

echo "Source Dir:    $KICAD_SOURCE_DIR"
echo "Builder Dir:   $BUILDER_DIR"
echo "Libraries Dir: $LIBRARIES_DIR"

if [ ! -d "$BUILDER_DIR" ]; then
    echo "Error: kicad-mac-builder directory not found at $BUILDER_DIR"
    exit 1
fi

if [ ! -d "$LIBRARIES_DIR" ]; then
    echo "Warning: Libraries directory not found at $LIBRARIES_DIR"
    echo "Global libraries (symbols, footprints, 3D models) will not be available."
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
    --extra-kicad-cmake-args='-DKICAD_BUNDLE_FILENAME="KiCad_Agentic_dev" -DKICAD_SCRIPTING=ON -DKICAD_SCRIPTING_MODULES=ON -DKICAD_SCRIPTING_WXPYTHON=ON -DKICAD_IPC_API=ON'

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

# --- KiCad Libraries Build ---
# Build and install global libraries (symbols, footprints, 3D models, templates)
# These are built from the local libraries directory and installed into SharedSupport

if [ -d "$LIBRARIES_DIR" ]; then
    SHARED_SUPPORT="$DEST_DIR/KiCad.app/Contents/SharedSupport"
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
        cmake "$lib_path" -DCMAKE_INSTALL_PREFIX="$SHARED_SUPPORT" > /dev/null
        make install > /dev/null
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



echo "Bundling kiutils..."
# Install kiutils into the bundle's site-packages
# Path determined from previous exploration: Python.framework/Versions/Current/lib/python3.9/site-packages
# We use the system pip/python to install into the target directory.
# 'Current' symlink should be reliable given the framework structure.

PYTHON_SITE_PACKAGES="$KICAD_APP_BUNDLE/Contents/Frameworks/Python.framework/Versions/Current/lib/python3.9/site-packages"

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

KICAD_PYTHON_DIR="$(cd "$SCRIPT_DIR/../kicad-python" && pwd)"

if [ -d "$KICAD_PYTHON_DIR" ]; then
    echo "Processing kicad-python at $KICAD_PYTHON_DIR"

    # Copy protos from kicad-agent (source) to kicad-python
    echo "Copying protobuf files..."
    mkdir -p "$KICAD_PYTHON_DIR/kicad/api/proto"
    cp -r "$KICAD_SOURCE_DIR/api/proto/"* "$KICAD_PYTHON_DIR/kicad/api/proto/"
    
    # host build deps
    echo "Installing build dependencies..."
    python3 -m pip install mypy-protobuf protoletariat

    # Run proto generation
    echo "Generating python protobuf bindings..."
    pushd "$KICAD_PYTHON_DIR" > /dev/null
    python3 tools/generate_protos.py
    popd > /dev/null
    
    # Install into bundle
    if [ -d "$PYTHON_SITE_PACKAGES" ]; then
        echo "Installing kicad-python dependencies to $PYTHON_SITE_PACKAGES"
        # Step 1: Install dependencies with binary wheels for cp39
        python3 -m pip install --target "$PYTHON_SITE_PACKAGES" --upgrade --python-version 3.9 --only-binary=:all: "protobuf>=6.33" "pynng>=0.8.0" typing_extensions

        echo "Installing kicad-python package..."
        # Step 2: Install kicad-python source without deps (deps are already installed)
        python3 -m pip install --target "$PYTHON_SITE_PACKAGES" --no-deps --upgrade "$KICAD_PYTHON_DIR"
    else
         echo "Warning: Python site-packages directory not found, skipping kicad-python install."
    fi

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

echo "Build and bundling complete."
