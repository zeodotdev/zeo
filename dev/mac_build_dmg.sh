#!/bin/bash
set -e

# Zeo macOS DMG Build Script (mac_build_dmg.sh)
# Creates a shareable DMG with Zeo (KiCad), Agent, Terminal, and all dependencies
#
# Usage:
#   ./mac_build_dmg.sh                    # Full build + DMG (default)
#   ./mac_build_dmg.sh --skip-build       # DMG only (requires previous build)
#   ./mac_build_dmg.sh --release "1.0"    # Named release DMG
#   ./mac_build_dmg.sh --sign "Developer ID Application: Name"  # Signed for distribution

# --- Configuration ---

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

KICAD_SOURCE_DIR="$WORKSPACE_DIR/src/zeo"
BUILDER_DIR="$WORKSPACE_DIR/packaging/kicad-mac-builder"
LIBRARIES_DIR="$WORKSPACE_DIR/libraries"
KICAD_PYTHON_DIR="$WORKSPACE_DIR/src/zeo-python"

BUILD_DIR="$BUILDER_DIR/build"
KICAD_DEST_DIR="$BUILD_DIR/kicad-dest"
DMG_OUTPUT_DIR="$BUILD_DIR/dmg"

# Parse arguments
SKIP_BUILD=false
RELEASE_NAME=""
SIGNING_IDENTITY=""
NOTARIZE=false

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
        --sign)
            SIGNING_IDENTITY="$2"
            shift 2
            ;;
        --notarize)
            NOTARIZE=true
            shift
            ;;
        --help|-h)
            echo "Zeo DMG Builder"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --skip-build          Skip building, only create DMG from existing build"
            echo "  --release NAME        Set release name (e.g., '1.0', 'beta')"
            echo "  --sign IDENTITY       Code signing identity for distribution"
            echo "  --notarize            Submit to Apple for notarization (requires --sign)"
            echo "  --help, -h            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                           # Full build + DMG"
            echo "  $0 --skip-build              # Create DMG from existing build"
            echo "  $0 --release 1.0             # Create release DMG named 'zeo-1.0.dmg'"
            echo "  $0 --sign 'Developer ID Application: Your Name'"
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
echo "Zeo DMG Builder"
echo "=============================================="
echo "Workspace:     $WORKSPACE_DIR"
echo "Source Dir:    $KICAD_SOURCE_DIR"
echo "Builder Dir:   $BUILDER_DIR"
echo "Libraries Dir: $LIBRARIES_DIR"
echo "Python Dir:    $KICAD_PYTHON_DIR"
echo "DMG Output:    $DMG_OUTPUT_DIR"
echo "Skip Build:    $SKIP_BUILD"
echo "Release Name:  ${RELEASE_NAME:-<auto>}"
echo "Signing:       ${SIGNING_IDENTITY:-<ad-hoc>}"
echo "=============================================="

# --- Validation ---

if [ ! -d "$KICAD_SOURCE_DIR" ]; then
    echo "Error: KiCad source directory not found at $KICAD_SOURCE_DIR"
    exit 1
fi

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
    echo ""
    echo "Install them with:"
    echo "  brew install ${MISSING_PACKAGES[*]}"
    exit 1
fi

# --- Build Phase ---

if [ "$SKIP_BUILD" = false ]; then
    echo ""
    echo "=========================================="
    echo "PHASE 1: Building KiCad with Agent/Terminal"
    echo "=========================================="

    # DMG builds are always release builds
    # Pass --verbose so build progress is visible (otherwise output only goes to log file)
    "$SCRIPT_DIR/mac_build_hard.sh" --release --verbose

    echo ""
    echo "Build phase complete."
else
    echo ""
    echo "Skipping build phase (--skip-build specified)"

    # Verify build exists
    if [ ! -d "$KICAD_DEST_DIR" ]; then
        echo "Error: No existing build found at $KICAD_DEST_DIR"
        echo "Run without --skip-build first."
        exit 1
    fi
fi

# --- DMG Creation Phase ---

echo ""
echo "=========================================="
echo "PHASE 2: Creating DMG Package"
echo "=========================================="

# Find the app bundle (check Zeo.app first, then fallbacks)
KICAD_APP_BUNDLE="$KICAD_DEST_DIR/Zeo.app"
if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    KICAD_APP_BUNDLE="$KICAD_DEST_DIR/KiCad.app"
fi
if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    KICAD_APP_BUNDLE="$KICAD_DEST_DIR/KiCad_Agentic_dev.app"
fi

