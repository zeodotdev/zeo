#!/usr/bin/env bash
set -euo pipefail

# Orchestrates transforming a KiCad build AppDir into an AppImage using go-appimage.
# All configuration comes from environment / GitLab CI variables, nothing hard-coded here.
# Expected env vars (all optional with sensible defaults):
#  APPDIR=/tmp/AppDir
#  OUTPUT_DIR=/tmp
#  APPIMAGE_TOOL_SOURCE=go-appimage           # 'go-appimage' (default) or 'appimagetool'
#  GO_APPIMAGE_CONTINUOUS_URL=https://github.com/probonopd/go-appimage/releases/expanded_assets/continuous
#  APPIMAGE_COMPRESSION=zstd                  # 'zstd' or 'gzip'
#  APP_VERSION (falls back to KICAD_BUILD_RELEASE)
#  ARCH=x86_64
#  PRIMARY_DESKTOP=usr/share/applications/org.kicad.kicad.desktop
#  EXTRA_DEPLOY_DESKTOPS=""                # space separated additional .desktop files
#  APP_NAME=KiCad
#  KICAD_BUILD_RELEASE (provided upstream)
#  SKIP_DEPLOY=0
#  ADDITIONAL_ENV_SNIPPETS="/tmp/AppDir/.env-python"
#  RUNTIME_ENV_FILE=go-appimage.yml (we patch env section if ADDITIONAL_ENV_SNIPPETS provided)
#  EXTRACT_DBGSYM=0                          # set to 1 to extract debug symbols before stripping
#  DBGSYM_ARCHIVE=dbgsym.tar.zst             # filename for the compressed debug symbol archive

APPDIR=${APPDIR:-/tmp/AppDir}
OUTPUT_DIR=${OUTPUT_DIR:-/tmp}
APPIMAGE_COMPRESSION=${APPIMAGE_COMPRESSION:-zstd}
ARCH=${ARCH:-x86_64}
APP_VERSION=${APP_VERSION:-${KICAD_BUILD_RELEASE:-dev}}
GO_APPIMAGE_CONTINUOUS_URL=${GO_APPIMAGE_CONTINUOUS_URL:-"https://github.com/probonopd/go-appimage/releases/expanded_assets/continuous"}
PRIMARY_DESKTOP=${PRIMARY_DESKTOP:-usr/share/applications/org.kicad.kicad.desktop}
EXTRA_DEPLOY_DESKTOPS=${EXTRA_DEPLOY_DESKTOPS:-""}
APPIMAGE_TOOL_SOURCE=${APPIMAGE_TOOL_SOURCE:-go-appimage}
SKIP_DEPLOY=${SKIP_DEPLOY:-0}
ADDITIONAL_ENV_SNIPPETS=${ADDITIONAL_ENV_SNIPPETS:-""}
RUNTIME_ENV_FILE=${RUNTIME_ENV_FILE:-go-appimage.yml}
EXTRACT_DBGSYM=${EXTRACT_DBGSYM:-0}
DBGSYM_ARCHIVE=${DBGSYM_ARCHIVE:-"dbgsym.tar.zst"}

cd "$(dirname "$0")/.."

