#!/usr/bin/env bash
set -e

# Bundle kicad-python (kipy) and dependencies into the AppDir.
# This script mirrors the functionality in mac_build_hard.sh for Linux AppImages.
#
# Configuration via environment variables:
#   ENABLE_KIPY_BUNDLE=1          Set to 0 to skip bundling
#   KIPY_SOURCE_DIR=/src/zeo-python   Path to kicad-python source
#   KICAD_SOURCE_DIR=/src/kicad       Path to KiCad/Zeo source (for proto files)
#   APPDIR=/tmp/AppDir                Target AppDir root
#   PYTHON_VERSION=3.11               Python version to target

if [[ "${ENABLE_KIPY_BUNDLE:-1}" != "1" ]]; then
    echo "[bundle_kipy] Skipping (ENABLE_KIPY_BUNDLE!=1)"
    exit 0
fi

APPDIR=${APPDIR:-/tmp/AppDir}
KIPY_SOURCE_DIR=${KIPY_SOURCE_DIR:-/src/zeo-python}
KICAD_SOURCE_DIR=${KICAD_SOURCE_DIR:-/src/kicad}
PYTHON_VERSION=${PYTHON_VERSION:-3.11}

echo "[bundle_kipy] Starting kicad-python bundling"
echo "[bundle_kipy] APPDIR: $APPDIR"
echo "[bundle_kipy] KIPY_SOURCE_DIR: $KIPY_SOURCE_DIR"
echo "[bundle_kipy] KICAD_SOURCE_DIR: $KICAD_SOURCE_DIR"
echo "[bundle_kipy] PYTHON_VERSION: $PYTHON_VERSION"

# Validate source directories. We refuse to ship an AppImage that omits kipy:
# the resulting binary would crash on first MCP launch / agent tool call with
# an opaque ImportError, and the build would not have flagged the problem.
if [ ! -d "$KIPY_SOURCE_DIR" ]; then
    echo "[bundle_kipy] ERROR: kicad-python source not found at $KIPY_SOURCE_DIR" >&2
    echo "[bundle_kipy] Pass KIPY_SOURCE_DIR explicitly or set ENABLE_KIPY_BUNDLE=0 to skip kipy." >&2
    exit 1
fi

if [ ! -d "$KICAD_SOURCE_DIR/api/proto" ]; then
    echo "[bundle_kipy] ERROR: KiCad proto sources not found at $KICAD_SOURCE_DIR/api/proto" >&2
    echo "[bundle_kipy] kipy needs these to generate its protobuf bindings." >&2
    exit 1
fi

# Determine site-packages location
SITE_PACKAGES="$APPDIR/usr/lib/python${PYTHON_VERSION}/dist-packages"
LOCAL_SITE_PACKAGES="$APPDIR/usr/local/lib/python${PYTHON_VERSION}/dist-packages"

# Create directories
mkdir -p "$SITE_PACKAGES"
mkdir -p "$LOCAL_SITE_PACKAGES"

# Copy proto files from KiCad source to kicad-python
if [ -d "$KICAD_SOURCE_DIR/api/proto" ]; then
    echo "[bundle_kipy] Copying protobuf files from KiCad source..."
    mkdir -p "$KIPY_SOURCE_DIR/kicad/api/proto"
    cp -r "$KICAD_SOURCE_DIR/api/proto/"* "$KIPY_SOURCE_DIR/kicad/api/proto/"
else
    echo "[bundle_kipy] WARNING: Proto files not found at $KICAD_SOURCE_DIR/api/proto"
fi

# Install build dependencies (poetry-core for building, mypy-protobuf and protoletariat for proto generation)
echo "[bundle_kipy] Installing build dependencies..."
pip3 install --break-system-packages --quiet poetry-core setuptools mypy-protobuf protoletariat

