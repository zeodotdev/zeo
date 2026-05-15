# zeo

Zeo monorepo. Consolidates Zeo's forks of the KiCad ecosystem into a single repository.

## Layout

```
src/
  zeo/              fork of KiCad (gitlab.com/kicad/code/kicad)
  zeo-python/       fork of kicad-python (gitlab.com/kicad/code/kicad-python)
  zeo-rust/         fork of kicad-rs (gitlab.com/kicad/code/kicad-rs)
  wzWidget/         submodule: fork of KiCad's wxWidgets fork
libraries/          submodules: KiCad symbol/footprint/3D libraries
packaging/
  kicad-appimage/   Linux AppImage packaging
  kicad-mac-builder/macOS .dmg packaging
  kicad-win-builder/Windows installer packaging
tools/
  freerouting/      autorouter (fork of github.com/freerouting/freerouting)
dev/                build scripts (mac/linux/windows)
```

## Getting started

Clone with submodules:

```
git clone --recursive https://github.com/zeodotdev/zeo.git
cd zeo
./dev/mac_build_fast.sh --install --python
```

If you already cloned without `--recursive`:

```
git submodule update --init --recursive
```

## Upstream syncs

`src/zeo`, `src/zeo-python`, `src/zeo-rust`, `packaging/*`, and `tools/freerouting`
are git subtrees. To pull upstream KiCad changes, see `UPSTREAMS.md` (TODO).

Library submodules and `src/wzWidget` are managed as regular git submodules.

## License

Zeo inherits KiCad's licensing (GPL-3.0 and others — see `src/zeo/LICENSE*`).
See `src/zeo/AUTHORS.txt` for KiCad contributors.

> **Note:** This README is a placeholder. A full Zeo README, NOTICE files,
> and meta-file pass (AUTHORS.ZEO, CONTRIBUTING) are tracked as the next
> phase of monorepo consolidation.
