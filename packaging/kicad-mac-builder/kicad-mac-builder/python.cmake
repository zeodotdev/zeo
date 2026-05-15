include(ExternalProject)

# There are certain snags with building your own Python, that, while we have been able to work around them fine in the
# past, have tripped up users and new potential developers*, and I'm eager to see if using this script that starts
# from the Python.org macOS bundle and makes it relocatable helps improve things.  Of course, until there are some
# CMake improvements around bundling, we'll still have to handle a fair amount of it ourselves, but it appears that
# these changes are starting to be discussed upstream.

# If we end up wanting to build our Python from source again, I have been able to get Python 3 working like we had before
# as long as I was careful about the following:

# When you compile Python as a Framework, it wants to write into /Applications.
# The documentation implies you can get around it by building the frameworkinstallframework target, but it ends up
# pulling in frameworkinstallapps and frameworkinstallunixtools, which write into places we don't want.
# I tried overriding that with DESTDIR, and fixup_bundle got so confused it pulled in nearly an entire macOS install.
# However, a detailed reading of the source revealed that if the Framework path is set as foo/Library/Frameworks, the
# build will write into foo/Applications, not /Applications.  BINGO!
# This means it is important that PYTHON_INSTALL_DIR ends in /Library/Frameworks.

# * Remember when we had to build Python with one core, because of a race condition when building Python as a
#   Framework on macOS?  (https://github.com/Homebrew/legacy-homebrew/issues/429)

include(ExternalProject)

# TODO: Can we add ${CMAKE_SOURCE_DIR}/python-requirements.txt as a dependency so it rebuilds here if it changes?

# relocatable-python uses "os-version" as an argument to build a URL to download Python from python.org
# Currently, the builds that are Universal (have arm64/x86_64 in them) use 11 in the url, which makes sense
if( MACOS_MIN_VERSION VERSION_GREATER 11 )
    set( RELOCATABLE_PYTHON_OS_VERSION_FLAG --os-version 11)
else()
    set( RELOCATABLE_PYTHON_OS_VERSION_FLAG "" ) # TODO do we need this?
endif()


ExternalProject_Add(
    python
    PREFIX  python
    GIT_REPOSITORY https://github.com/gregneagle/relocatable-python.git
    GIT_TAG main
    CONFIGURE_COMMAND 	""
    UPDATE_COMMAND      ""
    PATCH_COMMAND       ""
    BUILD_COMMAND cmake -E remove_directory Python.framework
    COMMAND <SOURCE_DIR>/make_relocatable_python_framework.py
            --pip-requirements ${CMAKE_SOURCE_DIR}/python-requirements.txt
            --python-version ${PYTHON_VERSION}
            ${RELOCATABLE_PYTHON_OS_VERSION_FLAG}

    INSTALL_COMMAND cmake -E remove_directory  ${PYTHON_INSTALL_DIR}
    COMMAND mkdir -p ${PYTHON_INSTALL_DIR}
    COMMAND cp -R Python.framework ${PYTHON_INSTALL_DIR}/ # we need to preserve symlinks!
)

ExternalProject_Add_Step(
    python
    verify_fixup
    COMMENT "Test bin/python3"
    DEPENDEES install
    COMMAND ${BIN_DIR}/verify-cli-python.sh "${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/bin/python3"
)

ExternalProject_Add_Step(
    python
    verify_ssl
    COMMENT "Make sure SSL is included"
    DEPENDEES install
    COMMAND "${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/bin/python3" -c "import ssl"
)

ExternalProject_Get_Property(python BINARY_DIR)
set( python_BINARY_DIR ${BINARY_DIR})
