#!/bin/bash
set -e

# Zeo Linux AppImage Build Script (appimage_build.sh --package)
# Creates a shareable AppImage with Zeo (KiCad), Agent, Terminal, and all dependencies
#
# Usage:
#   ./appimage_build.sh --package                    # Full build + AppImage (default)
#   ./appimage_build.sh --package --skip-build       # AppImage only (requires previous build)
#   ./appimage_build.sh --package --release "1.0"    # Named release AppImage
#   ./appimage_build.sh --package --light            # Build light version (no 3D packages)
#   ./appimage_build.sh --package --config <file>    # Use custom config file
#   ./appimage_build.sh --package --build-deps       # Build dependency images locally (wx, wxpython)

# --- Configuration ---

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

KICAD_SOURCE_DIR="$WORKSPACE_DIR/src/zeo"
APPIMAGE_DIR="$WORKSPACE_DIR/packaging/kicad-appimage"
LIBRARIES_DIR="$WORKSPACE_DIR/libraries"
KICAD_PYTHON_DIR="$WORKSPACE_DIR/src/zeo-python"
FREEROUTING_DIR="$WORKSPACE_DIR/tools/freerouting"

BUILD_DIR="$APPIMAGE_DIR/build"
OUTPUT_DIR="$BUILD_DIR/output"
CONFIG_FILE="$APPIMAGE_DIR/configs/zeo.json"

# Parse arguments
SKIP_BUILD=false
RELEASE_NAME=""
BUILD_LIGHT=false
FORCE_BUILD=false
BUILD_DEPS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --release)
            RELEASE_NAME="$2"
            shift 2
            ;;
        --light)
            BUILD_LIGHT=true
            shift
            ;;
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --force)
            FORCE_BUILD=true
            shift
            ;;
        --build-deps)
            BUILD_DEPS=true
            shift
            ;;
        --help|-h)
            echo "Zeo AppImage Builder"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --skip-build          Skip building, only create AppImage from existing build"
            echo "  --release NAME        Set release name (e.g., '1.0', 'beta')"
            echo "  --light               Build light version without 3D packages"
            echo "  --config FILE         Use custom config file (default: configs/zeo.json)"
            echo "  --force               Force rebuild of all dependencies"
            echo "  --build-deps          Build dependency images (base, wx, wxpython) locally"
            echo "                        instead of pulling from the KiCad GitLab registry"
            echo "  --help, -h            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                           # Full build + AppImage"
            echo "  $0 --skip-build              # Create AppImage from existing build"
            echo "  $0 --release 1.0             # Create release AppImage named 'zeo-1.0.AppImage'"
            echo "  $0 --light                   # Build without 3D packages for smaller size"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "=============================================="
echo "Zeo AppImage Builder"
echo "=============================================="
echo "Workspace:     $WORKSPACE_DIR"
echo "Source Dir:    $KICAD_SOURCE_DIR"
echo "AppImage Dir:  $APPIMAGE_DIR"
echo "Libraries Dir: $LIBRARIES_DIR"
echo "Python Dir:    $KICAD_PYTHON_DIR"
echo "Output Dir:    $OUTPUT_DIR"
echo "Config File:   $CONFIG_FILE"
echo "Skip Build:    $SKIP_BUILD"
echo "Release Name:  ${RELEASE_NAME:-<auto>}"
echo "Light Build:   $BUILD_LIGHT"
echo "=============================================="

# --- Validation ---

if [ ! -d "$KICAD_SOURCE_DIR" ]; then
    echo "Error: KiCad source directory not found at $KICAD_SOURCE_DIR"
    exit 1
fi

if [ ! -d "$APPIMAGE_DIR" ]; then
    echo "Error: kicad-appimage directory not found at $APPIMAGE_DIR"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file not found at $CONFIG_FILE"
    exit 1
fi

# --- Docker Check ---

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is required but not installed."
    echo ""
    echo "Please install Docker:"
    echo "  - Ubuntu/Debian: sudo apt install docker.io"
    echo "  - Or: https://docs.docker.com/engine/install/"
    exit 1
fi

# Check if Docker daemon is running
if ! docker info >/dev/null 2>&1; then
    echo "Error: Docker daemon is not running."
    echo ""
    echo "Start Docker with:"
    echo "  sudo systemctl start docker"
    exit 1
fi

# --- Load Config ---

echo ""
echo "=========================================="
echo "Loading configuration from $CONFIG_FILE"
echo "=========================================="

# Load config variables
eval "$(cd "$APPIMAGE_DIR" && ./scripts/load_config.sh "$CONFIG_FILE")"

