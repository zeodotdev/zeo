# dev/

Build scripts for the Zeo monorepo. Run from the monorepo root.

## One-time setup

```bash
git submodule update --init --recursive    # if not cloned with --recurse-submodules
./dev/setup-upstreams.sh                   # add upstream-* remotes (see ../UPSTREAMS.md)
```

For the agent, copy `src/zeo/agent/.env.example` → `src/zeo/agent/.env` and fill
in `ANTHROPIC_API_KEY`.

## Build

| Script | Platform |
|---|---|
| `./dev/mac_build.sh` | macOS — DMG |
| `./dev/appimage_build.sh` | Linux — AppImage (Docker-based) |
| `./dev/win_build_installer.ps1` | Windows — NSIS installer |

### Common flags (mac + linux)

| Flag | Effect |
|---|---|
| *(none)* | Full hard build + install |
| `--fast` | Incremental rebuild (requires prior hard build) |
| `--package` | Also produce distributable (DMG / AppImage) |
| `--skip-build` | Package-only (with `--package`) |
| `--release NAME` | Name the artifact (e.g. `--release 1.0` → `zeo-1.0.dmg`) |
| `--launch` | Launch the app after build |
| `--quit` | Kill running instance before build |
| `--target NAME` | Specific make/ninja target (implies `--fast`) |
| `--agent` | Shortcut for `--target agent --fast` |
| `--python` | Rebuild kipy (Python bindings) |
| `--install` | Run make/cmake install (fast mode) |
| `--debug` | Launch with `WXTRACE=KICAD_AGENT` (implies `--launch`) |
| `--lldb` | Launch under lldb |
| `--force` | Clear cmake cache, force full reconfigure |
| `--verbose` | Show build output in terminal (default: log only) |
| `--help` | Show full per-script flag list |

### Platform-specific flags

**macOS:** `--sign IDENTITY`, `--notarize` (both `--package` only).

**Linux:** `--light` (no 3D packages), `--config FILE`, `--build-deps`, `--setup`
(rebuild Docker dev image), `--clean` (wipe persistent build dir), `--reconfigure`
(force cmake reconfigure), `--gdb`.

### Examples

```bash
./dev/mac_build.sh                                # First build / clean rebuild
./dev/mac_build.sh --fast --launch                # Daily iteration
./dev/mac_build.sh --agent                        # Just rebuild the agent
./dev/mac_build.sh --package --release 1.0        # Build + DMG named zeo-1.0.dmg
./dev/mac_build.sh --skip-build --package         # DMG from existing build
./dev/mac_build.sh --package --sign 'Developer ID Application: Name' --notarize
```

## Layout

```
dev/
├── mac_build.sh                  macOS entry
├── appimage_build.sh             Linux entry
├── win_build_fast.ps1            Windows dev iteration
├── win_build_installer.ps1       Windows installer
├── setup-upstreams.sh            configures upstream-* remotes
├── assets/                       build-time image assets (DMG background)
├── log/                          build logs (gitignored)
└── utils/                        internal helpers — don't call directly
    ├── mac_{hard,fast,dmg}.sh
    ├── linux_{fast,appimage}.sh
    └── create-dmg-background.py
```

The `utils/` scripts can be invoked directly to debug a specific phase, but
they aren't the supported interface — flag sets may change.
