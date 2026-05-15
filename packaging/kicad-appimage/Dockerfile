# Main Dockerfile for building KiCad AppImage
# Allow building without explicitly passing REGISTRY by providing a sensible default.
ARG REGISTRY=registry.gitlab.com/kicad/packaging/kicad-appimage
ARG APPDIR_IMAGE=""

# Dependency image tags default to latest for local builds.
# CI passes version-specific tags to prevent race conditions when concurrent
# pipelines pin different dependency versions (e.g. OCCT V7_8_1 vs V7_9_1).
ARG BASE_TAG=latest
ARG WX_TAG=latest
ARG WXPYTHON_TAG=latest
ARG NGSPICE_TAG=latest
ARG OCCT_TAG=latest
ARG LIBS_TAG=latest
ARG PACKAGES3D_TAG=latest

# Import all dependency images using pinned tags
FROM ${REGISTRY}/base:${BASE_TAG} AS base
FROM ${REGISTRY}/wx:${WX_TAG} AS wx
FROM ${REGISTRY}/wxpython:${WXPYTHON_TAG} AS wxpython
FROM ${REGISTRY}/ngspice:${NGSPICE_TAG} AS ngspice
FROM ${REGISTRY}/occt:${OCCT_TAG} AS occt
FROM ${REGISTRY}/libs:${LIBS_TAG} AS libs
FROM ${REGISTRY}/packages3d:${PACKAGES3D_TAG} AS packages3d

#############################
# Stage: Build KiCad
#############################
FROM base AS kicad-build

ARG KICAD_BUILD_RELEASE=nightly
ARG KICAD_BUILD_TYPE=RelWithDebInfo
ARG CMAKE_EXTRA_ARGS
ARG KICAD_PATCHES

# Copy all dependency layers (toolchains, libs)
COPY --from=wx / /
COPY --from=wxpython / /
COPY --from=ngspice / /
COPY --from=occt / /
COPY --from=libs / /

# Source context (passed through build context kicad-src)
COPY --from=kicad-src . /src/kicad

# Copy and apply AppImage-specific patches
COPY patches/ /tmp/patches/

WORKDIR /src/kicad

