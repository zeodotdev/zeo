include(ExternalProject)

if (NOT DEFINED KICAD_CMAKE_BUILD_TYPE )
    message( FATAL_ERROR "KICAD_CMAKE_BUILD_TYPE must be set.  Please see the README or try build.py." )
elseif ( KICAD_CMAKE_BUILD_TYPE STREQUAL "Release" )
    set(wxwidgets_MAKE_ARGS "BUILD=debug")
else ( ) # assume debug
    set(wxwidgets_MAKE_ARGS "BUILD=debug")
endif()


ExternalProject_Add(
    wxwidgets
    PREFIX  wxwidgets
    GIT_REPOSITORY https://gitlab.com/kicad/code/wxWidgets.git
    GIT_TAG kicad/macos-wx-3.2
    CONFIGURE_COMMAND   CPPFLAGS=-D__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=1 MAC_OS_X_VERSION_MIN_REQUIRED=${MACOS_MIN_VERSION} CC=clang CXX=clang++ ./configure
                        --prefix=${wxwidgets_INSTALL_DIR}
                        --with-macosx-version-min=${MACOS_MIN_VERSION}
                        --enable-unicode
                        --with-osx_cocoa
                        --enable-sound
                        --enable-graphics_ctx
                        --enable-display
                        --enable-geometry
                        --enable-debug_flag
                        --enable-debug
                        --enable-optimise
                        --disable-debugreport
                        --enable-uiactionsim
                        --enable-autoidman
                        --enable-monolithic
                        --enable-aui
                        --enable-html
                        --disable-stl
                        --enable-richtext
                        --disable-mediactrl
                        --with-libjpeg=builtin
                        --with-libpng=builtin
                        --with-regex=builtin
                        --with-libtiff=builtin
                        --with-zlib=builtin
                        --with-expat=builtin
                        --without-liblzma
                        --with-opengl
    UPDATE_COMMAND ""
    BUILD_COMMAND make ${wxwidgets_MAKE_ARGS}
    BUILD_IN_SOURCE 1
 )

ExternalProject_Add_Step(
    wxwidgets
    sign_dylibs
    COMMENT "Ad-hoc signing wxWidgets dylibs for hardened runtime compatibility"
    DEPENDEES install
    COMMAND find ${wxwidgets_INSTALL_DIR}/lib -type f -name "*.dylib" -exec codesign --force --sign - {} +
)

ExternalProject_Get_Property(wxwidgets SOURCE_DIR)
set( wxwidgets_SOURCE_DIR ${SOURCE_DIR})

ExternalProject_Add(
    wxpython
    DEPENDS python wxwidgets
    BUILD_IN_SOURCE     1
    GIT_REPOSITORY https://github.com/wxWidgets/Phoenix.git
    GIT_TAG 78938da1218483024b3a7acf55b5fb5513882916
    UPDATE_COMMAND      ""
    PATCH_COMMAND       ""
    CONFIGURE_COMMAND   ""
    BUILD_COMMAND WXWIN=${wxwidgets_SOURCE_DIR} MAC_OS_X_VERSION_MIN_REQUIRED=${MACOS_MIN_VERSION} CPPFLAGS=-I${PYTHON_INSTALL_DIR}/Python.framework/Versions/${PYTHON_X_Y_VERSION}/include/python${PYTHON_X_Y_VERSION} ${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/bin/python3 build.py --use_syswx --prefix=${wxwidgets_INSTALL_DIR} touch etg --nodoc sip build_py
    INSTALL_COMMAND MAC_OS_X_VERSION_MIN_REQUIRED=${MACOS_MIN_VERSION} ${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/bin/python3 build.py install_py --prefix=${wxwidgets_INSTALL_DIR} # --nodoc
    COMMAND rm -f ${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/lib/site-packages/wx/libwx*.dylib # TODO: see if this is still needed
    COMMAND find ${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/lib/python${PYTHON_X_Y_VERSION}/site-packages/wx -type f -name "*.so" -exec codesign --force --sign - {} +
)

ExternalProject_Add_Step(
    wxpython
    download_dox
    COMMENT "Getting wxpython's desired Doxygen version"
    DEPENDEES configure
    WORKING_DIRECTORY <SOURCE_DIR>
    COMMAND MAC_OS_X_VERSION_MIN_REQUIRED=${MACOS_MIN_VERSION} ${PYTHON_INSTALL_DIR}/Python.framework/Versions/Current/bin/python3 -c "import build$<SEMICOLON> build.getDoxCmd()"
    COMMAND bash -c "if [ ! -e bin/doxygen ]$<SEMICOLON> then cd bin$<SEMICOLON> ln -s <SOURCE_DIR>/bin/doxygen-*-darwin doxygen$<SEMICOLON> fi" # This isn't great, would love to improve this
)

ExternalProject_Add_Step(
    wxpython
    prep_dox
    COMMENT "Generating wxwidgets XML with doyxgen"
    DEPENDEES download_dox
    DEPENDERS build
    WORKING_DIRECTORY ${wxwidgets_SOURCE_DIR}/docs/doxygen
    COMMAND DOXYGEN=<SOURCE_DIR>/bin/doxygen ./regen.sh xml
)