fetch_go_appimage(){
  local url
  url="https://github.com/$(wget -q "${GO_APPIMAGE_CONTINUOUS_URL}" -O - | grep "appimagetool-.*-x86_64.AppImage" | head -n1 | cut -d '"' -f2)"
  wget -q "${url}" -O appimagetool.AppImage
  chmod +x appimagetool.AppImage
  ./appimagetool.AppImage --appimage-extract >/dev/null 2>&1
  mv squashfs-root/usr/bin/* /usr/local/bin/
  rm -rf squashfs-root appimagetool.AppImage
}

prepare_runtime_env(){
  # If environment snippet files exist, append them to config env section (simple append comment at end)
  if [[ -n "${ADDITIONAL_ENV_SNIPPETS}" ]]; then
    for f in ${ADDITIONAL_ENV_SNIPPETS}; do
      if [[ -f "${f}" ]]; then
        echo "# Injecting env snippet from ${f}" >&2
        # Append as comment; actual export handled in wrapper if needed
        cat "${f}" >> "${RUNTIME_ENV_FILE}" || true
      fi
    done
  fi
}

remove_gl_libs(){
  # Remove bundled GL/EGL/GLVND libraries that conflict with host GPU drivers.
  # go-appimage's deploy step automatically bundles GLVND dispatch libraries, but these
  # are just dispatchers that need vendor-specific backends (Mesa, NVIDIA, etc.) which
  # are NOT bundled. Keeping the dispatchers causes them to shadow the host's working GL
  # stack, resulting in "Could not create OpenGL Context" errors.
  echo "[build_appimage] Removing bundled GL/EGL/GLVND libraries (using host GPU drivers)" >&2
  local count=0
  for pattern in libGL.so* libGLX.so* libEGL.so* libGLdispatch.so* libOpenGL.so* libgbm.so* libdrm.so*; do
    for f in "${APPDIR}"/lib/x86_64-linux-gnu/${pattern} "${APPDIR}"/usr/lib/x86_64-linux-gnu/${pattern}; do
      if [[ -f "$f" || -L "$f" ]]; then
        rm -f "$f"
        count=$((count + 1))
      fi
    done
  done
  echo "[build_appimage] Removed ${count} GL-related libraries" >&2
}

remove_webkit_libs(){
  # Remove bundled WebKit2GTK libraries so the host's version is used at runtime.
  # WebKit2GTK spawns sub-processes (WebKitWebProcess, WebKitNetworkProcess) from
  # hard-coded paths on the host.  Bundling partial WebKit libraries causes ABI
  # mismatches with those host processes, breaking wxWebView (used by Agent and
  # Terminal).  Like GL drivers, WebKit2GTK must come from the host system.
  echo "[build_appimage] Removing bundled WebKit2GTK libraries (using host WebKit)" >&2
  local count=0
  for pattern in libwebkit2gtk* libjavascriptcoregtk* libsoup-3* libsoup-2*; do
    for f in "${APPDIR}"/lib/x86_64-linux-gnu/${pattern} "${APPDIR}"/usr/lib/x86_64-linux-gnu/${pattern}; do
      if [[ -f "$f" || -L "$f" ]]; then
        rm -f "$f"
        count=$((count + 1))
      fi
    done
  done
  echo "[build_appimage] Removed ${count} WebKit-related libraries" >&2
}

remove_duplicate_ld_linux(){
  # go-appimage deploys ld-linux into both lib64/ (patched, executable) and
  # usr/lib64/ (original, non-executable). The AppRun find command picks up
  # the usr/lib64/ copy first, which lacks execute permission, causing
  # "Permission denied" when trying to exec the bundled ld-linux on older hosts.
  echo "[build_appimage] Removing duplicate ld-linux from usr/lib64/" >&2
  local count=0
  for f in "${APPDIR}"/usr/lib64/ld-*.so*; do
    if [[ -f "$f" ]]; then
      rm -f "$f"
      count=$((count + 1))
    fi
  done
  echo "[build_appimage] Removed ${count} duplicate ld-linux files" >&2
}

extract_debug_symbols(){
  if [[ "${EXTRACT_DBGSYM}" != "1" ]]; then
    echo "[build_appimage] Debug symbol extraction disabled (EXTRACT_DBGSYM=${EXTRACT_DBGSYM})" >&2
    return 0
  fi

  local dbgdir="${OUTPUT_DIR}/dbgsym"
  echo "[build_appimage] Extracting debug symbols from ELF binaries" >&2

  local count=0
  while IFS= read -r f; do
    local relpath="${f#${APPDIR}/}"
    local dbgfile="${dbgdir}/${relpath}.debug"
    mkdir -p "$(dirname "${dbgfile}")"
    objcopy --only-keep-debug "$f" "${dbgfile}" 2>/dev/null && count=$((count + 1))
  done < <(find "${APPDIR}" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.kiface' -o -executable \) -print0 \
           | xargs -0 file \
           | awk -F: '/ELF.*not stripped/{print $1}')

  echo "[build_appimage] Extracted debug symbols from ${count} ELF files" >&2

  if [[ ${count} -gt 0 ]]; then
    local archive="${OUTPUT_DIR}/${DBGSYM_ARCHIVE}"
    echo "[build_appimage] Compressing debug symbols to ${archive}" >&2
    tar -C "${dbgdir}" -cf - . | zstd -19 -T0 -o "${archive}"
    local size
    size=$(du -h "${archive}" | cut -f1)
    echo "[build_appimage] Debug symbol archive: ${archive} (${size})" >&2
  fi

  rm -rf "${dbgdir}"
}

strip_binaries(){
  # Remove symbols and debug info from ELF binaries in the AppDir.
  # --strip-unneeded removes all symbols not needed for relocation (safe for .so and executables).
  # .kiface files are KiCad's plugin shared objects (largest binaries with debug info).
  local before after
  before=$(du -sm "${APPDIR}" | cut -f1)
  echo "[build_appimage] Stripping ELF binaries in AppDir (${before}MB before)" >&2

  # Log the 10 largest unstripped ELF files for diagnostics
  echo "[build_appimage] Largest unstripped ELF files:" >&2
  find "${APPDIR}" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.kiface' -o -executable \) -print0 \
    | xargs -0 file \
    | awk -F: '/ELF.*not stripped/{print $1}' \
    | xargs -I{} du -m {} 2>/dev/null \
    | sort -rn \
    | head -10 >&2

  local count=0
  while IFS= read -r f; do
    strip --strip-unneeded "$f" 2>/dev/null && count=$((count + 1))
  done < <(find "${APPDIR}" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.kiface' -o -executable \) -print0 \
           | xargs -0 file \
           | awk -F: '/ELF.*not stripped/{print $1}')
  after=$(du -sm "${APPDIR}" | cut -f1)
  echo "[build_appimage] Stripped ${count} ELF files (${before}MB -> ${after}MB)" >&2
}

run_deploy(){
  if [[ "${SKIP_DEPLOY}" == "1" ]]; then
    echo "[build_appimage] Skipping deploy step per SKIP_DEPLOY=1" >&2
    return 0
  fi
  appimagetool -s deploy "${APPDIR}/${PRIMARY_DESKTOP}" || true
  for d in ${EXTRA_DEPLOY_DESKTOPS}; do
    [[ -f "${APPDIR}/${d}" ]] && appimagetool -s deploy "${APPDIR}/${d}" || true
  done

  remove_gl_libs
  remove_webkit_libs
  remove_duplicate_ld_linux

  # Determine KiCad major version for versioned environment variable names (KICAD9_*, etc.)
  local kicad_maj="${KICAD_BUILD_MAJVERSION:-9}"

  # Patch the go-appimage generated AppRun to add KiCad-specific environment variables
  # This preserves all the useful setup (SSL certs, GStreamer, GTK theming) while adding our paths
  if [[ -f "${APPDIR}/AppRun" ]]; then
    echo "[build_appimage] Patching AppRun with KiCad environment variables (major=${kicad_maj})" >&2
    # Add APPDIR, KiCad paths, library env overrides, and GIO isolation after XDG_DATA_DIRS line.
    # APPDIR is required so KiCad can resolve its own executable path when the bundled
    # ld-linux wrapper causes /proc/self/exe to point to the dynamic linker.
    sed -i '/^export XDG_DATA_DIRS=/a \
\
############################################################################################\
# Export APPDIR for KiCad executable path resolution\
# go-appimage uses ld-linux as a wrapper which makes /proc/self/exe resolve to the\
# dynamic linker instead of the actual binary. KiCad uses APPDIR to find kiface plugins.\
############################################################################################\
\
export APPDIR="${HERE}"\
\
############################################################################################\
# KiCad-specific resource paths\
############################################################################################\
\
export KICAD_STOCK_DATA_HOME="${HERE}"/usr/share/kicad\
\
############################################################################################\
# KiCad versioned library paths\
# The compile-time KICAD_LIBRARY_DATA resolves to /usr/share/kicad which does not exist\
# on the host. Override the versioned env vars to point into the AppImage.\
############################################################################################\
\
export KICAD'"${kicad_maj}"'_SYMBOL_DIR="${HERE}"/usr/share/kicad/symbols\
export KICAD'"${kicad_maj}"'_FOOTPRINT_DIR="${HERE}"/usr/share/kicad/footprints\
export KICAD'"${kicad_maj}"'_3DMODEL_DIR="${HERE}"/usr/share/kicad/3dmodels\
export KICAD'"${kicad_maj}"'_TEMPLATE_DIR="${HERE}"/usr/share/kicad/template\
\
############################################################################################\
# Isolate GIO modules to prevent glibc version conflicts with host system\
# Without this, host GIO modules compiled against newer glibc fail to load\
############################################################################################\
\
export GIO_MODULE_DIR="${HERE}"/usr/lib/x86_64-linux-gnu/gio/modules\
export GIO_EXTRA_MODULES=""\
\
############################################################################################\
# WebKit2GTK compositing workaround\
# Must be set before wxWebView is created to avoid rendering issues in GTK3 apps\
############################################################################################\
\
export WEBKIT_DISABLE_COMPOSITING_MODE=1' "${APPDIR}/AppRun"

    # Add subcommand routing before go-appimage's binary detection.
    # Without this, arguments like "kicad-cli" are passed to kicad as project file paths.
    # We check if $1 matches a KiCad binary and override MAIN so the correct executable is launched.
    sed -i '/^if \[ -z "\$ARGV0" \]/i \
############################################################################################\
# KiCad subcommand routing\
# Allow "./AppImage kicad-cli ..." to launch the correct binary through ld-linux\
############################################################################################\
\
KICAD_APPS="bitmap2component eeschema gerbview kicad kicad-cli pcb_calculator pcbnew pl_editor"\
for _kapp in $KICAD_APPS; do\
  if [ "$1" = "$_kapp" ]; then\
    MAIN="$_kapp"\
    shift\
    break\
  fi\
done\
' "${APPDIR}/AppRun"

    # Replace go-appimage's ld-linux block with runtime glibc version comparison.
    # go-appimage generates: if [ -e "$LD_LINUX" ]; then ... exec "$LD_LINUX" ... fi
    # We replace this entire tail with logic that chooses between host and bundled ld-linux
    # based on which glibc is newer. This avoids both:
    #   - shadowing host libstdc++ on newer hosts (CXXABI_1.3.15 not found)
    #   - requiring glibc >= 2.36 on older hosts (GLIBC_2.36 not found)
    sed -i '/^LD_LINUX=.*ld-\*\.so/d' "${APPDIR}/AppRun"
    sed -i '/^if \[ -e "\$LD_LINUX" \]/,$ d' "${APPDIR}/AppRun"
    cat >> "${APPDIR}/AppRun" <<'TAILEOF'

############################################################################################
# Runtime glibc version comparison
# Determines whether to use the host or bundled dynamic linker based on glibc version.
#
# Newer host (glibc >= bundled): Use host ld-linux so host GPU drivers and WebKit work
#   natively. Bundled libs found via RUNPATH set by go-appimage on all deployed ELFs.
#   LD_LIBRARY_PATH does NOT include lib/x86_64-linux-gnu to avoid shadowing host libstdc++.
#
# Older host (glibc < bundled): Use bundled ld-linux + bundled glibc. Add
#   lib/x86_64-linux-gnu to LD_LIBRARY_PATH so bundled glibc takes priority over host's.
#   Host GL drivers work because glibc is backward compatible.
############################################################################################

LD_LINUX=$(find "$HERE/lib64" -name 'ld-*.so.*' -executable 2>/dev/null | head -n 1)
if [ -z "$LD_LINUX" ]; then
  LD_LINUX=$(find "$HERE" -name 'ld-*.so.*' | head -n 1)
fi

if [ -e "$LD_LINUX" ] ; then
  export GCONV_PATH="$HERE/usr/lib/gconv"
  export FONTCONFIG_FILE="$HERE/etc/fonts/fonts.conf"
  export GTK_PATH=$(find "$HERE/lib" -name gtk-* -type d)
  export GTK_THEME=Default
  export GDK_PIXBUF_MODULEDIR=$(find "$HERE" -name loaders -type d -path '*gdk-pixbuf*')
  export GDK_PIXBUF_MODULE_FILE=$(find "$HERE" -name loaders.cache -type f -path '*gdk-pixbuf*')
  export XDG_DATA_DIRS="${HERE}"/usr/share/:"${XDG_DATA_DIRS}"
  export PERLLIB="${HERE}"/usr/share/perl5/:"${HERE}"/usr/lib/perl5/:"${PERLLIB}"
  export GSETTINGS_SCHEMA_DIR="${HERE}"/usr/share/glib-2.0/runtime-schemas/:"${HERE}"/usr/share/glib-2.0/schemas/:"${GSETTINGS_SCHEMA_DIR}"
  export QT_PLUGIN_PATH="$(readlink -f "$(dirname "$(find "${HERE}" -type d -path '*/plugins/platforms' 2>/dev/null)" 2>/dev/null)" 2>/dev/null)"

  BUNDLED_GLIBC_VER=$(strings "$LD_LINUX" 2>/dev/null | sed -n 's/.*release version \([0-9]*\.[0-9]*\).*/\1/p' | head -n 1)
  HOST_GLIBC_VER=$(/usr/bin/ldd --version 2>&1 | sed -n '1s/.* \([0-9]*\.[0-9]*\)$/\1/p')

  USE_BUNDLED_LD=1
  if [ -n "$HOST_GLIBC_VER" ] && [ -n "$BUNDLED_GLIBC_VER" ]; then
    NEWER=$(printf '%s\n%s\n' "$HOST_GLIBC_VER" "$BUNDLED_GLIBC_VER" | sort -V | tail -n 1)
    if [ "$NEWER" = "$HOST_GLIBC_VER" ]; then
      USE_BUNDLED_LD=0
    fi
  fi

  if [ "$USE_BUNDLED_LD" = "1" ]; then
    # Older host: use bundled ld-linux with bundled glibc on LD_LIBRARY_PATH
    export LD_LIBRARY_PATH="${HERE}/lib/x86_64-linux-gnu:${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/usr/lib:${HERE}/usr/opencascade:${LD_LIBRARY_PATH}:/usr/lib/x86_64-linux-gnu:/usr/lib64:/usr/lib"
    case "$LD_LINUX" in
      *ld-linux*) exec "$LD_LINUX" --inhibit-cache "$MAIN_BIN" "$@" ;;
      *) exec "$LD_LINUX" "$MAIN_BIN" "$@" ;;
    esac
  else
    # Newer host: skip bundled ld-linux, let RUNPATH resolve bundled libs as fallback
    export LD_LIBRARY_PATH="${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/usr/lib:${HERE}/usr/opencascade:${LD_LIBRARY_PATH}:/usr/lib/x86_64-linux-gnu:/usr/lib64:/usr/lib"
    exec "${MAIN_BIN}" "$@"
  fi
