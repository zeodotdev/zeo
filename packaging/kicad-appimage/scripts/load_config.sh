#!/bin/sh
set -eu

# Read a build config JSON and output shell variable assignments.
#
# Usage:
#   eval $(scripts/load_config.sh configs/nightly.json)
#   source <(scripts/load_config.sh configs/release-9.0.7.json)
#
# With --gitlab-vars, outputs GitLab pipeline variable JSON array instead:
#   scripts/load_config.sh --gitlab-vars configs/release-9.0.7.json

CONFIG=""
GITLAB_VARS=0

for arg in "$@"; do
    case "$arg" in
        --gitlab-vars) GITLAB_VARS=1 ;;
        *) CONFIG="$arg" ;;
    esac
done

if [ -z "$CONFIG" ]; then
    echo "Usage: $(basename "$0") [--gitlab-vars] <config.json>" >&2
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: Config file not found: $CONFIG" >&2
    exit 1
fi

# Parse JSON with python3 (available in most build environments) or jq as fallback
parse_json() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "
import json, sys
with open('$CONFIG') as f:
    c = json.load(f)

kicad = c.get('kicad', {})
libs = c.get('libraries', {})
deps = c.get('dependencies', {})

python_cfg = c.get('python', {})
kipy_cfg = c.get('kipy', {})
freerouting_cfg = c.get('freerouting', {})

vars = {
    'APPIMAGE_PREFIX': c.get('appimage_prefix', ''),
    'UPLOAD_SUBDIR': c.get('upload_subdir', ''),
    'KICAD_BUILD_TYPE': c.get('build_type', 'Nightly'),
    'KICAD_BUNDLE_NAME': c.get('bundle_name', 'KiCad'),
    'KICAD_BRANCH': kicad.get('ref', 'master'),
    'KICAD_SOURCE_PATH': kicad.get('source_path', ''),
    'KICAD_RELEASE_TAG': c.get('release_tag', kicad.get('ref', '')) if c.get('build_type') == 'Release' else '',
    'KICAD_SYMBOLS_BRANCH': libs.get('symbols', 'master'),
    'KICAD_FOOTPRINTS_BRANCH': libs.get('footprints', 'master'),
    'KICAD_TEMPLATES_BRANCH': libs.get('templates', 'master'),
    'KICAD_PACKAGES3D_BRANCH': libs.get('packages3d', 'master'),
    'LIBRARIES_SOURCE_PATH': libs.get('source_path', ''),
    'WX_VERSION': deps.get('wx', ''),
    'WXPYTHON_VERSION': deps.get('wxpython', ''),
    'OCCT_VERSION': deps.get('occt', ''),
    'NGSPICE_VERSION': deps.get('ngspice', ''),
    'PYTHON_VERSION': python_cfg.get('version', ''),
    'ENABLE_KIPY_BUNDLE': '1' if kipy_cfg.get('enabled', False) else '',
    'KIPY_SOURCE_PATH': kipy_cfg.get('source_path', ''),
    'ENABLE_FREEROUTING_BUNDLE': '1' if freerouting_cfg.get('enabled', False) else '',
    'FREEROUTING_SOURCE_PATH': freerouting_cfg.get('source_path', ''),
}

patches = c.get('patches', [])
if patches:
    vars['KICAD_PATCHES'] = ' '.join(patches)

cmake_extra_args = c.get('cmake_extra_args', [])
if cmake_extra_args:
    vars['CMAKE_EXTRA_ARGS'] = ' '.join(cmake_extra_args)

python_extra_packages = python_cfg.get('extra_packages', [])
if python_extra_packages:
    vars['APT_EXTRA_PACKAGES'] = ' '.join(python_extra_packages)

if c.get('wx_egl'):
    vars['WX_USE_EGL'] = 'ON'

if $GITLAB_VARS:
    entries = [{'key': k, 'value': v} for k, v in vars.items() if v]
    json.dump(entries, sys.stdout, indent=2)