# Named releases get -DZEO_RELEASE=ON so ZEO_BASE_URL points at https://www.zeo.dev
# (see src/zeo/include/zeo/zeo_constants.h). Dev/unnamed builds stay on staging.
if [ -n "$RELEASE_NAME" ]; then
    CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS:+$CMAKE_EXTRA_ARGS }-DZEO_RELEASE=ON"
fi

# Display loaded config
echo "Build Type:    ${KICAD_BUILD_TYPE:-Nightly}"
echo "Bundle Name:   ${KICAD_BUNDLE_NAME:-Zeo}"
echo "KiCad Branch:  ${KICAD_BRANCH:-local}"
echo "WX Version:    ${WX_VERSION:-default}"
echo "Python:        ${PYTHON_VERSION:-default}"
echo "Kipy Bundle:   ${ENABLE_KIPY_BUNDLE:-0}"
echo "Freerouting:   ${ENABLE_FREEROUTING_BUNDLE:-0}"
if [ -n "$RELEASE_NAME" ]; then
    echo "Release:       yes (production URLs)"
else
    echo "Release:       no  (staging URLs)"
fi

# --- Build Dependency Images Locally (optional) ---

REGISTRY="registry.gitlab.com/kicad/packaging/kicad-appimage"

if [ "$BUILD_DEPS" = true ] && [ "$SKIP_BUILD" = false ]; then
    echo ""
    echo "=========================================="
    echo "Building dependency images locally"
    echo "=========================================="

    cd "$APPIMAGE_DIR"

    _dep_nocache=""
    [ "$FORCE_BUILD" = true ] && _dep_nocache="--no-cache"

    # 1. Base image
    echo "--- Building base image ---"
    docker build $_dep_nocache \
        -t "${REGISTRY}/base:latest" \
        -f docker/Dockerfile.base .

    # 2. wxWidgets (requires base)
    echo "--- Building wx ${WX_VERSION:-3.2.9} ---"
    docker build $_dep_nocache \
        --build-arg "BASE_IMAGE=${REGISTRY}/base:latest" \
        --build-arg "WX_VERSION=${WX_VERSION:-3.2.9}" \
        -t "${REGISTRY}/wx:latest" \
        -f docker/Dockerfile.wx .

    # 3. wxPython (requires base + wx)
    echo "--- Building wxpython ${WXPYTHON_VERSION:-4.2.4} ---"
    docker build $_dep_nocache \
        --build-arg "BASE_IMAGE=${REGISTRY}/base:latest" \
        --build-arg "WX_IMAGE=${REGISTRY}/wx:latest" \
        --build-arg "WXPYTHON_VERSION=${WXPYTHON_VERSION:-4.2.4}" \
        -t "${REGISTRY}/wxpython:latest" \
        -f docker/Dockerfile.wxpython .

    echo "Dependency images built successfully."
fi

# --- Build Phase ---