else
  exec "${MAIN_BIN}" "$@"
fi
TAILEOF

  else
    # Fallback: create minimal AppRun if go-appimage didn't generate one
    echo "[build_appimage] Creating minimal AppRun (go-appimage did not generate one)" >&2
    cat > "${APPDIR}/AppRun" <<RUNEOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export APPDIR="\$HERE"
export KICAD_STOCK_DATA_HOME="\$HERE/usr/share/kicad"
export XDG_DATA_DIRS="\$HERE/usr/share:\${XDG_DATA_DIRS:-/usr/share}"
export LD_LIBRARY_PATH="\$HERE/usr/lib/x86_64-linux-gnu:\$HERE/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib64:/usr/lib:\$LD_LIBRARY_PATH"
export PYTHONHOME="\$HERE/usr"
export PYTHONPATH="\$HERE/usr/lib/python3/dist-packages:\$HERE/usr/lib/python3.11/site-packages:\$PYTHONPATH"
export KICAD${kicad_maj}_SYMBOL_DIR="\$HERE/usr/share/kicad/symbols"
export KICAD${kicad_maj}_FOOTPRINT_DIR="\$HERE/usr/share/kicad/footprints"
export KICAD${kicad_maj}_3DMODEL_DIR="\$HERE/usr/share/kicad/3dmodels"
export KICAD${kicad_maj}_TEMPLATE_DIR="\$HERE/usr/share/kicad/template"
export GIO_MODULE_DIR="\$HERE/usr/lib/x86_64-linux-gnu/gio/modules"
export GIO_EXTRA_MODULES=""
exec "\$HERE/usr/bin/kicad.sh" "\$@"
RUNEOF
    chmod +x "${APPDIR}/AppRun"
  fi
}

build_image(){
  # go-appimage's appimagetool does not support the -comp flag (unlike the original appimagetool).
  # Compression is handled internally by mksquashfs with its default settings.
  VERSION="${APP_VERSION}" appimagetool "${APPDIR}" || {
    echo "[build_appimage] appimagetool failed" >&2; return 1; }
  mv *.AppImage* "${OUTPUT_DIR}"/
}

main(){
  fetch_go_appimage
  prepare_runtime_env
  run_deploy
  extract_debug_symbols
  strip_binaries
  build_image
  echo "[build_appimage] AppImage(s) placed in ${OUTPUT_DIR}" >&2
}

main "$@"