# Generate Python protobuf bindings
if [ -f "$KIPY_SOURCE_DIR/tools/generate_protos.py" ]; then
    echo "[bundle_kipy] Generating Python protobuf bindings..."
    echo "[bundle_kipy] Proto input files:"
    find "$KIPY_SOURCE_DIR/kicad/api/proto" -name "*.proto" 2>/dev/null || echo "  (none found)"
    pushd "$KIPY_SOURCE_DIR" > /dev/null
    python3 tools/generate_protos.py
    echo "[bundle_kipy] Generated proto files:"
    find "$KIPY_SOURCE_DIR/kipy/proto" -name "*_pb2.py" 2>/dev/null | head -20 || echo "  (none generated)"
    popd > /dev/null
else
    echo "[bundle_kipy] WARNING: generate_protos.py not found, skipping proto generation"
fi

# Install kiutils (dependency for kicad-python)
echo "[bundle_kipy] Installing kiutils..."
pip3 install --break-system-packages --target "$SITE_PACKAGES" --upgrade kiutils

# Install kicad-python dependencies
echo "[bundle_kipy] Installing kicad-python dependencies..."
pip3 install --break-system-packages --target "$SITE_PACKAGES" --upgrade \
    "protobuf>=5.0" \
    "pynng>=0.8.0" \
    typing_extensions

# Install kicad-python itself (without deps, they're already installed)
echo "[bundle_kipy] Installing kicad-python package..."
pip3 install --break-system-packages --target "$SITE_PACKAGES" --no-deps --upgrade --verbose "$KIPY_SOURCE_DIR"

# Copy generated proto files directly (pip wheel may not include them)
echo "[bundle_kipy] Copying generated proto files..."
if [ -d "$KIPY_SOURCE_DIR/kipy/proto" ]; then
    cp -r "$KIPY_SOURCE_DIR/kipy/proto/"* "$SITE_PACKAGES/kipy/proto/"
    PB2_COUNT=$(find "$SITE_PACKAGES/kipy/proto" -name '*_pb2.py' | wc -l)
    echo "[bundle_kipy] Copied $PB2_COUNT proto files to $SITE_PACKAGES/kipy/proto/"
fi

# Verify the bundle is actually importable. A "kipy directory exists" check
# isn't enough — pip can leave kipy in place while silently failing to install
# pynng/protobuf, which crashes the user at runtime. Mirror the import chain
# zeo-mcp / agent run_shell perform on launch so any breakage fails the build.
echo "[bundle_kipy] Verifying kipy import chain..."
if [ ! -d "$SITE_PACKAGES/kipy" ]; then
    echo "[bundle_kipy] ERROR: kipy not staged at $SITE_PACKAGES/kipy" >&2
    ls -la "$SITE_PACKAGES" >&2 || true
    exit 1
fi

if ! PYTHONPATH="$SITE_PACKAGES:$LOCAL_SITE_PACKAGES" python3 - <<PYEOF
import sys
sys.path.insert(0, "$SITE_PACKAGES")
sys.path.insert(0, "$LOCAL_SITE_PACKAGES")
import kipy
from kipy.client import KiCadClient
from kipy.mcp.server import run
print(f"[bundle_kipy] OK: kipy at {kipy.__file__}")
PYEOF
then
    echo "[bundle_kipy] ERROR: kipy smoke test failed — staged bundle is not importable" >&2
    echo "[bundle_kipy] Contents of $SITE_PACKAGES:" >&2
    ls -la "$SITE_PACKAGES" >&2 || true
    exit 1
fi

# Update PYTHONPATH in the environment file
RUNTIME_ENV_FILE="$APPDIR/.env-kipy"
{
    echo "# Generated by bundle_kipy.sh"
    echo "# Source this file in your wrapper script"
    echo "export PYTHONPATH=\"\${APPDIR}/usr/lib/python${PYTHON_VERSION}/dist-packages:\${APPDIR}/usr/local/lib/python${PYTHON_VERSION}/dist-packages:\${PYTHONPATH}\""
} > "$RUNTIME_ENV_FILE"

echo "[bundle_kipy] Complete."