else:
    for k, v in vars.items():
        if v:
            print(f'{k}=\"{v}\"')
"
    elif command -v jq >/dev/null 2>&1; then
        build_type=$(jq -r '.build_type // "Nightly"' "$CONFIG")
        kicad_ref=$(jq -r '.kicad.ref // "master"' "$CONFIG")
        release_tag=$(jq -r '.release_tag // empty' "$CONFIG")

        if [ "$GITLAB_VARS" -eq 1 ]; then
            jq -r --arg bt "$build_type" --arg kr "$kicad_ref" '
                [
                    (if .appimage_prefix then {key: "APPIMAGE_PREFIX", value: .appimage_prefix} else empty end),
                    (if .upload_subdir then {key: "UPLOAD_SUBDIR", value: .upload_subdir} else empty end),
                    {key: "KICAD_BUILD_TYPE", value: $bt},
                    {key: "KICAD_BRANCH", value: $kr},
                    (if $bt == "Release" then {key: "KICAD_RELEASE_TAG", value: (.release_tag // $kr)} else empty end),
                    {key: "KICAD_SYMBOLS_BRANCH", value: (.libraries.symbols // "master")},
                    {key: "KICAD_FOOTPRINTS_BRANCH", value: (.libraries.footprints // "master")},
                    {key: "KICAD_TEMPLATES_BRANCH", value: (.libraries.templates // "master")},
                    {key: "KICAD_PACKAGES3D_BRANCH", value: (.libraries.packages3d // "master")},
                    {key: "WX_VERSION", value: (.dependencies.wx // empty)},
                    {key: "WXPYTHON_VERSION", value: (.dependencies.wxpython // empty)},
                    {key: "OCCT_VERSION", value: (.dependencies.occt // empty)},
                    {key: "NGSPICE_VERSION", value: (.dependencies.ngspice // empty)},
                    (if .patches then {key: "KICAD_PATCHES", value: (.patches | join(" "))} else empty end),
                    (if .wx_egl then {key: "WX_USE_EGL", value: "ON"} else empty end)
                ] | map(select(.value != null and .value != ""))
            ' "$CONFIG"
        else
            echo "KICAD_BUILD_TYPE=\"$build_type\""
            echo "KICAD_BRANCH=\"$kicad_ref\""
            [ "$build_type" = "Release" ] && echo "KICAD_RELEASE_TAG=\"${release_tag:-$kicad_ref}\""
            jq -r '
                (if .appimage_prefix then "APPIMAGE_PREFIX=\"\(.appimage_prefix)\"" else empty end),
                (if .upload_subdir then "UPLOAD_SUBDIR=\"\(.upload_subdir)\"" else empty end),
                "KICAD_SYMBOLS_BRANCH=\"\(.libraries.symbols // "master")\"",
                "KICAD_FOOTPRINTS_BRANCH=\"\(.libraries.footprints // "master")\"",
                "KICAD_TEMPLATES_BRANCH=\"\(.libraries.templates // "master")\"",
                "KICAD_PACKAGES3D_BRANCH=\"\(.libraries.packages3d // "master")\"",
                (if .dependencies.wx then "WX_VERSION=\"\(.dependencies.wx)\"" else empty end),
                (if .dependencies.wxpython then "WXPYTHON_VERSION=\"\(.dependencies.wxpython)\"" else empty end),
                (if .dependencies.occt then "OCCT_VERSION=\"\(.dependencies.occt)\"" else empty end),
                (if .dependencies.ngspice then "NGSPICE_VERSION=\"\(.dependencies.ngspice)\"" else empty end),
                (if .patches then "KICAD_PATCHES=\"\(.patches | join(" "))\"" else empty end),
                (if .wx_egl then "WX_USE_EGL=\"ON\"" else empty end)
            ' "$CONFIG"
        fi
    else
        echo "ERROR: Neither python3 nor jq found" >&2
        exit 1
    fi
}

parse_json