# Apply patches for AppImage-specific modifications.
# When KICAD_PATCHES is set (from config), apply only the listed patches.
# When empty, apply all patches for backward compatibility with direct CI usage.
RUN if [ -n "$KICAD_PATCHES" ]; then \
        for p in $KICAD_PATCHES; do \
            echo "Applying patch: $p"; \
            patch -p1 < "/tmp/patches/$p"; \
        done; \
    else \
        for p in /tmp/patches/*.patch; do \
            [ -f "$p" ] && patch -p1 < "$p" || true; \
        done; \
    fi

# Inline CDN resources into HTML templates so they work offline and in
# WebKit2GTK's file:// security context (which blocks cross-origin requests).
RUN set -ex \
    && mkdir -p /tmp/cdn \
    && wget -q -O /tmp/cdn/tailwind.js    "https://cdn.tailwindcss.com/3.4.17" \
    && wget -q -O /tmp/cdn/hljs.js        "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js" \
    && wget -q -O /tmp/cdn/hljs-dark.css   "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/vs2015.min.css" \
    && wget -q -O /tmp/cdn/xterm.css      "https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css" \
    && wget -q -O /tmp/cdn/xterm.js       "https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js" \
    && wget -q -O /tmp/cdn/xterm-fit.js   "https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js" \
    && wget -q -O /tmp/cdn/xterm-uni.js   "https://cdn.jsdelivr.net/npm/xterm-addon-unicode11@0.6.0/lib/xterm-addon-unicode11.min.js" \
    && echo "[cdn] Downloaded CDN resources for offline embedding"

# Patch agent HTML template: replace CDN script/link tags with inline content
COPY <<'PYEOF' /tmp/patch_agent_html.py
import pathlib
tmpl = pathlib.Path('/src/kicad/agent/view/unified_template.html')
html = tmpl.read_text()
for old_tag, cdn_file, wrapper in [
    ('<script src="https://cdn.tailwindcss.com/3.4.17"></script>',
     '/tmp/cdn/tailwind.js', 'script'),
    ('<link id="hljs-theme" rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/vs2015.min.css">',
     '/tmp/cdn/hljs-dark.css', 'style id="hljs-theme"'),
    ('<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>',
     '/tmp/cdn/hljs.js', 'script'),
]:
    content = open(cdn_file).read()
    close_tag = wrapper.split()[0]
    html = html.replace(old_tag, '<' + wrapper + '>' + content + '</' + close_tag + '>')
tmpl.write_text(html)
print('[cdn] Patched agent unified_template.html (%d bytes)' % len(html))
PYEOF
RUN python3 /tmp/patch_agent_html.py

# Patch terminal: inline xterm.js CSS and JS into the C++ source.
# The HTML lives inside a R"HTML(...)HTML" raw string literal so we only
# need to worry about the wxString::Format %% escaping for literal %.
COPY <<'PYEOF' /tmp/patch_terminal_cpp.py
import pathlib, re
src = pathlib.Path("/src/kicad/terminal/pty_webview_panel.cpp")
code = src.read_text()
replacements = [
    ('<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css">',
     "/tmp/cdn/xterm.css", "style"),
    ('<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>',
     "/tmp/cdn/xterm.js", "script"),
    ('<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>',
     "/tmp/cdn/xterm-fit.js", "script"),
    ('<script src="https://cdn.jsdelivr.net/npm/xterm-addon-unicode11@0.6.0/lib/xterm-addon-unicode11.min.js"></script>',
     "/tmp/cdn/xterm-uni.js", "script"),
]
for old_tag, cdn_file, wrapper in replacements:
    content = open(cdn_file).read()
    content = re.sub(r'%(?!%)', '%%', content)
    code = code.replace(old_tag, "<" + wrapper + ">" + content + "</" + wrapper + ">")
src.write_text(code)
print("[cdn] Patched terminal pty_webview_panel.cpp (%d bytes)" % len(code))
PYEOF
RUN python3 /tmp/patch_terminal_cpp.py

# Build and install into AppDir
# Clean any stale CMake state that may come from a local source context
# Use CMake's built-in FindProtobuf module instead of config mode (Debian testing compatibility)
RUN set -ex \
    && rm -rf build CMakeCache.txt CMakeFiles \
    && mkdir -p build \
    && cd build \
    && cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DDEFAULT_INSTALL_PATH=/usr \
        -DKICAD_USE_CMAKE_FINDPROTOBUF=ON \
        ${CMAKE_EXTRA_ARGS:-} \
        .. \
    && ninja -j"$(nproc)" \
    && DESTDIR=/tmp/AppDir ninja install

#############################
# Stage: Base for AppImage assembly
#############################
FROM base AS appimage-base
ARG KICAD_BUILD_RELEASE
ARG KICAD_BUILD_MAJVERSION
ARG KICAD_BUILD_TYPE
ARG KICAD_BUNDLE_NAME=KiCad
ARG ENABLE_PYTHON_BUNDLE=1
ARG PYTHON_VERSION
ARG PYTHON_APT_PACKAGES
ARG APT_EXTRA_PACKAGES
ARG APPIMAGE_COMPRESSION=zstd
ARG APP_VERSION
ARG APPIMAGE_TOOL_SOURCE=go-appimage
ARG GO_APPIMAGE_CONTINUOUS_URL
# Zeo-specific: enable kipy and freerouting bundling
ARG ENABLE_KIPY_BUNDLE=0
ARG ENABLE_FREEROUTING_BUNDLE=0

ENV KICAD_BUILD_RELEASE=${KICAD_BUILD_RELEASE} \
    KICAD_BUILD_MAJVERSION=${KICAD_BUILD_MAJVERSION} \
    KICAD_BUILD_TYPE=${KICAD_BUILD_TYPE} \
    KICAD_BUNDLE_NAME=${KICAD_BUNDLE_NAME} \
    ENABLE_PYTHON_BUNDLE=${ENABLE_PYTHON_BUNDLE} \
    PYTHON_VERSION=${PYTHON_VERSION} \
    PYTHON_APT_PACKAGES=${PYTHON_APT_PACKAGES} \
    APT_EXTRA_PACKAGES=${APT_EXTRA_PACKAGES} \
    APPIMAGE_COMPRESSION=${APPIMAGE_COMPRESSION} \
    APP_VERSION=${APP_VERSION} \
    APPIMAGE_TOOL_SOURCE=${APPIMAGE_TOOL_SOURCE} \
    GO_APPIMAGE_CONTINUOUS_URL=${GO_APPIMAGE_CONTINUOUS_URL} \
    ENABLE_KIPY_BUNDLE=${ENABLE_KIPY_BUNDLE} \
    ENABLE_FREEROUTING_BUNDLE=${ENABLE_FREEROUTING_BUNDLE} \
    RUNTIME_ENV_FILE=/go-appimage.yml \
    PRIMARY_DESKTOP=usr/share/applications/org.zeo.kicad.desktop

# zstd CLI needed for debug symbol archive compression (libzstd-dev in base is library-only)
RUN apt-get update && apt-get install -y --no-install-recommends zstd && rm -rf /var/lib/apt/lists/*

# Copy built KiCad tree
COPY --from=kicad-build /tmp/AppDir /tmp/AppDir

# Fix desktop file Exec= to match the actual binary name (OUTPUT_NAME Zeo in CMake)
RUN sed -i 's/^Exec=kicad/Exec=Zeo/' /tmp/AppDir/usr/share/applications/org.zeo.kicad.desktop

# Add helper scripts and configuration (kept under version control)
COPY scripts/ /scripts/
COPY go-appimage.yml /go-appimage.yml
RUN chmod +x /scripts/*.sh

WORKDIR /tmp

# Bundle Python (optional, controlled by ENABLE_PYTHON_BUNDLE)
RUN /scripts/bundle_python.sh || (echo "Python bundling script exited with code $?" >&2)

# Bundle kipy/kicad-python (optional, controlled by ENABLE_KIPY_BUNDLE)
# Copy zeo-python source from build context and KiCad source for proto files
COPY --from=zeo-python-src . /src/zeo-python
COPY --from=kicad-build /src/kicad/api/proto /src/kicad/api/proto

# Python script to fix protoc-generated imports (must be a file, not inline)
COPY <<'PYEOF' /tmp/fix_proto_imports.py
import os, re, fnmatch, sys
output_path = sys.argv[1]
input_path = sys.argv[2]
pkg_prefix = 'kipy.proto'
proto_packages = sorted(e for e in os.listdir(input_path) if os.path.isdir(os.path.join(input_path, e)))
if proto_packages:
    pkg_pattern = '|'.join(re.escape(p) for p in proto_packages)
    import_re = re.compile(r'^(from )(' + pkg_pattern + r')([\s.])', re.MULTILINE)
    for root, _, files in os.walk(output_path):
        for fn in fnmatch.filter(files, '*_pb2.py') + fnmatch.filter(files, '*_pb2.pyi'):
            fp = os.path.join(root, fn)
            with open(fp) as f: content = f.read()
            fixed = import_re.sub(r'\g<1>' + pkg_prefix + r'.\2\3', content)
            if fixed != content:
                with open(fp, 'w') as f: f.write(fixed)
    print('[kipy] Fixed proto imports for %d packages' % len(proto_packages))
PYEOF

# Bundle kipy: generate protos, install package + dependencies into AppDir.
RUN set -ex && \
    if [ "${ENABLE_KIPY_BUNDLE}" != "1" ]; then echo "[kipy] Skipping (disabled)"; exit 0; fi && \
    SITE="/tmp/AppDir/usr/lib/python${PYTHON_VERSION}/dist-packages" && \
    mkdir -p "$SITE" && \
    PROTO_IN="/src/kicad/api/proto" && \
    PROTO_OUT="/src/zeo-python/kipy/proto" && \
    echo "[kipy] Proto input files:" && find "$PROTO_IN" -name '*.proto' && \
    mkdir -p "$PROTO_OUT" && \
    protoc --python_out="$PROTO_OUT" --proto_path="$PROTO_IN" \
        $(find "$PROTO_IN" -name '*.proto') && \
    echo "[kipy] Generated proto files:" && find "$PROTO_OUT" -name '*_pb2.py' && \
    cd /src/zeo-python && python3 /tmp/fix_proto_imports.py kipy/proto /src/kicad/api/proto && \
    # Create version file (generated at build time, listed in .gitignore)
    echo 'KICAD_API_VERSION = "0.1.1"' > /src/zeo-python/kipy/kicad_api_version.py && \
    pip3 install --break-system-packages --quiet poetry-core setuptools && \
    pip3 install --break-system-packages --target "$SITE" --upgrade \
        "protobuf>=5.0" "pynng>=0.8.0" typing_extensions kiutils && \
    pip3 install --break-system-packages --target "$SITE" --no-deps --upgrade /src/zeo-python && \
    cp -r "$PROTO_OUT/"* "$SITE/kipy/proto/" && \
    PB2_COUNT=$(find "$SITE/kipy/proto" -name '*_pb2.py' | wc -l) && \
    echo "[kipy] Installed kipy with $PB2_COUNT proto files to $SITE" && \
    pip3 install --break-system-packages --target "$SITE" --upgrade \
        textual mcp matplotlib Pillow && \
    # Overwrite the old apt-installed typing_extensions with the pip version
    # (apt version lacks 'deprecated' needed by protobuf/pydantic)
    cp -f "$SITE/typing_extensions.py" /tmp/AppDir/usr/lib/python3/dist-packages/typing_extensions.py 2>/dev/null || true && \
    echo "[kipy] CLI dependencies installed"

# Bundle Freerouting (optional, controlled by ENABLE_FREEROUTING_BUNDLE)
# Requires freerouting source to be mounted at /src/freerouting
RUN if [ "${ENABLE_FREEROUTING_BUNDLE}" = "1" ] && [ -f /scripts/bundle_freerouting.sh ]; then \
        /scripts/bundle_freerouting.sh || echo "Freerouting bundling failed (non-fatal)"; \
    fi

# When APPDIR_IMAGE is set, use pre-compiled image from registry.
# When empty (local builds), fall back to the inline appimage-base stage.
FROM ${APPDIR_IMAGE:-appimage-base} AS appdir

# Build standard AppImage (zstd)
FROM appdir AS appimage
ARG APPIMAGE_PREFIX=kicad-nightly
ARG EXTRACT_DBGSYM=0
ARG DBGSYM_ARCHIVE=dbgsym.tar.zst
ENV APPIMAGE_PREFIX=${APPIMAGE_PREFIX} \
    EXTRACT_DBGSYM=${EXTRACT_DBGSYM} \
    DBGSYM_ARCHIVE=${DBGSYM_ARCHIVE}

# Copy library data into /tmp/AppDir.
# The libs and packages3d images install under /usr/installtemp/ via cmake
# --prefix.  Map installtemp/share → AppDir/usr/share so paths resolve at runtime.
COPY --from=packages3d /usr/installtemp/share /tmp/AppDir/usr/share
# Copy generic libs early so specialized builds can override
COPY --from=libs /usr/installtemp/share /tmp/AppDir/usr/share
COPY --from=ngspice / /tmp/AppDir/
# Place OCCT near end but before wx to allow wx override only of wx libs
COPY --from=occt / /tmp/AppDir/
# Ensure dedicated wx build overrides any wx libraries pulled in by libs
COPY --from=wx / /tmp/AppDir/
# Finally add wxPython bindings built against that wx
COPY --from=wxpython / /tmp/AppDir/

# Copy wrapper scripts (both kicad.sh and zeo.sh for flexibility)
COPY kicad.sh /tmp/AppDir/usr/bin/kicad.sh
COPY zeo.sh /tmp/AppDir/usr/bin/zeo.sh
RUN chmod +x /tmp/AppDir/usr/bin/kicad.sh /tmp/AppDir/usr/bin/zeo.sh

# Sanity check: import KiCad Python modules (pcbnew + wxPython) with bundled Python
RUN if [ -x /tmp/AppDir/usr/bin/python3 ]; then \
        echo '[sanity] Testing KiCad Python imports (pcbnew + wx required)'; \
        PY_VER=$(ls -d /tmp/AppDir/usr/lib/python3.*/lib-dynload 2>/dev/null | head -1 | grep -o '3\.[0-9]*' || echo "3"); \
        export LD_LIBRARY_PATH="/tmp/AppDir/usr/lib/x86_64-linux-gnu:/tmp/AppDir/usr/lib:/tmp/AppDir/lib:/tmp/AppDir/lib/x86_64-linux-gnu:/tmp/AppDir/usr/opencascade:$LD_LIBRARY_PATH"; \
        export PYTHONPATH="/tmp/AppDir/usr/local/lib/python${PY_VER}/dist-packages:/tmp/AppDir/usr/lib/python3/dist-packages:$PYTHONPATH"; \
        printf '%s\n' \
            'import sys,importlib,traceback,os' \
            'print("[sanity] Python:",sys.version)' \
            'print("[sanity] sys.path:")' \
            'import pprint; pprint.pp(sys.path)' \
            'print("[sanity] LD_LIBRARY_PATH:", os.environ.get("LD_LIBRARY_PATH", "Not set"))' \
            'def try_import(name, required=False):' \
            '    print(f"[sanity] Importing {name}...")' \
            '    try:' \
            '        m=importlib.import_module(name)' \
            '        ver=getattr(m, "__version__", getattr(m, "version", None))' \
            '        print(f"[sanity] OK {name} version={ver}")' \
            '    except Exception as e:' \
            '        print(f"[sanity] FAIL {name}: {e}")' \
            '        if required:' \
            '            traceback.print_exc()' \
            '            raise' \
            'try_import("pcbnew", required=True)' \
            'try_import("wx", required=True)' \
            'try_import("wx.aui", required=True)' \
            > /tmp/py_sanity.py; \
        /tmp/AppDir/usr/bin/python3 /tmp/py_sanity.py || { echo 'Python import sanity check failed' >&2; exit 1; }; \
    else echo '[sanity] Skipped python import test (python not bundled)'; fi