if [ ! -d "$KICAD_APP_BUNDLE" ]; then
    echo "Error: Could not find app bundle (Zeo.app, KiCad.app, or KiCad_Agentic_dev.app) in $KICAD_DEST_DIR"
    exit 1
fi

echo "Using app bundle: $KICAD_APP_BUNDLE"

# Verify agent and terminal are bundled
APPS_DIR="$KICAD_APP_BUNDLE/Contents/Applications"
PLUGINS_DIR="$KICAD_APP_BUNDLE/Contents/PlugIns"

if [ ! -d "$APPS_DIR/agent.app" ] && [ ! -d "$APPS_DIR/Agent.app" ]; then
    echo "Warning: Agent.app not found in bundle"
fi

if [ ! -d "$APPS_DIR/terminal.app" ] && [ ! -d "$APPS_DIR/Terminal.app" ]; then
    echo "Warning: Terminal.app not found in bundle"
fi

# Verify Python and kipy
PYTHON_SITE_PACKAGES="$KICAD_APP_BUNDLE/Contents/Frameworks/Python.framework/Versions/Current/lib/python3.10/site-packages"
if [ -d "$PYTHON_SITE_PACKAGES/kipy" ]; then
    echo "kipy package found in bundle"

    # Fix permissions so all users can read the bundled Python packages.
    # Without this, apps installed by one user won't work for other users.
    echo "Fixing permissions on bundled Python packages..."
    find "$PYTHON_SITE_PACKAGES" -type d -exec chmod 755 {} \;
    find "$PYTHON_SITE_PACKAGES" -type f -exec chmod 644 {} \;
else
    echo "Warning: kipy package not found in bundle"
fi

