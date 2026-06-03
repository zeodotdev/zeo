# zeo

Zeo's monorepo

Zeo is a powerful, modern fork of [KiCad](https://kicad.org).

## Quickstart

```bash
git clone --recurse-submodules https://github.com/zeodotdev/zeo.git
cd zeo
./dev/setup-upstreams.sh        # one-time: configure upstream remotes
./dev/mac_build.sh              # full build + install on macOS
```

Substitute the platform script:

| OS | Script |
|---|---|
| macOS | `./dev/mac_build.sh` |
| Linux | `./dev/appimage_build.sh` |
| Windows | `./dev/win_build_installer.ps1` |

Run any script with `--help` for flags. See [`dev/README.md`](dev/README.md)
for the full flag set + examples.

If you cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

## Layout

```
src/zeo                          KiCad source fork (subtree)
src/zeo-python, src/zeo-rust     KiCad language-binding forks (subtrees)
src/wzWidget                     wxWidgets fork (submodule — KiCad needs custom wx)
libraries/                       KiCad symbols/footprints/3D models (submodules → gitlab.com/kicad)
packaging/{appimage,mac-builder,win-builder}   per-OS packaging (subtrees)
tools/freerouting                autorouter (subtree)
dev/                             build scripts
```

## Upstream sync

See [`UPSTREAMS.md`](UPSTREAMS.md). Use `git subtree pull` for subtrees and
in-submodule sync + pin-bump for submodules.

## License

Zeo as a whole is licensed under [AGPL-3.0-or-later](LICENSE). Zeo's own code
and modifications are AGPLv3; incorporated upstream code (KiCad, freerouting,
wxWidgets, and the vendored third-party libraries) retains its original
license. See [`LICENSE.README`](LICENSE.README) for how the licenses combine,
and [`src/zeo/LICENSE*`](src/zeo/) / [`src/zeo/AUTHORS.txt`](src/zeo/AUTHORS.txt)
for upstream terms and contributors.