# Build AppImage and rename to match naming convention
RUN /scripts/build_appimage.sh
RUN /scripts/rename_appimage.sh /tmp "${APPIMAGE_PREFIX}" "${KICAD_BUILD_RELEASE}"

# Use a minimal base instead of scratch to avoid export issues
FROM alpine:latest AS build-kicad
COPY --from=appimage /tmp/*.AppImage /
RUN --mount=from=appimage,source=/tmp,target=/staging \
    cp /staging/*-dbgsym.tar.zst / 2>/dev/null || true

# Build light AppImage (no 3D packages, gzip compression)
FROM appdir AS appimage-light
ARG APPIMAGE_COMPRESSION=gzip
ARG APPIMAGE_PREFIX=kicad-nightly
ENV APPIMAGE_COMPRESSION=${APPIMAGE_COMPRESSION} \
    APPIMAGE_PREFIX=${APPIMAGE_PREFIX}

# Copy dependency layers (same as full build, minus packages3d for smaller size)
COPY --from=libs /usr/installtemp/share /tmp/AppDir/usr/share
COPY --from=ngspice / /tmp/AppDir/
COPY --from=occt / /tmp/AppDir/
COPY --from=wx / /tmp/AppDir/
COPY --from=wxpython / /tmp/AppDir/

# Copy wrapper scripts (both kicad.sh and zeo.sh for flexibility)
COPY kicad.sh /tmp/AppDir/usr/bin/kicad.sh
COPY zeo.sh /tmp/AppDir/usr/bin/zeo.sh
RUN chmod +x /tmp/AppDir/usr/bin/kicad.sh /tmp/AppDir/usr/bin/zeo.sh
RUN APPIMAGE_COMPRESSION=gzip /scripts/build_appimage.sh
RUN /scripts/rename_appimage.sh /tmp "${APPIMAGE_PREFIX}" "${KICAD_BUILD_RELEASE}" lite

# Use a minimal base instead of scratch to avoid export issues
FROM alpine:latest AS build-kicad-light
COPY --from=appimage-light /tmp/*.AppImage /
