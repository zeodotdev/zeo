#!/bin/bash
# Fix ngspice symlinks - replace absolute symlinks with actual files for code signing

APP_BUNDLE="$1"

if [ -z "$APP_BUNDLE" ]; then
    echo "Usage: $0 <path-to-Zeo.app>"
    exit 1
fi

SIM_DIR="$APP_BUNDLE/Contents/PlugIns/sim"

if [ ! -d "$SIM_DIR" ]; then
    echo "PlugIns/sim directory not found, skipping"
    exit 0
fi

echo "Fixing ngspice symlinks in $SIM_DIR"

cd "$SIM_DIR" || exit 1

for f in *.dylib; do
    if [ -L "$f" ]; then
        target=$(readlink "$f")
        # Check if it's an absolute path outside the bundle
        if [[ "$target" == /* ]] && [[ "$target" != "$APP_BUNDLE"* ]]; then
            if [ -f "$target" ]; then
                echo "Resolving external symlink: $f -> $target"
                rm "$f"
                cp "$target" "$f"
                echo "Resolved: $f"
            else
                echo "Warning: symlink target not found: $target"
            fi
        fi
    fi
done

# Recreate relative symlink if needed
if [ -f "libngspice.0.dylib" ] && [ ! -e "libngspice.dylib" ]; then
    ln -sf libngspice.0.dylib libngspice.dylib
    echo "Created libngspice.dylib symlink"
fi

echo "Done fixing ngspice symlinks"

# General pass: replace any absolute symlinks pointing outside the bundle
# with real files (codesign --strict rejects out-of-bundle symlinks)
echo "Scanning bundle for out-of-bundle symlinks: $APP_BUNDLE"
while IFS= read -r -d '' link; do
    target=$(readlink "$link")
    if [[ "$target" == /* ]] && [[ "$target" != "$APP_BUNDLE"* ]]; then
        if [ -f "$target" ]; then
            echo "Resolving external symlink: $link -> $target"
            cp "$target" "$link.fix_tmp" && mv "$link.fix_tmp" "$link"
            echo "Resolved: $link"
        elif [ -d "$target" ]; then
            echo "Skipping directory symlink (should be handled elsewhere): $link -> $target"
        else
            echo "Warning: external symlink target not found: $link -> $target"
        fi
    fi
done < <(find "$APP_BUNDLE" -type l -print0)

echo "Done scanning for out-of-bundle symlinks"
