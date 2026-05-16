#!/bin/bash
# Idempotently add all upstream remotes for subtree syncs.
# Run once per clone. See UPSTREAMS.md for sync recipe.
set -e
cd "$(dirname "$0")/.."

add_remote() {
  local name=$1 url=$2
  if git remote | grep -qx "$name"; then
    git remote set-url "$name" "$url"
    echo "  updated $name -> $url"
  else
    git remote add "$name" "$url"
    echo "  added   $name -> $url"
  fi
}

add_remote upstream-kicad         https://gitlab.com/kicad/code/kicad.git
add_remote upstream-kicad-py      https://gitlab.com/kicad/code/kicad-python.git
add_remote upstream-kicad-rs      https://gitlab.com/kicad/code/kicad-rs.git
add_remote upstream-wxwidgets     https://gitlab.com/kicad/code/wxWidgets.git
add_remote upstream-freerouting   https://github.com/freerouting/freerouting.git
add_remote upstream-appimage      https://gitlab.com/kicad/packaging/kicad-appimage.git
add_remote upstream-mac-builder   https://gitlab.com/kicad/packaging/kicad-mac-builder.git
add_remote upstream-win-builder   https://gitlab.com/kicad/packaging/kicad-win-builder.git

echo ""
echo "Done. See UPSTREAMS.md for sync recipes."
