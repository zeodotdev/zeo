#!/usr/bin/env bash
set -euo pipefail

# Renames KiCad AppImages from go-appimage's default naming (KiCad-VERSION-ARCH.AppImage)
# to the project naming convention: PREFIX-VERSION-ARCH[-SUFFIX].AppImage
#
# Usage: rename_appimage.sh OUTPUT_DIR PREFIX VERSION [SUFFIX]
#   OUTPUT_DIR  Directory containing the AppImage files
#   PREFIX      Filename prefix (e.g. "kicad-nightly", "kicad")
#   VERSION     Version string (e.g. "9.99.0.5128.g2f8c1dc0f7" or "9.0.7")
#   SUFFIX      Optional suffix after arch (e.g. "lite")
#
# Examples:
#   Nightly full:  kicad-nightly-9.99.0.5128.g2f8c1dc0f7-x86_64.AppImage
#   Nightly lite:  kicad-nightly-9.99.0.5128.g2f8c1dc0f7-x86_64-lite.AppImage
#   Release full:  kicad-9.0.7-x86_64.AppImage
#   Release lite:  kicad-9.0.7-x86_64-lite.AppImage

OUTPUT_DIR="${1:?Usage: rename_appimage.sh OUTPUT_DIR PREFIX VERSION [SUFFIX]}"
PREFIX="${2:?Missing PREFIX argument}"
VERSION="${3:?Missing VERSION argument}"
SUFFIX="${4:-}"

shopt -s nullglob
files=("${OUTPUT_DIR}"/*.AppImage)

if [ ${#files[@]} -eq 0 ]; then
    echo "[rename_appimage] No *.AppImage files found in ${OUTPUT_DIR}" >&2
    exit 1
fi

for src in "${files[@]}"; do
    basename=$(basename "$src")
    arch=$(echo "$basename" | grep -oP 'x86_64|aarch64' || true)
    arch=${arch:-x86_64}

    if [ -n "$SUFFIX" ]; then
        dest="${OUTPUT_DIR}/${PREFIX}-${VERSION}-${arch}-${SUFFIX}.AppImage"
    else
        dest="${OUTPUT_DIR}/${PREFIX}-${VERSION}-${arch}.AppImage"
    fi
    if [ "$src" = "$dest" ]; then
        echo "[rename_appimage] ${basename} already has correct name, skipping" >&2
    else
        echo "[rename_appimage] ${basename} -> $(basename "$dest")" >&2
        mv "$src" "$dest"
    fi
done

exit 0
