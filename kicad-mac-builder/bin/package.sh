#!/bin/bash

set -e

# This script, unfortunately, uses a lot of environment variables for configuration
# I'd love to see this redone!

# PACKAGE_TYPE must be set to unified (legacy, should handle...)
# KICAD_INSTALL_DIR used to grab files for the dmg
# KICAD_SOURCE_DIR used for git sha in filename (see RELEASE_NAME)
# RELEASE_NAME can be used for dmg naming
# PACKAGING_DIR directory that TEMPLATE is in, also contains background
# DMG_DIR output directory that will contain the dmg
# TEMPLATE filename of template dmg, minus the end .tar.bz2

SCRIPT_DIR=$(dirname "$(stat -f "$0")")

cleanup() {
    echo "Making sure any mounts are unmounted."
    if [ -n "${MOUNTPOINT}" ]; then
        hdiutil detach "${MOUNTPOINT}" || true
        diskutil unmount "${MOUNTPOINT}" || true
        rm -rf "${MOUNTPOINT}" || true
    fi
}
trap cleanup EXIT

setup_dmg()
{
    # make the mountpoint
    if [ -e "${MOUNTPOINT}" ]; then
        # it might be a leftover mount from a crashed previous run, so try to unmount it before removing it.
        hdiutil detach "${MOUNTPOINT}" || true
        diskutil unmount "${MOUNTPOINT}" || true
        rm -r "${MOUNTPOINT}"
    fi
    mkdir -p "${MOUNTPOINT}"

    # untar the template
    if [ -e "${TEMPLATE}" ]; then
        rm "${TEMPLATE}"
    fi
    tar xf "${PACKAGING_DIR}"/"${TEMPLATE}".tar.bz2
    if [ ! -e "${TEMPLATE}" ]; then
        echo "Unable to find ${TEMPLATE}"
        exit 1
    fi

    # resize the template, and mount it

    if ! hdiutil resize -sectors "${DMG_SIZE}" "${TEMPLATE}"; then
        hdiutil resize -limits kicadtemplate.dmg # Debugging step, to see what the limits are.
        hdiutil resize -sectors 10167525 "${TEMPLATE}"
        hdiutil resize -limits kicadtemplate.dmg
        if ! hdiutil resize -sectors "${DMG_SIZE}" "${TEMPLATE}"; then
          echo "Cannot resize template dmg, exiting."
          exit 1
        fi
    fi
    diskutil unmount /Volumes/"${MOUNT_NAME}" || true
    DEVICE=$(hdiutil attach "${TEMPLATE}" -noautoopen -nobrowse -mountpoint "${MOUNTPOINT}" | awk '/Apple_HFS/ {print $1}')
    mdutil -i off "${MOUNTPOINT}" || true
}


fixup_and_cleanup()
{
    # update background of the DMG
    cp "${PACKAGING_DIR}"/background.png "${MOUNTPOINT}"/.
    # Rehide the background file
    SetFile -a V "${MOUNTPOINT}"/background.png

    # Retry up to 5 times, increasing wait each time
    DETACHED=0
    sync
    sleep 5
    for i in 1 2 3 4 5; do
        if hdiutil detach "${DEVICE}"; then
            DETACHED=1
            break
        else
            echo "hdiutil detach failed (attempt $i), resource busy. Retrying in $((i*5)) seconds..."
            echo "Mounted volumes:"
            mount | grep "${MOUNTPOINT}" || true
            hdiutil info
            lsof "${MOUNTPOINT}" || true
            sync || true
            sleep $((i*5))
        fi
    done
    if [ $DETACHED -eq 0 ]; then
        echo "Error: Could not detach ${MOUNTPOINT} after 5 attempts."
        hdiutil detach -force "${DEVICE}" || {
            echo "Failed to force detach ${DEVICE}."
            exit 1
        }
    fi

    rm -r "${MOUNTPOINT}"

    #set it so the DMG autoopens on download/mount for supported platforms (x86)
    hdiutil attach "${TEMPLATE}" -noautoopen -nobrowse -mountpoint /Volumes/"${MOUNT_NAME}"
    if ! sysctl -n machdep.cpu.brand_string | grep Apple > /dev/null; then
      # we have to be careful how we detect the arch--we may be in rosetta!
      bless /Volumes/"${MOUNT_NAME}" --openfolder /Volumes/"${MOUNT_NAME}"
    fi

    UNMOUNTED=1
    for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
        if hdiutil detach "/Volumes/${MOUNT_NAME}"; then
          UNMOUNTED=0
          break
        else
          echo "Retrying..."
          lsof "/Volumes/${MOUNT_NAME}" || true
          sync || true
          sleep 10
        fi
    done
    if [ $UNMOUNTED -ne 0 ]; then
        echo "Error unmounting /Volumes/${MOUNT_NAME}"
        exit 1
    fi

    if [ -e "${DMG_NAME}" ] ; then
        rm -r "${DMG_NAME}"
    fi
    #hdiutil convert "${TEMPLATE}"  -format UDBZ -imagekey -o "${DMG_NAME}" # bzip2 based is a little bit smaller, but opens much, much slower.
    hdiutil convert "${TEMPLATE}"  -format UDZO -imagekey zlib-level=9 -o "${DMG_NAME}" # This used zlib, and bzip2 based (above) is slower but more compression

    rm "${TEMPLATE}"

    if [ -n "${SIGNING_IDENTITY}" ]; then
      codesign --sign "${SIGNING_IDENTITY}" --verbose "${DMG_NAME}"
    fi

    if [ -n "${SIGNING_IDENTITY}" ] && [ -n "${APPLE_DEVELOPER_USERNAME}" ] && [ -n "${APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME}" ] && [ -n "${DMG_NOTARIZATION_ID}" ] && [ -n "${ASC_PROVIDER}" ]; then
      "${SCRIPT_DIR}/apple.py" notarize \
          --apple-developer-username "${APPLE_DEVELOPER_USERNAME}" \
          --apple-developer-password-keychain-name "${APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME}" \
          --notarization-id "${DMG_NOTARIZATION_ID}" \
          --asc-provider "${ASC_PROVIDER}" \
          "${DMG_NAME}"
    fi

    mkdir -p "${DMG_DIR}"

    # If you move a file to the directory it's in, `mv` returns an error.  Ignore that one.
    output="$(mv "${DMG_NAME}" "${DMG_DIR}"/ 2>&1 || true)"
    if [ ! $? ]; then
        if ! echo "$line" | grep ' are identical$'; then
            echo "Error: ${output}"
      exit 1
  fi
    fi
}

