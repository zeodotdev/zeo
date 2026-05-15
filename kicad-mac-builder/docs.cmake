if ( SKIP_DOCS_UPDATE )
    # Don't download docs from <URL>; use the contents of <DOWNLOAD_DIR> instead
    set(docs_DOWNLOAD_COMMAND_OVERRIDE DOWNLOAD_COMMAND echo "Using contents of <DOWNLOAD_DIR> instead of downloading ${DOCS_TARBALL_URL}")
else()
    set(docs_DOWNLOAD_COMMAND_OVERRIDE )
endif()

ExternalProject_Add(
    docs
    PREFIX  docs
    URL ${DOCS_TARBALL_URL}
    ${docs_DOWNLOAD_COMMAND_OVERRIDE}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(docs SOURCE_DIR)
set(docs_SOURCE_DIR ${SOURCE_DIR} )