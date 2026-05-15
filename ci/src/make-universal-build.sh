#!/bin/bash

set -euxo pipefail

# Build an arm64 version of KiCad, and an x86_64 version of KiCad, combine them, and re-sign them.
# This is not very elegant.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

KICAD_MAC_BUILDER_DIR=${SCRIPT_DIR}/../../

"$SCRIPT_DIR"/watermark.sh --both

if [ "$(arch)" != "arm64" ]; then
  echo "Expected 'arch' to return 'arm64'. Are you in a terminal running under Rosetta, maybe?"
  exit 1
fi

if [ -z "${MACOS_MIN_VERSION:-}" ]; then
  MACOS_MIN_VERSION_ARG=""
else
  MACOS_MIN_VERSION_ARG="--macos-min-version ${MACOS_MIN_VERSION}"
fi

ORIG_PATH="$PATH"

rm -rf build/ build-arm64/ build-x86_64/ build-universal/

echo "Running build.py for arm64..."
export PATH="/opt/homebrew/bin:$ORIG_PATH"
start_time=$SECONDS
CFLAGS="-I/$(/opt/homebrew/bin/brew --prefix)/include" CXXFLAGS="-I/$(/opt/homebrew/bin/brew --prefix)/include" WX_SKIP_DOXYGEN_VERSION_CHECK=true ./build.py --arch=arm64 --kicad-source-dir=../kicad --target package-kicad-unified $MACOS_MIN_VERSION_ARG
elapsed=$(( SECONDS - start_time ))
echo "arm64 took $elapsed seconds."
# save some disk space
rm -rf build/packages3d
rm -rf build/wxpython-prefix
rm -rf build/kicad
rm -rf build/wxwidgets
rm -rf build/footprints
mv build build-arm64


echo "Running build.py for x86_64..."
export PATH="/usr/local/bin:$ORIG_PATH"
start_time=$SECONDS
CFLAGS="-I/$(/usr/local/bin/brew --prefix)/include" CXXFLAGS="-I/$(/usr/local/bin/brew --prefix)/include" WX_SKIP_DOXYGEN_VERSION_CHECK=true arch -x86_64 ./build.py --arch=x86_64 --kicad-source-dir=../kicad --target package-kicad-unified $MACOS_MIN_VERSION_ARG
elapsed=$(( SECONDS - start_time ))
echo "x86_64 took $elapsed seconds."
# save some disk space
rm -rf build/packages3d
rm -rf build/wxpython-prefix
rm -rf build/kicad
rm -rf build/wxwidgets
rm -rf build/footprints
mv build build-x86_64

echo "Combining arm64 and x86_64 KiCad bundles into a Universal KiCad bundle..."
ditto --arch arm64 build-arm64/kicad-dest build-universal/thinned-arm64
ditto --arch x86_64 build-x86_64/kicad-dest build-universal/thinned-x86_64
rm -rf build-x86_64/kicad-dest/KiCad.app/Contents/SharedSupport/3dmodels
ditto build-arm64/kicad-dest build-universal/dest
rm -rf build-arm64/kicad-dest/KiCad.app/Contents/SharedSupport/3dmodels

cd build-universal/dest

for app in *.app; do
  cd "$app"
  for f in `find . -not -name "*.kicad_mod" -not -name "*.step" -not -name "*.wrl" -not -name "*.kicad_sym" -not -name "*.png" -not -name "*.py" -not -name "*.pyc" -not -name "*.h" -not -name "*.txt" -not -name "*.html" -not -name "*.xml" -type f`; do
    if file "$f" | grep 'Mach-O\|library' > /dev/null; then
        if [ "$app" == "KiCad.app" ]; then
          layers="../.."
        else
          layers="../../../../.."
        fi

      THIN_X86_64_VERSION="$layers/thinned-x86_64/$app/$f"
      THIN_ARM64_VERSION="$layers/thinned-arm64/$app/$f"

      # python3.9-intel64 has no arm64 component, so it doesn't show up in the thinned version
      if echo "$f" | grep python3.9-intel64; then
        continue
      fi

      # remove the one we copied in here, so we can replace it with a new version combined of the two source versions
      # When we just add the "missing" ones, the linker output isn't correct.  See the otool -L output, for instance.

      rm "$f"
      echo "Combining $THIN_X86_64_VERSION and $THIN_ARM64_VERSION..."
      lipo "$THIN_X86_64_VERSION" "$THIN_ARM64_VERSION" -create -output "$f"
    fi
  done
  cd -
done

cd ../

echo "Adhoc-signing Universal bundle..."

"$KICAD_MAC_BUILDER_DIR"/kicad-mac-builder/bin/apple.py sign \
  --certificate-id - \
  --entitlements "${KICAD_MAC_BUILDER_DIR}/kicad-mac-builder/signing/entitlements.plist" \
  "${KICAD_MAC_BUILDER_DIR}/build-universal/dest/KiCad.app"


echo "The adhoc-signed Universal bundles are in build-universal/dest."
echo "Before these could be distributed, they should be signed with an Apple certificate and notarized."