#if [ "${VERBOSE}" ]; then
    set -x
#fi

echo "PACKAGING_DIR: ${PACKAGING_DIR}"
echo "KICAD_SOURCE_DIR: ${KICAD_SOURCE_DIR}"
echo "KICAD_INSTALL_DIR: ${KICAD_INSTALL_DIR}"
echo "VERBOSE: ${VERBOSE}"
echo "TEMPLATE: ${TEMPLATE}"
echo "DMG_DIR: ${DMG_DIR}"
echo "PACKAGE_TYPE: ${PACKAGE_TYPE}"
if [ -n "${RELEASE_NAME}" ]; then # if RELEASE_NAME is unset, or is set to empty string
    echo "RELEASE_NAME: ${RELEASE_NAME}"
else
    echo "RELEASE_NAME: unspecified"
fi
echo "SIGNING_IDENTITY: ${SIGNING_IDENTITY}"
echo "APPLE_DEVELOPER_USERNAME: ${APPLE_DEVELOPER_USERNAME}"
echo "APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME: ${APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME}"
echo "DMG_NOTARIZATION_ID: ${DMG_NOTARIZATION_ID}"
echo "ASC_PROVIDER: ${ASC_PROVIDER}"
echo "pwd: $(pwd)"

if [ ! -e "${PACKAGING_DIR}" ]; then
    echo "PACKAGING_DIR must be set and exist."
    exit 1
fi

if [ -z "${TEMPLATE}" ]; then
    echo "TEMPLATE must be set."
    exit 1
fi

if [ -z "${DMG_DIR}" ]; then
    echo "DMG_DIR must be set."
    exit 1
fi

if [ "${PACKAGE_TYPE}" != "nightly" ] && [ "${PACKAGE_TYPE}" != "extras" ] && [ "${PACKAGE_TYPE}" != "unified" ]; then
    echo "PACKAGE_TYPE must be either \"nightly\", \"extras\", or \"unified\"."
    exit 1
fi

if [ "${PACKAGE_TYPE}" != "extras" ] && [ ! -e "${KICAD_SOURCE_DIR}" ]; then
    echo "In nightly and unified, KICAD_SOURCE_DIR must be set and exist."
    exit 1
fi

if [ "${PACKAGE_TYPE}" != "extras" ] && [ ! -e "${KICAD_INSTALL_DIR}" ]; then
    echo "In nightly and unified, KICAD_INSTALL_DIR must be set and exist."
    exit 1
fi

NOW=$(date +%Y%m%d-%H%M%S)

case "${PACKAGE_TYPE}" in
    nightly)
        KICAD_GIT_REV=$(cd "${KICAD_SOURCE_DIR}" && git rev-parse --short HEAD)
        MOUNT_NAME='KiCad'
        DMG_SIZE=15567525
        if [ -z "$RELEASE_NAME" ]; then
            DMG_NAME=kicad-nightly-"${NOW}"-"${KICAD_GIT_REV}".dmg
        else
            DMG_NAME=kicad-nightly-"${RELEASE_NAME}".dmg
        fi
    ;;
    extras)
        MOUNT_NAME='KiCad Extras'
        DMG_SIZE=9.5G
        if [ -z "$RELEASE_NAME" ]; then
            DMG_NAME=kicad-extras-"${NOW}".dmg
        else
            DMG_NAME=kicad-extras-"${RELEASE_NAME}".dmg
        fi
    ;;
    unified)
        KICAD_GIT_REV=$(cd "${KICAD_SOURCE_DIR}" && git rev-parse --short HEAD)
        MOUNT_NAME='KiCad'
        DMG_SIZE=15567525
        if [ -z "$RELEASE_NAME" ]; then
            DMG_NAME=kicad-unified-"${NOW}"-"${KICAD_GIT_REV}".dmg
        else
            DMG_NAME=kicad-unified-"${RELEASE_NAME}".dmg
        fi
    ;;
    *)
        echo "PACKAGE_TYPE must be either \"nightly\", \"extras\", or \"unified\"."
        exit 1
esac

MOUNTPOINT=kicad-mnt

setup_dmg

case "${PACKAGE_TYPE}" in
    nightly)
      exit 1
    ;;
    extras)
      exit 1
    ;;
    unified)
        mkdir -p "${MOUNTPOINT}"/KiCad
        rsync -al "${KICAD_INSTALL_DIR}"/* "${MOUNTPOINT}"/KiCad/. # IMPORTANT: must preserve symlinks
        echo "Moving demos"
        mv "${MOUNTPOINT}"/KiCad/demos "${MOUNTPOINT}"/
    ;;
    *)
        echo "PACKAGE_TYPE must be \"unified\"."
        exit 1
esac

fixup_and_cleanup

echo "Done creating ${DMG_NAME} in ${DMG_DIR}"