if [ "$SKIP_BUILD" = false ]; then
    echo ""
    echo "=========================================="
    echo "PHASE 1: Building KiCad/Zeo with Docker"
    echo "=========================================="

    mkdir -p "$BUILD_DIR"
    mkdir -p "$OUTPUT_DIR"

    cd "$APPIMAGE_DIR"

    # Determine target stage
    if [ "$BUILD_LIGHT" = true ]; then
        TARGET_STAGE="build-kicad-light"
        APPIMAGE_PREFIX="${KICAD_BUNDLE_NAME:-zeo}-lite"
    else
        TARGET_STAGE="build-kicad"
        APPIMAGE_PREFIX="${KICAD_BUNDLE_NAME:-zeo}"
    fi

    # Build Docker args
    DOCKER_BUILD_ARGS=(
        --build-arg "KICAD_BUILD_TYPE=${KICAD_BUILD_TYPE:-Zeo}"
        --build-arg "KICAD_BUILD_RELEASE=${RELEASE_NAME:-dev}"
        --build-arg "KICAD_BUNDLE_NAME=${KICAD_BUNDLE_NAME:-Zeo}"
        --build-arg "KICAD_BUILD_MAJVERSION=10"
        --build-arg "APPIMAGE_PREFIX=$APPIMAGE_PREFIX"
    )

    # Add dependency image tags if specified in config.
    # The KiCad GitLab registry only publishes version-specific tags for CI
    # pipelines.  For local builds the Dockerfile defaults to "latest", which
    # is the only tag publicly available.  Only override the defaults when
    # the requested tag actually exists in the registry (checked via manifest
    # inspect) so that a missing CI-only tag does not break the build.
    for _dep_var in WX_VERSION:WX_TAG WXPYTHON_VERSION:WXPYTHON_TAG OCCT_VERSION:OCCT_TAG NGSPICE_VERSION:NGSPICE_TAG; do
        _cfg_var="${_dep_var%%:*}"
        _arg_name="${_dep_var##*:}"
        eval "_val=\${$_cfg_var:-}"
        if [ -n "$_val" ]; then
            _img="${_arg_name%_TAG}"                         # e.g. WX, OCCT
            _img_lower="$(echo "$_img" | tr '[:upper:]' '[:lower:]')"
            _full="registry.gitlab.com/kicad/packaging/kicad-appimage/${_img_lower}:${_val}"
            if docker manifest inspect "$_full" >/dev/null 2>&1; then
                DOCKER_BUILD_ARGS+=(--build-arg "${_arg_name}=${_val}")
            else
                echo "Note: Image tag ${_img_lower}:${_val} not found in registry, using 'latest'"
            fi
        fi
    done

    # Add Python configuration
    [ -n "$PYTHON_VERSION" ] && DOCKER_BUILD_ARGS+=(--build-arg "PYTHON_VERSION=$PYTHON_VERSION")
    [ -n "$APT_EXTRA_PACKAGES" ] && DOCKER_BUILD_ARGS+=(--build-arg "APT_EXTRA_PACKAGES=$APT_EXTRA_PACKAGES")

    # Add CMake extra args
    [ -n "$CMAKE_EXTRA_ARGS" ] && DOCKER_BUILD_ARGS+=(--build-arg "CMAKE_EXTRA_ARGS=$CMAKE_EXTRA_ARGS")

    # Add patches
    [ -n "$KICAD_PATCHES" ] && DOCKER_BUILD_ARGS+=(--build-arg "KICAD_PATCHES=$KICAD_PATCHES")

    # Add Zeo-specific bundle flags
    [ "$ENABLE_KIPY_BUNDLE" = "1" ] && DOCKER_BUILD_ARGS+=(--build-arg "ENABLE_KIPY_BUNDLE=1")
    [ "$ENABLE_FREEROUTING_BUNDLE" = "1" ] && DOCKER_BUILD_ARGS+=(--build-arg "ENABLE_FREEROUTING_BUNDLE=1")

    # Force rebuild if requested
    [ "$FORCE_BUILD" = true ] && DOCKER_BUILD_ARGS+=(--no-cache)

    # Build contexts for local sources.
    # zeo-python-src and freerouting-src are always provided (empty dir fallback)
    # so that COPY --from=<context> in the Dockerfile doesn't fail.
    # The bundle scripts check ENABLE_*_BUNDLE and skip if disabled.
    _empty_ctx=$(mktemp -d)
    trap "rm -rf $_empty_ctx" EXIT

    _zeo_python_ctx="$_empty_ctx"
    _freerouting_ctx="$_empty_ctx"

    if [ -d "$KICAD_PYTHON_DIR" ]; then
        _zeo_python_ctx="$KICAD_PYTHON_DIR"
    else
        echo "Warning: zeo-python not found at $KICAD_PYTHON_DIR, kipy will not be bundled"
    fi

    if [ -d "$FREEROUTING_DIR" ]; then
        _freerouting_ctx="$FREEROUTING_DIR"
    else
        echo "Warning: freerouting not found at $FREEROUTING_DIR, freerouting will not be bundled"
    fi

    BUILD_CONTEXTS=(
        --build-context "kicad-src=$KICAD_SOURCE_DIR"
        --build-context "zeo-python-src=$_zeo_python_ctx"
        --build-context "freerouting-src=$_freerouting_ctx"
    )

    echo ""
    echo "Building Docker image..."
    echo "Target: $TARGET_STAGE"
    echo "Docker args: ${DOCKER_BUILD_ARGS[*]}"
    echo ""

    # Run Docker build
    docker build \
        "${DOCKER_BUILD_ARGS[@]}" \
        "${BUILD_CONTEXTS[@]}" \
        --target "$TARGET_STAGE" \
        --output "type=local,dest=$OUTPUT_DIR" \
        -f Dockerfile \
        .

    echo ""
    echo "Build phase complete."
else
    echo ""
    echo "Skipping build phase (--skip-build specified)"

    # Verify build exists
    if [ ! -d "$OUTPUT_DIR" ] || [ -z "$(ls -A "$OUTPUT_DIR"/*.AppImage 2>/dev/null)" ]; then
        echo "Error: No existing AppImage found in $OUTPUT_DIR"
        echo "Run without --skip-build first."
        exit 1
    fi
fi

# --- Rename and Finalize ---

echo ""
echo "=========================================="
echo "PHASE 2: Finalizing AppImage"
echo "=========================================="

