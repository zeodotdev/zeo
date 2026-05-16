#!/bin/bash
set -e
set -o pipefail

# Zeo Linux Fast Build Script (appimage_build.sh --fast)
# Single command that auto-sets-up, builds, and launches Zeo in Docker.
# Auto-detects when the Docker image needs rebuilding via Dockerfile hashing.
# Typical rebuild: 5-15s for one changed .cpp file.

# --- Configuration ---

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

KICAD_SOURCE_DIR="$WORKSPACE_DIR/src/zeo"
KICAD_PYTHON_DIR="$WORKSPACE_DIR/src/zeo-python"
APPIMAGE_DIR="$WORKSPACE_DIR/packaging/kicad-appimage"
LIBRARIES_DIR="$WORKSPACE_DIR/libraries"
CONFIG_FILE="$APPIMAGE_DIR/configs/zeo.json"

DEV_IMAGE="zeo-dev"
BUILD_DIR="$APPIMAGE_DIR/build/dev-build"

# Detect the real user's home directory.  When running as root (which is
# typical for Docker), $HOME is /root.  Derive the actual user home from
# the workspace path (e.g. /home/ganesh/workspaces/... → /home/ganesh).
REAL_HOME=$(echo "$WORKSPACE_DIR" | grep -oP '^/home/[^/]+')
if [ -z "$REAL_HOME" ]; then
  REAL_HOME="$HOME"
fi

NCPU=$(nproc)
REGISTRY="registry.gitlab.com/kicad/packaging/kicad-appimage"

# --- Inline Dockerfile ---
# Stored as a variable so we can hash it for auto-rebuild detection.

DEV_DOCKERFILE="FROM ${REGISTRY}/base:latest

# Compile-time and runtime dependencies (same as kicad-build stage in Dockerfile)
COPY --from=${REGISTRY}/wx:latest / /
COPY --from=${REGISTRY}/wxpython:latest / /
COPY --from=${REGISTRY}/ngspice:latest / /
COPY --from=${REGISTRY}/occt:latest / /

# Symbol and footprint libraries for realistic testing.
# The libs image installs to /usr/installtemp/share; move to /usr/share
# so Zeo finds them at the default install prefix.
COPY --from=${REGISTRY}/libs:latest /usr/installtemp/share /usr/share

# The libs:latest image ships KiCad 9 library tables that reference
# \\\${KICAD9_*} env vars. Zeo is KiCad 10, which registers KICAD10_*.
# Rewrite the global library tables so new installs get the right vars.
RUN sed -i 's/KICAD9_/KICAD10_/g' /usr/share/kicad/template/sym-lib-table /usr/share/kicad/template/fp-lib-table 2>/dev/null || true

RUN ldconfig