# Resolve absolute symlinks pointing outside the bundle (e.g. 3dmodels, images.tar.gz)
# These are build artifacts that link to the build tree and would break on other machines.
# Relative symlinks (e.g. sub-app Frameworks -> ../../Frameworks) are intentional and kept.
# This must happen BEFORE signing, otherwise codesign verification fails.
echo "Resolving external symlinks in bundle..."
find "$KICAD_APP_BUNDLE" -type l | while read -r link; do
    target=$(readlink "$link")
    case "$target" in
        /*)
            echo "  Resolving: $(basename "$link") -> $target"
            rm "$link"
            cp -R "$target" "$link"
            ;;
    esac
done

# --- App Bundle Signing Phase (for notarization) ---

if [ -n "$SIGNING_IDENTITY" ]; then
    echo ""
    echo "=========================================="
    echo "PHASE 2: Signing App Bundle for Notarization"
    echo "=========================================="

    APPLE_PY="$BUILDER_DIR/kicad-mac-builder/bin/apple.py"

    if [ ! -f "$APPLE_PY" ]; then
        echo "Error: apple.py signing script not found at $APPLE_PY"
        exit 1
    fi

    # For notarization, we need:
    # --hardened-runtime: Required by Apple for notarization
    # --timestamp: Secure timestamp from Apple's servers
    echo "Signing all binaries in $KICAD_APP_BUNDLE..."
    echo "This may take several minutes..."

    python3 "$APPLE_PY" sign \
        --certificate-id "$SIGNING_IDENTITY" \
        --hardened-runtime \
        --timestamp \
        "$KICAD_APP_BUNDLE"

    echo ""
    echo "App bundle signing complete."
    echo ""
    echo "=========================================="
    echo "PHASE 3: Creating DMG Package"
    echo "=========================================="
else
    echo ""
    echo "=========================================="
    echo "PHASE 2: Creating DMG Package"
    echo "=========================================="
    echo "(Skipping app bundle signing - no --sign identity provided)"
fi

# Create output directory
mkdir -p "$DMG_OUTPUT_DIR"

# Generate DMG name
NOW=$(date +%Y%m%d-%H%M%S)
KICAD_GIT_REV=$(cd "$KICAD_SOURCE_DIR" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")

if [ -n "$RELEASE_NAME" ]; then
    DMG_NAME="zeo-${RELEASE_NAME}.dmg"
else
    DMG_NAME="zeo-${NOW}-${KICAD_GIT_REV}.dmg"
fi

DMG_PATH="$DMG_OUTPUT_DIR/$DMG_NAME"
TEMP_DMG="$DMG_OUTPUT_DIR/zeo-temp.dmg"
MOUNT_POINT="/Volumes/Zeo"

echo "Creating DMG: $DMG_NAME"

# Cleanup function
cleanup_dmg() {
    echo "Cleaning up..."
    if [ -d "$MOUNT_POINT" ]; then
        hdiutil detach "$MOUNT_POINT" 2>/dev/null || true
        diskutil unmount "$MOUNT_POINT" 2>/dev/null || true
    fi
    if [ -f "$TEMP_DMG" ]; then
        rm -f "$TEMP_DMG" 2>/dev/null || true
    fi
}
trap cleanup_dmg EXIT

# Calculate required size (add 20% buffer)
echo "Calculating required DMG size..."
INSTALL_SIZE_KB=$(du -sk "$KICAD_APP_BUNDLE" | cut -f1)
DMG_SIZE_KB=$((INSTALL_SIZE_KB * 120 / 100))
DMG_SIZE_MB=$((DMG_SIZE_KB / 1024))

echo "Install size: ${INSTALL_SIZE_KB}KB (~${DMG_SIZE_MB}MB with buffer)"

# Create a temporary writable DMG
echo "Creating temporary DMG..."
rm -f "$TEMP_DMG"
hdiutil create -size ${DMG_SIZE_MB}m -fs HFS+ -volname "Zeo" "$TEMP_DMG"

# Mount the DMG at /Volumes/Zeo so Finder can configure window layout
echo "Mounting DMG..."
# Unmount if already mounted from a previous failed run
if [ -d "$MOUNT_POINT" ]; then
    hdiutil detach "$MOUNT_POINT" 2>/dev/null || diskutil unmount "$MOUNT_POINT" 2>/dev/null || true
    sleep 1
fi
DEVICE=$(hdiutil attach "$TEMP_DMG" -noautoopen -mountpoint "$MOUNT_POINT" | awk '/Apple_HFS/ {print $1}')

if [ -z "$DEVICE" ]; then
    echo "Error: Failed to mount DMG"
    exit 1
fi

echo "Mounted at $MOUNT_POINT (device: $DEVICE)"

# Disable Spotlight indexing on DMG
mdutil -i off "$MOUNT_POINT" 2>/dev/null || true

# Copy app directly to DMG root (clean drag-to-install layout)
# Use ditto to preserve code signatures (stored in extended attributes)
APP_NAME=$(basename "$KICAD_APP_BUNDLE")
echo "Copying $APP_NAME to DMG root..."
ditto "$KICAD_APP_BUNDLE" "$MOUNT_POINT/$APP_NAME"

# Create Applications symlink for drag-to-install
echo "Creating Applications symlink..."
ln -s /Applications "$MOUNT_POINT/Applications"

# Apply DMG window settings using AppleScript for drag-to-install layout
# No custom background — relies on system Dark Mode for white labels on dark window
echo "Configuring DMG window layout..."
osascript << 'APPLESCRIPT'
tell application "Finder"
    activate
    with timeout of 600 seconds
        tell disk "Zeo"
            open
            delay 3
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set bounds of container window to {100, 100, 700, 500}
            set theViewOptions to the icon view options of container window
            set arrangement of theViewOptions to not arranged
            set icon size of theViewOptions to 128
            -- Position icons: Zeo.app on left, Applications on right
            set position of item "Zeo.app" of container window to {150, 200}
            set position of item "Applications" of container window to {450, 200}
            close
            open
            update without registering applications
            delay 2
            close
        end tell
    end timeout
end tell
APPLESCRIPT

# Sync and unmount
echo "Syncing filesystem..."
sync
sleep 2

echo "Unmounting DMG..."
UNMOUNTED=false
for i in 1 2 3 4 5; do
    if hdiutil detach "$DEVICE" 2>/dev/null; then
        UNMOUNTED=true
        break
    else
        echo "Retrying unmount (attempt $i)..."
        sync
        sleep $((i * 2))
    fi
done

if [ "$UNMOUNTED" = false ]; then
    echo "Warning: Normal unmount failed, forcing..."
    hdiutil detach -force "$DEVICE" || true
fi

# Convert to compressed read-only DMG
echo "Converting to compressed DMG..."
rm -f "$DMG_PATH"
hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$DMG_PATH"

# Remove temp DMG
rm -f "$TEMP_DMG"

# Sign the DMG
if [ -n "$SIGNING_IDENTITY" ]; then
    echo "Signing DMG with: $SIGNING_IDENTITY"
    codesign --sign "$SIGNING_IDENTITY" --verbose "$DMG_PATH"

    # Verify signature
    echo "Verifying signature..."
    codesign --verify --verbose "$DMG_PATH"
else
    echo "Using ad-hoc signing (DMG will work on this machine only without Gatekeeper approval)"
    codesign --sign - --verbose "$DMG_PATH" 2>/dev/null || true
fi

# Notarize if requested
if [ "$NOTARIZE" = true ] && [ -n "$SIGNING_IDENTITY" ]; then
    echo ""
    echo "=========================================="
    echo "Notarization"
    echo "=========================================="
    echo ""
    echo "To notarize this DMG:"
    echo ""
    echo "  # First time only: store credentials in Keychain"
    echo "  xcrun notarytool store-credentials \"ZEO_NOTARIZE\""
    echo ""
    echo "  # Submit for notarization (uses stored credentials)"
    echo "  xcrun notarytool submit --keychain-profile \"ZEO_NOTARIZE\" --wait '$DMG_PATH'"
    echo ""
    echo "  # After approval, staple the ticket to the DMG"
    echo "  xcrun stapler staple '$DMG_PATH'"
    echo ""
    echo "  # If notarization fails, check the log:"
    echo "  xcrun notarytool log <submission-id> --keychain-profile \"ZEO_NOTARIZE\""
    echo ""
fi

# Upload debug symbols to Sentry for crash symbolication
# This ensures symbols match the exact binaries shipped in the DMG
if command -v sentry-cli >/dev/null 2>&1; then
    echo ""
    echo "=========================================="
    echo "Sentry Debug Symbol Upload"
    echo "=========================================="
    SENTRY_RELEASE_FLAG=""
    if [ -n "$RELEASE_NAME" ]; then
        SENTRY_RELEASE_FLAG="--release $RELEASE_NAME"
    fi
    echo "Uploading debug symbols from installed bundle..."
    sentry-cli debug-files upload \
        --org moonshine-e2 \
        --project zeo \
        "$KICAD_DEST_DIR/Zeo.app" \
        --include-sources \
        --log-level=warn || echo "Warning: Sentry debug symbol upload failed (non-fatal)"
else
    echo ""
    echo "Warning: sentry-cli not found — skipping debug symbol upload."
    echo "  Install with: brew install getsentry/tools/sentry-cli"
fi

# Display final DMG info
echo ""
echo "=========================================="
echo "DMG CREATION COMPLETE"
echo "=========================================="
echo ""
echo "DMG Location: $DMG_PATH"
echo "DMG Size:     $(du -h "$DMG_PATH" | cut -f1)"
echo ""

# Verify DMG contents
echo "Verifying DMG contents..."
VERIFY_MOUNT="/tmp/kicad-verify-$$"
mkdir -p "$VERIFY_MOUNT"
hdiutil attach "$DMG_PATH" -noautoopen -nobrowse -mountpoint "$VERIFY_MOUNT" >/dev/null

echo "Contents:"
ls -la "$VERIFY_MOUNT"
echo ""

# Check for agent and terminal
if [ -d "$VERIFY_MOUNT/$APP_NAME/Contents/Applications" ]; then
    echo "Bundled Applications:"
    ls -la "$VERIFY_MOUNT/$APP_NAME/Contents/Applications"
fi

# Check for Freerouting
if [ -f "$VERIFY_MOUNT/$APP_NAME/Contents/SharedSupport/freerouting/freerouting.jar" ]; then
    echo ""
    echo "Freerouting: included"
else
    echo ""
    echo "Warning: Freerouting JAR not found in bundle"
fi

hdiutil detach "$VERIFY_MOUNT" >/dev/null
rm -rf "$VERIFY_MOUNT"

echo ""
echo "To install:"
echo "  1. Open $DMG_PATH"
echo "  2. Drag Zeo.app to Applications (drag-to-install layout)"
echo ""
echo "To share this DMG:"
if [ -n "$SIGNING_IDENTITY" ]; then
    echo "  - DMG is signed and can be distributed"
    echo "  - For Gatekeeper approval, notarize the DMG (see above)"
else
    echo "  - DMG uses ad-hoc signing (works on builder's machine)"
    echo "  - For distribution, re-run with --sign 'Developer ID Application: Name'"
fi
echo ""
echo "=============================================="