# Find the built AppImage
APPIMAGE_PATH=$(ls -t "$OUTPUT_DIR"/*.AppImage 2>/dev/null | head -1)

if [ -z "$APPIMAGE_PATH" ]; then
    echo "Error: No AppImage found in $OUTPUT_DIR"
    exit 1
fi

echo "Found AppImage: $APPIMAGE_PATH"

# Generate final name
NOW=$(date +%Y%m%d-%H%M%S)
KICAD_GIT_REV=$(cd "$KICAD_SOURCE_DIR" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
ARCH=$(uname -m)

if [ -n "$RELEASE_NAME" ]; then
    FINAL_NAME="zeo-${RELEASE_NAME}-${ARCH}.AppImage"
else
    FINAL_NAME="zeo-${NOW}-${KICAD_GIT_REV}-${ARCH}.AppImage"
fi

FINAL_PATH="$OUTPUT_DIR/$FINAL_NAME"

# Rename if different
if [ "$APPIMAGE_PATH" != "$FINAL_PATH" ]; then
    echo "Renaming to: $FINAL_NAME"
    mv "$APPIMAGE_PATH" "$FINAL_PATH"
    APPIMAGE_PATH="$FINAL_PATH"
fi

# Make executable
chmod +x "$APPIMAGE_PATH"

# Display final info
echo ""
echo "=========================================="
echo "APPIMAGE CREATION COMPLETE"
echo "=========================================="
echo ""
echo "AppImage Location: $APPIMAGE_PATH"
echo "AppImage Size:     $(du -h "$APPIMAGE_PATH" | cut -f1)"
echo ""

# Verify AppImage
echo "Verifying AppImage..."
if "$APPIMAGE_PATH" --appimage-version >/dev/null 2>&1; then
    echo "AppImage verification: OK"
else
    echo "Warning: AppImage verification failed (may still work)"
fi

# Install kipy to host Python for CLI tools (zeo shell/tool/monitor).
# Uses Docker base image for proto generation since the host may not have protoc.
if [ -d "$KICAD_PYTHON_DIR" ]; then
    echo ""
    echo "==========================================="
    echo "Installing kipy to host Python (CLI tools)"
    echo "==========================================="

    KIPY_SITE="$BUILD_DIR/_python_site"
    mkdir -p "$KIPY_SITE"

    # Generate protos and install kipy+deps inside Docker, output to host-mounted dir
    docker run --rm \
        -v "$KICAD_SOURCE_DIR:/src/kicad:ro" \
        -v "$KICAD_PYTHON_DIR:/src/zeo-python:ro" \
        -v "$KIPY_SITE:/output" \
        registry.gitlab.com/kicad/packaging/kicad-appimage/base:latest \
        bash -c '
            set -e
            cp -r /src/zeo-python /tmp/zeo-python
            mkdir -p /tmp/zeo-python/kicad/api/proto
            cp -r /src/kicad/api/proto/* /tmp/zeo-python/kicad/api/proto/ 2>/dev/null || true
            cd /tmp/zeo-python
            # generate_protos.py has built-in fallbacks: it skips --mypy_out if
            # protoc-gen-mypy is absent, and uses its own _fix_imports if protol
            # is absent. Do NOT install mypy-protobuf/protoletariat here — their
            # version requirements conflict with each other on protobuf.
            [ -f tools/generate_protos.py ] && python3 tools/generate_protos.py
            if [ ! -f kipy/kicad_api_version.py ]; then
                echo "KICAD_API_VERSION = \"dev\"" > kipy/kicad_api_version.py
            fi
            pip3 install --break-system-packages --target /output --upgrade --quiet \
                "protobuf>=6.33" "pynng>=0.8.0" typing_extensions textual mcp matplotlib
            pip3 install --break-system-packages --target /output --no-deps --upgrade --quiet /tmp/zeo-python
            PYTHONPATH=/output python3 -c "import kipy; print(\"[kipy] import OK\")"
        '

    # Register on host Python's path via .pth file
    PY_SITE=$(python3 -c "import site; print(site.getusersitepackages())" 2>/dev/null || echo "")
    if [ -n "$PY_SITE" ]; then
        mkdir -p "$PY_SITE"
        echo "$KIPY_SITE" > "$PY_SITE/kipy.pth"
    fi

    if python3 -c "import kipy" 2>/dev/null; then
        echo "kipy installed successfully."
    else
        echo "Warning: kipy installation failed. Check Docker output above."
    fi
fi

echo ""
echo "To run Zeo:"
echo "  $APPIMAGE_PATH"
echo ""
echo "To run with specific app:"
echo "  $APPIMAGE_PATH pcbnew"
echo "  $APPIMAGE_PATH eeschema"
echo "  $APPIMAGE_PATH agent"
echo "  $APPIMAGE_PATH terminal"
echo ""
echo "To extract AppImage contents:"
echo "  $APPIMAGE_PATH --appimage-extract"
echo ""
echo "=============================================="
