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

### Submodules (`libraries/`, `src/wzWidget`)

`libraries/` is a single submodule pointing at `zeodotdev/zeo-libraries`,
which itself contains 7 KiCad libraries as subtrees (`kicad-symbols/`,
`kicad-footprints/`, etc.). To sync libraries from upstream KiCad:

```
cd libraries
git subtree pull --prefix=kicad-symbols    https://gitlab.com/kicad/libraries/kicad-symbols.git    master --squash
git subtree pull --prefix=kicad-footprints https://gitlab.com/kicad/libraries/kicad-footprints.git master --squash
# ... etc for each kicad-* subtree
git push origin main
cd ..
git add libraries
git commit -m "build: bump zeo-libraries submodule"
```

For Zeo-proprietary library content, opt in by adding the private
`zeo-libraries-private` repo manually inside `libraries/`:

```
git -C libraries submodule add https://github.com/zeodotdev/zeo-libraries-private.git zeo-private
# Requires github auth. Not in zeo-libraries' .gitmodules — team members add per-clone.
```

For `src/wzWidget`, sync inside the submodule and bump the pin:

```
cd src/wzWidget
git fetch https://gitlab.com/kicad/code/wxWidgets.git kicad/macos-wx-3.2
git merge FETCH_HEAD
git push origin kicad/macos-wx-3.2
cd ../..
git add src/wzWidget && git commit -m "build: bump wzWidget"
```

## Notes

- `src/wzWidget` is a submodule (not a subtree) because wxWidgets has its own
  nested submodules (`3rdparty/catch`, `nanosvg`, `pcre`) that need to stay
  self-contained inside the wxWidgets repo.
- `libraries/` was originally 7 individual submodules; consolidated into one
  `zeo-libraries` submodule for simpler ops and to make room for Zeo-proprietary
  libraries alongside the KiCad mirrors.
- The `kicadpp/*` GitLab forks and the standalone `zeodotdev/{zeo-python, zeo-rust,
  freerouting, dev}` mirrors are archived. Upstream pulls go directly to
  `gitlab.com/kicad/*` (and `github.com/freerouting/freerouting`).
