#!/usr/bin/env bash
#
# sync.sh — Clone or pull all repos in the kicadpp workspace.
#
# This script lives in dev/ and operates on the kicadpp/ workspace (its parent).
#
# Usage:
#   ./sync.sh          # Clone missing repos, pull existing ones
#   ./sync.sh --clone  # Clone only (skip pull for existing repos)
#   ./sync.sh --pull   # Pull only (skip clone for missing repos)
#

set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Verify we're in the right workspace
if [[ "$(basename "$WORKSPACE_DIR")" != "kicadpp" && "$(basename "$WORKSPACE_DIR")" != "zeo" ]]; then
    echo "Error: expected workspace dir to be 'kicadpp' or 'zeo', got '$(basename "$WORKSPACE_DIR")'"
    exit 1
fi

# Each entry: <relative-path-from-kicadpp> <git-url> <default-branch>
REPOS=(
    "code/zeo                            https://github.com/zeodotdev/zeo.git                              main"
    "code/zeo-python                     https://github.com/zeodotdev/zeo-python.git                       main"
    "dev                                 https://github.com/zeodotdev/dev.git                              main"
    "libraries/kicad-footprint-generator https://gitlab.com/kicadpp/libraries/kicad-footprint-generator.git master"
    "libraries/kicad-footprints          https://gitlab.com/kicadpp/libraries/kicad-footprints.git          master"
    "libraries/kicad-library-utils       https://gitlab.com/kicadpp/libraries/kicad-library-utils.git       master"
    "libraries/kicad-packages3D-source   https://gitlab.com/kicadpp/libraries/kicad-packages3D-source.git   master"
    "libraries/kicad-packages3D          https://gitlab.com/kicadpp/libraries/kicad-packages3D.git          master"
    "libraries/kicad-symbols             https://gitlab.com/kicadpp/libraries/kicad-symbols.git             master"
    "libraries/kicad-templates           https://gitlab.com/kicadpp/libraries/kicad-templates.git           master"
    "packaging/kicad-mac-builder         https://gitlab.com/kicadpp/packaging/kicad-mac-builder.git         master"
    "tools/freerouting                   https://github.com/zeodotdev/freerouting.git                       master"
    "web                                 https://github.com/zeodotdev/web.git                               main"
)

MODE="both"
if [[ "${1:-}" == "--clone" ]]; then
    MODE="clone"
elif [[ "${1:-}" == "--pull" ]]; then
    MODE="pull"
fi

cloned=0
pulled=0
skipped=0
failed=0

for entry in "${REPOS[@]}"; do
    read -r rel_path url branch <<< "$entry"
    full_path="$WORKSPACE_DIR/$rel_path"

    if [[ -d "$full_path/.git" ]]; then
        if [[ "$MODE" == "clone" ]]; then
            echo "  skip  $rel_path (already exists)"
            ((skipped++)) || true
            continue
        fi
        echo "  pull  $rel_path"
        if git -C "$full_path" pull --ff-only 2>&1 | sed 's/^/         /'; then
            ((pulled++)) || true
        else
            echo "         ⚠ pull failed (dirty tree or diverged?)"
            ((failed++)) || true
        fi
    else
        if [[ "$MODE" == "pull" ]]; then
            echo "  skip  $rel_path (not cloned)"
            ((skipped++)) || true
            continue
        fi
        echo "  clone $rel_path"
        mkdir -p "$(dirname "$full_path")"
        if git clone "$url" "$full_path" 2>&1 | sed 's/^/         /'; then
            ((cloned++)) || true
        else
            echo "         ⚠ clone failed"
            ((failed++)) || true
        fi
    fi
done

echo ""
echo "Done: $cloned cloned, $pulled pulled, $skipped skipped, $failed failed"
