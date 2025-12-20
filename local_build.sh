#!/bin/bash
set -e

# Recommended debug build type for development
BUILD_TYPE=Debug

# Assume we are in the source root
BUILD_DIR="build/local_debug"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Standard macOS build arguments (adjust paths as needed)
# Using standard brew prefix for dependencies
BREW_PREFIX=$(brew --prefix)

cmake \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_PREFIX_PATH="$BREW_PREFIX" \
    -DKICAD_SCRIPTING_WXPYTHON=ON \
    -DKICAD_USE_EGL=ON \
    -DKICAD_SPICE=OFF \
    -DKICAD_BUILD_QA_TESTS=OFF \
    ../../

# Build KiCad and the Agent
# Adjust -j to your core count
make -j$(sysctl -n hw.ncpu) common kicad kicad_agent

echo "Build complete. To install/run:"
echo "  cd $BUILD_DIR"
echo "  make install"
