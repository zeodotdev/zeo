# Zeo Development Setup

## Layout

`dev/` lives inside the monorepo at `/Users/gmp/workspaces/zeodotdev/zeo/dev/`.
Everything is relative to the monorepo root (one directory up from this one).
Submodules under `libraries/` and `src/wzWidget` must be initialized first.

```
git clone --recurse-submodules https://github.com/zeodotdev/zeo.git
# or if cloned without --recurse:
git submodule update --init --recursive

./dev/setup-upstreams.sh   # one-time: add upstream-* remotes for syncs
```

See `UPSTREAMS.md` (root) for the upstream sync recipe.

## API keys for the agent

The Zeo agent needs LLM API keys.

```bash
cd src/zeo/agent
cp .env.example .env
# Edit .env and add ANTHROPIC_API_KEY (required) and OPENAI_API_KEY (optional)
```

Get an Anthropic key at <https://console.anthropic.com/>.

## Building

Three platform scripts (each handles dev iteration + packaging):

| Script | Platform | Purpose |
|---|---|---|
| `./dev/mac_build.sh` | macOS | hard build + install + DMG |
| `./dev/appimage_build.sh` | Linux | dev iteration + AppImage |
| `./dev/win_build.ps1` | Windows | dev iteration + NSIS installer |

Common flags across all three:

| Flag | Effect |
|---|---|
| *(none)* | Full hard build + install |
| `--fast` | Incremental build (requires prior hard build) |
| `--package` | Also produce distributable (DMG / AppImage / Installer) |
| `--skip-build` | Skip build; package-only (with `--package`) |
| `--release NAME` | Name the artifact (e.g. `--release 1.0`) |
| `--launch` | Launch the app after build |
| `--quit` | Kill running instance before build |
| `--target NAME` | Specific make/ninja target (implies `--fast`) |
| `--agent` | Shortcut for `--target agent --fast` |
| `--python` | Rebuild kipy (Python bindings) |
| `--install` | Run make/cmake install (fast mode) |
| `--debug` | Launch with `WXTRACE=KICAD_AGENT` (implies `--launch`) |
| `--lldb` | Launch under lldb (mac/linux) |
| `--force` | Clear cmake cache, force full reconfigure |
| `--verbose` | Show build output in terminal |
| `--help` | Show full per-script flag list |

Platform-specific extras: `--sign IDENTITY` / `--notarize` (mac), `--light` /
`--config FILE` / `--build-deps` / `--setup` / `--clean` / `--reconfigure` /
`--gdb` (linux). See `./dev/<script> --help` for each.

### Examples

```bash
./dev/mac_build.sh                                  # Full hard build + install
./dev/mac_build.sh --fast --launch                  # Incremental + launch
./dev/mac_build.sh --agent                          # Just rebuild the agent
./dev/mac_build.sh --package --release 1.0          # Build + DMG named zeo-1.0.dmg
./dev/mac_build.sh --skip-build --package           # DMG from existing build
./dev/mac_build.sh --package --sign 'Developer ID Application: Name' --notarize
```

## Internal scripts (`dev/_internal/`)

The unified entries delegate to per-mode helpers in `_internal/`. You can call
them directly if a mode breaks, but they aren't the supported interface — flag
sets and naming may change. For mac these are `mac_hard.sh`, `mac_fast.sh`, and
`mac_dmg.sh`.

(Linux and Windows scripts are not yet consolidated; they live at the dev/ root
for now as `linux_build_fast.sh`, `linux_build_appimage.sh`, `win_build_fast.ps1`,
`win_build_installer.ps1`.)
