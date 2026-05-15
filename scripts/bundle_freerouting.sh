#!/usr/bin/env bash
set -e

# Bundle Freerouting autorouter into the AppDir.
# Freerouting is a Java-based autorouter that integrates with KiCad.
#
# Configuration via environment variables:
#   ENABLE_FREEROUTING_BUNDLE=1       Set to 0 to skip bundling
#   FREEROUTING_SOURCE_DIR=/src/freerouting   Path to Freerouting source
#   APPDIR=/tmp/AppDir                        Target AppDir root

if [[ "${ENABLE_FREEROUTING_BUNDLE:-1}" != "1" ]]; then
    echo "[bundle_freerouting] Skipping (ENABLE_FREEROUTING_BUNDLE!=1)"
    exit 0
fi

APPDIR=${APPDIR:-/tmp/AppDir}
FREEROUTING_SOURCE_DIR=${FREEROUTING_SOURCE_DIR:-/src/freerouting}
FREEROUTING_DEST="$APPDIR/usr/share/kicad/freerouting"

echo "[bundle_freerouting] Starting Freerouting bundling"
echo "[bundle_freerouting] APPDIR: $APPDIR"
echo "[bundle_freerouting] FREEROUTING_SOURCE_DIR: $FREEROUTING_SOURCE_DIR"

# Validate source directory
if [ ! -d "$FREEROUTING_SOURCE_DIR" ]; then
    echo "[bundle_freerouting] WARNING: Freerouting source not found at $FREEROUTING_SOURCE_DIR, skipping"
    exit 0
fi

# Check for Java
if ! command -v java >/dev/null 2>&1; then
    echo "[bundle_freerouting] WARNING: Java not found, cannot build Freerouting"
    exit 0
fi

# Check for pre-built JAR first
PREBUILT_JAR="$FREEROUTING_SOURCE_DIR/build/libs/freerouting-executable.jar"
if [ -f "$PREBUILT_JAR" ]; then
    echo "[bundle_freerouting] Using pre-built JAR: $PREBUILT_JAR"
    mkdir -p "$FREEROUTING_DEST"
    cp "$PREBUILT_JAR" "$FREEROUTING_DEST/freerouting.jar"
    echo "[bundle_freerouting] SUCCESS: Freerouting JAR installed"
    exit 0
fi

# Build Freerouting if gradlew exists
if [ -f "$FREEROUTING_SOURCE_DIR/gradlew" ]; then
    echo "[bundle_freerouting] Building Freerouting with Gradle..."
    pushd "$FREEROUTING_SOURCE_DIR" > /dev/null
    ./gradlew executableJar --no-daemon
    popd > /dev/null

    # Check for built JAR
    if [ -f "$PREBUILT_JAR" ]; then
        mkdir -p "$FREEROUTING_DEST"
        cp "$PREBUILT_JAR" "$FREEROUTING_DEST/freerouting.jar"
        echo "[bundle_freerouting] SUCCESS: Freerouting JAR built and installed"
    else
        echo "[bundle_freerouting] WARNING: Freerouting JAR not found after build"
        echo "[bundle_freerouting] Expected at: $PREBUILT_JAR"
    fi
else
    echo "[bundle_freerouting] WARNING: gradlew not found in $FREEROUTING_SOURCE_DIR"
fi

echo "[bundle_freerouting] Complete."
