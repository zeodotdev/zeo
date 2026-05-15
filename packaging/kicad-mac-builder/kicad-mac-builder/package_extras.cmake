# Maybe a better long term solution is to actually mount the DMG before we do this, and install directly into the DMG

ExternalProject_Add(
    package-extras
    DEPENDS packages3d
    PREFIX package-extras
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND   ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND VERBOSE=1
                    PACKAGING_DIR=${CMAKE_SOURCE_DIR}/extras-packaging
                    TEMPLATE=kicad-extras-template.dmg
                    PACKAGE_TYPE=extras
                    DMG_DIR=${DMG_DIR}
                    RELEASE_NAME=${RELEASE_NAME}
                    SIGNING_IDENTITY=${SIGNING_IDENTITY}
                    APPLE_DEVELOPER_USERNAME=${APPLE_DEVELOPER_USERNAME}
                    APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME=${APPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME}
                    DMG_NOTARIZATION_ID=${DMG_NOTARIZATION_ID}
                    ASC_PROVIDER=${ASC_PROVIDER}
                    ${BIN_DIR}/package.sh
)

SET_TARGET_PROPERTIES(package-extras PROPERTIES EXCLUDE_FROM_ALL True)
