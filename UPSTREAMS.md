# Upstream syncs

This monorepo consolidates Zeo's forks of KiCad and related projects. Each
upstream is tracked differently depending on how the component was imported.

## Adding upstream remotes (one-time per clone)

```
./dev/setup-upstreams.sh
```

This script is idempotent. It adds these remotes:

| Remote name           | URL                                                  | Tracks                    |
|-----------------------|------------------------------------------------------|---------------------------|
| `upstream-kicad`      | gitlab.com/kicad/code/kicad                          | `src/zeo`                 |
| `upstream-kicad-py`   | gitlab.com/kicad/code/kicad-python                   | `src/zeo-python`          |
| `upstream-kicad-rs`   | gitlab.com/kicad/code/kicad-rs                       | `src/zeo-rust`            |
| `upstream-wxwidgets`  | gitlab.com/kicad/code/wxWidgets                      | `src/wzWidget` (submodule)|
| `upstream-freerouting`| github.com/freerouting/freerouting                   | `tools/freerouting`       |
| `upstream-appimage`   | gitlab.com/kicad/packaging/kicad-appimage            | `packaging/kicad-appimage`|
| `upstream-mac-builder`| gitlab.com/kicad/packaging/kicad-mac-builder         | `packaging/kicad-mac-builder` |
| `upstream-win-builder`| gitlab.com/kicad/packaging/kicad-win-builder         | `packaging/kicad-win-builder` |

## Pulling from upstream

### Subtrees (`src/zeo`, `src/zeo-python`, `src/zeo-rust`, `tools/freerouting`, `packaging/*`)

```
git subtree pull --prefix=src/zeo            upstream-kicad        master --squash
git subtree pull --prefix=src/zeo-python     upstream-kicad-py     main   --squash
git subtree pull --prefix=src/zeo-rust       upstream-kicad-rs     main   --squash
git subtree pull --prefix=tools/freerouting  upstream-freerouting  master --squash
git subtree pull --prefix=packaging/kicad-appimage    upstream-appimage     main   --squash
git subtree pull --prefix=packaging/kicad-mac-builder upstream-mac-builder  master --squash
git subtree pull --prefix=packaging/kicad-win-builder upstream-win-builder  master --squash
```

Always use `--squash` to keep the monorepo log readable.

### Submodules (`libraries/*`, `src/wzWidget`)

Each submodule is its own clone with its own upstream. Sync inside the submodule,
then bump the pin in the monorepo:

```
cd libraries/kicad-symbols
git remote add upstream https://gitlab.com/kicad/libraries/kicad-symbols.git  # one-time
git fetch upstream && git merge upstream/master    # or rebase
git push origin master                              # push to zeodotdev/kicad-symbols
cd ../..
git add libraries/kicad-symbols
git commit -m "build: bump kicad-symbols submodule"
```

Same pattern for the other 6 libraries and `src/wzWidget`.

## Notes

- `src/wzWidget` is a submodule (not a subtree) because wxWidgets has its own
  nested submodules (`3rdparty/catch`, `nanosvg`, `pcre`) that need to stay
  self-contained inside the wxWidgets repo.
- The `kicadpp/*` GitLab forks are no longer used. They were the migration
  source; the GitHub mirrors at `zeodotdev/*` are now authoritative.
