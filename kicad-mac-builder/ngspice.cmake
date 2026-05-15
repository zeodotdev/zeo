include(ExternalProject)

ExternalProject_Add(
    ngspice
    PREFIX  ngspice
    GIT_REPOSITORY git://git.code.sf.net/p/ngspice/ngspice
    GIT_TAG ngspice-45.2
    UPDATE_COMMAND      ""
    PATCH_COMMAND       ""
    CONFIGURE_COMMAND ./autogen.sh
    COMMAND ./configure --prefix=${ngspice_INSTALL_DIR} --with-ngshared
        --enable-xspice --enable-cider --disable-debug --disable-openmp
        LDFLAGS=-L/usr/local/opt/bison/lib
    BUILD_COMMAND  ${BIN_DIR}/run-with-path.sh ${CMAKE_MAKE_PROGRAM} /usr/local/opt/bison/bin:$ENV{PATH}
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
)