# Debuggers + common dev tools for the integrated terminal.
# The Zeo terminal runs inside Docker, so host-installed tools (nvim, htop,
# etc.) are not available unless installed here.
RUN apt-get update && apt-get install -y --no-install-recommends \
    gdb lldb librsvg2-bin \
    neovim htop tmux less curl wget git nano \
    && rm -rf /var/lib/apt/lists/*

# Minimal xdg-open shim. Installing xdg-utils via apt can pull in
# dependencies that conflict with pre-compiled wx/GTK from registry images.
# This shim uses the D-Bus portal API (gdbus is part of glib, already
# available) to ask the HOST desktop to open URLs in its default browser.
RUN printf '#!/bin/sh\ngdbus call --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.portal.OpenURI.OpenURI \"\" \"\$1\" {} 2>/dev/null && exit 0\ngio open \"\$@\" 2>/dev/null && exit 0\necho \"Open in browser: \$1\" >&2\n' > /usr/bin/xdg-open && chmod +x /usr/bin/xdg-open"

# --- Argument Parsing ---

BUILD_TARGET=""
DO_SETUP=false
DO_BUILD_ONLY=false
DO_QUIT=false
DO_DEBUG=false
DO_GDB=false
DO_LLDB=false
DO_CLEAN=false
DO_RECONFIGURE=false
DO_PYTHON=false

print_usage() {
  echo "Usage: $0 [OPTIONS]"
  echo ""
  echo "Fast incremental Zeo build + launch using Docker."
  echo "Auto-builds the dev Docker image on first run or when the Dockerfile changes."
  echo "Loads build config from: configs/zeo.json"
  echo ""
  echo "Options:"
  echo "  (default)          Build and launch Zeo"
  echo "  --build-only       Build without launching"
  echo "  --setup            Force rebuild of the dev Docker image"
  echo "  --agent            Build agent target only"
  echo "  --target <name>    Build a specific ninja target"
  echo "  --python           Force reinstall of kipy (kicad-python)"
  echo "  --launch           (no-op, launch is now the default)"
  echo "  --quit             Kill running Zeo instances before build"
  echo "  --debug            Launch with WXTRACE=KICAD_AGENT"
  echo "  --gdb              Launch under gdb debugger"
  echo "  --lldb             Launch under lldb debugger"
  echo "  --reconfigure      Force cmake reconfigure on next build"
  echo "  --clean            Remove persistent build directory and start fresh"
  echo "  --help             Show this help message"
}

while [[ $# -gt 0 ]]; do
  case $1 in
  --setup)
    DO_SETUP=true
    shift
    ;;
  --build-only)
    DO_BUILD_ONLY=true
    shift
    ;;
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
  --launch)
    # no-op for backward compat; launch is now the default
    shift
    ;;
  --quit)
    DO_QUIT=true
    shift
    ;;
  --debug)
    DO_DEBUG=true
    shift
    ;;
  --gdb)
    DO_GDB=true
    shift
    ;;
  --lldb)
    DO_LLDB=true
    shift
    ;;
  --reconfigure)
    DO_RECONFIGURE=true
    shift
    ;;
  --clean)
    DO_CLEAN=true
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

# --- Load Config ---

load_config() {
  if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file not found at $CONFIG_FILE"
    exit 1
  fi

  eval "$(cd "$APPIMAGE_DIR" && ./scripts/load_config.sh "$CONFIG_FILE")"
}

# --- Auto-Setup: Build Dev Docker Image with Fingerprinting ---

ensure_dev_image() {
  local hash_file="$BUILD_DIR/.dev-image-hash"
  local current_hash
  current_hash=$(echo "$DEV_DOCKERFILE" | sha256sum | awk '{print $1}')

  local needs_build=false

  if $DO_SETUP; then
    log "Forced image rebuild requested (--setup)."
    needs_build=true
  elif ! docker image inspect "$DEV_IMAGE" >/dev/null 2>&1; then
    log "Dev image '$DEV_IMAGE' not found, building..."
    needs_build=true
  elif [ ! -f "$hash_file" ]; then
    log "No image hash on record, rebuilding to be safe..."
    needs_build=true
  elif [ "$(cat "$hash_file")" != "$current_hash" ]; then
    log "Dockerfile changed (hash mismatch), rebuilding image..."
    needs_build=true
  fi

  if ! $needs_build; then
    return 0
  fi

  log "Building dev Docker image ($DEV_IMAGE)..."
  log "This pulls dependency images from the KiCad registry (may take a few minutes first time)."
  echo ""

  TMPCTX=$(mktemp -d)
  trap "rm -rf $TMPCTX" EXIT
  echo "$DEV_DOCKERFILE" | docker build -t "$DEV_IMAGE" -f - "$TMPCTX"

  mkdir -p "$BUILD_DIR"
  echo "$current_hash" >"$hash_file"

  log "Dev image '$DEV_IMAGE' built successfully."
}

# --- Validation ---

validate_prerequisites() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is required but not installed."
    echo "  sudo apt install docker.io"
    exit 1
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "Error: Docker daemon is not running."
    echo "  sudo systemctl start docker"
    exit 1
  fi
  if [ ! -d "$KICAD_SOURCE_DIR" ]; then
    echo "Error: Zeo source directory not found at $KICAD_SOURCE_DIR"
    exit 1
  fi
}

quit_zeo() {
  local found=false
  pgrep -x zeo >/dev/null 2>&1 && found=true
  pgrep -x kicad >/dev/null 2>&1 && found=true
  if ! $found; then
    return
  fi
  log "Killing running Zeo instances..."
  pkill -x zeo 2>/dev/null || true
  pkill -x kicad 2>/dev/null || true
  for i in $(seq 1 10); do
    if ! pgrep -x zeo >/dev/null 2>&1 && ! pgrep -x kicad >/dev/null 2>&1; then
      return
    fi
    sleep 0.5
  done
  log "Force killing..."
  pkill -9 -x zeo 2>/dev/null || true
  pkill -9 -x kicad 2>/dev/null || true
  sleep 0.5
}

# --- Kipy Install ---

# Install kipy packages to $BUILD_DIR/_python_site (host-mounted, persistent).
# Runs in a separate container so the packages survive container exit.
# The launch container then creates a .pth file to make Python find them.
install_kipy() {
  local python_site="$BUILD_DIR/_python_site"
  local stamp_file="$BUILD_DIR/.kipy-stamp"
  local force=$1

  if [ ! -d "$KICAD_PYTHON_DIR" ]; then
    log "Warning: zeo-python directory not found at $KICAD_PYTHON_DIR, skipping kipy."
    return 0
  fi

  # Check if install is needed: forced, missing packages, or source changed
  if ! $force && [ -d "$python_site/kipy" ] && [ -f "$stamp_file" ]; then
    if [ -z "$(find "$KICAD_PYTHON_DIR" -newer "$stamp_file" -name '*.py' -print -quit 2>/dev/null)" ]; then
      log "kipy is up to date (use --python to force reinstall)."
      return 0
    fi
    log "kipy source changed, reinstalling..."
  fi

  log "Installing kicad-python (kipy) to $python_site ..."
  mkdir -p "$python_site"

  docker run --rm \
    --network host \
    -v "$KICAD_SOURCE_DIR:/src/kicad:ro" \
    -v "$KICAD_PYTHON_DIR:/src/zeo-python:ro" \
    -v "$python_site:/build/_python_site" \
    "$DEV_IMAGE" \
    bash -c '
            set -e
            # Copy proto files from zeo to zeo-python
            mkdir -p /tmp/zeo-python
            cp -r /src/zeo-python/* /tmp/zeo-python/
            mkdir -p /tmp/zeo-python/kicad/api/proto
            cp -r /src/kicad/api/proto/* /tmp/zeo-python/kicad/api/proto/

            # Generate python protobuf bindings
            cd /tmp/zeo-python
            if [ -f tools/generate_protos.py ]; then
                python3 tools/generate_protos.py
                # Verify at least one _pb2.py was generated
                PB2_COUNT=$(find /tmp/zeo-python/kipy/proto -name "*_pb2.py" 2>/dev/null | wc -l)
                if [ "$PB2_COUNT" -eq 0 ]; then
                    echo "ERROR: Proto generation produced no _pb2.py files. Check protoc output above."
                    exit 1
                fi
                echo "[FAST] Generated $PB2_COUNT protobuf Python files."
            fi

            # Generate kicad_api_version.py (normally done by build.py in the git submodule)
            if [ ! -f kipy/kicad_api_version.py ]; then
                VERSION=$(cd /src/kicad && git describe --long 2>/dev/null || echo "dev")
                echo "KICAD_API_VERSION = \"$VERSION\"" > kipy/kicad_api_version.py
            fi

            # Install kipy deps + package into the persistent mounted dir
            python3 -m pip install --break-system-packages \
                --target /build/_python_site --upgrade --quiet \
                "protobuf>=6.33" "pynng>=0.8.0" typing_extensions textual mcp matplotlib
            python3 -m pip install --break-system-packages \
                --target /build/_python_site --no-deps --upgrade --quiet \
                /tmp/zeo-python

            # Verify
            PYTHONPATH=/build/_python_site python3 -c "import kipy; print(\"[FAST] kipy import OK\")"
        '

  touch "$stamp_file"
  log "kicad-python installed."
}

# --- Main ---

START_TIME=$(date +%s)

echo "=============================================="
echo "Zeo Fast Build"
echo "=============================================="

# Handle --clean (exit early)
if $DO_CLEAN; then
  log "Cleaning build directory..."
  rm -rf "$BUILD_DIR"
  log "Clean complete."
  exit 0
fi

# Load cmake args from zeo.json config (same config the AppImage build uses)
load_config

# Display config
if [ -n "$BUILD_TARGET" ]; then
  echo "Target:   $BUILD_TARGET"
else
  echo "Target:   (all)"
fi
$DO_QUIT && echo "Quit:     yes"
$DO_PYTHON && echo "Python:   force reinstall"
$DO_BUILD_ONLY && echo "Mode:     build-only"
$DO_DEBUG && echo "Debug:    yes"
$DO_GDB && echo "GDB:      yes"
$DO_LLDB && echo "LLDB:     yes"
echo "CPUs:     $NCPU"
echo "Build:    $BUILD_DIR"
echo "Config:   $CONFIG_FILE"
echo "=============================================="

validate_prerequisites

# Auto-setup: ensure dev image exists and is up to date
mkdir -p "$BUILD_DIR"
ensure_dev_image

# Quit running instances
if $DO_QUIT; then
  quit_zeo
fi

# Handle --reconfigure
if $DO_RECONFIGURE && [ -d "$BUILD_DIR" ]; then
  log "Removing cmake cache for reconfigure..."
  rm -f "$BUILD_DIR/CMakeCache.txt"
  rm -f "$BUILD_DIR/build.ninja"
  rm -rf "$BUILD_DIR/CMakeFiles"
fi

# --- Compile ---

# Build cmake configure command (only runs if build.ninja doesn't exist)
# Uses the same flags as the AppImage Dockerfile + cmake_extra_args from zeo.json
# Plus KICAD_BUNDLE_FILENAME=Zeo to match the mac builder
CMAKE_CONFIGURE=""
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  log "Configuring cmake (first time or after clean/reconfigure)..."
  CMAKE_CONFIGURE="cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DDEFAULT_INSTALL_PATH=/usr \
        -DKICAD_USE_CMAKE_FINDPROTOBUF=ON \
        -DKICAD_BUNDLE_FILENAME=Zeo \
        ${CMAKE_EXTRA_ARGS:-} \
        /src/kicad && "
fi

NINJA_CMD="ninja -j$NCPU"
if [ -n "$BUILD_TARGET" ]; then
  NINJA_CMD="$NINJA_CMD $BUILD_TARGET"
fi

log "Building in $BUILD_DIR ..."

if ! docker run --rm \
  -v "$KICAD_SOURCE_DIR:/src/kicad" \
  -v "$BUILD_DIR:/build" \
  -w /build \
  "$DEV_IMAGE" \
  bash -c "git config --global --add safe.directory /src/kicad && ${CMAKE_CONFIGURE}${NINJA_CMD}"; then
  log "ERROR: Build failed"
  exit 1
fi

log "Build complete."

# --- Launch (default unless --build-only) ---

if ! $DO_BUILD_ONLY; then
  # Install kipy to persistent host-mounted dir (skips if up to date)
  install_kipy $DO_PYTHON

  log "Installing and launching Zeo..."

  # Allow X11 access for Docker
  xhost +local: >/dev/null 2>&1 || true

  DOCKER_RUN_ARGS=(
    --rm -it
    -e "DISPLAY=${DISPLAY}"
    -e "HOME=${REAL_HOME}"
    -e "XDG_CONFIG_HOME=${REAL_HOME}/.config"
    -e "XDG_DATA_HOME=${REAL_HOME}/.local/share"
    # KiCad settings path — bypass version/platform discovery so settings
    # persist reliably across Docker container restarts.
    -e "KICAD_CONFIG_HOME=${REAL_HOME}/.config/kicad"
    # Stock data and library paths (matching the AppImage's kicad.sh).
    # KICAD_STOCK_DATA_HOME tells KiCad where to find resources, schemas,
    # demos, scripting, etc. The versioned KICAD10_* vars are used in
    # global library tables (sym-lib-table, fp-lib-table).
    -e "KICAD_STOCK_DATA_HOME=/usr/share/kicad"
    -e "KICAD10_SYMBOL_DIR=/usr/share/kicad/symbols"
    -e "KICAD10_FOOTPRINT_DIR=/usr/share/kicad/footprints"
    -e "KICAD10_3DMODEL_DIR=/usr/share/kicad/3dmodels"
    -e "KICAD10_TEMPLATE_DIR=/usr/share/kicad/template"
    -e "KICAD10_DESIGN_BLOCK_DIR=/usr/share/kicad/blocks"
    # Bridge: the libs:latest image ships KiCad 9 tables. Users who
    # already ran the setup wizard have KICAD9_* refs in their saved
    # global tables. These env vars let those paths resolve correctly.
    -e "KICAD9_SYMBOL_DIR=/usr/share/kicad/symbols"
    -e "KICAD9_FOOTPRINT_DIR=/usr/share/kicad/footprints"
    -e "KICAD9_3DMODEL_DIR=/usr/share/kicad/3dmodels"
    -e "KICAD9_TEMPLATE_DIR=/usr/share/kicad/template"
    # Source + build at fixed container paths (used by cmake/ninja)
    -v "$KICAD_SOURCE_DIR:/src/kicad"
    -v "$BUILD_DIR:/build"
    # Host filesystem at real paths so KiCad file dialogs,
    # zeo shell, etc. can browse/create files normally.
    -v "/home:/home"
    -v "/tmp:/tmp"
    --network host
    # WebKit2GTK spawns sandboxed child processes (WebKitWebProcess) that
    # need SYS_ADMIN for bubblewrap and relaxed seccomp. Without these the
    # WebKit renders crash silently, killing the parent. Also disable the
    # WebKit sandbox explicitly for the dev container.
    --cap-add=SYS_PTRACE
    --cap-add=SYS_ADMIN
    --security-opt seccomp=unconfined
    --security-opt apparmor=unconfined
    -e "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1"
    # Disable JSC JIT — the JIT's stack management assumes native memory
    # layout that doesn't hold inside Docker containers, causing
    # JSC::sanitizeStackForVM to abort. Interpreted JS is slightly slower
    # but stable.
    -e "JSC_useJIT=0"
    # Suppress harmless GTK/a11y/canberra warnings in Docker
    -e "NO_AT_BRIDGE=1"
    -e "GTK_MODULES="
    # Force dark GTK theme so KiCad's native UI (menus, dialogs, tree)
    # matches the Zeo apps (agent, VCS, terminal) which default to dark.
    -e "GTK_THEME=Adwaita:dark"
  )

  # GPU access for OpenGL (3D viewer, PCB renderer)
  if [ -d /dev/dri ]; then
    DOCKER_RUN_ARGS+=(--device /dev/dri)
  fi

  # Forward XDG_RUNTIME_DIR - contains D-Bus socket, Wayland socket,
  # PulseAudio socket, and other runtime services.
  if [ -n "$XDG_RUNTIME_DIR" ] && [ -d "$XDG_RUNTIME_DIR" ]; then
    DOCKER_RUN_ARGS+=(
      -e "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}"
      -v "${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR}"
    )
  fi

  # Wayland display
  if [ -n "$WAYLAND_DISPLAY" ]; then
    DOCKER_RUN_ARGS+=(-e "WAYLAND_DISPLAY=${WAYLAND_DISPLAY}")
  fi

  # Forward D-Bus session bus - needed for wxLaunchDefaultBrowser (OAuth
  # sign-in), desktop notifications, and other desktop integration.
  # GTK_USE_PORTAL=1 tells GIO to use the D-Bus portal API for URI
  # handling, so URLs open in the host's default browser.
  if [ -n "$DBUS_SESSION_BUS_ADDRESS" ]; then
    DOCKER_RUN_ARGS+=(
      -e "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS}"
      -e "GTK_USE_PORTAL=1"
    )
  fi

  # Forward SSH agent - needed for git SSH operations (push/pull/clone).
  if [ -n "$SSH_AUTH_SOCK" ]; then
    DOCKER_RUN_ARGS+=(
      -e "SSH_AUTH_SOCK=${SSH_AUTH_SOCK}"
      -v "${SSH_AUTH_SOCK}:${SSH_AUTH_SOCK}"
    )
  fi

  # Install and launch in the same container so installed binaries persist.
  # Use 'exec' for the normal launch so Zeo replaces bash as PID 1.
  # This ensures Zeo receives SIGTERM/SIGINT directly, allowing wxWidgets
  # to run its exit handlers and save settings before the container stops.
  LAUNCH_CMD="exec /usr/bin/Zeo"

  if $DO_LLDB; then
    LAUNCH_CMD="echo 'Use bt for backtrace, bt all for all threads' && lldb -o run /usr/bin/Zeo"
  elif $DO_GDB; then
    LAUNCH_CMD="echo 'Use bt for backtrace, bt all for all threads' && gdb -ex run /usr/bin/Zeo"
  elif $DO_DEBUG; then
    LAUNCH_CMD="WXTRACE=KICAD_AGENT exec /usr/bin/Zeo"
  fi

  # Register kipy packages via a .pth file so Python's site module adds
  # /build/_python_site to sys.path. This works even though the `zeo`
  # script clears PYTHONPATH — .pth files are processed by site.py
  # during interpreter init, before any user code runs.
  KIPY_PTH=""
  if [ -d "$BUILD_DIR/_python_site/kipy" ]; then
    KIPY_PTH='PY_SITE=$(python3 -c "import site; print(site.getsitepackages()[0])") && echo "/build/_python_site" > "$PY_SITE/kipy.pth" && '
  fi

  INSTALL_CMD="git config --global safe.directory '*' && ninja install"

  log "Running ninja install + launch..."
  docker run "${DOCKER_RUN_ARGS[@]}" -w /build "$DEV_IMAGE" bash -c "${INSTALL_CMD} && ${KIPY_PTH}cd \${HOME} && ${LAUNCH_CMD}"
fi

# --- Timing ---

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
log "Done in ${ELAPSED}s."
